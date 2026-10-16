#include "native_roster_portrait_file.h"
#include <cassert>
#include <chrono>
#include <iostream>
using namespace legodimensions::native_roster::portrait_file;
std::vector<uint8_t> Header(uint32_t width = 128, uint32_t height = 256) {
  std::vector<uint8_t> b{137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R'};
  for (auto n : {width, height})
    for (int shift : {24,16,8,0}) b.push_back(uint8_t(n >> shift));
  b.insert(b.end(), {8,6,0,0,0,0,0,0,0});
  return b;
}
int main() {
  Dimensions d{1,1};
  auto bytes = Header();
  assert(ValidateHeader(bytes.data(), bytes.size(), d));
  assert(d.width == 128 && d.height == 256);
  assert(!ValidateHeader(nullptr, 33, d) && d.width == 0 && d.height == 0);
  assert(!ValidateHeader(bytes.data(), 32, d));
  assert(!ValidateHeader(bytes.data(), kMaxBytes + 1, d));
  for (auto dims : {Dimensions{0,128}, {128,0}, {2049,128}, {128,2049}, {UINT32_MAX,1}}) {
    auto b = Header(dims.width,dims.height); assert(!ValidateHeader(b.data(),b.size(),d));
  }
  auto maximum = Header(2048,2048);
  assert(ValidateHeader(maximum.data(),maximum.size(),d));
  for (size_t pos : {size_t(0),size_t(7),size_t(11),size_t(12),size_t(15),size_t(24),size_t(25),size_t(26),size_t(27),size_t(28)}) {
    auto b = bytes; b[pos] = 255; assert(!ValidateHeader(b.data(),b.size(),d));
  }
  for (uint8_t color : {0,2,3,4,6}) {
    auto b=bytes; b[25]=color; assert(ValidateHeader(b.data(),b.size(),d));
  }
  auto b=bytes; b[24]=16; b[25]=3; assert(!ValidateHeader(b.data(),b.size(),d));
  b=bytes; b[24]=1; b[25]=0; assert(ValidateHeader(b.data(),b.size(),d));
  assert(Sibling({}).empty());
  assert(Sibling("/private/Tag Name.bin") == "/private/Tag Name.png");
  assert(Sibling("tag.backup.bin") == "tag.backup.png");
  assert(Sibling("tag") == "tag.png");
  auto temp = std::filesystem::temp_directory_path() /
    ("native-portrait-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  assert(std::filesystem::create_directory(temp));
  auto write = [&](const char* name, const std::vector<uint8_t>& data) {
    std::ofstream file(temp/name,std::ios::binary);
    file.write(reinterpret_cast<const char*>(data.data()),data.size());
    assert(file.good());
  };
  assert(Read({},d).empty());
  assert(Read(temp/"missing.png",d).empty());
  assert(Read(temp,d).empty());
  write("header.png",bytes);
  assert(Read(temp/"header.png",d) == bytes && d.width == 128);
  // Header-only is deliberately accepted by preflight; the SDK decoder rejects it.
  write("short.png",std::vector<uint8_t>(32)); assert(Read(temp/"short.png",d).empty());
  b=Header(2049,1); write("huge-dim.png",b); assert(Read(temp/"huge-dim.png",d).empty());
  b=bytes; b.resize(kMaxBytes); write("limit.png",b); assert(Read(temp/"limit.png",d).size()==kMaxBytes);
  b.push_back(0); write("too-big.png",b); assert(Read(temp/"too-big.png",d).empty());
  write("not-png.png",std::vector<uint8_t>(64,'X')); assert(Read(temp/"not-png.png",d).empty());
  // Reading the sibling never changes or opens the writable tag.
  const std::vector<uint8_t> tag(180,42); write("tag.bin",tag); write("tag.png",bytes);
  assert(Read(Sibling(temp/"tag.bin"),d)==bytes);
  std::ifstream file(temp/"tag.bin",std::ios::binary);
  std::vector<uint8_t> tag_after((std::istreambuf_iterator<char>(file)),{}); assert(tag_after==tag);
  file.close(); std::filesystem::remove_all(temp);
  std::cout << "portrait file validation passed\n";
}
