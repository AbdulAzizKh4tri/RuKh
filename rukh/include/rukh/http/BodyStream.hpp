/**
 * @file BodyStream.hpp
 * @brief HttpRequest body stream
 */
#pragma once

#include <span>

#include <rukh/Exceptions.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/core/Task.hpp>

namespace rukh::http {

/// Internal. Body stream callback to read data from the stream.
using BodyReadFn = std::move_only_function<core::Task<size_t>(std::span<unsigned char>, size_t)>;
/// Internal. Body stream callback to drain all data from the stream.
using BodyDrainFn = std::move_only_function<core::Task<void>(size_t)>;

/// Callback to stream BodyStream's data directly, without having to make an intermediate copy in the handler.
using StreamFn = std::move_only_function<core::Task<void>(std::span<unsigned char>)>;

/// HttpRequest body stream
class BodyStream {
public:
  BodyStream(size_t contentLength, BodyReadFn readFn, BodyDrainFn drainFn)
      : remaining_(contentLength), readFn_(std::move(readFn)), drainFn_(std::move(drainFn)) {}

  /**
   * @brief Read body data into @p buf
   * @param buf
   * @returns The number of bytes read
   *
   * Reads the minimum of @c buf.size() and @c remaining_ bytes.
   */
  core::Task<size_t> read(std::span<unsigned char> buf) {
    if (exhausted_)
      co_return 0;
    size_t n = co_await readFn_(buf, remaining_);
    remaining_ -= n;
    if (n == 0 || remaining_ == 0)
      exhausted_ = true;
    co_return n;
  }

  /**
   * @brief Read the entire body into @p data
   * @param data
   * @param limit
   * @returns number of bytes read.
   */
  core::Task<size_t> readAll(std::string &data, size_t limit = ServerConfig::MAX_CONTENT_LENGTH) {
    if (exhausted_)
      co_return 0;

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

    co_return data.size();
  }

  /**
   * @brief Stream the body as you wish. @p fn will be called with a span of stream data.
   * @see StreamFn
   */
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

  /// Drain the unused bytes from the body stream. HttpConnection handles this automatically.
  core::Task<void> drain() {
    if (exhausted_)
      co_return;
    co_await drainFn_(remaining_);
  }

  /// Stream has been fully read
  bool isExhausted() { return exhausted_; }

private:
  bool exhausted_ = false;
  size_t remaining_ = 0;
  BodyReadFn readFn_;
  BodyDrainFn drainFn_;
};
} // namespace rukh
