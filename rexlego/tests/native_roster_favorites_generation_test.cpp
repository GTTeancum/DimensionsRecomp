#include "native_roster_browser.h"
#include <cassert>
using namespace legodimensions::native_roster;
using namespace std::chrono_literals;
int main() {
 std::mutex m;std::condition_variable cv;bool entered=false,release=false;int calls=0;
 CatalogBrowser browser([&](const auto&,const auto&){
   std::unique_lock lock(m);
   if(++calls==1){entered=true;cv.notify_all();cv.wait(lock,[&]{return release;});return std::string("old save failed");}
   return std::string("latest save failed");
 });
 browser.favorites_path="unused-test-path";
 Tag a;a.path="resource_1001.bin";a.initial_id=ExpectedModel(1001);assert(MetadataFor(a.path));
 browser.ToggleFavorite(a);
 {std::unique_lock lock(m);assert(cv.wait_for(lock,3s,[&]{return entered;}));}
 browser.ToggleFavorite(a); // Latest desired set is empty.
 assert(browser.favorites.empty());browser.ObserveFavorites();assert(browser.status=="Saving favorites...");
 {std::lock_guard lock(m);release=true;cv.notify_all();}
 browser.ShutdownFavorites();
 assert(browser.favorites.empty()); // No stale rollback to the first toggle.
 assert(browser.status.find("latest save failed")!=std::string::npos);
 assert(browser.status.find("old save failed")==std::string::npos);
 assert(browser.status.find("Session changes retained")!=std::string::npos);
 browser.ToggleFavorite(a); // Stopped writer: explicit unsaved, no new I/O.
 assert(browser.IsFavorite(a) && browser.status.find("not saved")!=std::string::npos);
 browser.ObserveFavorites();assert(browser.status.find("writer stopped")!=std::string::npos);
}
