/**
 * @file ListenerSocket.hpp
 * @brief Listener socket for accepting TCP/TLS connections.
 */
#pragma once

#include <memory>
#include <netdb.h>
#include <optional>

#include <rukh/net/Socket.hpp>
#include <rukh/net/TcpStream.hpp>
#include <rukh/net/TlsStream.hpp>

namespace rukh::net {

/// Listener socket for accepting TCP/TLS connections.
class ListenerSocket {
public:
  ListenerSocket(std::string const &host, std::string port);

  /**
   * @brief Start listening on the socket
   * @param backlog The maximum number of pending connections, new requests for connections will be refused if this
   * limit is reached.
   */
  void listen(int backlog);

  /**
   * @brief Accept a TLS connection
   * @param ctx The SSL_CTX to use. requires SSL_CTX to be set up.
   *
   * Check `HttpServer::setTlsContext` for reference
   *
   * @returns A TlsStream on success, nullptr if there are no pending connection requests.
   */
  std::unique_ptr<TlsStream> acceptTls(SSL_CTX *ctx);

  /**
   * @brief Accept a TCP connection
   * @returns A TcpStream on success, nullptr if there are no pending connection requests.
   */
  std::unique_ptr<TcpStream> accept();

  /**
   * @brief Accept a raw file descriptor.
   *
   * Escape hatch in case of server overload. You need to reset connections without constructing streams.
   * @returns A raw file descriptor on success, -1 if there are no pending connection requests
   */
  int acceptRawFd();

  /// Check `Socket::setNonBlocking`
  int setSocketNonBlocking();
  int getFd();

private:
  Socket socket_;
  std::string host_, port_;

  /// bind socket for reuse with this addr and port
  void bind_socket(std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> &res);
};
} // namespace rukh::net
