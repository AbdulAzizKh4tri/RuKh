#include <liburing.h>
#include <liburing/io_uring.h>
#include <memory>
#include <rukh/http/HttpServer.hpp>

#include <atomic>
#include <csignal>
#include <thread>

#include <rukh/Exceptions.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/core/Executor.hpp>
#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/SocketAwaitables.hpp>
#include <rukh/http/HttpConnection.hpp>

namespace rukh::http {

HttpServer::HttpServer(ErrorFactory &errorFactory) : errorFactory_(errorFactory) {}

void HttpServer::setTlsContext(std::string certPath, std::string keyPath) {
  // takes the cert.pem and key.pem files and sets up the context needed
  // for TLS
  tlsContext_ = std::shared_ptr<SSL_CTX>(SSL_CTX_new(TLS_server_method()), SSL_CTX_free);
  if (!tlsContext_)
    throw ServerException("Failed to create SSL_CTX");

  if (SSL_CTX_use_certificate_file(tlsContext_.get(), certPath.c_str(), SSL_FILETYPE_PEM) <= 0)
    throw ServerException("Failed to load certificate");

  if (SSL_CTX_use_PrivateKey_file(tlsContext_.get(), keyPath.c_str(), SSL_FILETYPE_PEM) <= 0)
    throw ServerException("Failed to load private key");

  SSL_CTX_set_options(tlsContext_.get(), SSL_OP_ENABLE_KTLS);
}

void HttpServer::addListener(const std::string &host, const std::string &port) {
  ListenerConfig config = {host, port, false};
  listenerConfigs_.push_back(config);
}

void HttpServer::addTlsListener(const std::string &host, const std::string &port) {
  ListenerConfig config = {host, port, true};
  listenerConfigs_.push_back(config);
};

void HttpServer::setRouter(Router &router) { router_ = &router; };

std::atomic<bool> HttpServer::shutdown = false;

void HttpServer::run(u_int N) {
  signal(SIGPIPE, SIG_IGN);

  signal(SIGINT, [](int) {
    if (shutdown) {
      SPDLOG_WARN("Terminated");
      exit(0);
    }
    SPDLOG_INFO("Shutting down...");
    HttpServer::shutdown = true;
  });
  signal(SIGTERM, [](int) {
    if (shutdown) {
      SPDLOG_WARN("Terminated");
      exit(0);
    }
    SPDLOG_INFO("Shutting down...");
    HttpServer::shutdown = true;
  });

  if (!router_)
    throw ServerException("Call setRouter() before run()");

  if (N == 0)
    N = std::thread::hardware_concurrency();

  std::vector<std::thread> executorThreads;

  if (N > 1) /// Pure tomfoolery
    SPDLOG_INFO("KAGE BUNSHIN NO JUTSU");

  io_uring firstRing = {};
  io_uring_params firstParams = {};

  if (io_uring_queue_init_params(ServerConfig::IO_URING_RING_SIZE, &firstRing, &firstParams) < 0)
    throw ServerException("Failed to initialize io_uring");

  unsigned int max_iou_workers[2] = {/*bounded*/ N, /*unbounded*/ N * 2};
  io_uring_register_iowq_max_workers(&firstRing, max_iou_workers);

  for (int i = 0; i < N; i++)
    executorThreads.emplace_back([this, i, &firstRing] { workerMain(firstRing, i == 0); });
  for (auto &t : executorThreads)
    t.join();

  spdlog::shutdown();
}

void HttpServer::workerMain(io_uring &firstRing, bool isFirst) {

  io_uring ring;
  if (isFirst) {
    ring = firstRing;
  } else {
    io_uring_params params = {};
    params.flags = IORING_SETUP_ATTACH_WQ;
    params.wq_fd = firstRing.ring_fd;
    if (io_uring_queue_init_params(ServerConfig::IO_URING_RING_SIZE, &ring, &params) < 0)
      throw ServerException("Failed to initialize io_uring");
  }

  core::Executor executor(ring);

  core::tl_executor = &executor;
  std::vector<std::unique_ptr<net::ListenerSocket>> tcpListeners, tlsListeners;

  for (auto &config : listenerConfigs_) {
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

  core::tl_executor->run(shutdown);
}

ErrorFactory &HttpServer::getErrorFactory() { return errorFactory_; }

core::Task<void> HttpServer::tcpAcceptLoop(net::ListenerSocket &listener) {
  core::tl_executor->registerReadFd(listener.getFd());
  for (;;) {
    co_await core::ReadAwaitable{listener.getFd(), now() + std::chrono::seconds(3)};
    if (core::tl_timed_out) {
      core::tl_timed_out = false;
      if (shutdown)
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

      auto stream = listener.accept();
      if (not stream)
        break;
      globalConnectionCount_.fetch_add(1, std::memory_order_relaxed);
      core::tl_executor->spawn(handleConnection(std::move(stream)));
    }
  }
}

core::Task<void> HttpServer::tlsAcceptLoop(net::ListenerSocket &listener) {
  core::tl_executor->registerReadFd(listener.getFd());
  for (;;) {
    co_await core::ReadAwaitable{listener.getFd(), now() + std::chrono::seconds(3)};
    if (core::tl_timed_out) {
      core::tl_timed_out = false;
      if (shutdown)
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

      auto stream = listener.acceptTls(tlsContext_.get());
      if (not stream)
        break;
      globalConnectionCount_.fetch_add(1, std::memory_order_relaxed);
      core::tl_executor->spawn(handleConnection(std::move(stream)));
    }
  }
}

template <typename Stream> core::Task<void> HttpServer::handleConnection(std::unique_ptr<Stream> stream) {
  int fd = stream->getFd();
  core::tl_executor->registerReadFd(fd);

  HttpConnection<Stream> conn(stream.get(), *router_, errorFactory_, shutdown, globalConnectionCount_);
  co_await conn.run();
  core::tl_executor->unregister(fd);
}
} // namespace rukh::http
