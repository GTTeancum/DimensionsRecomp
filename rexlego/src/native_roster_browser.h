#pragma once
#include "native_roster_catalog.h"
#include "native_roster_metadata_lookup.h"
#include <set>
#include <sstream>
#include "native_roster_favorites_writer.h"
namespace legodimensions::native_roster {
struct CatalogBrowser {
  explicit CatalogBrowser(FavoritesWriter::Writer writer=FavoritesWriter::WriteAtomic)
      : favorites_writer_(std::move(writer)) {}
  // User-selected filters only. No current game puzzle/ability context inferred.
  int franchise=0, year=0, type=0, ability=0; // zero=all; type1=characters,2=vehicles
  bool favorites_only=false, filters_open=false;
  int filter_focus=0, cursor=0;
  std::set<int> favorites;
  std::vector<int> visible;
  std::filesystem::path favorites_path;
  std::string status;
  static constexpr int kPageSize=4;
  void Initialize(const TagCatalog& tags) {
    if (!tags.empty() && !tags[0].path.empty())
      favorites_path=tags[0].path.parent_path().parent_path()/"favorites.txt";
    LoadFavorites(); Rebuild(tags);
  }
  bool Matches(const Tag& tag) const {
    const auto* m=MetadataFor(tag);
    if (franchise && (!m || std::string_view(m->franchise)!=metadata::kFranchises[franchise-1])) return false;
    if (year && (!m || !(m->years & (1u<<year)))) return false;
    if (type && (!m || (type==1 ? m->build!=0 : m->build==0))) return false;
    if (ability && (!m || !(m->abilities[(ability-1)/64] & (uint64_t(1)<<((ability-1)%64))))) return false;
    if (favorites_only && (!m || !favorites.count(m->resource))) return false;
    return true;
  }
  void Rebuild(const TagCatalog& tags, int preserve=-1) {
    visible.clear();
    for(size_t i=0;i<tags.size() && i<kMaxCatalogEntries;++i) if(Matches(tags[i])) visible.push_back(static_cast<int>(i));
    cursor=0;
    for(size_t i=0;i<visible.size();++i) if(visible[i]==preserve) cursor=static_cast<int>(i);
  }
  int Selected() const { return visible.empty() ? -1 : visible[cursor]; }
  void Select(int entry) { for(size_t i=0;i<visible.size();++i) if(visible[i]==entry) cursor=static_cast<int>(i); }
  void Navigate(int direction) {
    if(visible.empty() || !direction) return;
    const int n=static_cast<int>(visible.size()); cursor=(cursor+(direction<0?n-1:1))%n;
  }
  void Page(int direction) {
    if(visible.empty())return;
    const int n=static_cast<int>(visible.size()), pages=(n+kPageSize-1)/kPageSize;
    const int page=(cursor/kPageSize+(direction<0?pages-1:1))%pages;
    cursor=std::min(n-1,page*kPageSize+cursor%kPageSize);
  }
  void AdjustFilter(int direction) {
    auto cycle=[&](int& value,int count){value=(value+(direction<0?count-1:1))%count;};
    switch(filter_focus) {
      case 0:cycle(franchise,metadata::kFranchises.size()+1);break;
      case 1:cycle(year,3);break;
      case 2:cycle(type,3);break;
      case 3:cycle(ability,metadata::kAbilities.size()+1);break;
      case 4:favorites_only=!favorites_only;break;
    }
  }
  bool IsFavorite(const Tag& tag) const { auto* m=MetadataFor(tag); return m && favorites.count(m->resource); }
  void ToggleFavorite(const Tag& tag) {
    const auto* m=MetadataFor(tag);
    if(!m){status="Favorites require a recognized catalog entry.";return;}
    if(favorites.count(m->resource)) favorites.erase(m->resource); else favorites.insert(m->resource);
    SaveFavorites(); // Optimistic session state; errors never roll back newer edits.
  }
  void LoadFavorites() {
    if(favorites_path.empty())return;
    std::error_code ec;
    if(!std::filesystem::exists(favorites_path,ec))return;
    if(ec || !std::filesystem::is_regular_file(favorites_path,ec) || ec ||
       std::filesystem::file_size(favorites_path,ec)>4096 || ec){status="Favorites file could not be read safely.";return;}
    std::ifstream f(favorites_path,std::ios::binary);
    if (!f) { status="Favorites could not be opened."; return; }
    std::array<char,4097> data{};
    f.read(data.data(),data.size());
    const auto count=f.gcount();
    if(count>4096 || !f.eof()){status="Favorites file exceeded its limit or could not be read.";return;}
    std::istringstream input(std::string(data.data(),static_cast<size_t>(count)));
    std::set<int> loaded; std::string line; size_t bytes=0;
    while(std::getline(input,line)) {
      bytes+=line.size()+1;
      if(bytes>4096 || line.empty() || line.size()>8 || line.find_first_not_of("0123456789")!=std::string::npos){status="Favorites file is invalid; no favorites loaded.";return;}
      int id=std::stoi(line);
      if(!KnownResource(id)){status="Favorites file contains an unknown entry.";return;}
      loaded.insert(id);
    }
    if(!input.eof()){status="Favorites read failed.";return;}
    favorites=std::move(loaded);
  }
  bool SaveFavorites() {
    const uint64_t generation=favorites_writer_.Submit(favorites_path,favorites);
    requested_generation_=generation;
    if(!generation){status="Favorites not saved: destination unavailable or writer stopped. Session changes retained.";return false;}
    status="Saving favorites...";return true;
  }
  void ObserveFavorites() {
    const auto done=favorites_writer_.Read();
    if(!requested_generation_ || done.generation!=requested_generation_ || done.generation==observed_generation_)return;
    observed_generation_=done.generation;
    status=done.error.empty() ? "Favorites saved." : "Favorites not saved: "+done.error+" Session changes retained.";
  }
  // Owner teardown can call this explicitly to inspect/log the final save status.
  // Regular frames only call ObserveFavorites(), which never performs file I/O.
  void ShutdownFavorites() {favorites_writer_.Shutdown();ObserveFavorites();}
 private:
  uint64_t requested_generation_=0, observed_generation_=0;
  FavoritesWriter favorites_writer_;
};
} // namespace legodimensions::native_roster
