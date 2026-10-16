#include <rex/input/portal/toypad_led_animation.h>
#include <cassert>
using namespace rex::input;
int main() {
  ToypadLightAnimation l;
  assert(EvaluateToypadLight(l,0)==ToypadRgb{});
  l.mode=1;l.r=255;assert((EvaluateToypadLight(l,999)==ToypadRgb{255,0,0}));
  l.mode=2;l.from_b=255;l.on_ticks=2;l.off_ticks=3;l.count=1;
  assert((EvaluateToypadLight(l,79)==ToypadRgb{255,0,0}));
  assert((EvaluateToypadLight(l,80)==ToypadRgb{0,0,255}));
  assert((EvaluateToypadLight(l,99999)==ToypadRgb{0,0,255}));
  l.count=2;assert((EvaluateToypadLight(l,199)==ToypadRgb{0,0,255}));
  assert((EvaluateToypadLight(l,200)==ToypadRgb{255,0,0}));
  l.count=255;assert((EvaluateToypadLight(l,200*999+80)==ToypadRgb{0,0,255}));
  l.mode=3;l.speed_ticks=1;l.count=1;
  assert((EvaluateToypadLight(l,0)==ToypadRgb{0,0,255}));
  assert((EvaluateToypadLight(l,40)==ToypadRgb{255,0,0}));
  l.count=2;assert((EvaluateToypadLight(l,80)==ToypadRgb{0,0,255}));
  l.count=0;assert((EvaluateToypadLight(l,80)==ToypadRgb{0,0,255}));
  const auto mid=EvaluateToypadLight(l,20);assert(mid[0]>=127&&mid[0]<=128&&mid[2]>=127&&mid[2]<=128);
  l.started_ms=100;assert((EvaluateToypadLight(l,99)==ToypadRgb{0,0,255}));
}
