/**
 * @file CompressorFactory.hpp
 * @brief Factory for HTTP content compressors based on encoding
 */
#pragma once

#include <rukh/ServerConfig.hpp>
#include <rukh/http/compression/BrotliCompressor.hpp>
#include <rukh/http/compression/GzipCompressor.hpp>
#include <rukh/http/compression/ICompressor.hpp>

namespace rukh::http::compression {

/**
 * @brief Factory for HTTP content compressors based on encoding
 * @param encoding The encoding to get a compressor for (eg: "br", "gzip", "*")
 */
inline std::unique_ptr<ICompressor> getCompressor(std::string_view encoding) {
  if (encoding == "identity")
    return nullptr;
  if (encoding == "br")
    return std::make_unique<BrotliCompressor>();
  if (encoding == "gzip")
    return std::make_unique<GzipCompressor>(ServerConfig::STATIC_GZIP_COMPRESS_LEVEL);
  if (encoding == "*")
    return std::make_unique<BrotliCompressor>(); // default
  return nullptr;
}
} // namespace rukh::http::compression
