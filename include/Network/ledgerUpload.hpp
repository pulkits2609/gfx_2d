#pragma once

#include<memory>
#include<string>
#include"Network/protocol.hpp"

//Optional HTTP adapter. Gameplay and the GFX server do not depend on it.
class LedgerUpload{
    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    public:
        enum class State{ Idle, Pending, Saved, Failed };
        LedgerUpload();
        ~LedgerUpload();
        LedgerUpload(const LedgerUpload&) = delete;
        LedgerUpload& operator=(const LedgerUpload&) = delete;
        void Start(const std::string& address, int port, const Protocol::Json& results);
        void Poll();
        void Reset();
        State GetState() const;
        const std::string& GetStatusText() const;
        const std::string& GetHash() const;
};
