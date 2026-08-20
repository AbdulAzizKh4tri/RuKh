/**
 * @file GzipCompressor.hpp
 * @brief Gzip compression implementation for HTTP response bodies.
 *
 * Provides a gzip-based implementation of @ref rukh::http::compression::ICompressor.
 *
 * Gzip is a lossless compression format based on the DEFLATE compression
 * algorithm. The compressed representation produced by this class uses the
 * `gzip` HTTP content-coding.
 *
 * @see https://www.rfc-editor.org/rfc/rfc1952
 * @see https://www.rfc-editor.org/rfc/rfc1951
 * @see https://zlib.net/
 * @see https://www.rfc-editor.org/rfc/rfc9110
 */

#pragma once

#include <string>
#include <string_view>
#include <zlib.h>

#include <rukh/Exceptions.hpp>
#include <rukh/http/compression/ICompressor.hpp>

namespace rukh::http::compression {

/**
 * @brief Compresses data using gzip/DEFLATE through zlib.
 *
 * Implements @ref rukh::http::compression::ICompressor using the zlib
 * `deflate` streaming API.
 *
 * @param level The zlib compression level. Defaults to
 *        `Z_DEFAULT_COMPRESSION`.
 *
 * @note The zlib stream is initialized with a 15-bit DEFLATE window and
 *       gzip framing (`15 + 16` in the `windowBits` argument to
 *       `deflateInit2`).
 *
 * @see rukh::http::compression::ICompressor
 * @see https://zlib.net/manual.html
 * @see https://zlib.net/manual.html#Advanced
 * @see https://www.rfc-editor.org/rfc/rfc1952
 * @see https://www.rfc-editor.org/rfc/rfc1951
 */
class GzipCompressor : public ICompressor {
public:
  GzipCompressor(int level = Z_DEFAULT_COMPRESSION) : level_(level) {
    stream_.zalloc = Z_NULL;
    stream_.zfree = Z_NULL;
    stream_.opaque = Z_NULL;
    if (deflateInit2(&stream_, level_, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
      throw CompressorException("deflateInit2 failed");
    initialized_ = true;
  }

  ~GzipCompressor() {
    if (initialized_)
      deflateEnd(&stream_);
  }

  GzipCompressor(GzipCompressor &&other) noexcept : stream_(other.stream_), initialized_(other.initialized_) {
    other.initialized_ = false;
    other.stream_ = {};
  }

  GzipCompressor &operator=(GzipCompressor &&other) noexcept {
    if (this == &other)
      return *this;
    if (initialized_)
      deflateEnd(&stream_);
    stream_ = other.stream_;
    initialized_ = other.initialized_;
    other.initialized_ = false;
    other.stream_ = {};
    return *this;
  }

  GzipCompressor(const GzipCompressor &) = delete;
  GzipCompressor &operator=(const GzipCompressor &) = delete;

  std::string compress(std::string_view input) override {
    stream_.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    stream_.avail_in = static_cast<uInt>(input.size());

    std::string output;
    char buf[4096];
    int ret;
    do {
      stream_.next_out = reinterpret_cast<Bytef *>(buf);
      stream_.avail_out = sizeof(buf);
      ret = deflate(&stream_, Z_FINISH);
      if (ret == Z_STREAM_ERROR)
        throw CompressorException("gzip compress failed: stream error");
      output.append(buf, sizeof(buf) - stream_.avail_out);
    } while (ret != Z_STREAM_END);

    return output;
  }

  std::string feedChunk(std::string_view input) override {
    stream_.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    stream_.avail_in = static_cast<uInt>(input.size());

    std::string output;
    char buf[4096];
    do {
      stream_.next_out = reinterpret_cast<Bytef *>(buf);
      stream_.avail_out = sizeof(buf);
      int ret = deflate(&stream_, Z_SYNC_FLUSH);
      if (ret != Z_OK && ret != Z_BUF_ERROR)
        throw CompressorException("gzip feedChunk failed: deflate returned " + std::to_string(ret));
      output.append(buf, sizeof(buf) - stream_.avail_out);
    } while (stream_.avail_out == 0);

    return output;
  }

  std::string finish() override {
    stream_.next_in = Z_NULL;
    stream_.avail_in = 0;

    std::string output;
    char buf[4096];
    int ret;
    do {
      stream_.next_out = reinterpret_cast<Bytef *>(buf);
      stream_.avail_out = sizeof(buf);
      ret = deflate(&stream_, Z_FINISH);
      if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR)
        throw CompressorException("gzip finish failed: deflate returned " + std::to_string(ret));
      output.append(buf, sizeof(buf) - stream_.avail_out);
    } while (ret == Z_OK);

    return output;
  }

  std::string_view getEncoding() const noexcept override { return "gzip"; }

private:
  z_stream stream_{};
  int level_;
  bool initialized_ = false;
};
} // namespace rukh::http::compression
