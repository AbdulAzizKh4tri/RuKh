#include <rukh/net/TcpStream.hpp>

#include <arpa/inet.h>
#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <netinet/tcp.h>
#include <span>
#include <spdlog/spdlog.h>
#include <sys/socket.h>
#include <sys/types.h>

#include <rukh/net/utils.hpp>

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

int TcpStream::setSocketNonBlocking() { return socket_.setNonBlocking(); }

void TcpStream::resetConnection() {
  linger l{1, 0};
  setsockopt(socket_.getFd(), SOL_SOCKET, SO_LINGER, &l, sizeof(l));
}

std::string TcpStream::getIp() const { return ip_; }
uint16_t TcpStream::getPort() const { return port_; }

int TcpStream::getFd() const { return socket_.getFd(); }
} // namespace rukh::net
