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
void idle(Controller& c){for(int i=0;i<3000&&c.Read().busy;++i)std::this_thread::sleep_for(1ms);assert(!c.Read().busy);}
struct Mock {
 std::mutex m;std::condition_variable cv;std::array<bool,7> occupied{},present{};int fail=-1,removes=0,events=0;bool hold=false,entered=false,release=false;
 Backend backend(){Backend b;
 b.occupied=[this]{std::lock_guard l(m);return occupied;};
 b.load_fresh=[this](Entry,const auto&,Slot s){std::lock_guard l(m);occupied[int(s)]=present[int(s)]=true;return Result{true,{}};};
 b.temporary_remove=[this](Slot s){std::lock_guard l(m);present[int(s)]=false;++events;return Result{true,{}};};
 b.move=[this](Slot a,Slot z){std::lock_guard l(m);occupied[int(a)]=present[int(a)]=false;occupied[int(z)]=present[int(z)]=true;return Result{true,{}};};
 b.remove=[this](Slot s){std::unique_lock l(m);++removes;if(hold){entered=true;cv.notify_all();cv.wait(l,[this]{return release;});}if(int(s)==fail)return Result{false,"save failed"};if(present[int(s)])++events;occupied[int(s)]=present[int(s)]=false;return Result{true,{}};};return b;}
};
int main(int argc,char**argv){assert(argc==2);std::filesystem::path root=argv[1];std::filesystem::create_directories(root);TagCatalog tags;
 for(int i=0;i<7;++i){auto path=root/(std::to_string(i)+".bin");std::ofstream f(path);f<<std::string(180,'X');Tag t;t.path=path;t.name=std::to_string(i);tags.push_back(t);}
 auto fill=[&](Controller& c,int count){for(int i=0;i<count;++i){assert(c.Place(static_cast<Entry>(i),static_cast<Slot>(i)));idle(c);}};
 {Mock m;Controller c(m.backend(),tags);assert(!c.Clear());assert(m.removes==0);fill(c,7);assert(c.Lift(static_cast<Entry>(6)));idle(c);int events=m.events;assert(c.Clear());idle(c);assert(m.removes==7&&m.events==events+6);auto s=c.Read();assert(s.lifted_entry==-1);for(int v:s.slots)assert(v==-1);for(bool v:m.occupied)assert(!v);for(const auto&t:tags)assert(std::filesystem::file_size(t.path)==180);}
 {Mock m;Controller c(m.backend(),tags);fill(c,7);m.fail=2;assert(c.Clear());idle(c);auto s=c.Read();assert(s.slots[0]==-1&&s.slots[1]==-1&&s.slots[2]==2&&s.slots[6]==6);assert(m.removes==3&&m.occupied[2]);assert(s.status.find("save failed")!=std::string::npos);assert(!s.faulted);}
 {Mock m;Controller c(m.backend(),tags);fill(c,2);{std::lock_guard l(m.m);m.occupied[6]=m.present[6]=true;}assert(c.Clear());idle(c);assert(m.removes==2&&m.occupied[6]);} // Unmanaged slot untouched.
 {Mock m;Controller c(m.backend(),tags);fill(c,3);m.hold=true;assert(c.Clear());{std::unique_lock l(m.m);m.cv.wait(l,[&]{return m.entered;});}c.CancelClear();{std::lock_guard l(m.m);m.release=true;m.cv.notify_all();}idle(c);assert(m.removes==1&&m.occupied[1]&&m.occupied[2]);assert(c.Read().status.find("cancelled")!=std::string::npos);}
 {Mock m;Controller c(m.backend(),tags);fill(c,2);{std::lock_guard l(m.m);m.occupied[0]=false;}assert(c.Clear());idle(c);assert(c.Read().faulted&&m.removes==0&&m.occupied[1]);}
 ClearConfirmation confirm;assert(!confirm.Press());assert(confirm.armed());confirm.Tick(1);assert(confirm.Press());assert(!confirm.armed());assert(!confirm.Press());confirm.Tick(5);assert(!confirm.Press());confirm.Reset();assert(!confirm.armed());
 PadModel p;p.Submitted(PadModel::Operation::Clear);assert(p.Back()==PadModel::Action::Cancel);assert(p.Back()==PadModel::Action::None);
}
