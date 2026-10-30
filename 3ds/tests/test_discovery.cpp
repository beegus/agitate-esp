#include "discovery.hpp"

#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

namespace {

void appendU16(std::vector<uint8_t> &packet, uint16_t value) {
  packet.push_back(static_cast<uint8_t>(value >> 8));
  packet.push_back(static_cast<uint8_t>(value));
}

void appendU32(std::vector<uint8_t> &packet, uint32_t value) {
  packet.push_back(static_cast<uint8_t>(value >> 24));
  packet.push_back(static_cast<uint8_t>(value >> 16));
  packet.push_back(static_cast<uint8_t>(value >> 8));
  packet.push_back(static_cast<uint8_t>(value));
}

void appendName(std::vector<uint8_t> &packet,
                const std::string &first,
                const std::string &second) {
  packet.push_back(static_cast<uint8_t>(first.size()));
  packet.insert(packet.end(), first.begin(), first.end());
  packet.push_back(static_cast<uint8_t>(second.size()));
  packet.insert(packet.end(), second.begin(), second.end());
  packet.push_back(0);
}

std::vector<uint8_t> makeResponse(uint8_t a, uint8_t b, uint8_t c, uint8_t d) {
  std::vector<uint8_t> packet;
  appendU16(packet, 0);
  appendU16(packet, 0x8400);
  appendU16(packet, 1);
  appendU16(packet, 1);
  appendU16(packet, 0);
  appendU16(packet, 0);

  appendName(packet, "agitate-esp", "local");
  appendU16(packet, 1);
  appendU16(packet, 1);

  packet.push_back(0xc0);
  packet.push_back(0x0c);
  appendU16(packet, 1);
  appendU16(packet, 0x8001);
  appendU32(packet, 120);
  appendU16(packet, 4);
  packet.push_back(a);
  packet.push_back(b);
  packet.push_back(c);
  packet.push_back(d);
  return packet;
}

}  // namespace

int main() {
  using agitate3ds::IPv4Address;
  using agitate3ds::parseMdnsAResponse;

  const std::vector<uint8_t> packet = makeResponse(192, 168, 1, 224);
  const IPv4Address local = {{192, 168, 1, 45}};
  const IPv4Address mask = {{255, 255, 255, 0}};
  IPv4Address result = {};
  std::string error;
  assert(parseMdnsAResponse(packet.data(), packet.size(), "agitate-esp.local",
                            local, mask, result, error));
  assert(result.octets[0] == 192 && result.octets[1] == 168 &&
         result.octets[2] == 1 && result.octets[3] == 224);

  const std::vector<uint8_t> differentSubnet =
      makeResponse(10, 0, 0, 24);
  assert(!parseMdnsAResponse(differentSubnet.data(), differentSubnet.size(),
                             "agitate-esp.local", local, mask, result, error));

  assert(!parseMdnsAResponse(packet.data(), packet.size(), "other.local",
                             local, mask, result, error));
  assert(!parseMdnsAResponse(packet.data(), 8, "agitate-esp.local", local,
                             mask, result, error));
  return 0;
}
