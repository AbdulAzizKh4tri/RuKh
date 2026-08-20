/**
 * @file TlsStream.hpp
 * @brief TLS stream
 */

#pragma once

#include <arpa/inet.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <spdlog/spdlog.h>
#include <sys/socket.h>

#include <rukh/net/Socket.hpp>
#include <rukh/net/StreamResults.hpp>

namespace rukh::net {

/// TLS stream
class TlsStream {
public:
  /// Check `HttpServer::setTlsContext` for reference
  TlsStream(int fd, SSL_CTX *ctx, sockaddr_storage addr, socklen_t len);

  TlsStream(TlsStream &&other) noexcept;

  TlsStream &operator=(TlsStream &&other) noexcept;

  TlsStream(TlsStream const &) = delete;
  TlsStream &operator=(TlsStream const &) = delete;

  ~TlsStream();

  /// Performs the TLS handshake. May block.
  HandshakeResult handshake();

  /// receive data over TLS into the passed buffer
  ReceiveResult receive(std::span<unsigned char> buf) const;

  /// send data with TLS and return number of bytes sent
  ssize_t send(const std::span<const unsigned char> data) const;

  /// abort connection, RST
  void resetConnection();

  /// Check `Socket::setNonBlocking`
  int setSocketNonBlocking();

  std::string getIp() const;
  uint16_t getPort() const;

  int getFd() const;

private:
  Socket socket_;
  SSL *ssl_ = nullptr;
  std::string ip_;
  uint16_t port_;
};
} // namespace rukh::net
