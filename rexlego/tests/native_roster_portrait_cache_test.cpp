#include "native_roster_portrait_cache.h"
#include <cassert>
#include <iostream>
using legodimensions::native_roster::PortraitCacheBudget;
int main() {
  PortraitCacheBudget budget(315);
  auto release = [](size_t) {};
  for (int i = 0; i < 315; ++i) {
    const std::vector<int> wanted{i};
    budget.BeginFrame(wanted);
    assert(budget.Next(wanted) == i);
    assert(budget.Reserve(1024 * 1024, release));
    budget.Commit(i, 1024 * 1024);
    assert(budget.Count() <= 32 && budget.Bytes() <= 32 * 1024 * 1024);
    assert(budget.Next(wanted) == -1);
  }
  PortraitCacheBudget pinned(40);
  std::vector<int> all;
  for (int i = 0; i < 32; ++i) all.push_back(i);
  pinned.BeginFrame(all);
  for (size_t i = 0; i < 32; ++i) {
    assert(pinned.Reserve(1024 * 1024, release)); pinned.Commit(i, 1024 * 1024);
  }
  bool evicted = false;
  assert(!pinned.Reserve(1, [&](size_t) { evicted = true; })); assert(!evicted);
  pinned.BeginFrame({0, 32});
  assert(pinned.Reserve(16 * 1024 * 1024, [&](size_t i) { assert(i != 0); evicted = true; }));
  assert(evicted); pinned.Commit(32,16 * 1024 * 1024);
  assert(pinned.Bytes() <= PortraitCacheBudget::kMaxRgbaBytes);
  assert(!pinned.Reserve(PortraitCacheBudget::kMaxRgbaBytes + 1, release));
  PortraitCacheBudget failed(3); failed.BeginFrame({-1, 99, 0, 1});
  assert(failed.Next({-1,99,0,1}) == 0); failed.Fail(0);
  assert(failed.Next({-1,99,0,1}) == 1); failed.Fail(1);
  assert(failed.Next({-1,99,0,1}) == -1);
  assert(failed.Next({}) == -1);
  std::cout << "portrait cache budget tests passed\n";
}
