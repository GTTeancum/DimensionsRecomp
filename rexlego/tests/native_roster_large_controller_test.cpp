#include "native_roster.h"
#include <cassert>
#include <chrono>
#include <fstream>
#include <thread>
using namespace legodimensions::native_roster;
static void Idle(Controller& c) {
  for(int i=0;i<2000 && c.Read().busy;++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  assert(!c.Read().busy);
}
int main(int argc,char** argv) {
  assert(argc==2); auto root=std::filesystem::path(argv[1]); assert(std::filesystem::create_directory(root));
  TagCatalog tags(315);
  for(size_t i=0;i<tags.size();++i) {
    tags[i].path=root/(std::to_string(i)+".bin"); tags[i].name="Token "+std::to_string(i);
    std::ofstream f(tags[i].path,std::ios::binary); f<<std::string(180,'T');
  }
  std::array<bool,7> occupied{};
  Backend b; b.occupied=[&]{return occupied;};
  b.load_fresh=[&](Entry,const auto&,Slot s){assert(!occupied[int(s)]);occupied[int(s)]=true;return Result{true,{}};};
  b.remove=[&](Slot s){occupied[int(s)]=false;return Result{true,{}};};
  b.temporary_remove=[](Slot){return Result{true,{}};};
  b.move=[&](Slot a,Slot z){occupied[int(a)]=false;occupied[int(z)]=true;return Result{true,{}};};
  Controller c(b,tags); assert(c.Read().slots.size()==315);
  for(int slot=0;slot<7;++slot) {assert(c.Place(static_cast<Entry>(308+slot),static_cast<Slot>(slot)));Idle(c);}
  auto s=c.Read(); for(int slot=0;slot<7;++slot) assert(s.slots[308+slot]==slot && s.occupied[slot]);
  assert(!c.Place(static_cast<Entry>(315),Slot::Center));
  assert(c.Place(static_cast<Entry>(1),Slot::Center)); Idle(c); assert(c.Read().slots[1]==-1);
  assert(c.Remove(static_cast<Entry>(314)));Idle(c);assert(!c.Read().occupied[6]);
  assert(c.Place(static_cast<Entry>(1),Slot::Right3));Idle(c);assert(c.Read().slots[1]==6);
  c.Shutdown(); std::filesystem::remove_all(root);
}
