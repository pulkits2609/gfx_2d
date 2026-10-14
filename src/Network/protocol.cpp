#include"Network/protocol.hpp"
#include<stdexcept>

Protocol::Header Protocol::EncodeLength(std::uint32_t length){
    return {static_cast<std::uint8_t>(length >> 24), static_cast<std::uint8_t>(length >> 16),
        static_cast<std::uint8_t>(length >> 8), static_cast<std::uint8_t>(length)};
}

std::uint32_t Protocol::DecodeLength(const Header& header){
    return (static_cast<std::uint32_t>(header[0]) << 24)
        | (static_cast<std::uint32_t>(header[1]) << 16)
        | (static_cast<std::uint32_t>(header[2]) << 8)
        | static_cast<std::uint32_t>(header[3]);
}

std::vector<std::uint8_t> Protocol::Encode(const Json& message){
    std::string body = message.dump();
    if(body.empty() || body.size() > MaximumBodySize) throw std::length_error("Control frame is too large");
    Header header = EncodeLength(static_cast<std::uint32_t>(body.size()));
    std::vector<std::uint8_t> frame(header.begin(), header.end());
    frame.insert(frame.end(), body.begin(), body.end());
    return frame;
}

bool Protocol::Parse(const std::string& body, Json& message, std::string& error){
    if(body.empty() || body.size() > MaximumBodySize){ error = "Invalid body size"; return false; }
    //limit nesting as well as bytes; control messages do not need deep JSON trees
    bool tooDeep = false;
    message = Json::parse(body, [&](int depth, Json::parse_event_t, Json&){
        if(depth > 12) tooDeep = true;
        return depth <= 12;
    }, false);
    if(tooDeep || message.is_discarded() || !message.is_object()){
        error = "Expected a shallow JSON object"; return false;
    }
    if(!message.contains("version") || !message["version"].is_number_integer()
        || message["version"] != Version){ error = "Unsupported protocol version"; return false; }
    if(!message.contains("type") || !message["type"].is_string()
        || message["type"].get_ref<const std::string&>().size() > 32){
        error = "Invalid message type"; return false;
    }
    if(message.contains("requestId") && (!message["requestId"].is_number_unsigned()
        || message["requestId"].get<std::uint64_t>() > 0xffffffffu)){
        error = "Invalid requestId"; return false;
    }
    return true;
}

bool Protocol::ValidName(const std::string& name){
    if(name.empty() || name.size() > 24 || name.front() == ' ' || name.back() == ' ') return false;
    for(unsigned char letter : name) if(letter < 32 || letter > 126) return false;
    return true;
}

Protocol::Json Protocol::Message(const std::string& type){
    return {{"version", Version}, {"type", type}};
}
