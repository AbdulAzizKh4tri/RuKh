/**
 * @file TcpStream.hpp
 * @brief TCP stream
 */

#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <sys/sendfile.h>
#include <sys/socket.h>

#include <rukh/core/Task.hpp>
#include <rukh/net/Socket.hpp>
#include <rukh/net/StreamResults.hpp>

namespace rukh::net {

///  TCP stream
class TcpStream {
public:
  TcpStream(int fd, sockaddr_storage addr, socklen_t len);

  /// send data and return number of bytes sent
  ssize_t send(const std::span<const unsigned char> data) const;

  /// send file and return number of bytes sent
  core::Task<ssize_t> sendFile(int fileFd, off_t offset, size_t count) const;

  /// receive data into the passed buffer
  ReceiveResult receive(std::span<unsigned char> data) const;

  /// unused for TCP, but needed to maintain a common Stream interface
  HandshakeResult handshake();

  /// whether file can be sent over this socket using sendFile.
  bool supportsSendFile();

  /// Check `Socket::setNonBlocking`
  int setSocketNonBlocking();

  /// abort connection, RST
  void resetConnection();

  std::string getIp() const;
  uint16_t getPort() const;

  int getFd() const;

private:
  Socket socket_;
  std::string ip_;
  uint16_t port_;
};
} // namespace rukh::net
