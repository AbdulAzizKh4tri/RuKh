#include "rukh/core/SocketAwaitables.hpp"
#include <rukh/net/TcpStream.hpp>

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <netinet/tcp.h>
#include <span>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/FileIoHelpers.hpp>
#include <rukh/core/SpliceAwaitable.hpp>
#include <rukh/core/YieldAwaitable.hpp>
#include <rukh/net/StreamResults.hpp>
#include <rukh/net/utils.hpp>
#include <rukh/utils.hpp>

namespace rukh::net {

TcpStream::TcpStream(int fd, sockaddr_storage addr, socklen_t len) : socket_(fd) {
  auto [ip, port] = resolvePeerAddress(addr, len);
  ip_ = ip;
  port_ = port;

  int flag = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
    SPDLOG_ERROR("ERROR on setsockopt {}", strerror(errno));
    throw std::runtime_error("Failed to set TCP_NODELAY");
  };
}

ssize_t TcpStream::send(const std::span<const unsigned char> data) const {
  ssize_t n = ::send(socket_.getFd(), data.data(), data.size(), 0);
  if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return 0;
  return n;
}

core::Task<ssize_t> TcpStream::sendFile(int fileFd, off_t offset, size_t count) const {
  core::Pipe pipe = core::tl_pipe_pool.acquire();
  size_t remaining = count;
  while (remaining > 0) {
    size_t chunkSize = std::min(remaining, ServerConfig::PIPE_BUFFER_SIZE);

    int n1;
    for (;;) {
      n1 = co_await core::SpliceAwaitable{fileFd, offset, pipe.in, -1, chunkSize};
      if (n1 != -EAGAIN)
        break;
      co_await core::YieldAwaitable{};
    }
    if (n1 <= 0) {
      core::tl_pipe_pool.discard(pipe);
      co_return n1;
    }

    size_t inPipe = n1;
    while (inPipe > 0) {
      int n2;
      for (;;) {
        n2 = co_await core::SpliceAwaitable{pipe.out, -1, socket_.getFd(), -1, inPipe};
        if (n2 != -EAGAIN)
          break;
        co_await core::WriteAwaitable{socket_.getFd(),
                                      rukh::now() + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S)};
      }
      if (n2 <= 0) {
        core::tl_pipe_pool.discard(pipe);
        co_return n2;
      }
      inPipe -= n2;
    }
    offset += n1;
    remaining -= n1;
  }
  core::tl_pipe_pool.release(pipe);
  co_return count;
}

ReceiveResult TcpStream::receive(std::span<unsigned char> data) const {
  ssize_t n = ::recv(socket_.getFd(), data.data(), data.size(), 0);
  if (n > 0)
    return ReceiveResult::data(n);
  if (n == 0)
    return ReceiveResult::closed();
  if (errno == EAGAIN || errno == EWOULDBLOCK)
    return ReceiveResult::wouldBlock();
  if (errno == ECONNRESET)
    return ReceiveResult::closed();
  return ReceiveResult::error();
}

HandshakeResult TcpStream::handshake() { return HandshakeResult::NO_TLS; }

bool TcpStream::supportsSendFile() { return true; }

int TcpStream::setSocketNonBlocking() { return socket_.setNonBlocking(); }

void TcpStream::resetConnection() {
  linger l{1, 0};
  setsockopt(socket_.getFd(), SOL_SOCKET, SO_LINGER, &l, sizeof(l));
}

std::string TcpStream::getIp() const { return ip_; }
uint16_t TcpStream::getPort() const { return port_; }

int TcpStream::getFd() const { return socket_.getFd(); }
} // namespace rukh::net
