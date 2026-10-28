#include "triggerlab/AtomicFile.hpp"
#include <atomic>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

namespace triggerlab {
bool writeAtomicTextFile(const std::filesystem::path& path,const std::string& data,std::string& error) {
    std::filesystem::path directory;
    bool owned=false;
    try {
        if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
#ifdef _WIN32
        // Win32 replacement may reject concurrent moves to the same destination.
        struct WriterLock {
            HANDLE handle=nullptr;
            explicit WriterLock(const std::filesystem::path& destination) {
                auto name=std::filesystem::absolute(destination).lexically_normal().wstring();
                if(!name.empty()) CharLowerBuffW(name.data(),static_cast<DWORD>(name.size()));
                std::uint64_t hash=14695981039346656037ULL;
                for(wchar_t character:name) {hash^=static_cast<std::uint64_t>(character);hash*=1099511628211ULL;}
                const auto key=L"Local\\TriggerLabSettings-"+std::to_wstring(hash);
                handle=CreateMutexW(nullptr,FALSE,key.c_str());
                if(!handle) throw std::runtime_error("Cannot create settings writer lock");
                const auto status=WaitForSingleObject(handle,5000);
                if(status!=WAIT_OBJECT_0 && status!=WAIT_ABANDONED) {
                    CloseHandle(handle);handle=nullptr;
                    throw std::runtime_error("Settings writer lock timed out");
                }
            }
            ~WriterLock() {if(handle) {ReleaseMutex(handle);CloseHandle(handle);}}
            WriterLock(const WriterLock&)=delete;
            WriterLock& operator=(const WriterLock&)=delete;
        } writerLock(path);
#endif
        static std::atomic<unsigned long long> sequence{0};
        for(int attempt=0;attempt<64 && !owned;++attempt) {
            directory=path;
            directory += ".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(sequence.fetch_add(1));
            owned=std::filesystem::create_directory(directory);
        }
        if(!owned) throw std::runtime_error("Cannot reserve temporary settings directory");
        const auto temporary=directory/"data";
        {
            std::ofstream out(temporary,std::ios::binary|std::ios::trunc);
            out.write(data.data(),static_cast<std::streamsize>(data.size()));
            out.flush();
            if(!out) throw std::runtime_error("Cannot write temporary settings file");
            out.close();
            if(!out) throw std::runtime_error("Cannot close settings file");
        }
#ifdef _WIN32
        if(!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
            throw std::runtime_error("Cannot replace settings file");
#else
        std::filesystem::rename(temporary,path);
#endif
        std::error_code ignored;std::filesystem::remove(directory,ignored);
        error.clear();return true;
    } catch(const std::exception& e) {
        error=e.what();
        if(owned) {std::error_code ignored;std::filesystem::remove_all(directory,ignored);}
        return false;
    }
}

}
