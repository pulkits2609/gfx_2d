#include"Network/voiceProcess.hpp"
#include<chrono>
#include<filesystem>
#include<stdexcept>
#ifdef _WIN32
#include<windows.h>
#else
#include<fcntl.h>
#include<poll.h>
#include<spawn.h>
#include<sys/wait.h>
#include<unistd.h>
#include<csignal>
#include<pthread.h>
extern char** environ;
#endif

namespace{
    constexpr std::size_t MaximumLine = 8192, MaximumQueue = 128;
    using Json = Protocol::Json;
    Json Failure(const std::string& message){
        return {{"v", 2}, {"event", "process_error"}, {"data", {{"message", message}}}};
    }
#ifdef _WIN32
    void Close(HANDLE& handle){ if(handle && handle != INVALID_HANDLE_VALUE) CloseHandle(handle); handle = nullptr; }
#else
    void Close(int& handle){ if(handle >= 0) close(handle); handle = -1; }
#endif
}

struct VoiceProcess::Native{
#ifdef _WIN32
    HANDLE input = nullptr, output = nullptr, diagnostics = nullptr;
    std::atomic<HANDLE> process{nullptr};
#else
    int input = -1, output = -1, diagnostics = -1;
    std::atomic<pid_t> process{0};
#endif
};

VoiceProcess::VoiceProcess():native(std::make_unique<Native>()){}
VoiceProcess::~VoiceProcess(){ Stop(); }

std::string VoiceProcess::DefaultExecutable(){
#ifdef _WIN32
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    path.resize(size);
    return (std::filesystem::path(path).parent_path() / "voice" / "voice_client.exe").u8string();
#else
    std::array<char, 4096> path{};
    const auto size = readlink("/proc/self/exe", path.data(), path.size() - 1);
    if(size <= 0) return "voice/voice_client";
    return (std::filesystem::path(std::string(path.data(), size)).parent_path() / "voice" / "voice_client").string();
#endif
}

void VoiceProcess::Start(const std::string& executable){
    Stop();
    { std::lock_guard<std::mutex> lock(mutex); incoming.clear(); outgoing.clear(); }
    stopping = finished = false;
    worker = std::thread([this, executable]{ Run(executable); });
}

bool VoiceProcess::Send(const Json& message){
    const std::string line = message.dump() + "\n";
    if(line.size() > MaximumLine + 1 || stopping || finished) return false;
    std::lock_guard<std::mutex> lock(mutex);
    if(outgoing.size() >= MaximumQueue) return false;
    outgoing.push_back(line);
    return true;
}

std::vector<Json> VoiceProcess::Poll(){
    std::lock_guard<std::mutex> lock(mutex);
    std::vector<Json> messages;
    messages.swap(incoming);
    return messages;
}

bool VoiceProcess::Publish(const Json& message){
    std::lock_guard<std::mutex> lock(mutex);
    if(incoming.size() >= MaximumQueue) return false;
    incoming.push_back(message);
    return true;
}

void VoiceProcess::Stop(){
    stopping = true;
    //Normally stdin EOF lets the child stop its devices. A wedged child must
    //not hold GFX open forever, including a child that stops reading stdin.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while(worker.joinable() && !finished && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if(!finished){
#ifdef _WIN32
        if(auto process = native->process.load()) TerminateProcess(process, 1);
#else
        if(auto process = native->process.load()) kill(process, SIGKILL);
#endif
    }
    if(worker.joinable()) worker.join();
}

void VoiceProcess::Run(const std::string& executable){
    std::string buffered, pending, diagnostics;
    std::size_t offset = 0;
    bool launched = false;
    try{
        const auto path = std::filesystem::absolute(std::filesystem::u8path(executable));
#ifdef _WIN32
        HANDLE childInput = nullptr, childOutput = nullptr, childDiagnostics = nullptr;
        SECURITY_ATTRIBUTES security{sizeof(SECURITY_ATTRIBUTES), nullptr, TRUE};
        if(!CreatePipe(&childInput, &native->input, &security, 65536)
            || !CreatePipe(&native->output, &childOutput, &security, 65536)
            || !CreatePipe(&native->diagnostics, &childDiagnostics, &security, 65536)){
            Close(childInput); Close(childOutput); Close(childDiagnostics);
            throw std::runtime_error("Could not create voice pipes");
        }
        SetHandleInformation(native->input, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(native->output, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(native->diagnostics, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = childInput;
        startup.StartupInfo.hStdOutput = childOutput;
        startup.StartupInfo.hStdError = childDiagnostics;
        SIZE_T bytes = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        std::vector<unsigned char> attributes(bytes);
        startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(attributes.data());
        const bool initialized = InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes) != FALSE;
        HANDLE handles[]{childInput, childOutput, childDiagnostics};
        const bool restricted = initialized && UpdateProcThreadAttribute(startup.lpAttributeList, 0,
            PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr);
        PROCESS_INFORMATION process{};
        std::wstring command = L"\"" + path.wstring() + L"\" --ipc";
        const bool created = restricted && CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, path.parent_path().c_str(), &startup.StartupInfo, &process);
        if(initialized) DeleteProcThreadAttributeList(startup.lpAttributeList);
        Close(childInput); Close(childOutput); Close(childDiagnostics);
        if(!created) throw std::runtime_error("Voice client could not start. Check --voice-client or the voice runtime folder.");
        CloseHandle(process.hThread);
        native->process = process.hProcess;
#else
        //Only the pipe worker blocks SIGPIPE; the game keeps its existing signal handling.
        sigset_t blocked; sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
        int input[2]{-1, -1}, output[2]{-1, -1}, errors[2]{-1, -1};
        if(pipe2(input, O_CLOEXEC) || pipe2(output, O_CLOEXEC) || pipe2(errors, O_CLOEXEC)){
            for(auto pair : {input, output, errors}){ Close(pair[0]); Close(pair[1]); }
            throw std::runtime_error("Could not create voice pipes");
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, input[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO);
        posix_spawn_file_actions_adddup2(&actions, errors[1], STDERR_FILENO);
        for(int fd : {input[0], input[1], output[0], output[1], errors[0], errors[1]}) posix_spawn_file_actions_addclose(&actions, fd);
        std::string executablePath = path.string();
        char mode[] = "--ipc";
        char* arguments[]{executablePath.data(), mode, nullptr};
        pid_t process = 0;
        const int error = posix_spawn(&process, executablePath.c_str(), &actions, nullptr, arguments, environ);
        posix_spawn_file_actions_destroy(&actions);
        Close(input[0]); Close(output[1]); Close(errors[1]);
        native->input = input[1]; native->output = output[0]; native->diagnostics = errors[0];
        if(error) throw std::runtime_error("Voice client could not start. Check --voice-client or the voice runtime folder.");
        native->process = process;
        for(int fd : {native->input, native->output, native->diagnostics}) fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);
#endif
        launched = true;
        const auto readPipe = [&](auto pipe, bool json){
            std::array<char, 4096> bytes{};
            for(unsigned part = 0; part < 8; part++){
#ifdef _WIN32
                DWORD available = 0, count = 0;
                if(!PeekNamedPipe(pipe, nullptr, 0, nullptr, &available, nullptr) || !available) break;
                if(!ReadFile(pipe, bytes.data(), std::min<DWORD>(available, bytes.size()), &count, nullptr)) break;
#else
                const auto count = read(pipe, bytes.data(), bytes.size());
                if(count <= 0) break;
#endif
                if(!json){
                    diagnostics.append(bytes.data(), count);
                    if(diagnostics.size() > 1024) diagnostics.erase(0, diagnostics.size() - 1024);
                    continue;
                }
                for(std::size_t i = 0; i < static_cast<std::size_t>(count); i++){
                    if(bytes[i] == '\n'){
                        if(!buffered.empty() && buffered.back() == '\r') buffered.pop_back();
                        auto message = Json::parse(buffered);
                        if(!message.is_object() || message.value("v", 0) != 2 || !Publish(message))
                            throw std::runtime_error("Invalid or excessive voice IPC output");
                        buffered.clear();
                    }
                    else{
                        if(buffered.size() >= MaximumLine) throw std::runtime_error("Voice IPC line exceeds 8192 bytes");
                        buffered += bytes[i];
                    }
                }
            }
        };
        while(!stopping){
            readPipe(native->output, true);
            readPipe(native->diagnostics, false);
#ifdef _WIN32
            if(WaitForSingleObject(native->process, 0) == WAIT_OBJECT_0) break;
#else
            int status = 0;
            if(waitpid(native->process, &status, WNOHANG) > 0){ native->process = 0; break; }
#endif
            if(pending.empty()){
                std::lock_guard<std::mutex> lock(mutex);
                if(!outgoing.empty()){ pending = std::move(outgoing.front()); outgoing.pop_front(); offset = 0; }
            }
            if(!pending.empty()){
#ifdef _WIN32
                DWORD count = 0;
                if(!WriteFile(native->input, pending.data() + offset, static_cast<DWORD>(pending.size() - offset), &count, nullptr))
                    throw std::runtime_error("Voice input pipe closed");
#else
                const auto count = write(native->input, pending.data() + offset, pending.size() - offset);
                if(count < 0 && errno != EAGAIN && errno != EINTR) throw std::runtime_error("Voice input pipe closed");
#endif
                if(count > 0) offset += count;
                if(offset == pending.size()) pending.clear();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(!stopping) Publish(Failure("Voice client exited. Use Retry voice to reconnect."));
    }
    catch(const std::exception& error){ Publish(Failure(error.what())); }
    Close(native->input); //EOF requests a clean child shutdown.
    if(launched){
#ifdef _WIN32
        if(WaitForSingleObject(native->process, 1000) == WAIT_TIMEOUT) TerminateProcess(native->process, 1);
        WaitForSingleObject(native->process, 1000);
        HANDLE process = native->process.exchange(nullptr); Close(process);
#else
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
        while(native->process && std::chrono::steady_clock::now() < deadline){
            if(waitpid(native->process, nullptr, WNOHANG) > 0){ native->process = 0; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        if(auto process = native->process.exchange(0)){ kill(process, SIGKILL); waitpid(process, nullptr, 0); }
#endif
    }
    Close(native->output); Close(native->diagnostics);
    finished = true;
}
