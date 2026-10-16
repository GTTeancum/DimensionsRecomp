#include "native_roster_portrait_trust.h"
#include <cassert>
#include <iostream>
#include <chrono>
using namespace legodimensions::native_roster::portrait_file;
int main() {
  // Verify the SDK-derived SHA256 implementation used by the pre-decode gate.
  legodimensions::native_roster::portrait_sha256::SHA256 hash;
  assert(hash("abc",3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  assert(hash("",0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
  for (size_t i = 0; i < 4; ++i) {
    assert(MatchesDigest(i,kTrustedSizes[i],kTrustedSha256[i]));
    assert(!MatchesDigest(i,kTrustedSizes[i]+1,kTrustedSha256[i]));
    assert(!MatchesDigest(i,kTrustedSizes[i],kTrustedSha256[(i+1)%4]));
    auto changed=std::string(kTrustedSha256[i]); changed[0]=changed[0]=='0'?'1':'0';
    assert(!MatchesDigest(i,kTrustedSizes[i],changed));
    std::vector<uint8_t> untrusted(kTrustedSizes[i],0);
    // Even an apparently valid, small IHDR cannot reach the decoder unless exact bytes match.
    const std::vector<uint8_t> header{137,80,78,71,13,10,26,10,0,0,0,13,'I','H','D','R',0,0,0,1,0,0,0,1,8,6,0,0,0,0,0,0,0};
    std::copy(header.begin(),header.end(),untrusted.begin());
    Dimensions d; assert(ValidateHeader(untrusted.data(),untrusted.size(),d));
    assert(!IsTrusted(i,untrusted));
    untrusted[100] = 42; assert(!IsTrusted(i,untrusted));
    const auto path = std::filesystem::temp_directory_path() /
        ("portrait-untrusted-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + ".png");
    { std::ofstream f(path, std::ios::binary); f.write(reinterpret_cast<const char*>(untrusted.data()), untrusted.size()); assert(f.good()); }
    assert(Read(path,d).size() == kTrustedSizes[i]); // Preflight alone would accept this.
    assert(ReadTrusted(path,i,d).empty() && d.width == 0 && d.height == 0);
    std::filesystem::remove(path);
  }
  assert(!MatchesDigest(4,110583,kTrustedSha256[0]));
  assert(!IsTrusted(0,{})); assert(!IsTrusted(4,{}));
  Dimensions d{1,1}; assert(ReadTrusted({},0,d).empty() && d.width==0 && d.height==0);
  std::cout << "portrait SHA256 trust gate passed\n";
}
