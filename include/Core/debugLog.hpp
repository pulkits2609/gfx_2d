#pragma once

//CMake enables this only for the Debug configuration, like VOICE_DEBUG.
//The Release macro does not evaluate its arguments or format any messages.
#ifdef GFX_DEBUG
#include<chrono>
#include<ctime>
#include<iomanip>
#include<iostream>
#include<mutex>
#include<sstream>

namespace GfxDebug{
    inline void Print(const std::string& message){
        static std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        const auto now = std::chrono::system_clock::now();
        const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
        const std::time_t time = std::chrono::system_clock::to_time_t(now);
        std::tm utc{};
#ifdef _WIN32
        gmtime_s(&utc, &time);
#else
        gmtime_r(&time, &utc);
#endif
        std::ostringstream line;
        line << '[' << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
            << std::setfill('0') << std::setw(3) << milliseconds << "Z] [GFX DEBUG] " << message;
        std::cerr << line.str() << std::endl;
    }
}

#define GFX_DEBUG_LOG(message) \
    do{ \
        std::ostringstream gfxDebugMessage; \
        gfxDebugMessage << message; \
        GfxDebug::Print(gfxDebugMessage.str()); \
    }while(false)
#else
#define GFX_DEBUG_LOG(message) do{}while(false)
#endif
