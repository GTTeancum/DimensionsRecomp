#include "native_roster.h"
#include <cassert>
#include <fstream>
#include <mutex>
#include <thread>
#include <chrono>
#include <condition_variable>
using namespace legodimensions::native_roster;
using namespace std::chrono_literals;
void idle(Controller& c) { for(int i=0;i<1000 && c.Read().busy;i++) std::this_thread::sleep_for(2ms); assert(!c.Read().busy); }
int main(int argc, char** argv) {
 assert(argc==2);
 const std::filesystem::path root=argv[1];
 assert(std::filesystem::create_directory(root));
 std::array<std::filesystem::path,4> paths{};
 // Synthetic bytes only for mock tests; never sent to SDK or real backend.
 for(int i=0;i<4;i++) { paths[i]=root/("roster-mock-test-"+std::to_string(i)); std::ofstream f(paths[i], std::ios::binary); std::string data(180,'T'); f<<data; }
 std::array<bool,7> occupied{}; std::mutex m; int loads=0,removes=0,temps=0,moves=0; std::chrono::steady_clock::time_point removed;
 Backend b;
 b.occupied=[&] {std::lock_guard lock(m); return occupied;};
 b.load_fresh=[&](Entry, const auto&, Slot slot){ std::lock_guard lock(m); assert(!occupied[int(slot)]); occupied[int(slot)]=true; ++loads; return Result{true,{}};};
 b.remove=[&](Slot slot){std::lock_guard lock(m); occupied[int(slot)]=false; ++removes; return Result{true,{}};};
 b.temporary_remove=[&](Slot){std::lock_guard lock(m); removed=std::chrono::steady_clock::now(); ++temps; return Result{true,{}};};
 b.move=[&](Slot src,Slot dst){std::lock_guard lock(m); if(src!=dst) assert(std::chrono::steady_clock::now()-removed>=500ms); occupied[int(src)]=false; occupied[int(dst)]=true; ++moves; return Result{true,{}};};
 Controller c(b,paths);
 assert(c.Place(Entry::Batman,Slot::Left1)); idle(c); assert(loads==1 && c.Read().slots[0]==0);
 assert(c.Place(Entry::Gandalf,Slot::Left1)); idle(c); assert(loads==1 && c.Read().slots[1]==-1);
 assert(c.Place(Entry::Batman,Slot::Center)); idle(c); assert(moves==1 && c.Read().slots[0]==3);
 assert(c.Place(Entry::Batman,Slot::Right1));
 for(;;) {{std::lock_guard lock(m); if(temps==2)break;}std::this_thread::sleep_for(1ms);}
 c.CancelMove(); idle(c); assert(c.Read().slots[0]==3 && moves==2);
 assert(c.Remove(Entry::Batman)); idle(c); assert(removes==1 && c.Read().slots[0]==-1);
 assert(c.Place(Entry::Batman,Slot::Center)); idle(c); assert(loads==2);
 assert(c.Place(Entry::Batman,Slot::Right3));
 for(;;) {{std::lock_guard lock(m); if(temps==3)break;}std::this_thread::sleep_for(1ms);}
 c.Shutdown(); assert(c.Read().slots[0]==3 && moves==3); assert(!c.Place(Entry::Gandalf,Slot::Left1)); c.Shutdown();
 {
  std::lock_guard lock(m); occupied.fill(false);
 }
 Controller regions(b,paths);
 assert(regions.PlaceInRegion(Entry::Batman,Region::Left)); idle(regions);
 assert(regions.PlaceInRegion(Entry::Gandalf,Region::Left)); idle(regions);
 assert(regions.PlaceInRegion(Entry::Wyldstyle,Region::Left)); idle(regions);
 assert(regions.Read().slots[0]==0 && regions.Read().slots[1]==1 && regions.Read().slots[2]==2);
 assert(regions.PlaceInRegion(Entry::Batmobile,Region::Left)); idle(regions); assert(regions.Read().slots[3]==-1);
 assert(regions.PlaceInRegion(Entry::Batmobile,Region::Center)); idle(regions); assert(regions.Read().slots[3]==3);
 assert(regions.PlaceInRegion(Entry::Batman,Region::Center)); idle(regions); assert(regions.Read().slots[0]==0);
 assert(regions.PlaceInRegion(Entry::Batman,Region::Right)); idle(regions); assert(regions.Read().slots[0]==4);
 assert(regions.PlaceInRegion(Entry::Gandalf,Region::Right)); idle(regions); assert(regions.Read().slots[1]==5);
 regions.Shutdown();
 // Deterministically stop in the post-delay occupancy callback. Cancellation
 // must win before commit even though the 500ms wait already completed.
 {
  std::lock_guard lock(m); occupied.fill(false);
 }
 std::mutex gate_mutex;
 std::condition_variable gate_cv;
 int occupancy_calls = 0;
 bool at_commit_gate = false, release_gate = false;
 Backend gated = b;
 gated.occupied = [&] {
   {
     std::unique_lock lock(gate_mutex);
     // Placement check/finish are 1/2; move initial is 3; post-delay is 4.
     if (++occupancy_calls == 4) {
       at_commit_gate = true;
       gate_cv.notify_all();
       gate_cv.wait(lock, [&] { return release_gate; });
     }
   }
   std::lock_guard lock(m); return occupied;
 };
 Controller late_cancel(gated, paths);
 assert(late_cancel.Place(Entry::Batman, Slot::Left1)); idle(late_cancel);
 assert(late_cancel.Place(Entry::Batman, Slot::Right1));
 {
   std::unique_lock lock(gate_mutex);
   assert(gate_cv.wait_for(lock, 3s, [&] { return at_commit_gate; }));
 }
 late_cancel.CancelMove();
 {
   std::lock_guard lock(gate_mutex); release_gate = true;
 }
 gate_cv.notify_all();
 idle(late_cancel);
 assert(late_cancel.Read().slots[0] == 0);
 {
   std::lock_guard lock(m); assert(occupied[0] && !occupied[4]);
 }
 late_cancel.Shutdown();
 for(auto p:paths) std::filesystem::remove(p);
}
