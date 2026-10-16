#include "native_roster_model.h"
#include <cassert>
using namespace legodimensions::native_roster;
int main() {
  Snapshot s; s.occupancy_known = true; s.slots[0] = 0; s.occupied[0] = true;
  PadModel p; p.focus = 0;
  auto lift = [&] {
    assert(p.Activate(s) == PadModel::Action::Lift);
    p.Submitted(PadModel::Operation::Lift);s.lifted_entry=0;p.Observe(s);
    assert(p.mode==PadModel::Mode::Carrying);
  };
  lift();
  p.Navigate(1,0); assert(p.focus == 3);
  assert(p.Back()==PadModel::Action::Cancel);p.Submitted(PadModel::Operation::Restore);s.lifted_entry=-1;p.Observe(s);
  assert(p.focus == 0 && p.mode == PadModel::Mode::Browse && s.slots[0] == 0);
  lift(); p.Navigate(1,0); assert(p.Activate(s) == PadModel::Action::Place);
  p.Submitted(); s.busy = true; p.Observe(s); assert(p.mode == PadModel::Mode::Pending);
  assert(p.Back() == PadModel::Action::Cancel); assert(p.Back() == PadModel::Action::None);
  s.busy = false; s.lifted_entry=-1; p.Observe(s); assert(p.focus == 0); // Cancel won.
  lift(); p.Navigate(1,0); p.Submitted(); p.Back(); s.slots[0] = 3; s.lifted_entry=-1;
  p.Observe(s); assert(p.focus == 3); // Commit won; no false rollback.
  s.occupied[0] = false; s.occupied[3] = true;
  p.focus = 4; p.Activate(s); assert(p.mode == PadModel::Mode::Picker);
  p.entry = 0; p.Activate(s); assert(p.focus == 3 && p.mode == PadModel::Mode::Browse);
  p.focus = 4; p.Activate(s); p.entry = 1; p.Activate(s);
  assert(p.mode == PadModel::Mode::Carrying && p.source == -1);
  p.Navigate(-1,0); assert(p.Activate(s) == PadModel::Action::None); // Occupied.
  p.Back(); p.focus = 6; p.Navigate(0,1); assert(p.focus == 6);
  p.Navigate(0,-1); assert(p.focus == 4); p.Navigate(-1,0); assert(p.focus == 3);
  p.focus = 3; p.Navigate(-1,0); assert(p.focus == 0);
  p.Navigate(1,0); assert(p.focus == 3); p.Navigate(1,0); assert(p.focus == 4);
  p.Navigate(-1,0); assert(p.focus == 3);
  s.occupancy_known = false; assert(p.Activate(s) == PadModel::Action::None);
  p.Submitted(PadModel::Operation::Add); assert(p.Back() == PadModel::Action::None);
  p.Submitted(PadModel::Operation::Remove); assert(p.Back() == PadModel::Action::None);
  StickNavigation n; assert(n.Read(1,0,.1f)[0] == 0); n.Read(0,0,.1f);
  assert(n.Read(1,0,.01f)[0] == 1); assert(n.Read(1,0,.1f)[0] == 0);
  assert(n.Read(1,0,.3f)[0] == 1); n.Read(0,0,.01f);
  assert(n.Read(0,1,.01f)[1] == -1);
}
