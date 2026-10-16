#pragma once
#include "native_roster.h"
#include <functional>
namespace rex::input { class EmulatedToypad; }
namespace legodimensions::native_roster {
// Runs once after config load and before runtime construction.
bool ConfigureMode();
TagCatalog ConfiguredTags();
Backend MakeBackend(std::function<rex::input::EmulatedToypad*()> portal, TagCatalog catalog);
}
