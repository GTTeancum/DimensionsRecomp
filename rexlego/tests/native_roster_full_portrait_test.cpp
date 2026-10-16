#include "native_roster_portrait_trust.h"
#include <rex/ui/image_decode.h>
#include <cassert>
#include <filesystem>
#include <iostream>
int main(int argc,char**argv) {
 assert(argc==2);namespace pf=legodimensions::native_roster::portrait_file;
 const std::filesystem::path root=argv[1];size_t count=0;
 for(const auto& binding:pf::kResourceBindings) {
  const auto path=root/("resource_"+std::to_string(binding.tag_resource_id)+".png");
  pf::Dimensions d;const auto data=pf::ReadTrusted(path,binding.portrait_resource_id,d);assert(!data.empty());
  int w=0,h=0;const auto rgba=rex::ui::DecodeImageRGBA(data.data(),data.size(),w,h);
  assert(w==int(d.width)&&h==int(d.height)&&rgba.size()==size_t(w)*h*4);
  auto changed=data;changed.back()^=1;assert(!pf::IsTrusted(binding.portrait_resource_id,changed));++count;
 }
 assert(count==315);std::cout<<"PASS 315 exact-hash bound portraits decoded with actual SDK; altered data rejected\n";
}
