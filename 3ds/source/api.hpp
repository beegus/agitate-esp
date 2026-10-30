#pragma once

#include <3ds.h>

#include <string>

namespace agitate3ds {

struct ApiResponse {
  bool ok = false;
  unsigned statusCode = 0;
  std::string body;
  std::string error;
};

class ApiClient {
 public:
  ApiClient();

  bool initialize(std::string &error);
  void shutdown();
  bool wifiConnected() const;
  bool pauseWorkerIfIdle();
  void resumeWorker();

  bool discoverAsync();
  bool setServerUrl(const std::string &url);
  bool getAsync(const std::string &path);
  bool postAsync(const std::string &path, const std::string &body = "");
  bool takeResponse(ApiResponse &response);

 private:
  struct WorkerState;
  static void workerMain(void *arg);
  bool submit(bool discovery,
              bool isPost,
              const std::string &path,
              const std::string &body);

  Thread workerThread_;
  WorkerState *workerState_;
  bool accessPointReady_;
  bool httpReady_;
};

}  // namespace agitate3ds
