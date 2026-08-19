#pragma once

#include <stdexcept>
#include <string>
#include <string_view>
#include <zlib.h>

#include <rukh/http/compression/ICompressor.hpp>

namespace rukh::http::compression  {

class GzipCompressor : public ICompressor {
public:
  GzipCompressor(int level = Z_DEFAULT_COMPRESSION) : level_(level) {
    stream_.zalloc = Z_NULL;
    stream_.zfree = Z_NULL;
    stream_.opaque = Z_NULL;
    if (deflateInit2(&stream_, level_, Z_DEFLATED, 15 + 16, 8, Z_DEFAULT_STRATEGY) != Z_OK)
      throw std::runtime_error("deflateInit2 failed");
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
        throw std::runtime_error("gzip compress failed: stream error");
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
        throw std::runtime_error("gzip feedChunk failed: deflate returned " + std::to_string(ret));
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
        throw std::runtime_error("gzip finish failed: deflate returned " + std::to_string(ret));
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
} // namespace rukh
