#include "native_roster_backend.h"
#include <rex/input/portal/emulated_toypad.h>
#include <rex/logging.h>
#include <cassert>
#include <chrono>
#include <fstream>
#include <iostream>
#include <thread>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace legodimensions::native_roster;
using namespace std::chrono_literals;
namespace fs=std::filesystem;
std::array<uint8_t,180> ReadTag(const fs::path& p) {
 std::array<uint8_t,180> b{};std::ifstream f(p,std::ios::binary);
 f.read(reinterpret_cast<char*>(b.data()),180);assert(f.gcount()==180);return b;
}
void Idle(Controller& c) {
 const auto deadline=std::chrono::steady_clock::now()+5s;
 while(c.Read().busy && std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(2ms);
 assert(!c.Read().busy && !c.Read().faulted);
}
void Drain(rex::input::EmulatedToypad& p) {
 for(int i=0;i<256;++i){std::array<uint8_t,32>b{};uint32_t n=0;uint16_t state=0;p.Read(b,n,state);if(!n)return;}
 assert(false && "Unexpected response backlog");
}
int main(int argc,char**argv) {
 assert(argc==3);const fs::path source=argv[1],root=argv[2];
 assert(fs::create_directory(root));
 std::array<fs::path,4> tags{};const char*names[]={"batman.bin","gandalf.bin","wyldstyle.bin","batmobile.bin"};
 const uint32_t ids[]={1,2,3,1006};
 for(int i=0;i<4;++i){tags[i]=root/names[i];assert(fs::copy_file(source/names[i],tags[i]));assert(rex::input::EmulatedToypad::InspectFigureId(ReadTag(tags[i]))==ids[i]);}
 rex::InitLoggingEarly();
 {
  rex::input::EmulatedToypad portal(false);
  auto backend=MakeBackend([&]{return &portal;}, DiscoverCatalog(root, rex::input::EmulatedToypad::InspectFigureId));
  Controller c(backend,tags);
  for(int i=0;i<3;++i){assert(c.PlaceInRegion(static_cast<Entry>(i),Region::Left));Idle(c);assert(c.Read().slots[i]==i);}
  assert(c.PlaceInRegion(Entry::Batmobile,Region::Left));Idle(c);assert(c.Read().slots[3]==-1);
  assert(c.PlaceInRegion(Entry::Batmobile,Region::Center));Idle(c);assert(c.Read().slots[3]==3);
  assert(c.PlaceInRegion(Entry::Batman,Region::Center));Idle(c);assert(c.Read().slots[0]==0);
  Drain(portal);
  const auto start=std::chrono::steady_clock::now();
  assert(c.PlaceInRegion(Entry::Batmobile,Region::Right));Idle(c);
  assert(std::chrono::steady_clock::now()-start>=500ms && c.Read().slots[3]==4);
  Drain(portal);
  // Exercise real guest-facing tag write, on a disposable test copy only.
  auto expected=ReadTag(tags[3]);expected[152]=0x19;expected[153]=0x27;expected[154]=0x35;expected[155]=0x43;
  std::array<uint8_t,32>write{0x55,9,0xD3,1,3,38,0x19,0x27,0x35,0x43};
  assert(XSUCCEEDED(portal.Write(write)));assert(ReadTag(tags[3])==expected);
  Drain(portal);
#ifdef _WIN32
  // Deny replacement of this test copy: failed removal must retain the figure.
  HANDLE held=CreateFileW(tags[3].c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
  assert(held!=INVALID_HANDLE_VALUE);
  assert(c.Remove(Entry::Batmobile));Idle(c);assert(c.Read().slots[3]==4 && portal.GetOccupancy()[2]);
  assert(ReadTag(tags[3])==expected);assert(CloseHandle(held));
#endif
  assert(c.Remove(Entry::Batmobile));Idle(c);assert(c.Read().slots[3]==-1);
  assert(c.PlaceInRegion(Entry::Batmobile,Region::Center));Idle(c);assert(ReadTag(tags[3])==expected);
  assert(c.PlaceInRegion(Entry::Batmobile,Region::Right));std::this_thread::sleep_for(50ms);c.CancelMove();Idle(c);assert(c.Read().slots[3]==3);
  c.Shutdown();
  assert(c.Read().slots[3]==3);
 }
 {
  rex::input::EmulatedToypad restarted(false);
  Controller c(MakeBackend([&]{return &restarted;}, DiscoverCatalog(root, rex::input::EmulatedToypad::InspectFigureId)),tags);
  assert(c.PlaceInRegion(Entry::Batmobile,Region::Center));Idle(c);
  assert(c.Read().slots[3]==3 && ReadTag(tags[3])[152]==0x19);c.Shutdown();
 }
 rex::FlushLogging();
 std::cout<<"PASS: real SDK starter identity, capacity, moves, protocol writes, persistence, failed removal retention, reload and backend restart\n";
}
