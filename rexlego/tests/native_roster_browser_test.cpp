#include "native_roster_browser.h"
#include <cassert>
#include <thread>
#include <chrono>
using namespace legodimensions::native_roster;
void Wait(CatalogBrowser& b) {
 auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);
 do {b.ObserveFavorites();if(b.status!="Saving favorites...")return;std::this_thread::sleep_for(std::chrono::milliseconds(1));} while(std::chrono::steady_clock::now()<end);
 assert(false);
}
int main(int argc,char** argv) {
  assert(argc==2); const std::filesystem::path root=argv[1];
  assert(std::filesystem::create_directory(root)); assert(std::filesystem::create_directory(root/"tags"));
  TagCatalog tags;
  for(const auto& m:metadata::kRecords){Tag t;t.path=root/"tags"/("resource_"+std::to_string(m.resource)+".bin");t.name=m.name;t.initial_id=ExpectedModel(m.resource);tags.push_back(t);}
  CatalogBrowser b;b.Initialize(tags);assert(b.visible.size()==315);
  assert(MetadataFor("batman.bin") && std::string(MetadataFor("batman.bin")->name)=="Batman");
  assert(!MetadataFor("resource_1001.bin.exe"));assert(!MetadataFor("1001.bin"));
  b.type=1;b.Rebuild(tags);assert(b.visible.size()==75);
  b.type=2;b.Rebuild(tags);assert(b.visible.size()==240);
  b.type=0;b.franchise=1;b.Rebuild(tags);
  for(int i:b.visible)assert(std::string(MetadataFor(tags[i].path)->franchise)==metadata::kFranchises[0]);
  b.franchise=0;b.year=2;b.Rebuild(tags);
  for(int i:b.visible)assert(MetadataFor(tags[i].path)->years&(1<<2));
  b.year=0;b.ability=1;b.Rebuild(tags);
  for(int i:b.visible)assert(MetadataFor(tags[i].path)->abilities[0]&1);
  b.ability=0;b.Rebuild(tags);b.cursor=314;b.Navigate(1);assert(b.cursor==0);b.Navigate(-1);assert(b.cursor==314);
  b.Page(1);assert(b.cursor<4);
  b.ToggleFavorite(tags[0]);Wait(b);assert(b.IsFavorite(tags[0]));
  CatalogBrowser restored;restored.Initialize(tags);assert(restored.IsFavorite(tags[0]));
  b.favorites_only=true;b.Rebuild(tags);assert(b.visible.size()==1 && b.visible[0]==0);
  b.ToggleFavorite(tags[0]);Wait(b);b.Rebuild(tags);assert(b.visible.empty() && b.Selected()==-1);b.Navigate(1);b.Page(-1);
  {std::ofstream f(root/"favorites.txt",std::ios::binary);f<<std::string(4097,'1');}
  CatalogBrowser invalid;invalid.Initialize(tags);assert(invalid.favorites.empty() && !invalid.status.empty());
  {std::ofstream f(root/"favorites.txt",std::ios::binary);f<<"99999999\n";}
  invalid.status.clear();invalid.LoadFavorites();assert(invalid.favorites.empty() && !invalid.status.empty());
  // A failed asynchronous save retains session edits and reports unsaved state.
  invalid.favorites_path=root/"missing"/"favorites.txt";invalid.ToggleFavorite(tags[0]);Wait(invalid);assert(!invalid.favorites.empty() && invalid.status.find("not saved")!=std::string::npos);
  b.ShutdownFavorites();restored.ShutdownFavorites();invalid.ShutdownFavorites();
  std::filesystem::remove_all(root);
}
