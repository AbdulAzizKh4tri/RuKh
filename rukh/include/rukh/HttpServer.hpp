#pragma once

#include <atomic>
#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <vector>

#include <rukh/ErrorFactory.hpp>
#include <rukh/Router.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/net/ListenerSocket.hpp>

/**
 * @namespace rukh
 * @brief Main library namespace.
 */
namespace rukh {

struct ListenerConfig {
  std::string host;
  std::string port;
  bool isTls;
};

class HttpServer {
public:
  static std::atomic<bool> shutdown_;

  HttpServer(ErrorFactory &errorFactory);

  void setTlsContext(std::string certPath, std::string keyPath);

  // Add listeners, either TCP or TLS
  void addListener(const std::string &host, const std::string &port);

  void addTlsListener(const std::string &host, const std::string &port);

  void setRouter(Router &router);

  void run(int N);

  ErrorFactory &getErrorFactory();

  void setListenBacklog(int backlog) { listenBacklog_ = backlog; }

private:
  int listenBacklog_ = 10000;
  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<ListenerConfig> listenersConfigs_;

  Router *router_ = nullptr;
  ErrorFactory &errorFactory_;

  std::atomic<int> globalConnectionCount_ = 0;

  void workerMain();

  core::Task<void> tcpAcceptLoop(net::ListenerSocket &listener);

  core::Task<void> tlsAcceptLoop(net::ListenerSocket &listener);

  template <typename Stream> core::Task<void> handleConnection(std::unique_ptr<Stream> stream);
};
} // namespace rukh
