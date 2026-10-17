#pragma once

#include"Network/protocol.hpp"
#include<atomic>
#include<deque>
#include<memory>
#include<mutex>
#include<thread>

//A small cross-platform pipe wrapper. The worker drains both child outputs;
//the render thread only exchanges bounded queues of JSON messages.
class VoiceProcess{
    public:
        VoiceProcess();
        ~VoiceProcess();
        VoiceProcess(const VoiceProcess&) = delete;
        VoiceProcess& operator=(const VoiceProcess&) = delete;
        void Start(const std::string& executable);
        bool Send(const Protocol::Json& message);
        std::vector<Protocol::Json> Poll();
        void Stop();
        bool Running() const{ return !finished; }
        static std::string DefaultExecutable();

    private:
        struct Native;
        std::unique_ptr<Native> native;
        std::thread worker;
        std::mutex mutex;
        std::deque<std::string> outgoing;
        std::vector<Protocol::Json> incoming;
        std::atomic<bool> stopping{false}, finished{true};
        void Run(const std::string& executable);
        bool Publish(const Protocol::Json& message);
};
