#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <expected>
#include <span>
#include <string>

#include <rukh/Exceptions.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/core/utils.hpp>
#include <rukh/net/ConnectionIO.hpp>

namespace rukh {

enum class ChunkError { MALFORMED, CHUNK_TOO_LARGE, REQUEST_SIZE_LIMIT_EXCEEDED };

template <typename Stream> class ChunkDecoder {
public:
  core::Task<std::expected<size_t, ChunkError>> readSome(net::ConnectionIO<Stream> &io, std::span<unsigned char> buf,
                                                         std::chrono::steady_clock::time_point deadline) {
    if (state_ == State::DONE)
      co_return size_t{0};

    size_t written = 0;

    while (written < buf.size()) {

      if (state_ == State::CHUNK_SIZE) {
        auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf.begin(), crlf.end());
        while (it == io.readBufferEnd()) {
          handleIO(co_await io.read(deadline));
          it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf.begin(), crlf.end());
        }

        size_t chunkSize = 0;
        auto [ptr, ec] = std::from_chars(reinterpret_cast<const char *>(io.readBufferData()),
                                         reinterpret_cast<const char *>(&*it), chunkSize, 16);

        if (ec != std::errc{})
          co_return std::unexpected(ChunkError::MALFORMED);

        io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 2);

        if (chunkSize == 0) {
          state_ = State::TRAILER;
          continue;
        }

        if (chunkSize > ServerConfig::MAX_TE_CHUNK_LENGTH)
          co_return std::unexpected(ChunkError::CHUNK_TOO_LARGE);

        totalBytes_ += chunkSize;
        if (totalBytes_ > ServerConfig::MAX_TE_LENGTH)
          co_return std::unexpected(ChunkError::REQUEST_SIZE_LIMIT_EXCEEDED);

        chunkRemaining_ = chunkSize;
        state_ = State::CHUNK_BODY;
        continue;
      }

      if (state_ == State::CHUNK_BODY) {
        if (io.getReadBufferSize() == 0)
          handleIO(co_await io.read(deadline));

        size_t toCopy = std::min({io.getReadBufferSize(), chunkRemaining_, buf.size() - written});
        std::memcpy(buf.data() + written, io.readBufferData(), toCopy);
        io.eraseFromReadBuffer(toCopy);
        written += toCopy;
        chunkRemaining_ -= toCopy;

        if (chunkRemaining_ == 0)
          state_ = State::CHUNK_CRLF;

        continue;
      }

      if (state_ == State::CHUNK_CRLF) {
        while (io.getReadBufferSize() < 2)
          handleIO(co_await io.read(deadline));

        if (io.readBufferData()[0] != '\r' || io.readBufferData()[1] != '\n')
          co_return std::unexpected(ChunkError::MALFORMED);

        io.eraseFromReadBuffer(2);
        state_ = State::CHUNK_SIZE;
        continue;
      }

      if (state_ == State::TRAILER) {
        // Fast path: no trailers, just a bare \r\n
        if (io.getReadBufferSize() >= 2 && io.readBufferData()[0] == '\r' && io.readBufferData()[1] == '\n') {
          io.eraseFromReadBuffer(2);
          state_ = State::DONE;
          break;
        }

        auto it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf2.begin(), crlf2.end());
        while (it == io.readBufferEnd()) {
          handleIO(co_await io.read(deadline));
          it = std::search(io.readBufferBegin(), io.readBufferEnd(), crlf2.begin(), crlf2.end());
        }

        trailers_.assign(reinterpret_cast<const char *>(io.readBufferData()), std::distance(io.readBufferBegin(), it));
        io.eraseFromReadBuffer(std::distance(io.readBufferBegin(), it) + 4);
        state_ = State::DONE;
        break;
      }

      break;
    }

    if (state_ == State::DONE && written == 0)
      co_return size_t{0};
    co_return written;
  }

  const std::string &getTrailers() const { return trailers_; }
  bool isDone() const { return state_ == State::DONE; }

  void reset() {
    state_ = State::CHUNK_SIZE;
    chunkRemaining_ = 0;
    totalBytes_ = 0;
    trailers_.clear();
  }

private:
  enum class State { CHUNK_SIZE, CHUNK_BODY, CHUNK_CRLF, TRAILER, DONE };

  static void handleIO(net::ReadResult r) {
    if (r == net::ReadResult::BUFFER_LIMIT_EXCEEDED)
      throw ServerException("Buffer limit exceeded", 500);
    if (r == net::ReadResult::CLOSED || r == net::ReadResult::ERROR)
      throw ConnectionException("Connection closed");
    if (r == net::ReadResult::TIMED_OUT)
      throw ConnectionException("Read timed out");
  }

  State state_ = State::CHUNK_SIZE;
  size_t chunkRemaining_ = 0;
  size_t totalBytes_ = 0;
  std::string trailers_;
};
} // namespace rukh
