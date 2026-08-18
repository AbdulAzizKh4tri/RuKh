/**
 * @file StreamResults.hpp
 * @brief Result types for stream operations
 * @see TcpStream
 * @see TlsStream
 */
#pragma once

#include <sys/types.h>

namespace rukh::net {

/// Handshake Result
enum class HandshakeResult { DONE, WANT_READ, WANT_WRITE, ERROR, NO_TLS };

struct ReceiveResult {
  enum class Status { DATA, CLOSED, WOULD_BLOCK, ERROR } status;
  ssize_t bytes = 0;

  static ReceiveResult data(ssize_t n) { return {Status::DATA, n}; }
  static ReceiveResult closed() { return {Status::CLOSED, 0}; }
  static ReceiveResult wouldBlock() { return {Status::WOULD_BLOCK, 0}; }
  static ReceiveResult error() { return {Status::ERROR, 0}; }
};
} // namespace rukh::net
