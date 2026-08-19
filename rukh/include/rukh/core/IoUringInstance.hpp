/**
 * @file IoUringInstance.hpp
 * @brief RAII wrapper for io_uring
 */

#pragma once

#include <cstdint>
#include <liburing.h>

#include <rukh/ServerConfig.hpp>

namespace rukh::core {

/// RAII wrapper for io_uring
class IoUringInstance {
public:
  IoUringInstance(int depth = ServerConfig::IO_URING_RING_SIZE) {
    if (io_uring_queue_init(depth, &ring_, 0) < 0)
      throw std::runtime_error("io_uring_queue_init failed");
  }

  ~IoUringInstance() { io_uring_queue_exit(&ring_); }

  IoUringInstance(const IoUringInstance &) = delete;
  IoUringInstance &operator=(const IoUringInstance &) = delete;
  IoUringInstance(IoUringInstance &&) = delete;
  IoUringInstance &operator=(IoUringInstance &&) = delete;

  /**
   * @brief Prepare a file read operation
   *
   * @param fd the file fd to read from.
   * @param buf the buffer to write the read data into
   * @param len the length of data to be read
   * @param userData Used to identify which read completed. Check @c IoUringInstance::drainCompletions
   * @param offset The offset at which to read the file from, -1 to let the system keep track of that.
   *
   * @see ioSubmit
   *
   */
  bool prepRead(int fd, void *buf, size_t len, uint64_t userData, uint64_t offset) {
    auto *sqe = io_uring_get_sqe(&ring_);
    if (not sqe)
      return false;
    io_uring_prep_read(sqe, fd, buf, len, offset);
    io_uring_sqe_set_data64(sqe, userData);
    return true;
  }

  /**
   * @brief Prepare a file write operation
   *
   * @param fd the file fd to write to.
   * @param buf the buffer to write the data from.
   * @param len the length of data to be written.
   * @param userData Used to identify which write completed. Check @c IoUringInstance::drainCompletions
   * @param offset The offset in the file where we want to write, -1 to let the system keep track of that.
   *
   * @see ioSubmit
   */
  bool prepWrite(int fd, const void *buf, size_t len, uint64_t userData, uint64_t offset) {
    auto *sqe = io_uring_get_sqe(&ring_);
    if (not sqe)
      return false;
    io_uring_prep_write(sqe, fd, buf, len, offset);
    io_uring_sqe_set_data64(sqe, userData);
    return true;
  }

  /**
   * @brief Submit the io_uring ring to the kernel. This is what actually starts the I/O.
   */
  void ioSubmit() { io_uring_submit(&ring_); }

  /**
   * @brief Check io_uring's "Completion Queue Entry" (CQEs) and call callback on them
   *
   * @tparam Callback
   * @param callback The callback to tell IoUring what to do with the completed entry.
   *
   * @details
   * The callback should have the following signature:
   * @code
   * void callback(uint64_t userData, int res);
   * @endcode
   *
   * @param userData The identifying sequence set during @c prepRead / @c prepWrite.
   * @param res The result of the cqe.
   */
  template <typename Callback> void drainCompletions(Callback callback) {
    io_uring_cqe *cqe;
    while (io_uring_peek_cqe(&ring_, &cqe) == 0) {
      callback(cqe->user_data, cqe->res);
      io_uring_cqe_seen(&ring_, cqe);
    }
  }

private:
  io_uring ring_;
};
} // namespace rukh::core
