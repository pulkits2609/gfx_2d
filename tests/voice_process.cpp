#include"Network/voiceProcess.hpp"
#include<chrono>
#include<iostream>
#include<functional>
#include<thread>

using Json = Protocol::Json;
using Clock = std::chrono::steady_clock;

namespace{
    void Check(bool result, const char* reason){ if(!result) throw std::runtime_error(reason); }
    void Wait(const std::function<bool()>& condition){
        const auto end = Clock::now() + std::chrono::seconds(5);
        while(Clock::now() < end){
            if(condition()) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        throw std::runtime_error("Voice pipe checkpoint timed out");
    }
    void Event(VoiceProcess& process, const char* name){
        Wait([&]{
            for(const auto& message : process.Poll()) if(message.value("event", "") == name) return true;
            return false;
        });
    }

    int Fixture(){
        std::cout << Json{{"v", 2}, {"event", "ready"}, {"data", Json::object()}}.dump() << std::endl;
        //More than a Windows pipe buffer: the parent must drain stderr too.
        std::cerr << std::string(150000, 'x') << std::flush;
        std::string line;
        while(std::getline(std::cin, line)){
            const auto request = Json::parse(line);
            const std::string operation = request.at("op");
            if(operation == "bad") std::cout << std::string(9000, 'x') << std::endl;
            else if(operation == "hang") std::this_thread::sleep_for(std::chrono::seconds(30));
            else if(operation == "flood"){
                for(int i = 0; i < 500; i++) std::cout << "{\"v\":2,\"event\":\"status\",\"data\":{}}\n";
                std::cout << std::flush;
            }
            else std::cout << Json{{"v", 2}, {"id", request.at("id")}, {"ok", true}, {"data", Json::object()}}.dump() << std::endl;
        }
        return 0;
    }
}

int main(int argc, char* argv[]){
    if(argc == 2 && std::string(argv[1]) == "--ipc") return Fixture();
    try{
        VoiceProcess process;
        process.Start("this-voice-client-does-not-exist");
        Event(process, "process_error");
        Wait([&]{ return !process.Running(); });
        process.Start(argv[0]);
        Event(process, "ready");
        Check(process.Send({{"v", 2}, {"id", "one"}, {"op", "status"}}), "Could not queue IPC request");
        Wait([&]{
            for(const auto& message : process.Poll()) if(message.value("id", "") == "one") return true;
            return false;
        });
        Check(!process.Send({{"v", 2}, {"id", "large"}, {"op", std::string(9000, 'x')}}), "Oversized IPC input accepted");
        process.Send({{"v", 2}, {"id", "bad"}, {"op", "bad"}});
        Event(process, "process_error");
        Wait([&]{ return !process.Running(); });
        process.Start(argv[0]); Event(process, "ready");
        process.Send({{"v", 2}, {"id", "flood"}, {"op", "flood"}});
        Wait([&]{ return !process.Running(); }); //Intentionally do not drain stdout events.
        Check(process.Poll().size() <= 128, "IPC receive queue grew without a bound");
        process.Start(argv[0]); Event(process, "ready");
        process.Send({{"v", 2}, {"id", "hang"}, {"op", "hang"}});
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        const auto before = Clock::now();
        process.Stop();
        Check(Clock::now() - before < std::chrono::seconds(4), "A wedged voice child held GFX shutdown open");
        Check(!process.Running(), "Voice worker survived shutdown");
        std::cout << "Voice process failure and pipe checks passed\n";
    }
    catch(const std::exception& error){ std::cerr << error.what() << '\n'; return 1; }
}
