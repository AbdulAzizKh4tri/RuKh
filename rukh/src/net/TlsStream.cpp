#include <rukh/net/TlsStream.hpp>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <sys/socket.h>

#include <spdlog/spdlog.h>

#include <rukh/net/utils.hpp>

namespace rukh::net {

TlsStream::TlsStream(int fd, SSL_CTX *ctx, sockaddr_storage addr, socklen_t len) : socket_(fd) {
  ssl_ = SSL_new(ctx);
  if (!ssl_)
    throw std::runtime_error("Failed to create SSL object");
  SSL_set_fd(ssl_, fd);
  auto [ip, port] = resolvePeerAddress(addr, len);
  ip_ = ip;
  port_ = port;

  int flag = 1;
  if (setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag)) < 0) {
    SPDLOG_CRITICAL("ERROR on setsockopt {}", strerror(errno));
    throw std::runtime_error("Failed to set TCP_NODELAY");
  };
}

TlsStream::TlsStream(TlsStream &&other) noexcept
    : socket_(std::move(other.socket_)), ssl_(other.ssl_), ip_(std::move(other.ip_)), port_(other.port_) {
  other.ssl_ = nullptr; // prevent double-free in destructor
}

TlsStream &TlsStream::operator=(TlsStream &&other) noexcept {
  if (this == &other)
    return *this;
  if (ssl_) {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
  }
  socket_ = std::move(other.socket_);
  ssl_ = other.ssl_;
  ip_ = std::move(other.ip_);
  port_ = other.port_;
  other.ssl_ = nullptr;
  return *this;
}

TlsStream::~TlsStream() {
  if (ssl_) {
    SSL_shutdown(ssl_);
    SSL_free(ssl_);
  }
}

HandshakeResult TlsStream::handshake() {
  int ret = SSL_accept(ssl_);
  if (ret == 1)
    return HandshakeResult::DONE;

  int err = SSL_get_error(ssl_, ret);
  switch (err) {
  case SSL_ERROR_WANT_READ:
    return HandshakeResult::WANT_READ;
  case SSL_ERROR_WANT_WRITE:
    return HandshakeResult::WANT_WRITE;
  default:
    SPDLOG_ERROR("TLS handshake error for {}:{} — SSL error code {}", ip_, port_, err);
    return HandshakeResult::ERROR;
  }
}

ReceiveResult TlsStream::receive(std::span<unsigned char> buf) const {
  int n = SSL_read(ssl_, buf.data(), buf.size());
  if (n > 0)
    return ReceiveResult::data(n);

  int err = SSL_get_error(ssl_, n);
  switch (err) {
  case SSL_ERROR_WANT_READ:
  case SSL_ERROR_WANT_WRITE:
    return ReceiveResult::wouldBlock();
  case SSL_ERROR_ZERO_RETURN: // clean TLS shutdown
    return ReceiveResult::closed();
  case SSL_ERROR_SYSCALL: // TCP closed without close_notify — treat as closed
    return ReceiveResult::closed();
  default:
    SPDLOG_ERROR("SSL_read error for {}:{} — SSL error {}", ip_, port_, err);
    return ReceiveResult::error();
  }
}

ssize_t TlsStream::send(const std::span<const unsigned char> data) const {
  int n = SSL_write(ssl_, data.data(), data.size());
  if (n <= 0) {
    int err = SSL_get_error(ssl_, n);
    if (err == SSL_ERROR_WANT_WRITE || err == SSL_ERROR_WANT_READ)
      return 0;
    return -1;
  }
  return n;
}

core::Task<ssize_t> TlsStream::sendFile(int fileFd, off_t offset, size_t count) const { co_return -1; }

void TlsStream::resetConnection() {
  linger l{1, 0};
  setsockopt(socket_.getFd(), SOL_SOCKET, SO_LINGER, &l, sizeof(l));
}

bool TlsStream::supportsSendFile() { return false; }

int TlsStream::setSocketNonBlocking() { return socket_.setNonBlocking(); }

std::string TlsStream::getIp() const { return ip_; }
uint16_t TlsStream::getPort() const { return port_; }

int TlsStream::getFd() const { return socket_.getFd(); }
} // namespace rukh::net
