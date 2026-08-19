#pragma once

namespace rukh::http::compression  {

class ICompressor {
public:
  virtual std::string compress(std::string_view input) = 0;
  virtual std::string feedChunk(std::string_view input) = 0;
  virtual std::string finish() = 0;
  virtual std::string_view getEncoding() const = 0;
  virtual ~ICompressor() = default;
};
} // namespace rukh
