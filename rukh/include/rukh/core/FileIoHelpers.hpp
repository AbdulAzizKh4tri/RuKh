/**
 * @file FileIoHelpers.hpp
 * @brief Everything FileIo
 * \todo docs
 */
#pragma once

#include <fcntl.h>
#include <spdlog/spdlog.h>

#include <rukh/ServerConfig.hpp>

namespace rukh::core {

enum class FileOpenError {
  NotFound,          // ENOENT, ENOTDIR (bad path component)
  Forbidden,         // EACCES, EPERM
  Malformed,         // ENAMETOOLONG, ELOOP
  ResourceExhausted, // EMFILE, ENFILE, ENOMEM
  Unexpected         // anything else — log with errno
};

struct Pipe {
  int in;
  int out;
};

class PipePool {
public:
  Pipe acquire() {
    if (!freePipes_.empty()) {
      Pipe p = freePipes_.back();
      freePipes_.pop_back();
      return p;
    }

    int fds[2];
    if (::pipe2(fds, O_NONBLOCK) < 0)
      throw std::runtime_error("Failed to create pipe");

    if (ServerConfig::PIPE_BUFFER_SIZE != 64 * 1024) {
      auto r = fcntl(fds[1], F_SETPIPE_SZ, ServerConfig::PIPE_BUFFER_SIZE);
      if (r < 0)
        SPDLOG_ERROR("FAILED TO SET PIPE SIZE");
    }

    return {fds[1], fds[0]};
  }

  void release(Pipe p) { freePipes_.push_back(p); }

  void discard(Pipe p) {
    ::close(p.in);
    ::close(p.out);
  }

private:
  std::vector<Pipe> freePipes_;
};

} // namespace rukh::core
