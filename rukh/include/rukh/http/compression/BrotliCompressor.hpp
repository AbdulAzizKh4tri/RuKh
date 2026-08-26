/**
 * @file BrotliCompressor.hpp
 * @brief Brotli compression implementation for HTTP response bodies.
 *
 * Provides a Brotli-based implementation of @ref rukh::http::compression::ICompressor.
 *
 * Brotli is a lossless compression algorithm developed by Google and is
 * commonly used for HTTP content encoding. The compressed representation
 * produced by this class uses the `br` content-coding defined for HTTP.
 *
 * @see https://github.com/google/brotli
 * @see https://www.rfc-editor.org/rfc/rfc7932
 * @see https://www.rfc-editor.org/rfc/rfc9110
 * @see https://brotli.org/
 */

#pragma once

#include "rukh/ServerConfig.hpp"
#include <brotli/encode.h>
#include <string>
#include <string_view>

#include <rukh/Exceptions.hpp>
#include <rukh/http/compression/ICompressor.hpp>

namespace rukh::http::compression {

/**
 * @brief Compresses data using the Brotli compression algorithm.
 *
 * Implements @ref rukh::http::compression::ICompressor using the Brotli
 * encoder API.
 *
 * @see rukh::http::compression::ICompressor
 * @see https://github.com/google/brotli
 * @see https://github.com/google/brotli/blob/master/c/include/brotli/encode.h
 * @see https://www.rfc-editor.org/rfc/rfc7932
 */
class BrotliCompressor : public ICompressor {
public:
  BrotliCompressor(int quality = BROTLI_DEFAULT_QUALITY) {
    state_ = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);
    if (!state_)
      throw CompressorException("BrotliEncoderCreateInstance failed");
    BrotliEncoderSetParameter(state_, BROTLI_PARAM_QUALITY, quality);
  }

  ~BrotliCompressor() {
    if (state_)
      BrotliEncoderDestroyInstance(state_);
  }

  BrotliCompressor(BrotliCompressor &&other) noexcept : state_(other.state_) { other.state_ = nullptr; }

  BrotliCompressor &operator=(BrotliCompressor &&other) noexcept {
    if (this == &other)
      return *this;
    if (state_)
      BrotliEncoderDestroyInstance(state_);
    state_ = other.state_;
    other.state_ = nullptr;
    return *this;
  }

  BrotliCompressor(const BrotliCompressor &) = delete;
  BrotliCompressor &operator=(const BrotliCompressor &) = delete;

  std::string compress(std::string_view input) override {
    size_t maxSize = BrotliEncoderMaxCompressedSize(input.size());
    std::string output(maxSize, '\0');
    size_t encodedSize = maxSize;

    if (!BrotliEncoderCompress(BROTLI_DEFAULT_QUALITY, BROTLI_DEFAULT_WINDOW, BROTLI_DEFAULT_MODE, input.size(),
                               reinterpret_cast<const uint8_t *>(input.data()), &encodedSize,
                               reinterpret_cast<uint8_t *>(output.data())))
      throw CompressorException("BrotliEncoderCompress failed");

    output.resize(encodedSize);
    return output;
  }

  std::string feedChunk(std::string_view input) override { return processStream(input, BROTLI_OPERATION_FLUSH); }

  std::string finish() override { return processStream({}, BROTLI_OPERATION_FINISH); }

  std::string_view getEncoding() const noexcept override { return "br"; }

private:
  BrotliEncoderState *state_ = nullptr;

  std::string processStream(std::string_view input, BrotliEncoderOperation op) {
    const uint8_t *nextIn = reinterpret_cast<const uint8_t *>(input.data());
    size_t availIn = input.size();
    std::string output;
    uint8_t buf[ServerConfig::COMPRESSION_BUFFER_SIZE];

    do {
      uint8_t *nextOut = buf;
      size_t availOut = sizeof(buf);
      if (!BrotliEncoderCompressStream(state_, op, &availIn, &nextIn, &availOut, &nextOut, nullptr))
        throw CompressorException("BrotliEncoderCompressStream failed");
      output.append(reinterpret_cast<char *>(buf), sizeof(buf) - availOut);
    } while (availIn > 0 || BrotliEncoderHasMoreOutput(state_));

    return output;
  }
};
} // namespace rukh::http::compression
