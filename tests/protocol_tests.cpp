#include"Network/protocol.hpp"
#include<iostream>
#include<stdexcept>

void Check(bool condition, const char* message){ if(!condition) throw std::runtime_error(message); }

int main(){
    try{
        for(std::uint32_t length : {1u, 255u, 256u, 16384u, 0xffffffffu}){
            auto header = Protocol::EncodeLength(length);
            Check(Protocol::DecodeLength(header) == length, "length round trip");
        }
        Check(Protocol::EncodeLength(0x01020304) == Protocol::Header{1, 2, 3, 4}, "big-endian header");
        auto message = Protocol::Message("hello");
        message["name"] = "Player 1";
        auto encoded = Protocol::Encode(message);
        Protocol::Json parsed;
        std::string error;
        std::string body(encoded.begin() + 4, encoded.end());
        Check(Protocol::Parse(body, parsed, error) && parsed == message, "JSON frame round trip");
        Check(!Protocol::Parse("{", parsed, error), "malformed JSON");
        Check(!Protocol::Parse("[]", parsed, error), "object required");
        Check(!Protocol::Parse("{\"version\":2,\"type\":\"hello\"}", parsed, error), "version check");
        Check(!Protocol::Parse("{\"version\":1,\"type\":true}", parsed, error), "message type check");
        Check(!Protocol::Parse("{\"version\":1,\"type\":\"ping\",\"requestId\":-1}", parsed, error), "request ID check");
        Check(!Protocol::ValidName("") && !Protocol::ValidName(" Name")
            && !Protocol::ValidName("Bad\nName") && Protocol::ValidName("Player 4"), "name validation");
        auto deep = Protocol::Message("ping");
        Protocol::Json nested = 0;
        for(int i = 0; i < 20; i++) nested = Protocol::Json::array({nested});
        deep["nested"] = nested;
        Check(!Protocol::Parse(deep.dump(), parsed, error), "nesting limit");
        bool rejected = false;
        try{ Protocol::Encode(Protocol::Json(std::string(Protocol::MaximumBodySize + 1, 'x'))); }
        catch(const std::length_error&){ rejected = true; }
        Check(rejected, "outgoing frame size");
        std::cout << "Protocol checks passed\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
