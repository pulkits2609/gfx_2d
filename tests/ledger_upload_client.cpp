#include"Network/ledgerUpload.hpp"
#include<chrono>
#include<iostream>
#include<thread>

int main(int argc, char* argv[]){
    if(argc != 3) return 2;
    LedgerUpload upload;
    Protocol::Json result{{"resultId", "gfx-result-test"}, {"match", 7},
        {"durationSeconds", 180}, {"teamCount", 2}, {"winningTeam", 0},
        {"scores", {8, 5, 0, 0}}, {"players", Protocol::Json::array()}};
    std::string mode = argv[2];
    if(mode == "missing-id") result.erase("resultId");
    int port = std::stoi(argv[1]);
    std::string address = "127.0.0.1";
    if(mode == "invalid-address") address = "http://localhost";
    upload.Start(address, port, result);
    if(mode == "cancel"){
        upload.Reset();
        for(int i = 0; i < 20; i++){
            upload.Poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        return upload.GetState() == LedgerUpload::State::Idle ? 0 : 1;
    }
    unsigned polls = 0;
    auto started = std::chrono::steady_clock::now();
    while(upload.GetState() == LedgerUpload::State::Pending){
        upload.Poll();
        polls++;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
        if(std::chrono::steady_clock::now() - started > std::chrono::seconds(11)) return 3;
    }
    std::cout << Protocol::Json{{"saved", upload.GetState() == LedgerUpload::State::Saved},
        {"message", upload.GetStatusText()}, {"hash", upload.GetHash()}, {"polls", polls}}.dump() << '\n';
    return 0;
}
