#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace agitate3ds {

struct IPv4Address {
  uint8_t octets[4];
};

bool parseMdnsAResponse(const uint8_t *packet,
                        size_t packetSize,
                        const std::string &hostname,
                        const IPv4Address &localAddress,
                        const IPv4Address &netmask,
                        IPv4Address &address,
                        std::string &error);

}  // namespace agitate3ds
