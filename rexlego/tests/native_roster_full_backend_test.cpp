#include "native_roster_backend.h"
#include <rex/input/portal/emulated_toypad.h>
#include <rex/logging.h>
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
using namespace legodimensions::native_roster;
namespace fs=std::filesystem;
void Idle(Controller& c) {
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);
 while(c.Read().busy&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(2));
 assert(!c.Read().busy&&!c.Read().faulted);
}
int main(int argc,char**argv) {
 assert(argc==3);const fs::path source=argv[1],root=argv[2];assert(fs::create_directory(root));
 size_t files=0;
 for(const auto&e:fs::directory_iterator(source))if(e.path().extension()==".bin") {
  assert(fs::file_size(e.path())==180);assert(fs::copy_file(e.path(),root/e.path().filename()));++files;
 }
 assert(files==315);
 const auto catalog=DiscoverCatalog(root,rex::input::EmulatedToypad::InspectFigureId);
 size_t valid=0;for(const auto&t:catalog)if(t.unavailable.empty())++valid;assert(valid==315);
 rex::InitLoggingEarly();
 {rex::input::EmulatedToypad pad(false);Controller c(MakeBackend([&]{return &pad;},catalog),catalog);
 std::set<TagUid> picked;std::vector<size_t> owners;
 for(size_t i=0;i<catalog.size()&&owners.size()<7;++i)if(catalog[i].unavailable.empty()&&picked.insert(catalog[i].uid).second) {
   assert(c.Place(static_cast<Entry>(i),static_cast<Slot>(owners.size())));Idle(c);assert(c.Read().slots[i]==int(owners.size()));owners.push_back(i);
 }
 assert(owners.size()==7);for(bool b:c.Read().occupied)assert(b);
 assert(c.Lift(static_cast<Entry>(owners[3])));Idle(c);assert(c.Read().lifted_entry==int(owners[3]));
 std::this_thread::sleep_for(std::chrono::milliseconds(550));assert(c.Read().lifted_entry==int(owners[3]));
 assert(c.Drop(Slot::Center));Idle(c);assert(c.Read().lifted_entry<0);
 assert(c.Clear());Idle(c);for(bool b:c.Read().occupied)assert(!b);
 // Exercise fresh identity for every existing bundled token in turn. No tag
 // payload is fabricated and all writes are confined to disposable copies.
 for(size_t i=0;i<catalog.size();++i)if(catalog[i].unavailable.empty()) {
  assert(c.Place(static_cast<Entry>(i),Slot::Center));Idle(c);assert(c.Read().slots[i]==3);
  assert(c.Remove(static_cast<Entry>(i)));Idle(c);assert(c.Read().slots[i]<0);
 }
 c.Shutdown();}
 rex::FlushLogging();std::cout<<"PASS 315 existing token identities, seven slots, held pickup, restore, clear and sequential placement\n";
}
