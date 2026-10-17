#include"Network/voiceManager.hpp"
#include<algorithm>

using boost::asio::ip::tcp;
using ErrorCode = boost::system::error_code;

struct VoiceManager::Connection{
    tcp::socket socket;
    Protocol::Header header{};
    std::string body;
    std::deque<std::shared_ptr<std::vector<std::uint8_t>>> outgoing;
    explicit Connection(boost::asio::io_context& context):socket(context){}
};

VoiceManager::VoiceManager(boost::asio::io_context& io, VoiceServerSettings options):
    context(io), settings(std::move(options)), resolver(io), timer(io){}

VoiceManager::~VoiceManager(){ Stop(); }

void VoiceManager::Start(){
    if(running || !settings.enabled) return;
    running = true;
    Connect();
    Schedule();
}

void VoiceManager::Stop(){
    running = ready = false;
    resolver.cancel();
    timer.cancel();
    if(connection){ ErrorCode error; connection->socket.close(error); connection.reset(); }
    pending.clear();
}

void VoiceManager::Connect(){
    if(!running || connection) return;
    auto current = std::make_shared<Connection>(context);
    connection = current;
    lastRead = Clock::now();
    resolver.async_resolve(settings.host, std::to_string(settings.port),
        [this, current](ErrorCode error, tcp::resolver::results_type endpoints){
            if(connection != current || !running) return;
            if(error){ Lost("Voice server could not be reached"); return; }
            boost::asio::async_connect(current->socket, endpoints, [this, current](ErrorCode error, const tcp::endpoint&){
                if(connection != current || !running) return;
                if(error){ Lost("Voice server could not be reached"); return; }
                current->socket.set_option(tcp::no_delay(true));
                Read(current);
                Json hello = {{"role", "manager"}};
                if(managerId) hello.update({{"manager_id", managerId}, {"server_instance", instance}});
                Request("hello", hello, [this](bool ok, const Json& data){
                    if(!ok){
                        const std::string code = data.value("code", "connection_error");
                        Lost("Voice management: " + code, code == "server_restarted" || code == "manager_expired");
                        return;
                    }
                    if(!data.contains("manager_id") || !data["manager_id"].is_number_unsigned()
                        || !data.contains("server_instance") || !data["server_instance"].is_string()){
                        Lost("Invalid voice management registration", true); return;
                    }
                    managerId = data["manager_id"].get<std::uint32_t>();
                    instance = data["server_instance"].get<std::string>();
                    ready = true;
                    recoveryEnd = {};
                    if(onReady) onReady(data.value("resumed", false));
                });
            });
        });
}

void VoiceManager::Request(const std::string& operation, Json fields, Reply reply){
    if(!connection || (!ready && operation != "hello") || pending.size() >= 128){
        if(reply) reply(false, {{"code", "voice_unavailable"}});
        return;
    }
    const std::string id = std::to_string(++nextRequest);
    fields.update({{"v", 2}, {"id", id}, {"op", operation}});
    pending.emplace(id, Pending{std::move(reply), Clock::now() + std::chrono::seconds(5)});
    Send(fields);
}

void VoiceManager::Send(const Json& message){
    if(!connection) return;
    const std::string body = message.dump();
    if(body.empty() || body.size() > 65536 || connection->outgoing.size() >= 128){
        Lost("Voice control queue exceeded its limit"); return;
    }
    auto bytes = std::make_shared<std::vector<std::uint8_t>>();
    const auto header = Protocol::EncodeLength(static_cast<std::uint32_t>(body.size()));
    bytes->insert(bytes->end(), header.begin(), header.end());
    bytes->insert(bytes->end(), body.begin(), body.end());
    connection->outgoing.push_back(bytes);
    if(connection->outgoing.size() == 1) Write(connection);
}

void VoiceManager::Write(const std::shared_ptr<Connection>& current){
    auto bytes = current->outgoing.front();
    boost::asio::async_write(current->socket, boost::asio::buffer(*bytes),
        [this, current, bytes](ErrorCode error, std::size_t){
            if(connection != current || !running) return;
            if(error){ Lost("Voice management connection interrupted"); return; }
            current->outgoing.pop_front();
            if(!current->outgoing.empty()) Write(current);
        });
}

void VoiceManager::Read(const std::shared_ptr<Connection>& current){
    boost::asio::async_read(current->socket, boost::asio::buffer(current->header),
        [this, current](ErrorCode error, std::size_t){
            if(connection != current || !running) return;
            if(error){ Lost("Voice management connection interrupted"); return; }
            const auto length = Protocol::DecodeLength(current->header);
            if(!length || length > 65536){ Lost("Invalid voice control frame"); return; }
            current->body.assign(length, '\0');
            boost::asio::async_read(current->socket, boost::asio::buffer(current->body),
                [this, current](ErrorCode error, std::size_t){
                    if(connection != current || !running) return;
                    if(error){ Lost("Incomplete voice control frame"); return; }
                    try{ Receive(Json::parse(current->body)); }
                    catch(const std::exception&){ Lost("Invalid voice control response"); }
                    if(connection == current && running) Read(current);
                });
        });
}

void VoiceManager::Receive(const Json& message){
    if(!message.is_object() || message.value("v", 0) != 2) throw std::runtime_error("Voice protocol mismatch");
    lastRead = Clock::now();
    if(message.contains("event")){
        if(message["event"] == "ping") Send({{"v", 2}, {"op", "pong"}, {"nonce", message.at("data").at("nonce")}});
        else if(onEvent) onEvent(message);
        return;
    }
    auto found = pending.find(message.at("id").get<std::string>());
    if(found == pending.end()) return;
    auto reply = std::move(found->second.reply);
    pending.erase(found);
    const bool ok = message.at("ok").get<bool>();
    if(reply) reply(ok, message.at(ok ? "data" : "error"));
}

void VoiceManager::Lost(const std::string& reason, bool permanent){
    if(!running) return;
    const bool wasReady = ready;
    ready = false;
    resolver.cancel();
    if(connection){ ErrorCode error; connection->socket.close(error); connection.reset(); }
    auto failed = std::move(pending);
    pending.clear();
    if(managerId && recoveryEnd == Clock::time_point{}) recoveryEnd = Clock::now() + std::chrono::seconds(10);
    if(permanent){ managerId = 0; instance.clear(); recoveryEnd = {}; }
    nextConnect = Clock::now() + std::chrono::milliseconds(300);
    if(onLost && (wasReady || permanent)) onLost(permanent, reason);
    for(auto& entry : failed) if(entry.second.reply) entry.second.reply(false, {{"code", "connection_lost"}});
}

void VoiceManager::Schedule(){
    timer.expires_after(std::chrono::milliseconds(100));
    timer.async_wait([this](ErrorCode error){
        if(error || !running) return;
        const auto now = Clock::now();
        if(managerId && recoveryEnd != Clock::time_point{} && now >= recoveryEnd){
            Lost("Voice management recovery expired", true);
        }
        else if(connection && now - lastRead > std::chrono::seconds(6)) Lost("Voice server stopped responding");
        else if(std::any_of(pending.begin(), pending.end(), [now](const auto& item){ return now >= item.second.deadline; }))
            Lost("Voice control request timed out");
        if(!connection && now >= nextConnect) Connect();
        Schedule();
    });
}
