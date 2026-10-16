#include "native_roster_favorites_writer.h"
#include <cassert>
#include <fstream>
#include <future>
#include <vector>
using namespace legodimensions::native_roster;
using namespace std::chrono_literals;
static void Await(FavoritesWriter& w,uint64_t generation) {
  const auto deadline=std::chrono::steady_clock::now()+3s;
  while(w.Read().generation!=generation && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(1ms);
  assert(w.Read().generation==generation);
}
int main(int argc,char**argv) {
 assert(argc==2);const std::filesystem::path root=argv[1];assert(std::filesystem::create_directory(root));
 // Real exclusive-create, atomic-replace, cleanup and failure-preservation path.
 auto path=root/"favorites.txt";
 assert(FavoritesWriter::WriteAtomic(path,{1001}).empty());
 assert(FavoritesWriter::WriteAtomic(path,{1002,1003}).empty());
 {std::ifstream f(path);std::string data((std::istreambuf_iterator<char>(f)),{});assert(data=="1002\n1003\n");}
 assert(!FavoritesWriter::WriteAtomic(root/"missing"/"favorites.txt",{1001}).empty());
 assert(std::filesystem::create_directory(root/"directory-destination"));
 assert(!FavoritesWriter::WriteAtomic(root/"directory-destination",{1001}).empty());
 for(auto& e:std::filesystem::directory_iterator(root))assert(e.path().filename().string().find(".tmp-")==std::string::npos);
 // Block the active save: submits/read remain responsive; only latest queued
 // generation survives. Older failure must not override the latest completion.
 std::mutex mutex;std::condition_variable cv;bool entered=false,release=false;
 std::vector<int> calls;
 FavoritesWriter writer([&](const auto&,const auto& values){
   std::unique_lock lock(mutex);calls.push_back(*values.begin());
   if(calls.size()==1){entered=true;cv.notify_all();cv.wait(lock,[&]{return release;});return std::string("injected first failure");}
   return std::string{};
 });
 auto first=writer.Submit(path,{1});
 {std::unique_lock lock(mutex);assert(cv.wait_for(lock,3s,[&]{return entered;}));}
 const auto start=std::chrono::steady_clock::now();
 auto second=writer.Submit(path,{2});auto last=writer.Submit(path,{3});
 assert(first<second && second<last);assert(writer.Read().generation==0);
 assert(std::chrono::steady_clock::now()-start<250ms);
 auto shutdown=std::async(std::launch::async,[&]{writer.Shutdown();});
 assert(shutdown.wait_for(20ms)==std::future_status::timeout);
 {std::lock_guard lock(mutex);release=true;cv.notify_all();}
 assert(shutdown.wait_for(3s)==std::future_status::ready);shutdown.get();
 assert(writer.Read().generation==last && writer.Read().error.empty());
 assert((calls==std::vector<int>{1,3}));assert(writer.Submit(path,{4})==0);writer.Shutdown();
 // Exceptions are converted to visible failure, never worker termination.
 FavoritesWriter throwing([](const auto&,const auto&)->std::string{throw std::runtime_error("injected");});
 auto failed=throwing.Submit(path,{5});Await(throwing,failed);assert(!throwing.Read().error.empty());throwing.Shutdown();
 std::filesystem::remove_all(root);
}
