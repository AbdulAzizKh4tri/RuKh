#pragma once

#include <brotli/encode.h>
#include <string>
#include <string_view>

#include <rukh/Exceptions.hpp>
#include <rukh/http/compression/ICompressor.hpp>

namespace rukh::http::compression {

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
    uint8_t buf[4096];

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
