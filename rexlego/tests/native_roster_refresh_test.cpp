#include "native_roster.h"
#include <cassert>
#include <chrono>
#include <thread>
using namespace legodimensions::native_roster;
int main() {
  const auto owner = std::this_thread::get_id();
  Backend b;
  b.occupied = [owner] { assert(std::this_thread::get_id() != owner); return std::array<bool,7>{true,false,true,false,false,true,false}; };
  b.load_fresh = [](Entry,const auto&,Slot) {return Result{};};
  b.remove = b.temporary_remove = [](Slot) {return Result{};};
  b.move = [](Slot,Slot) {return Result{};};
  Controller c(b,TagCatalog{}); assert(!c.Read().occupancy_known); assert(c.Refresh());
  for (int i = 0; i < 1000 && c.Read().busy; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
  auto s = c.Read(); assert(!s.busy && s.occupancy_known && s.occupied[0] && s.occupied[2] && s.occupied[5]);
  for (int slot : s.slots) assert(slot == -1); // Unknown occupants have no invented identity.
  c.Shutdown(); assert(!c.Refresh());
}
