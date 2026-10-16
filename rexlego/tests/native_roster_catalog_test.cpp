#include "native_roster_catalog.h"
#include "native_roster_model.h"
#include <cassert>
#include <fstream>
#include <iostream>
using namespace legodimensions::native_roster;
// Synthetic tags are ONLY used with this test's mock ID inspector. Never load
// them into an SDK Toy Pad, and never use this writer in application code.
static TagBytes Bytes(uint32_t id, uint8_t uid) {
  TagBytes b{}; b[0]=4; b[1]=uid; b[144]=id&255; b[145]=(id>>8)&255; return b;
}
static uint32_t Id(const TagBytes& b) { return uint32_t(b[144]) | uint32_t(b[145])<<8; }
static void Write(const std::filesystem::path& p,const TagBytes& b) {
  std::ofstream f(p,std::ios::binary); f.write(reinterpret_cast<const char*>(b.data()),b.size());
}
int main(int argc,char** argv) {
  assert(argc==2);
  auto root=std::filesystem::path(argv[1]); assert(std::filesystem::create_directory(root));
  Write(root/"batman.bin",Bytes(1,1)); Write(root/"gandalf.bin",Bytes(2,2));
  Write(root/"wyldstyle.bin",Bytes(3,3)); Write(root/"batmobile.bin",Bytes(1006,4));
  Write(root/"alpha.bin",Bytes(55,5)); Write(root/"beta.BIN",Bytes(55,6));
  Write(root/"copy.bin",Bytes(55,5)); // same UID is a selectable alternative, never simultaneous
  Write(root/"blank.bin",Bytes(0,7));
  Write(root/"unknown.bin",Bytes(60000,8));
  Write(root/"zero.bin",TagBytes{});
  {std::ofstream f(root/"short.bin");f<<"invalid";}
  std::filesystem::create_hard_link(root/"alpha.bin",root/"hardlink.bin");
  std::error_code symlink_error;
  std::filesystem::create_symlink(root/"alpha.bin",root/"symlink.bin",symlink_error);
  if (symlink_error) {
    std::cerr << "Symlink fixture unavailable: " << symlink_error.value() << " " << symlink_error.message() << '\n';
#ifdef _WIN32
    // Windows privilege-not-held (1314) or access denied (5) limits only this
    // optional fixture. Do not request elevated access or change OS settings.
    if (symlink_error.value()!=1314 && symlink_error.value()!=5) return 2;
#else
    return 2;
#endif
  }
  auto tags=DiscoverCatalog(root,Id);
  assert(tags.size()==9); // four starters + alpha,beta,blank,copy,unknown
  assert(tags[0].initial_id==1 && tags[3].initial_id==1006);
  assert(tags[4].name=="alpha" && tags[5].name=="beta");
  assert(tags[4].uid!=tags[5].uid && tags[4].initial_id==tags[5].initial_id);
  assert(SameToken(tags[0],Bytes(1,1),1));
  assert(!SameToken(tags[0],Bytes(2,1),2));
  assert(!SameToken(tags[0],Bytes(1,9),1));
  assert(SameToken(tags[3],Bytes(1008,4),1008));
  assert(!SameToken(tags[3],Bytes(60000,4),60000));
  assert(tags[6].initial_id==0 && tags[6].name.find("blank gadget")!=std::string::npos);
  assert(SameToken(tags[6],Bytes(1006,7),1006));
  assert(!SameToken(tags[6],Bytes(1,7),1));
  assert(tags[7].uid==tags[4].uid);
  assert(!SameToken(tags[8],Bytes(60001,8),60001));
  TagBytes reread{}; assert(ReadWritableTag(root/"batman.bin",reread)); assert(reread==Bytes(1,1));
  assert(DisplayName("evil##\n.bin").find('#')==std::string::npos);
  auto again=DiscoverCatalog(root,Id); assert(again.size()==tags.size());
  for(size_t i=0;i<tags.size();++i) assert(again[i].path==tags[i].path);
  Snapshot s; s.slots.assign(315,-1); s.slots[314]=6; s.occupancy_known=true;
  assert(PadModel::Owner(s,6)==314);
  PadModel m; m.entry_count=315; m.mode=PadModel::Mode::Picker; m.entry=314;
  m.Navigate(0,1); assert(m.entry==0); m.Navigate(0,-1); assert(m.entry==314);
  m.Page(1); assert(m.entry<PadModel::kPageSize);
  m.entry=314; m.Activate(s); assert(m.focus==6 && m.mode==PadModel::Mode::Browse);
  s.slots.clear(); m.mode=PadModel::Mode::Picker; assert(m.Activate(s)==PadModel::Action::None);
  std::filesystem::remove_all(root);
}
