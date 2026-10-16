#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <exception>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <utility>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif
namespace legodimensions::native_roster {
class FavoritesWriter final {
 public:
  using Values=std::set<int>;
  // Empty error means success. Injected writers support deterministic unit tests.
  using Writer=std::function<std::string(const std::filesystem::path&,const Values&)>;
  struct Completion { uint64_t generation=0; std::string error; };
  explicit FavoritesWriter(Writer writer=WriteAtomic):writer_(std::move(writer)),worker_([this]{Run();}){}
  ~FavoritesWriter(){Shutdown();}
  FavoritesWriter(const FavoritesWriter&)=delete;
  FavoritesWriter& operator=(const FavoritesWriter&)=delete;
  uint64_t Submit(const std::filesystem::path& path,const Values& values) {
    if(path.empty() || values.size()>315)return 0;
    for(int id:values)if(id<=0 || id>99999999)return 0;
    std::lock_guard lock(mutex_);
    if(stopping_)return 0;
    const auto generation=++generation_;
    pending_=Request{generation,path,values}; // Bounded: replace only the queued request.
    cv_.notify_one();return generation;
  }
  Completion Read() const {std::lock_guard lock(mutex_);return completion_;}
  // Owner teardown only, never per-frame or during a favorite toggle. Finish
  // active and latest queued save, reject new submits, then join. Idempotent.
  void Shutdown() {
    {std::lock_guard lock(mutex_);stopping_=true;cv_.notify_one();}
    if(worker_.joinable())worker_.join();
  }
  static std::string WriteAtomic(const std::filesystem::path& path,const Values& values) {
    std::string data;
    if(path.empty() || values.size()>315)return "Invalid favorites destination or entry count.";
    for(int id:values) {
      if(id<=0 || id>99999999)return "Invalid favorite resource ID.";
      data+=std::to_string(id);data+='\n';
    }
    if(data.size()>4096)return "Favorites exceed the storage limit.";
    static std::atomic<uint64_t> nonce{0};
    std::filesystem::path temp;
#ifdef _WIN32
    HANDLE file=INVALID_HANDLE_VALUE;
#else
    int file=-1;
#endif
    for(int attempt=0;attempt<16;++attempt) {
      temp=path;temp+=".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(++nonce);
#ifdef _WIN32
      file=CreateFileW(temp.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
      if(file!=INVALID_HANDLE_VALUE)break;
      if(GetLastError()!=ERROR_FILE_EXISTS && GetLastError()!=ERROR_ALREADY_EXISTS)
        return "Favorites temporary file could not be created.";
#else
      file=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL,0600);
      if(file>=0)break;
      if(errno!=EEXIST)return "Favorites temporary file could not be created.";
#endif
    }
#ifdef _WIN32
    if(file==INVALID_HANDLE_VALUE)return "Favorites temporary name collisions; previous file retained.";
#else
    if(file<0)return "Favorites temporary name collisions; previous file retained.";
#endif
    bool ok=true;size_t offset=0;
    while(offset<data.size()) {
#ifdef _WIN32
      DWORD written=0;
      if(!WriteFile(file,data.data()+offset,static_cast<DWORD>(data.size()-offset),&written,nullptr) || !written){ok=false;break;}
      offset+=written;
#else
      const auto written=::write(file,data.data()+offset,data.size()-offset);
      if(written<0 && errno==EINTR)continue;
      if(written<=0){ok=false;break;}
      offset+=static_cast<size_t>(written);
#endif
    }
#ifdef _WIN32
    if(ok && !FlushFileBuffers(file))ok=false;
    if(!CloseHandle(file))ok=false;
#else
    if(ok && ::fsync(file)!=0)ok=false;
    if(::close(file)!=0)ok=false;
#endif
    std::error_code ec;
    if(!ok){std::filesystem::remove(temp,ec);return "Favorites write failed; previous file retained.";}
#ifdef _WIN32
    if(!MoveFileExW(temp.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
      ec=std::make_error_code(std::errc::io_error);
#else
    std::filesystem::rename(temp,path,ec);
#endif
    if(ec){std::error_code ignored;std::filesystem::remove(temp,ignored);return "Favorites replace failed; previous file retained.";}
    return {};
  }
 private:
  struct Request {uint64_t generation;std::filesystem::path path;Values values;};
  void Run() {
    for(;;) {
      Request request;
      {std::unique_lock lock(mutex_);cv_.wait(lock,[&]{return stopping_ || pending_.has_value();});
       if(!pending_)return;
       request=std::move(*pending_);pending_.reset();}
      std::string error;
      try {error=writer_(request.path,request.values);}
      catch(const std::exception&){error="Favorites save failed unexpectedly; changes remain unsaved.";}
      catch(...){error="Favorites save failed unexpectedly; changes remain unsaved.";}
      {std::lock_guard lock(mutex_);completion_={request.generation,std::move(error)};}
    }
  }
  Writer writer_;
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::optional<Request> pending_;
  Completion completion_;
  uint64_t generation_=0;
  bool stopping_=false;
  std::thread worker_;
};
} // namespace legodimensions::native_roster
