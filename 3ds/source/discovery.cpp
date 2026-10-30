#include "discovery.hpp"

#include <algorithm>

namespace agitate3ds {
namespace {

uint16_t readU16(const uint8_t *data) {
  return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8) |
                               static_cast<uint16_t>(data[1]));
}

uint32_t readU32(const uint8_t *data) {
  return (static_cast<uint32_t>(data[0]) << 24) |
         (static_cast<uint32_t>(data[1]) << 16) |
         (static_cast<uint32_t>(data[2]) << 8) |
         static_cast<uint32_t>(data[3]);
}

bool readDnsName(const uint8_t *packet,
                 size_t packetSize,
                 size_t offset,
                 std::string &name,
                 size_t &nextOffset) {
  name.clear();
  nextOffset = offset;
  size_t position = offset;
  bool jumped = false;

  for (size_t labelCount = 0; labelCount < 32; ++labelCount) {
    if (position >= packetSize) {
      return false;
    }

    const uint8_t length = packet[position];
    if ((length & 0xc0) == 0xc0) {
      if (position + 1 >= packetSize) {
        return false;
      }
      const size_t target =
          (static_cast<size_t>(length & 0x3f) << 8) | packet[position + 1];
      if (target >= packetSize) {
        return false;
      }
      if (!jumped) {
        nextOffset = position + 2;
        jumped = true;
      }
      position = target;
      continue;
    }

    if ((length & 0xc0) != 0 || length > 63) {
      return false;
    }
    ++position;
    if (length == 0) {
      if (!jumped) {
        nextOffset = position;
      }
      return true;
    }
    if (position + length > packetSize) {
      return false;
    }
    if (!name.empty()) {
      name.push_back('.');
    }
    for (size_t i = 0; i < length; ++i) {
      const unsigned char ch = packet[position + i];
      if (ch == 0 || ch >= 0x80) {
        return false;
      }
      name.push_back(static_cast<char>(
          ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch));
    }
    position += length;
    if (!jumped) {
      nextOffset = position;
    }
  }
  return false;
}

bool matchesSubnet(const IPv4Address &address,
                   const IPv4Address &localAddress,
                   const IPv4Address &netmask) {
  for (size_t i = 0; i < 4; ++i) {
    if ((address.octets[i] & netmask.octets[i]) !=
        (localAddress.octets[i] & netmask.octets[i])) {
      return false;
    }
  }
  return true;
}

bool isUnicast(const IPv4Address &address) {
  return address.octets[0] != 0 && address.octets[0] != 127 &&
         address.octets[0] < 224;
}

}  // namespace

bool parseMdnsAResponse(const uint8_t *packet,
                        size_t packetSize,
                        const std::string &hostname,
                        const IPv4Address &localAddress,
                        const IPv4Address &netmask,
                        IPv4Address &address,
                        std::string &error) {
  if (packet == nullptr || packetSize < 12 || hostname.empty()) {
    error = "invalid mDNS response";
    return false;
  }

  const uint16_t flags = readU16(packet + 2);
  const uint16_t questionCount = readU16(packet + 4);
  const uint16_t answerCount = readU16(packet + 6);
  const uint16_t authorityCount = readU16(packet + 8);
  const uint16_t additionalCount = readU16(packet + 10);
  if ((flags & 0x8000) == 0 || ((flags >> 11) & 0x0f) != 0 ||
      questionCount > 16 ||
      static_cast<size_t>(answerCount) + authorityCount + additionalCount >
          256) {
    error = "invalid mDNS response header";
    return false;
  }

  std::string expectedHostname;
  expectedHostname.reserve(hostname.size());
  for (unsigned char ch : hostname) {
    expectedHostname.push_back(static_cast<char>(
        ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch));
  }

  size_t position = 12;
  for (uint16_t i = 0; i < questionCount; ++i) {
    std::string questionName;
    size_t nextPosition = 0;
    if (!readDnsName(packet, packetSize, position, questionName,
                     nextPosition) ||
        nextPosition + 4 > packetSize) {
      error = "malformed mDNS question";
      return false;
    }
    position = nextPosition + 4;
  }

  const size_t recordCount =
      static_cast<size_t>(answerCount) + authorityCount + additionalCount;
  for (size_t i = 0; i < recordCount; ++i) {
    std::string recordName;
    size_t nextPosition = 0;
    if (!readDnsName(packet, packetSize, position, recordName, nextPosition) ||
        nextPosition + 10 > packetSize) {
      error = "malformed mDNS resource record";
      return false;
    }

    const uint16_t type = readU16(packet + nextPosition);
    const uint16_t recordClass = readU16(packet + nextPosition + 2) & 0x7fff;
    const uint32_t ttl = readU32(packet + nextPosition + 4);
    const uint16_t dataLength = readU16(packet + nextPosition + 8);
    const size_t dataPosition = nextPosition + 10;
    if (dataPosition + dataLength > packetSize) {
      error = "truncated mDNS resource record";
      return false;
    }

    if (recordName == expectedHostname && type == 1 && recordClass == 1 &&
        dataLength == 4 && ttl != 0) {
      IPv4Address candidate = {{
          packet[dataPosition],
          packet[dataPosition + 1],
          packet[dataPosition + 2],
          packet[dataPosition + 3],
      }};
      if (isUnicast(candidate) &&
          matchesSubnet(candidate, localAddress, netmask)) {
        address = candidate;
        error.clear();
        return true;
      }
    }

    position = dataPosition + dataLength;
  }

  error = "mDNS response has no matching IPv4 address";
  return false;
}

}  // namespace agitate3ds
