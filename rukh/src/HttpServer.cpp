#include <rukh/HttpServer.hpp>

#include <atomic>
#include <csignal>
#include <thread>

#include <rukh/ServerConfig.hpp>
#include <rukh/core/Awaitables.hpp>
#include <rukh/core/Executor.hpp>
#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/HttpConnection.hpp>

namespace rukh {

HttpServer::HttpServer(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

void HttpServer::setTlsContext(std::string certPath, std::string keyPath) {
  // takes the cert.pem and key.pem files and sets up the context needed
  // for TLS
  tlsContext_ = std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  if (!tlsContext_)
    throw std::runtime_error("Failed to create SSL_CTX");

  if (SSL_CTX_use_certificate_file(tlsContext_.get(), certPath.c_str(), SSL_FILETYPE_PEM) <= 0)
    throw std::runtime_error("Failed to load certificate");

  if (SSL_CTX_use_PrivateKey_file(tlsContext_.get(), keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
    throw std::runtime_error("Failed to load private key");
}

// Add listeners, either TCP or TLS
void HttpServer::addListener(const std::string &host, const std::string &port) {
  ListenerConfig config = {host, port, false};
  listenersConfigs_.push_back(config);
}

void HttpServer::addTlsListener(const std::string &host, const std::string &port) {
  ListenerConfig config = {host, port, true};
  listenersConfigs_.push_back(config);
};

void HttpServer::setRouter(Router &router) { router_ = &router; };

std::atomic<bool> HttpServer::shutdown_ = false;

void HttpServer::run(int N) {
  signal(SIGPIPE, SIG_IGN);

  signal(SIGINT, [](int) {
    if (shutdown_) {
      SPDLOG_WARN("Terminated");
      exit(0);
    }
    SPDLOG_INFO("Shutting down...");
    HttpServer::shutdown_ = true;
  });
  signal(SIGTERM, [](int) {
    if (shutdown_) {
      SPDLOG_WARN("Terminated");
      exit(0);
    }
    SPDLOG_INFO("Shutting down...");
    HttpServer::shutdown_ = true;
  });

  if (!router_)
    throw std::runtime_error("Call setRouter() before run()");

  if (N <= 0)
    N = std::thread::hardware_concurrency();

  std::vector<std::thread> executorThreads;

  if (N > 1)
    SPDLOG_INFO("KAGE BUNSHIN NO JUTSU");
  for (int i = 0; i < N; i++)
    executorThreads.emplace_back([this] { workerMain(); });
  for (auto &t : executorThreads)
    t.join();

  spdlog::shutdown();
}

void HttpServer::workerMain() {
  core::Executor executor;
  core::tl_executor = &executor;
  std::vector<std::unique_ptr<net::ListenerSocket>> tcpListeners, tlsListeners;

  for (auto &config : listenersConfigs_) {
    if (config.isTls) {
      auto listener = std::make_unique<net::ListenerSocket>(config.host, config.port);
      listener->setSocketNonBlocking();
      tlsListeners.push_back(std::move(listener));
    } else {
      auto listener = std::make_unique<net::ListenerSocket>(config.host, config.port);
      listener->setSocketNonBlocking();
      tcpListeners.push_back(std::move(listener));
    }
  }

  for (auto &listener : tcpListeners) {
    listener->listen(listenBacklog_);
    core::tl_executor->spawn(tcpAcceptLoop(*listener));
  }

  for (auto &listener : tlsListeners) {
    listener->listen(listenBacklog_);
    core::tl_executor->spawn(tlsAcceptLoop(*listener));
  }

  core::tl_executor->run(shutdown_);
}

ErrorFactory &HttpServer::getErrorFactory() { return errorFactory_; }

core::Task<void> HttpServer::tcpAcceptLoop(net::ListenerSocket &listener) {
  core::tl_executor->registerReadFd(listener.getFd());
  for (;;) {
    co_await ReadAwaitable{listener.getFd(), now() + std::chrono::seconds(3)};
    if (core::tl_timed_out) {
      core::tl_timed_out = false;
      if (shutdown_)
        co_return;
      continue;
    }
    for (;;) {
      if (globalConnectionCount_.load(std::memory_order_relaxed) >= ServerConfig::CONNECTION_LIMIT) {
        SPDLOG_WARN("Connection Limit {} hit, RST-ing new connections", ServerConfig::CONNECTION_LIMIT);
        for (int i = 0; i < 10; i++) {
          int fd = listener.acceptRawFd();
          if (fd == -1)
            break;
          linger l{1, 0};
          setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
          ::close(fd);
        }
        break;
      }

      auto streamOpt = listener.accept();
      if (!streamOpt)
        break;
      globalConnectionCount_.fetch_add(1, std::memory_order_relaxed);
      core::tl_executor->spawn(handleConnection(std::make_unique<net::TcpStream>(std::move(*streamOpt))));
    }
  }
}

core::Task<void> HttpServer::tlsAcceptLoop(net::ListenerSocket &listener) {
  core::tl_executor->registerReadFd(listener.getFd());
  for (;;) {
    co_await ReadAwaitable{listener.getFd(), now() + std::chrono::seconds(3)};
    if (core::tl_timed_out) {
      core::tl_timed_out = false;
      if (shutdown_)
        co_return;
      continue;
    }
    for (;;) {
      if (globalConnectionCount_.load(std::memory_order_relaxed) >= ServerConfig::CONNECTION_LIMIT) {
        SPDLOG_WARN("Connection Limit {} hit, RST-ing new connections", ServerConfig::CONNECTION_LIMIT);
        for (int i = 0; i < 10; i++) {
          int fd = listener.acceptRawFd();
          if (fd == -1)
            break;
          linger l{1, 0};
          setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
          ::close(fd);
        }
        break;
      }

      auto streamOpt = listener.acceptTls(tlsContext_.get());
      if (!streamOpt)
        break;
      globalConnectionCount_.fetch_add(1, std::memory_order_relaxed);
      auto stream = std::make_unique<net::TlsStream>(std::move(*streamOpt));
      core::tl_executor->spawn(handleConnection(std::move(stream)));
    }
  }
}

template <typename Stream> core::Task<void> HttpServer::handleConnection(std::unique_ptr<Stream> stream) {
  int fd = stream->getFd();
  core::tl_executor->registerReadFd(fd);

  HttpConnection<Stream> conn(std::move(stream), *router_, errorFactory_, shutdown_, globalConnectionCount_);
  co_await conn.run();
  core::tl_executor->unregister(fd);
}
} // namespace rukh
