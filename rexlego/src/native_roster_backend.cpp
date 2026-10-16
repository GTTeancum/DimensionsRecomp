#include "native_roster_backend.h"
#include "native_roster_metadata_lookup.h"
#include "native_roster_portrait_binding.h"
#include <fstream>
#include <stdexcept>
#include <optional>
#include <memory>
#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>
#include <rex/input/portal/emulated_toypad.h>

REXCVAR_DEFINE_BOOL(native_roster, true, "Input",
  "Use the in-game token catalog. Off retains legacy companion or physical portal mode.")
  .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);
REXCVAR_DEFINE_STRING(native_roster_tags, "native-roster/tags", "Input",
  "Private writable .bin token directory; existing tags only, restart to rescan")
  .lifecycle(rex::cvar::Lifecycle::kRequiresRestart);

namespace legodimensions::native_roster {
namespace {
struct Position { uint8_t pad, index; };
struct ActiveToken { TagUid uid; size_t entry; };
constexpr std::array<Position,7> kPositions{{{2,0},{2,3},{2,4},{1,1},{3,2},{3,5},{3,6}}};
Position Translate(Slot slot) { return kPositions.at(static_cast<size_t>(slot)); }

}
bool ConfigureMode() {
  const bool enabled=REXCVAR_GET(native_roster);
  if(enabled) {
    rex::cvar::SetFlagByName("toypad_emulation", "true");
    rex::cvar::SetFlagByName("toypad_companion_services", "false");
  } else {
    rex::cvar::SetFlagByName("toypad_companion_services", "true");
  }
  return enabled;
}
TagCatalog ConfiguredTags() {
  std::filesystem::path root=REXCVAR_GET(native_roster_tags);
  if(root.empty())return {};
  if(root.is_relative())root=rex::filesystem::GetExecutableFolder()/root;
  std::string warning;
  auto catalog = DiscoverCatalog(root, [](const TagBytes& bytes) {
    return rex::input::EmulatedToypad::InspectFigureId(bytes);
  }, &warning);
  for (auto& tag : catalog) {
    if (const auto* info = MetadataFor(tag)) tag.name = info->name;
    if (tag.portrait < 0 && MetadataFor(tag)) tag.portrait = portrait_file::PortraitForTagPath(tag.path);
  }
  if (!warning.empty()) REXLOG_WARN("Native roster: {}", warning);
  REXLOG_INFO("Native roster: discovered {} catalog entries", catalog.size());
  return catalog;
}
Backend MakeBackend(std::function<rex::input::EmulatedToypad*()> resolve, TagCatalog catalog) {
  auto get=[resolve]()->rex::input::EmulatedToypad& {
    auto* p=resolve();
    if(!p)throw std::runtime_error("Native placement backend is not ready");
    return *p;
  };
  // All callbacks execute on the controller worker. UID ownership survives
  // temporary lift and moves, and is released only after successful removal.
  auto active = std::make_shared<std::array<std::optional<ActiveToken>, 7>>();
  auto learned = std::make_shared<std::vector<std::optional<TagBytes>>>(catalog.size());
  Backend backend;
  backend.occupied=[get] {
    const auto physical=get().GetOccupancy();
    std::array<bool,7> result{};
    for(size_t i=0;i<result.size();++i)result[i]=physical[kPositions[i].index];
    return result;
  };
  backend.load_fresh=[get,active,learned,catalog=std::move(catalog)](Entry entry,const std::filesystem::path& path,Slot slot)->Result {
    const size_t index = static_cast<size_t>(entry);
    if (index >= catalog.size() || catalog[index].path != path)
      return {false,"Unknown token or changed catalog path"};
    const auto& selected = catalog[index];
    if (!selected.unavailable.empty()) return {false, selected.unavailable};
    auto& portal=get();const auto pos=Translate(slot);
    if(portal.GetOccupancy()[pos.index])return {false,"Location occupied"};
    // Opening existing input/output never creates or truncates a tag. Check
    // exact size again here; the Controller's earlier check is not authoritative.
    std::fstream file(path,std::ios::in|std::ios::out|std::ios::binary);
    if(!file)return {false,"Tag is not readable and writable"};
    std::array<uint8_t,rex::input::kToypadTagSize> bytes{};
    file.read(reinterpret_cast<char*>(bytes.data()),bytes.size());
    if(file.gcount()!=static_cast<std::streamsize>(bytes.size()))return {false,"Tag is not 180 bytes"};
    char extra;if(file.get(extra))return {false,"Tag is larger than 180 bytes"};
    if(!file.eof())return {false,"Tag read failed"};
    file.close();
    const auto id=rex::input::EmulatedToypad::InspectFigureId(bytes);
    const bool learned_exact = (*learned)[index] && *(*learned)[index] == bytes && ReadUid(bytes) == selected.uid;
    if(!SameToken(selected, bytes, id) && !learned_exact)
      return {false,"Tag UID or pinned identity changed; restart to rediscover"};
    // Distinct files with the same UID remain selectable alternatives, but
    // only one physical token identity can be on the pad at any time.
    const auto occupied = portal.GetOccupancy();
    for (size_t i = 0; i < active->size(); ++i) {
      if (occupied[kPositions[i].index] && !(*active)[i])
        return {false,"Unmanaged token present; cannot safely check UID ownership"};
      if ((*active)[i] && (*active)[i]->uid == selected.uid)
        return {false,"Another variant of this token UID is already on the Toy Pad"};
    }
    for (size_t i = 0; i < catalog.size(); ++i) {
      if (i == index || !catalog[i].unavailable.empty()) continue;
      std::error_code ec;
      if (std::filesystem::equivalent(path, catalog[i].path, ec) && !ec)
        return {false,"Token file is an alias of another catalog entry"};
    }
    const auto loaded=portal.LoadFigure(bytes,path.string(),pos.pad,pos.index,true);
    if(loaded!=id)throw std::runtime_error("Backend identity changed during placement");
    (*active)[static_cast<size_t>(slot)] = ActiveToken{selected.uid,index};
    REXLOG_INFO("Native roster: placed model {} at region {} slot {}",id,pos.pad,pos.index);
    return {true,{}};
  };
  backend.remove=[get,active,learned](Slot slot)->Result {
    auto& portal=get();const auto p=Translate(slot);
    const auto owner = (*active)[static_cast<size_t>(slot)];
    if (!owner) return {false,"Token ownership is unknown"};
    TagBytes saved{};
    if(!portal.RemoveFigureNative(p.pad,p.index,owner->uid,saved))
      return {false,"Entry identity changed or tag save failed; any present figure was retained"};
    (*learned)[owner->entry] = saved;
    (*active)[static_cast<size_t>(slot)].reset();
    return {true,{}};
  };
  backend.temporary_remove=[get](Slot slot)->Result {
    const auto p=Translate(slot);
    return get().TempRemove(p.index)?Result{true,{}}:Result{false,"Entry is absent"};
  };
  backend.move=[get,active](Slot from,Slot to)->Result {
    auto& portal=get();const auto a=Translate(from),b=Translate(to);
    const auto occupied=portal.GetOccupancy();
    if(!occupied[a.index])return {false,"Source entry is absent"};
    if(a.index==b.index)return portal.CancelRemove(a.index)?Result{true,{}}:Result{false,"Restore failed"};
    if(occupied[b.index])return {false,"Destination is occupied"};
    if(!portal.MoveFigure(b.pad,b.index,a.pad,a.index))throw std::runtime_error("Backend move outcome uncertain");
    (*active)[static_cast<size_t>(to)] = (*active)[static_cast<size_t>(from)];
    (*active)[static_cast<size_t>(from)].reset();
    return {true,{}};
  };
  return backend;
}
}
