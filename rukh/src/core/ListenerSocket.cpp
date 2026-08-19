#include <rukh/net/ListenerSocket.hpp>

#include <netinet/in.h> // sockaddr_in, INET_ADDRSTRLEN, htons
#include <spdlog/spdlog.h>
#include <sys/socket.h> // socket, bind, listen, accept, setsockopt, SOCK_STREAM, SOL_SOCKET, SO_REUSEADDR

#include <rukh/Exceptions.hpp>

namespace rukh::net {

ListenerSocket::ListenerSocket(std::string const &host, std::string port) {
  host_ = host;
  port_ = port;

  addrinfo hints = {}, *raw = nullptr;
  std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> res(nullptr, freeaddrinfo);

  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_flags = AI_PASSIVE;

  int gai = getaddrinfo(host_.c_str(), port_.c_str(), &hints, &raw);
  if (gai != 0) {
    SPDLOG_CRITICAL("ERROR on getaddrinfo {}", gai_strerror(gai));
    throw SocketException("Failed to locate address");
  }
  res.reset(raw);

  socket_ = Socket(socket(res->ai_family, res->ai_socktype, res->ai_protocol));
  if (!socket_) {
    SPDLOG_CRITICAL("Failed to create socket {}", strerror(errno));
    throw SocketException("Failed to create socket");
  }

  bind_socket(res);
}

void ListenerSocket::listen(int backlog) {
  if (::listen(socket_.getFd(), backlog) < 0) {
    SPDLOG_CRITICAL("ERROR on listening {}", strerror(errno));
    throw SocketException("Failed to listen on socket");
  }
  SPDLOG_INFO("Listening on {}:{}", host_, port_);
}

std::optional<TlsStream> ListenerSocket::acceptTls(SSL_CTX *ctx) {
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  int newSocket_fd = ::accept4(socket_.getFd(), (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (newSocket_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return std::nullopt;
    SPDLOG_CRITICAL("ERROR on accepting: {}", strerror(errno));
    throw SocketException("Failed to accept connection");
  }
  return TlsStream(newSocket_fd, ctx, addr, len);
}

std::optional<TcpStream> ListenerSocket::accept() {
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  int newSocket_fd = ::accept4(socket_.getFd(), (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (newSocket_fd < 0) {
    if (errno == EAGAIN || errno == EWOULDBLOCK)
      return std::nullopt;
    SPDLOG_CRITICAL("ERROR on accepting: {}", strerror(errno));
    throw SocketException("Failed to accept connection");
  }
  return TcpStream(newSocket_fd, addr, len);
}

int ListenerSocket::acceptRawFd() {
  sockaddr_storage addr{};
  socklen_t len = sizeof(addr);
  int fd = ::accept4(socket_.getFd(), (sockaddr *)&addr, &len, SOCK_NONBLOCK | SOCK_CLOEXEC);
  if (fd < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    return -1;
  return fd;
}

int ListenerSocket::setSocketNonBlocking() { return socket_.setNonBlocking(); }

int ListenerSocket::getFd() { return socket_.getFd(); }

void ListenerSocket::bind_socket(std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> &res) {
  int reuse = 1;
  if (setsockopt(socket_.getFd(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(int)) < 0) {
    SPDLOG_CRITICAL("ERROR on setsockopt {}", strerror(errno));
    throw SocketException("Failed to set reuse address");
  }

  if (setsockopt(socket_.getFd(), SOL_SOCKET, SO_REUSEPORT, &reuse, sizeof(int)) < 0) {
    SPDLOG_CRITICAL("ERROR on setsockopt {}", strerror(errno));
    throw SocketException("Failed to set reuse port");
  }

  if (bind(socket_.getFd(), res->ai_addr, res->ai_addrlen) < 0) {
    SPDLOG_CRITICAL("ERROR on binding {}", strerror(errno));
    throw SocketException("Failed to bind socket");
  }
}
} // namespace rukh::net
