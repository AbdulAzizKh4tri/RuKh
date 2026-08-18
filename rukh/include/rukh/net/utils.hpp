/**
 * @file utils.hpp
 * @brief Utility functions for rukh::net
 */
#pragma once

#include <arpa/inet.h>

namespace rukh::net {

struct PeerAddress {
  std::string ip;
  uint16_t port;
};

/// Resolve peer address from @c sockaddr_storage to @c PeerAddress
inline PeerAddress resolvePeerAddress(sockaddr_storage addr, socklen_t len) {
  PeerAddress result;
  if (addr.ss_family == AF_INET) {
    char ipstr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &((sockaddr_in *)&addr)->sin_addr, ipstr, INET_ADDRSTRLEN);
    result.ip = ipstr;
    result.port = ntohs(((sockaddr_in *)&addr)->sin_port);
  } else if (addr.ss_family == AF_INET6) {
    char ipstr[INET6_ADDRSTRLEN];
    inet_ntop(AF_INET6, &((sockaddr_in6 *)&addr)->sin6_addr, ipstr, INET6_ADDRSTRLEN);
    result.ip = ipstr;
    result.port = ntohs(((sockaddr_in6 *)&addr)->sin6_port);
  } else {
    throw std::runtime_error("Unknown address family");
  }
  return result;
}

} // namespace rukh::net
