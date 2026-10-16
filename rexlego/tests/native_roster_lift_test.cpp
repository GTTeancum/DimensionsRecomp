#include "native_roster.h"
#include "native_roster_model.h"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
using namespace legodimensions::native_roster;
using namespace std::chrono_literals;
void idle(Controller& c) { for(int i=0;i<2000 && c.Read().busy;i++) std::this_thread::sleep_for(1ms); assert(!c.Read().busy); }
int main(int argc,char**argv) {
 assert(argc==2); std::filesystem::path root=argv[1]; std::filesystem::create_directories(root);
 std::array<std::filesystem::path,4> paths{};
 for(int i=0;i<4;i++){ paths[i]=root/(std::to_string(i)+".bin"); std::ofstream f(paths[i]); f<<std::string(180,'T'); }
 std::mutex m; std::array<bool,7> owned{},present{}; int removes=0,adds=0; bool save_ok=true;
 auto lifted_at=std::chrono::steady_clock::now();
 Backend b;
 b.occupied=[&]{std::lock_guard l(m); return owned;};
 b.load_fresh=[&](Entry,const auto&,Slot s){std::lock_guard l(m); owned[int(s)]=present[int(s)]=true; ++adds; return Result{true,{}};};
 b.temporary_remove=[&](Slot s){std::lock_guard l(m); assert(owned[int(s)]); if(present[int(s)]){present[int(s)]=false;++removes;lifted_at=std::chrono::steady_clock::now();} return Result{true,{}};};
 b.move=[&](Slot a,Slot z){std::lock_guard l(m);assert(owned[int(a)]);if(a!=z){assert(!owned[int(z)]);assert(std::chrono::steady_clock::now()-lifted_at>=500ms);} if(!present[int(a)] || a!=z)++adds;owned[int(a)]=present[int(a)]=false;owned[int(z)]=present[int(z)]=true; return Result{true,{}};};
 b.remove=[&](Slot s){std::lock_guard l(m);if(!save_ok)return Result{false,"save failed"};if(present[int(s)])++removes;owned[int(s)]=present[int(s)]=false;return Result{true,{}};};
 Controller c(b,paths);assert(c.Place(Entry::Batman,Slot::Left1));idle(c);
 assert(c.Lift(Entry::Batman));idle(c);assert(c.Read().lifted_entry==0 && c.Read().slots[0]==0);assert(!present[0] && owned[0]);
 std::this_thread::sleep_for(550ms);assert(!present[0] && c.Read().lifted_entry==0); // No automatic drop.
 assert(!c.Place(Entry::Gandalf,Slot::Center));
 assert(c.Lift(Entry::Batman));idle(c);assert(removes==1); // Repeated pickup idempotent.
 assert(c.Drop(Slot::Right3));idle(c);assert(c.Read().slots[0]==6 && c.Read().lifted_entry==-1 && present[6]);assert(removes==1 && adds==2);
 assert(c.Lift(Entry::Batman));idle(c);c.Restore();idle(c);assert(present[6] && c.Read().lifted_entry==-1);
 assert(c.Lift(Entry::Batman));idle(c);assert(c.Drop(Slot::Right3));idle(c);assert(present[6]); // Same-slot restore.
 assert(c.Lift(Entry::Batman));idle(c);assert(c.Drop(Slot::Center));c.CancelMove();idle(c);assert(present[6] && c.Read().slots[0]==6); // Pending cancellation.
 assert(c.Lift(Entry::Batman));idle(c);save_ok=false;assert(c.Remove(Entry::Batman));idle(c);assert(c.Read().lifted_entry==0 && owned[6] && !present[6]);
 save_ok=true;int before=removes;assert(c.Remove(Entry::Batman));idle(c);assert(removes==before && !owned[6]); // No second remove event.
 assert(c.Place(Entry::Batman,Slot::Left1));idle(c);assert(c.Place(Entry::Gandalf,Slot::Center));idle(c);
 assert(c.Lift(Entry::Batman));idle(c);assert(c.Drop(Slot::Center));idle(c);assert(c.Read().lifted_entry==0 && !present[0] && present[3]);
 c.Shutdown();assert(present[0] && c.Read().lifted_entry==-1);c.Shutdown();assert(!c.Lift(Entry::Batman));
 // Model never claims real carrying before the backend confirms pickup.
 Snapshot s;s.occupancy_known=true;s.slots[0]=0;s.occupied[0]=true;
 PadModel p;p.focus=0;assert(p.Activate(s)==PadModel::Action::Lift);assert(p.mode==PadModel::Mode::Browse);
 p.Submitted(PadModel::Operation::Lift);s.lifted_entry=0;p.Observe(s);assert(p.mode==PadModel::Mode::Carrying);
 assert(p.Back()==PadModel::Action::Cancel);p.Submitted(PadModel::Operation::Restore);s.lifted_entry=-1;p.Observe(s);assert(p.mode==PadModel::Mode::Browse);
}
