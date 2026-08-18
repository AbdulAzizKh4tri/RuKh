/**
 * @file ConnectionIO.hpp
 * @brief Stream wrapper for easier co_await-able non-blocking reads and writes
 */

#pragma once

#include <memory>
#include <spdlog/spdlog.h>

#include <rukh/core/Executor.hpp>
#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/utils.hpp>
#include <rukh/net/StreamResults.hpp>

namespace rukh::net {

enum class ReadResult {
  DATA,
  CLOSED,
  WOULD_BLOCK,
  BUFFER_LIMIT_EXCEEDED,
  ERROR,
  TIMED_OUT,
};

enum class WriteResult {
  OK,
  ERROR,
  TIMED_OUT,
};

/// Stream wrapper for easier co_await-able non-blocking reads and writes
template <typename Stream> class ConnectionIO {
public:
  struct ReadDataAwaitable {
    ConnectionIO &io;
    std::chrono::steady_clock::time_point deadline;
    size_t maxBufferSize;
    ReadResult result = ReadResult::WOULD_BLOCK;

    bool await_ready() noexcept {
      result = io.drainIntoReadBuffer(maxBufferSize);
      return result != ReadResult::WOULD_BLOCK;
    }

    void await_suspend(std::coroutine_handle<> h) noexcept { core::tl_executor->waitForRead(io.getFd(), h, deadline); }

    ReadResult await_resume() noexcept {
      if (result != ReadResult::WOULD_BLOCK)
        return result;
      if (core::tl_timed_out)
        return ReadResult::TIMED_OUT;
      return io.drainIntoReadBuffer(maxBufferSize);
    }
  };

  struct WriteDataAwaitable {
    ConnectionIO &io;
    int inactivitySeconds;
    bool error = false;

    bool await_ready() noexcept {
      if (!io.flushFromWriteBuffer()) {
        error = true;
        return true;
      }
      return !io.hasPendingWrites();
    }

    void await_suspend(std::coroutine_handle<> h) noexcept {
      auto deadline = inactivitySeconds ? now() + std::chrono::seconds(inactivitySeconds)
                                        : std::chrono::steady_clock::time_point::max();
      core::tl_executor->enableWriteEvents(io.getFd());
      core::tl_executor->waitForWrite(io.getFd(), h, deadline);
    }

    WriteResult await_resume() noexcept {
      if (error)
        return WriteResult::ERROR;
      if (core::tl_timed_out)
        return WriteResult::TIMED_OUT;
      if (!io.flushFromWriteBuffer())
        return WriteResult::ERROR;

      if (not io.hasPendingWrites())
        core::tl_executor->disableWriteEvents(io.getFd());
      return WriteResult::OK;
    }
  };

  ConnectionIO(std::shared_ptr<Stream> stream) : stream_(std::move(stream)) {}

  [[nodiscard]] ReadDataAwaitable
  read(std::chrono::steady_clock::time_point deadline = std::chrono::steady_clock::time_point::max(),
       size_t maxBufferSize = ServerConfig::MAX_CONTENT_LENGTH) noexcept {
    return {*this, deadline, maxBufferSize};
  }

  [[nodiscard]] WriteDataAwaitable write(int inactivitySeconds = 0) noexcept { return {*this, inactivitySeconds}; }

  ReadResult drainIntoReadBuffer(size_t maxBufferSize) {
    bool gotData = false;

    for (;;) {
      size_t currentSize = getReadBufferSize();
      if (currentSize >= maxBufferSize)
        return ReadResult::BUFFER_LIMIT_EXCEEDED;

      size_t spaceToAllow = std::min(maxBufferSize - currentSize, size_t{4096});
      size_t oldSize = readBuffer_.size();
      readBuffer_.resize(oldSize + spaceToAllow);

      auto span = std::span<unsigned char>(readBuffer_.data() + oldSize, spaceToAllow);
      ReceiveResult result = stream_->receive(span);

      switch (result.status) {
      case ReceiveResult::Status::DATA:
        readBuffer_.resize(oldSize + result.bytes);
        gotData = true;
        break;
      case ReceiveResult::Status::WOULD_BLOCK:
        readBuffer_.resize(oldSize);
        return gotData ? ReadResult::DATA : ReadResult::WOULD_BLOCK;
      case ReceiveResult::Status::CLOSED:
        return ReadResult::CLOSED;
      case ReceiveResult::Status::ERROR:
        SPDLOG_ERROR("Receive error for {}:{}", stream_->getIp(), stream_->getPort());
        return ReadResult::ERROR;
      }
    }
  }

  bool flushFromWriteBuffer() {
    while (hasPendingWrites()) {
      ssize_t n = stream_->send(std::span(writeBufferBegin(), writeBufferEnd()));
      if (n < 0)
        return false;
      if (n == 0)
        return true;
      eraseFromWriteBuffer(n);
    }
    return true;
  }

  std::vector<unsigned char>::const_iterator readBufferBegin() const { return readBuffer_.begin() + readOffset_; }
  std::vector<unsigned char>::const_iterator readBufferEnd() const { return readBuffer_.end(); }
  std::vector<unsigned char>::const_iterator writeBufferBegin() const { return writeBuffer_.begin() + writeOffset_; }
  std::vector<unsigned char>::const_iterator writeBufferEnd() const { return writeBuffer_.end(); }

  void enqueue(std::vector<unsigned char> data) { writeBuffer_.insert(writeBuffer_.end(), data.begin(), data.end()); }

  bool hasPendingWrites() const { return getWriteBufferSize() > 0; }

  void eraseFromReadBuffer(size_t n) {
    readOffset_ += n;
    if (readOffset_ > readBuffer_.size() / 2) {
      readBuffer_.erase(readBuffer_.begin(), readBuffer_.begin() + readOffset_);
      readOffset_ = 0;
    }
  }

  void eraseFromWriteBuffer(size_t n) {
    writeOffset_ += n;
    if (writeOffset_ > writeBuffer_.size() / 2) {
      writeBuffer_.erase(writeBuffer_.begin(), writeBuffer_.begin() + writeOffset_);
      writeOffset_ = 0;
    }
  }

  std::string getReadBufferString(int end = -1) const {
    if (end < 0)
      return std::string(readBuffer_.begin() + readOffset_, readBuffer_.end());
    else
      return std::string(readBuffer_.begin() + readOffset_, readBuffer_.begin() + readOffset_ + end);
  }

  std::string getWriteBufferString(int end = -1) const {
    if (end < 0)
      return std::string(writeBuffer_.begin() + writeOffset_, writeBuffer_.end());
    else
      return std::string(writeBuffer_.begin() + writeOffset_, writeBuffer_.begin() + writeOffset_ + end);
  }

  const unsigned char *readBufferData() const { return readBuffer_.data() + readOffset_; }
  size_t getReadOffset() const { return readOffset_; }
  size_t getWriteOffset() const { return writeOffset_; }
  size_t getReadBufferSize() const { return readBuffer_.size() - readOffset_; }
  size_t getWriteBufferSize() const { return writeBuffer_.size() - writeOffset_; }
  std::vector<unsigned char> &getReadBuffer() { return readBuffer_; }
  std::vector<unsigned char> &getWriteBuffer() { return writeBuffer_; }

  std::string getIp() const { return stream_->getIp(); }
  uint16_t getPort() const { return stream_->getPort(); }
  int getFd() const { return stream_->getFd(); }
  HandshakeResult handshake() { return stream_->handshake(); }
  void resetConnection() { stream_->resetConnection(); }

private:
  std::shared_ptr<Stream> stream_;
  size_t readOffset_ = 0, writeOffset_ = 0;
  std::vector<unsigned char> readBuffer_;
  std::vector<unsigned char> writeBuffer_;
};
} // namespace rukh::net
