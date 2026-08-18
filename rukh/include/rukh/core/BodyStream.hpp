#pragma once

#include <span>

#include <rukh/Exceptions.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

using BodyReadFn = std::move_only_function<core::Task<size_t>(std::span<unsigned char>, size_t)>;
using BodyDrainFn = std::move_only_function<core::Task<void>(size_t)>;
using StreamFn = std::move_only_function<core::Task<void>(std::span<unsigned char>)>;

class BodyStream {
public:
  BodyStream(size_t contentLength, BodyReadFn readFn, BodyDrainFn drainFn)
      : remaining_(contentLength), readFn_(std::move(readFn)), drainFn_(std::move(drainFn)) {}

  core::Task<size_t> read(std::span<unsigned char> buf) {
    if (exhausted_)
      co_return 0;
    size_t n = co_await readFn_(buf, remaining_);
    remaining_ -= n;
    if (n == 0 || remaining_ == 0)
      exhausted_ = true;
    co_return n;
  }

  core::Task<std::string> readAll(std::string &data, size_t limit = ServerConfig::MAX_CONTENT_LENGTH) {
    if (exhausted_)
      co_return "";

    size_t bufferSize = 4096;
    if (remaining_ > 0)
      data.reserve(remaining_);
    else
      data.reserve(bufferSize);

    size_t n = 0;
    unsigned char scratch[4096];
    while (data.size() <= limit) {
      auto span = std::span<unsigned char>(scratch, sizeof(scratch));
      n = co_await read(span);
      if (n == 0)
        break;
      data.append(reinterpret_cast<char *>(scratch), n);
    }

    if (n > 0)
      throw ServerException("Content limit exceeded", 413, true);

    co_return data;
  }

  core::Task<void> streamUsing(StreamFn fn) {
    unsigned char buf[4096];
    auto span = std::span<unsigned char>(buf, sizeof(buf));
    for (;;) {
      size_t n = co_await read(span);
      if (n == 0)
        break;
      co_await fn(span.subspan(0, n));
    }
  }

  core::Task<void> drain() {
    if (exhausted_)
      co_return;
    co_await drainFn_(remaining_);
  }

  bool isExhausted() { return exhausted_; }

private:
  bool exhausted_ = false;
  size_t remaining_ = 0;
  BodyReadFn readFn_;
  BodyDrainFn drainFn_;
};
} // namespace rukh
