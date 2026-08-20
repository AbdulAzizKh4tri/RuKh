/**
 * @file ICompressor.hpp
 * @brief Interface for compressors
 */
#pragma once

namespace rukh::http::compression {

/**
 * @brief Common interface for HTTP content compressors.
 *
 * Implementations support one-shot compression as well as streaming
 * compression through chunk feeding and finalization.
 */
class ICompressor {
public:
  /**
   * @brief Compresses the full input in one shot.
   * @param input The complete uncompressed payload to compress.
   * @returns The compressed bytes for the given payload.
   */
  virtual std::string compress(std::string_view input) = 0;

  /**
   * @brief Feeds the next chunk into an ongoing streaming compression run.
   * @param input The next uncompressed chunk to add to the stream.
   * @returns Any compressed bytes produced by processing the chunk.
   */
  virtual std::string feedChunk(std::string_view input) = 0;

  /**
   * @brief Finishes a streaming compression run and flushes remaining output.
   * @returns Any final compressed bytes required to complete the stream.
   */
  virtual std::string finish() = 0;

  /**
   * @brief Returns the HTTP content-coding name for this compressor.
   * @returns The encoding token advertised in response headers.
   */
  virtual std::string_view getEncoding() const = 0;
  virtual ~ICompressor() = default;
};
} // namespace rukh::http::compression
