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

} // namespace rukh::core
