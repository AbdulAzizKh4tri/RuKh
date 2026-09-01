/**
 * @file HttpServer.hpp
 * @brief Bridges the TCP/TLS layer and the HTTP layer
 */

#pragma once

#include <atomic>
#include <liburing.h>
#include <memory>
#include <openssl/ssl.h>
#include <string>
#include <vector>

#include <rukh/core/Task.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/Router.hpp>
#include <rukh/net/ListenerSocket.hpp>

namespace rukh::http {

struct ListenerConfig {
  /// \todo Maybe add Router here so we can have per listener routes.
  std::string host;
  std::string port;
  bool isTls;
};

/// Bridges the TCP/TLS layer and the HTTP layer
class HttpServer {
public:
  /// The shutdown flag, used by the server and it's components to signal shutdown.
  static std::atomic<bool> shutdown;

  HttpServer(ErrorFactory &errorFactory);

  /**
   * @brief Configures the TLS context for the HTTP server.
   *
   * @param certPath Path to the certificate file.
   * @param keyPath  Path to the private key file.
   *
   * @throws std::runtime_error If the TLS context cannot be created, or the certificate / the private key
   * cannot be loaded. Happens before the the server is started.
   */
  void setTlsContext(std::string certPath, std::string keyPath);

  /// Add TCP listener
  void addListener(const std::string &host, const std::string &port);

  /// Add TLS listener
  void addTlsListener(const std::string &host, const std::string &port);

  /// The Router to be used by the server for dispatching requests.
  void setRouter(Router &router);

  /**
   * @brief Runs the HTTP server.
   *
   * @param N The number of executor threads to use. Defaults to 1. 0 will use thread::hardware_concurrency.
   *
   * starts @p N instances of workerMain.
   * Each instance of workerMain listens on the configured ports and accepts new connections.
   * RSTs connection if the connection count goes over the configured threshold.
   */
  void run(u_int N = 1);

  ErrorFactory &getErrorFactory();

  void setListenBacklog(int backlog) { listenBacklog_ = backlog; }

private:
  int listenBacklog_ = 10000;
  std::shared_ptr<SSL_CTX> tlsContext_ = nullptr;
  std::vector<ListenerConfig> listenerConfigs_;

  Router *router_ = nullptr;
  ErrorFactory &errorFactory_;

  std::atomic<int> globalConnectionCount_ = 0;

  void workerMain(io_uring &, bool);

  core::Task<void> tcpAcceptLoop(net::ListenerSocket &listener);

  core::Task<void> tlsAcceptLoop(net::ListenerSocket &listener);

  template <typename Stream> core::Task<void> handleConnection(std::unique_ptr<Stream> stream);
};
} // namespace rukh::http
