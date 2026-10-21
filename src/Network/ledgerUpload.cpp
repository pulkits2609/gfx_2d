#include"Network/ledgerUpload.hpp"
#include<boost/asio.hpp>
#include<boost/beast.hpp>
#include<algorithm>
#include<cctype>

namespace asio = boost::asio;
namespace http = boost::beast::http;
using tcp = asio::ip::tcp;
using ErrorCode = boost::system::error_code;

namespace{
    constexpr auto UploadTimeout = std::chrono::seconds(8);
    constexpr std::size_t MaximumResponseBytes = 65536;
    constexpr std::size_t MaximumRequestBytes = 65536;
    constexpr int MaximumJsonDepth = 12;

    struct Request : std::enable_shared_from_this<Request>{
        tcp::resolver resolver;
        boost::beast::tcp_stream stream;
        asio::steady_timer deadline;
        boost::beast::flat_buffer buffer;
        http::request<http::string_body> request;
        http::response_parser<http::string_body> response;
        LedgerUpload::State state = LedgerUpload::State::Pending;
        std::string message = "Uploading result...";
        std::string hash;
        std::string matchId;

        explicit Request(asio::io_context& context) : resolver(context), stream(context), deadline(context){
            response.body_limit(MaximumResponseBytes);
            response.header_limit(8192);
        }

        void Finish(LedgerUpload::State next, const std::string& text){
            if(state != LedgerUpload::State::Pending) return;
            state = next;
            message = text;
            resolver.cancel();
            deadline.cancel();
            ErrorCode ignored;
            stream.socket().close(ignored);
        }

        bool Failed(ErrorCode error){
            if(state != LedgerUpload::State::Pending) return true;
            if(!error) return false;
            Finish(LedgerUpload::State::Failed, "Could not reach the ledger or read its response. Check its address and port.");
            return true;
        }

        void ReadReply(){
            auto self = shared_from_this();
            http::async_read(stream, buffer, response, [self](ErrorCode error, std::size_t){
                if(self->Failed(error)) return;
                bool tooDeep = false;
                auto body = Protocol::Json::parse(self->response.get().body(), [&](int depth, Protocol::Json::parse_event_t, Protocol::Json&){
                    if(depth > MaximumJsonDepth) tooDeep = true;
                    return depth <= MaximumJsonDepth;
                }, false);
                if(tooDeep){
                    self->Finish(LedgerUpload::State::Failed, "Ledger response is nested too deeply.");
                    return;
                }
                unsigned status = self->response.get().result_int();
                if(status != 200 && status != 201){
                    std::string reason = "Ledger rejected the result (HTTP " + std::to_string(status) + ").";
                    if(body.is_object() && body.contains("error") && body["error"].is_string())
                        reason += " " + body["error"].get<std::string>().substr(0, 180);
                    self->Finish(LedgerUpload::State::Failed, reason);
                    return;
                }
                if(!body.is_object() || !body.contains("block") || !body["block"].is_object()){
                    self->Finish(LedgerUpload::State::Failed, "Ledger returned an invalid receipt.");
                    return;
                }
                const auto& block = body["block"];
                if(!block.contains("match_id") || block["match_id"] != self->matchId
                    || !block.contains("hash") || !block["hash"].is_string()){
                    self->Finish(LedgerUpload::State::Failed, "Ledger receipt does not match this result.");
                    return;
                }
                std::string hash = block["hash"].get<std::string>();
                bool validHash = hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](unsigned char value){
                    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f');
                });
                if(!validHash){
                    self->Finish(LedgerUpload::State::Failed, "Ledger returned an invalid record hash.");
                    return;
                }
                self->hash = hash;
                self->Finish(LedgerUpload::State::Saved, "Result recorded on the ledger.");
            });
        }

        void Begin(const std::string& address, int port, const Protocol::Json& results){
            matchId = results["resultId"].get<std::string>();
            request.method(http::verb::post);
            request.target("/api/blocks");
            request.version(11);
            std::string host = address;
            if(host.find(':') != std::string::npos) host = "[" + host + "]";
            request.set(http::field::host, host + ":" + std::to_string(port));
            request.set(http::field::content_type, "application/json");
            request.set(http::field::connection, "close");
            request.body() = Protocol::Json{{"match_id", matchId}, {"data", results}}.dump();
            if(request.body().size() > MaximumRequestBytes){
                Finish(LedgerUpload::State::Failed, "Match result is too large to upload.");
                return;
            }
            request.prepare_payload();
            auto self = shared_from_this();
            //One deadline also covers DNS lookup; no stage can keep the UI waiting forever.
            deadline.expires_after(UploadTimeout);
            deadline.async_wait([self](ErrorCode error){
                if(!error) self->Finish(LedgerUpload::State::Failed, "Ledger upload timed out. You can retry safely.");
            });
            resolver.async_resolve(address, std::to_string(port), [self](ErrorCode error, tcp::resolver::results_type endpoints){
                if(self->Failed(error)) return;
                self->stream.async_connect(endpoints, [self](ErrorCode error, const tcp::endpoint&){
                    if(self->Failed(error)) return;
                    http::async_write(self->stream, self->request, [self](ErrorCode error, std::size_t){
                        if(self->Failed(error)) return;
                        self->ReadReply();
                    });
                });
            });
        }
    };
}

struct LedgerUpload::Impl{
    asio::io_context context;
    std::shared_ptr<Request> request;
    State state = State::Idle;
    std::string message, hash;
};

LedgerUpload::LedgerUpload() : impl(std::make_unique<Impl>()){}
LedgerUpload::~LedgerUpload(){ Reset(); }

void LedgerUpload::Reset(){
    if(impl->request) impl->request->Finish(State::Failed, "Upload cancelled.");
    impl->context.restart();
    impl->context.poll();
    impl->request.reset();
    impl->state = State::Idle;
    impl->message.clear();
    impl->hash.clear();
}

void LedgerUpload::Start(const std::string& address, int port, const Protocol::Json& results){
    Reset();
    bool validAddress = !address.empty() && address.size() <= 127
        && address.find_first_of("/\\?#@ \t\r\n") == std::string::npos;
    if(!validAddress || port < 1 || port > 65535){
        impl->state = State::Failed;
        impl->message = "Enter a host name or IP address and a port from 1 to 65535. Leave out http://.";
        return;
    }
    if(!results.is_object() || !results.contains("resultId") || !results["resultId"].is_string()
        || results["resultId"].get_ref<const std::string&>().empty()){
        impl->state = State::Failed;
        impl->message = "This result needs an updated GFX server before it can be uploaded.";
        return;
    }
    impl->context.restart();
    impl->request = std::make_shared<Request>(impl->context);
    impl->request->Begin(address, port, results);
    Poll();
}

void LedgerUpload::Poll(){
    impl->context.restart();
    impl->context.poll();
    if(!impl->request) return;
    impl->state = impl->request->state;
    impl->message = impl->request->message;
    impl->hash = impl->request->hash;
}
LedgerUpload::State LedgerUpload::GetState() const{ return impl->state; }
const std::string& LedgerUpload::GetStatusText() const{ return impl->message; }
const std::string& LedgerUpload::GetHash() const{ return impl->hash; }
