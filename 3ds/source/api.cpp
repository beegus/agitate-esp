#include "api.hpp"

#include <3ds.h>

#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <malloc.h>
#include <netinet/in.h>
#include <new>
#include <sys/select.h>
#include <sys/socket.h>
#include <utility>
#include <unistd.h>
#include <vector>

#include "discovery.hpp"
#include "protocol.hpp"

namespace agitate3ds {
namespace {

constexpr u16 kDiscoveryPort = 42131;
constexpr u16 kMdnsPort = 5353;
constexpr char kDiscoveryRequest[] = "AGITATEESP_DISCOVER_V1";
constexpr char kDiscoveryResponse[] = "AGITATEESP/1";
constexpr char kMdnsHostname[] = "agitate-esp.local";
constexpr size_t kSocBufferSize = 0x100000;
constexpr size_t kMaximumResponseBytes = 256 * 1024;
constexpr size_t kDownloadChunkBytes = 1024;
constexpr u64 kHttpTimeoutNanoseconds = 5000000000ULL;

std::string resultMessage(const char *operation, Result result) {
  char message[96];
  std::snprintf(message, sizeof(message), "%s failed (0x%08lx)", operation,
                static_cast<unsigned long>(static_cast<u32>(result)));
  return message;
}

bool validServerUrl(const std::string &url) {
  static const char kHttpPrefix[] = "http://";
  if (url.compare(0, sizeof(kHttpPrefix) - 1, kHttpPrefix) != 0 ||
      url.size() <= sizeof(kHttpPrefix) - 1 ||
      url.size() > 32 ||
      url.find('/', sizeof(kHttpPrefix) - 1) != std::string::npos) {
    return false;
  }

  unsigned dots = 0;
  for (size_t i = sizeof(kHttpPrefix) - 1; i < url.size(); ++i) {
    if (url[i] == '.') {
      ++dots;
    } else if (url[i] < '0' || url[i] > '9') {
      return false;
    }
  }
  return dots == 3;
}

bool queryMdns(int socketFd,
               const in_addr &localAddress,
               const in_addr &netmask,
               std::string &serverAddress) {
  std::vector<u8> query(12, 0);
  query[5] = 1;
  static const char *const labels[] = {"agitate-esp", "local"};
  for (const char *label : labels) {
    const size_t length = std::strlen(label);
    query.push_back(static_cast<u8>(length));
    query.insert(query.end(), label, label + length);
  }
  query.push_back(0);
  query.push_back(0);
  query.push_back(1);
  query.push_back(0x80);
  query.push_back(1);

  sockaddr_in destination = {};
  destination.sin_family = AF_INET;
  destination.sin_port = htons(kMdnsPort);
  destination.sin_addr.s_addr = inet_addr("224.0.0.251");

  IPv4Address local = {};
  IPv4Address mask = {};
  std::memcpy(local.octets, &localAddress.s_addr, sizeof(local.octets));
  std::memcpy(mask.octets, &netmask.s_addr, sizeof(mask.octets));

  for (unsigned attempt = 0; attempt < 3; ++attempt) {
    const ssize_t sent =
        sendto(socketFd, query.data(), query.size(), 0,
               reinterpret_cast<const sockaddr *>(&destination),
               sizeof(destination));
    if (sent != static_cast<ssize_t>(query.size())) {
      continue;
    }

    for (;;) {
      fd_set readable;
      FD_ZERO(&readable);
      FD_SET(socketFd, &readable);
      timeval timeout = {};
      timeout.tv_usec = 400000;
      const int ready =
          select(socketFd + 1, &readable, nullptr, nullptr, &timeout);
      if (ready <= 0) {
        break;
      }

      std::array<u8, 1500> packet = {};
      sockaddr_in sender = {};
      socklen_t senderLength = sizeof(sender);
      const ssize_t received =
          recvfrom(socketFd, packet.data(), packet.size(), 0,
                   reinterpret_cast<sockaddr *>(&sender), &senderLength);
      if (received <= 0) {
        break;
      }

      IPv4Address address = {};
      std::string error;
      if (parseMdnsAResponse(packet.data(), static_cast<size_t>(received),
                             kMdnsHostname, local, mask, address, error)) {
        char formatted[16];
        std::snprintf(formatted, sizeof(formatted), "%u.%u.%u.%u",
                      static_cast<unsigned>(address.octets[0]),
                      static_cast<unsigned>(address.octets[1]),
                      static_cast<unsigned>(address.octets[2]),
                      static_cast<unsigned>(address.octets[3]));
        serverAddress = formatted;
        return true;
      }
    }
  }
  return false;
}

ApiResponse discoverServer() {
  ApiResponse response;
  u32 *socBuffer =
      static_cast<u32 *>(memalign(0x1000, kSocBufferSize));
  if (socBuffer == nullptr) {
    response.error = "LAN discovery memory allocation failed";
    return response;
  }

  const Result socResult = socInit(socBuffer, kSocBufferSize);
  if (R_FAILED(socResult)) {
    response.error = resultMessage("LAN discovery service initialization",
                                   socResult);
    std::free(socBuffer);
    return response;
  }

  in_addr localAddress = {};
  in_addr netmask = {};
  in_addr subnetBroadcast = {};
  if (SOCU_GetIPInfo(&localAddress, &netmask, &subnetBroadcast) != 0) {
    localAddress.s_addr = 0;
    netmask.s_addr = 0;
    subnetBroadcast.s_addr = 0;
  }

  int socketFd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (socketFd < 0) {
    char error[96];
    std::snprintf(error, sizeof(error), "LAN discovery socket failed (%d)",
                  errno);
    response.error = error;
  } else {
    const int reuseAddress = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuseAddress,
               sizeof(reuseAddress));
    sockaddr_in local = {};
    local.sin_family = AF_INET;
    local.sin_port = htons(kMdnsPort);
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    const bool boundToMdnsPort =
        bind(socketFd, reinterpret_cast<const sockaddr *>(&local),
             sizeof(local)) == 0;

    if (boundToMdnsPort) {
      const u8 multicastTtl = 255;
      const u8 multicastLoopback = 0;
      setsockopt(socketFd, IPPROTO_IP, IP_MULTICAST_TTL, &multicastTtl,
                 sizeof(multicastTtl));
      setsockopt(socketFd, IPPROTO_IP, IP_MULTICAST_LOOP, &multicastLoopback,
                 sizeof(multicastLoopback));
      ip_mreq membership = {};
      membership.imr_multiaddr.s_addr = inet_addr("224.0.0.251");
      membership.imr_interface = localAddress;
      setsockopt(socketFd, IPPROTO_IP, IP_ADD_MEMBERSHIP, &membership,
                 sizeof(membership));
    }
    std::string serverAddress;
    if (queryMdns(socketFd, localAddress, netmask, serverAddress)) {
      response.ok = true;
      response.body = serverAddress;
    }

    const int enableBroadcast = 1;
    if (!response.ok &&
        setsockopt(socketFd, SOL_SOCKET, SO_BROADCAST, &enableBroadcast,
                   sizeof(enableBroadcast)) < 0) {
      char error[96];
      std::snprintf(error, sizeof(error),
                    "LAN discovery broadcast setup failed (%d)", errno);
      response.error = error;
    } else if (!response.ok) {
      sockaddr_in destinations[2] = {};
      size_t destinationCount = 1;
      destinations[0].sin_family = AF_INET;
      destinations[0].sin_port = htons(kDiscoveryPort);
      destinations[0].sin_addr.s_addr = htonl(INADDR_BROADCAST);

      if (subnetBroadcast.s_addr != 0 &&
          subnetBroadcast.s_addr != destinations[0].sin_addr.s_addr) {
        destinations[1].sin_family = AF_INET;
        destinations[1].sin_port = htons(kDiscoveryPort);
        destinations[1].sin_addr = subnetBroadcast;
        destinationCount = 2;
      }

      for (unsigned attempt = 0; attempt < 5 && !response.ok; ++attempt) {
        for (size_t i = 0; i < destinationCount; ++i) {
          const ssize_t sent =
              sendto(socketFd, kDiscoveryRequest,
                     sizeof(kDiscoveryRequest) - 1, 0,
                     reinterpret_cast<const sockaddr *>(&destinations[i]),
                     sizeof(destinations[i]));
          if (sent < 0) {
            char error[96];
            std::snprintf(error, sizeof(error),
                          "LAN discovery broadcast failed (%d)", errno);
            response.error = error;
            break;
          }
          if (sent != static_cast<ssize_t>(sizeof(kDiscoveryRequest) - 1)) {
            response.error = "LAN discovery sent an incomplete request";
            break;
          }
        }
        if (!response.error.empty()) {
          break;
        }

        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socketFd, &readable);
        timeval timeout = {};
        timeout.tv_usec = 350000;
        const int ready =
            select(socketFd + 1, &readable, nullptr, nullptr, &timeout);
        if (ready < 0) {
          char error[96];
          std::snprintf(error, sizeof(error),
                        "LAN discovery wait failed (%d)", errno);
          response.error = error;
          break;
        }
        if (ready == 0) {
          continue;
        }

        char reply[sizeof(kDiscoveryResponse)] = {};
        sockaddr_in sender = {};
        socklen_t senderLength = sizeof(sender);
        const ssize_t received =
            recvfrom(socketFd, reply, sizeof(reply) - 1, 0,
                     reinterpret_cast<sockaddr *>(&sender), &senderLength);
        if (received < 0) {
          char error[96];
          std::snprintf(error, sizeof(error),
                        "LAN discovery response failed (%d)", errno);
          response.error = error;
          break;
        }
        if (received !=
                static_cast<ssize_t>(sizeof(kDiscoveryResponse) - 1) ||
            std::memcmp(reply, kDiscoveryResponse,
                        sizeof(kDiscoveryResponse) - 1) != 0) {
          continue;
        }

        const char *address = inet_ntoa(sender.sin_addr);
        if (address == nullptr) {
          response.error = "LAN discovery returned an invalid address";
          break;
        }
        response.body = address;
        response.ok = true;
      }
    }
    if (close(socketFd) < 0 && response.ok) {
      char error[96];
      std::snprintf(error, sizeof(error),
                    "LAN discovery socket close failed (%d)", errno);
      response.ok = false;
      response.body.clear();
      response.error = error;
    }
  }

  const Result exitResult = socExit();
  std::free(socBuffer);
  if (R_FAILED(exitResult) && response.error.empty()) {
    response.ok = false;
    response.body.clear();
    response.error = resultMessage("LAN discovery service shutdown",
                                   exitResult);
  }
  if (!response.ok && response.error.empty()) {
    response.error = "No agitate-esp controller found on this Wi-Fi network";
  }
  return response;
}

ApiResponse performRequest(const std::string &serverUrl,
                           bool isPost,
                           const std::string &path,
                           const std::string &body) {
  ApiResponse response;
  if (path.empty() || path[0] != '/' || path.size() > 256) {
    response.error = "invalid API path";
    return response;
  }
  if (body.size() > kMaximumRequestBytes) {
    response.error = "request body exceeds the 1024-byte limit";
    return response;
  }

  const std::string url = serverUrl + path;
  httpcContext context = {};
  const HTTPC_RequestMethod method =
      isPost ? HTTPC_METHOD_POST : HTTPC_METHOD_GET;
  Result result = httpcOpenContext(&context, method, url.c_str(), 0);
  if (R_FAILED(result)) {
    response.error = resultMessage("HTTP connection", result);
    return response;
  }

  result = httpcAddRequestHeaderField(&context, "User-Agent",
                                     "AgitateESP-3DS/1.0");
  if (R_FAILED(result)) {
    response.error = resultMessage("HTTP request header", result);
    httpcCloseContext(&context);
    return response;
  }

  std::vector<u32> alignedBody;
  if (isPost) {
    result = httpcAddRequestHeaderField(&context, "Content-Type",
                                       "application/json");
    if (R_SUCCEEDED(result) && !body.empty()) {
      alignedBody.resize((body.size() + sizeof(u32) - 1) / sizeof(u32), 0);
      std::memcpy(alignedBody.data(), body.data(), body.size());
      result = httpcAddPostDataRaw(&context, alignedBody.data(),
                                   static_cast<u32>(body.size()));
    }
    if (R_FAILED(result)) {
      response.error = resultMessage("HTTP POST setup", result);
      httpcCloseContext(&context);
      return response;
    }
  }

  result = httpcBeginRequest(&context);
  if (R_FAILED(result)) {
    response.error = resultMessage("HTTP request", result);
    httpcCloseContext(&context);
    return response;
  }

  u32 statusCode = 0;
  result = httpcGetResponseStatusCodeTimeout(
      &context, &statusCode, kHttpTimeoutNanoseconds);
  if (R_FAILED(result)) {
    response.error =
        result == static_cast<Result>(HTTPC_RESULTCODE_TIMEDOUT)
            ? "Controller HTTP response timed out; check Wi-Fi and controller IP"
            : resultMessage("HTTP response", result);
    httpcCloseContext(&context);
    return response;
  }
  response.statusCode = statusCode;

  std::array<u8, kDownloadChunkBytes> chunk = {};
  for (;;) {
    u32 startPosition = 0;
    result = httpcGetDownloadSizeState(&context, &startPosition, nullptr);
    if (R_FAILED(result)) {
      response.error = resultMessage("HTTP download state", result);
      httpcCloseContext(&context);
      return response;
    }

    u32 position = 0;
    Result downloadResult =
        static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING);
    while (position < chunk.size() &&
           downloadResult ==
               static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING)) {
      const u32 previousPosition = position;
      downloadResult = httpcReceiveDataTimeout(
          &context, chunk.data() + position,
          static_cast<u32>(chunk.size() - position),
          kHttpTimeoutNanoseconds);

      u32 currentPosition = 0;
      result =
          httpcGetDownloadSizeState(&context, &currentPosition, nullptr);
      if (R_FAILED(result)) {
        response.error = resultMessage("HTTP download state", result);
        httpcCloseContext(&context);
        return response;
      }
      if (currentPosition < startPosition ||
          currentPosition - startPosition > chunk.size()) {
        response.error = "invalid HTTP download progress";
        httpcCloseContext(&context);
        return response;
      }
      position = currentPosition - startPosition;

      if (downloadResult !=
              static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING) &&
          R_FAILED(downloadResult)) {
        response.error =
            downloadResult == static_cast<Result>(HTTPC_RESULTCODE_TIMEDOUT)
                ? "Controller HTTP download timed out"
                : resultMessage("HTTP response download", downloadResult);
        httpcCloseContext(&context);
        return response;
      }
      if (downloadResult ==
              static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING) &&
          position == previousPosition) {
        response.error = "HTTP response download timed out";
        httpcCloseContext(&context);
        return response;
      }
    }

    if (position > kMaximumResponseBytes - response.body.size()) {
      response.error = "server response exceeds the 256 KiB limit";
      httpcCloseContext(&context);
      return response;
    }
    response.body.append(reinterpret_cast<const char *>(chunk.data()),
                         position);

    if (downloadResult !=
        static_cast<Result>(HTTPC_RESULTCODE_DOWNLOADPENDING)) {
      break;
    }
  }

  httpcCloseContext(&context);
  response.ok = statusCode >= 200 && statusCode < 300;
  if (!response.ok) {
    char message[48];
    std::snprintf(message, sizeof(message), "HTTP status %lu",
                  static_cast<unsigned long>(statusCode));
    response.error = message;
  }
  return response;
}

}  // namespace

struct ApiClient::WorkerState {
  WorkerState()
      : requestIsDiscovery(false),
        requestIsPost(false),
        stopping(false),
        pauseRequested(false),
        workerPaused(false),
        requestReady(false),
        requestActive(false),
        responseReady(false),
        serverUrl(kFallbackServerUrl) {}

  LightLock lock;
  LightEvent event;
  bool requestIsDiscovery;
  bool requestIsPost;
  bool stopping;
  bool pauseRequested;
  bool workerPaused;
  bool requestReady;
  bool requestActive;
  bool responseReady;
  std::string requestPath;
  std::string requestBody;
  std::string serverUrl;
  ApiResponse response;
};

ApiClient::ApiClient()
    : workerThread_(nullptr),
      workerState_(nullptr),
      accessPointReady_(false),
      httpReady_(false) {}

bool ApiClient::initialize(std::string &error) {
  if (!accessPointReady_) {
    const Result result = acInit();
    if (R_FAILED(result)) {
      error = resultMessage("Wi-Fi service initialization", result);
      return false;
    }
    accessPointReady_ = true;
  }

  if (!httpReady_) {
    const Result result = httpcInit(0x1000);
    if (R_FAILED(result)) {
      error = resultMessage("HTTP service initialization", result);
      return false;
    }
    httpReady_ = true;
  }

  WorkerState *state = new (std::nothrow) WorkerState;
  if (state == nullptr) {
    httpcExit();
    httpReady_ = false;
    error = "HTTP worker state allocation failed";
    return false;
  }
  LightLock_Init(&state->lock);
  LightEvent_Init(&state->event, RESET_STICKY);

  workerThread_ =
      threadCreate(workerMain, state, 32 * 1024, 0x31, -2, false);
  if (workerThread_ == nullptr) {
    delete state;
    httpcExit();
    httpReady_ = false;
    error = "HTTP worker thread creation failed";
    return false;
  }
  workerState_ = state;
  error.clear();
  return true;
}

void ApiClient::shutdown() {
  if (workerThread_ != nullptr && workerState_ != nullptr) {
    WorkerState *state = workerState_;
    workerState_ = nullptr;
    LightLock_Lock(&state->lock);
    state->stopping = true;
    LightLock_Unlock(&state->lock);
    LightEvent_Signal(&state->event);

    const Result result = threadJoin(workerThread_, 1000000000ULL);
    if (R_SUCCEEDED(result)) {
      threadFree(workerThread_);
    } else {
      threadDetach(workerThread_);
    }
    workerThread_ = nullptr;
    httpReady_ = false;
  } else if (httpReady_) {
    httpcExit();
    httpReady_ = false;
  }
  if (accessPointReady_) {
    acExit();
    accessPointReady_ = false;
  }
}

bool ApiClient::wifiConnected() const {
  if (!accessPointReady_) {
    return false;
  }
  u32 status = 0;
  return R_SUCCEEDED(ACU_GetStatus(&status)) && status == 3;
}

bool ApiClient::pauseWorkerIfIdle() {
  WorkerState *state = workerState_;
  if (state == nullptr || workerThread_ == nullptr) {
    return false;
  }

  LightLock_Lock(&state->lock);
  if (state->stopping || state->pauseRequested || state->workerPaused ||
      state->requestReady || state->requestActive || state->responseReady) {
    LightLock_Unlock(&state->lock);
    return false;
  }
  state->pauseRequested = true;
  LightLock_Unlock(&state->lock);
  LightEvent_Signal(&state->event);

  for (unsigned attempt = 0; attempt < 1000; ++attempt) {
    LightLock_Lock(&state->lock);
    const bool paused = state->workerPaused;
    LightLock_Unlock(&state->lock);
    if (paused) {
      return true;
    }
    svcSleepThread(1000000ULL);
  }

  LightLock_Lock(&state->lock);
  state->pauseRequested = false;
  LightLock_Unlock(&state->lock);
  LightEvent_Signal(&state->event);
  return false;
}

void ApiClient::resumeWorker() {
  WorkerState *state = workerState_;
  if (state == nullptr || workerThread_ == nullptr) {
    return;
  }

  LightLock_Lock(&state->lock);
  state->pauseRequested = false;
  state->workerPaused = false;
  LightLock_Unlock(&state->lock);
  LightEvent_Signal(&state->event);
}

bool ApiClient::discoverAsync() {
  return submit(true, false, "", "");
}

bool ApiClient::setServerUrl(const std::string &url) {
  if (!validServerUrl(url)) {
    return false;
  }
  WorkerState *state = workerState_;
  if (state == nullptr || workerThread_ == nullptr) {
    return false;
  }

  LightLock_Lock(&state->lock);
  if (state->stopping || state->pauseRequested || state->workerPaused ||
      state->requestReady || state->requestActive || state->responseReady) {
    LightLock_Unlock(&state->lock);
    return false;
  }
  state->serverUrl = url;
  LightLock_Unlock(&state->lock);
  return true;
}

bool ApiClient::getAsync(const std::string &path) {
  return submit(false, false, path, "");
}

bool ApiClient::postAsync(const std::string &path, const std::string &body) {
  return submit(false, true, path, body);
}

bool ApiClient::submit(bool discovery,
                       bool isPost,
                       const std::string &path,
                       const std::string &body) {
  WorkerState *state = workerState_;
  if (state == nullptr || workerThread_ == nullptr || !httpReady_) {
    return false;
  }

  LightLock_Lock(&state->lock);
  if (state->stopping || state->requestReady || state->requestActive ||
      state->responseReady) {
    LightLock_Unlock(&state->lock);
    return false;
  }
  state->requestIsDiscovery = discovery;
  state->requestIsPost = isPost;
  state->requestPath = path;
  state->requestBody = body;
  state->requestReady = true;
  LightLock_Unlock(&state->lock);
  LightEvent_Signal(&state->event);
  return true;
}

bool ApiClient::takeResponse(ApiResponse &response) {
  WorkerState *state = workerState_;
  if (state == nullptr) {
    return false;
  }

  LightLock_Lock(&state->lock);
  if (!state->responseReady) {
    LightLock_Unlock(&state->lock);
    return false;
  }
  std::swap(response, state->response);
  state->responseReady = false;
  LightLock_Unlock(&state->lock);
  return true;
}

void ApiClient::workerMain(void *arg) {
  WorkerState *state = static_cast<WorkerState *>(arg);
  for (;;) {
    LightEvent_Wait(&state->event);
    LightEvent_Clear(&state->event);

    LightLock_Lock(&state->lock);
    if (state->stopping) {
      LightLock_Unlock(&state->lock);
      break;
    }
    if (state->pauseRequested) {
      state->workerPaused = true;
      LightLock_Unlock(&state->lock);
      continue;
    }
    state->workerPaused = false;
    if (!state->requestReady) {
      LightLock_Unlock(&state->lock);
      continue;
    }

    const bool isPost = state->requestIsPost;
    const bool isDiscovery = state->requestIsDiscovery;
    std::string path;
    std::string body;
    std::string serverUrl = state->serverUrl;
    path.swap(state->requestPath);
    body.swap(state->requestBody);
    state->requestReady = false;
    state->requestActive = true;
    LightLock_Unlock(&state->lock);

    ApiResponse response = isDiscovery
                               ? discoverServer()
                               : performRequest(serverUrl, isPost, path, body);

    LightLock_Lock(&state->lock);
    state->requestActive = false;
    if (!state->stopping) {
      std::swap(state->response, response);
      state->responseReady = true;
    }
    LightLock_Unlock(&state->lock);
  }

  httpcExit();
  delete state;
}

}  // namespace agitate3ds
