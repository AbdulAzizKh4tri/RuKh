/**
 * @file HttpConnection.hpp
 * @brief HTTP connection with the entire HTTP lifecycle
 */

#pragma once

#include <chrono>
#include <exception>
#include <memory>
#include <spdlog/spdlog.h>
#include <sys/timerfd.h>
#include <sys/types.h>
#include <variant>

#include <rukh/Exceptions.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/TypeHelpers.hpp>
#include <rukh/core/Awaitables.hpp>
#include <rukh/core/ExecutorContext.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/http/BodyStream.hpp>
#include <rukh/http/ChunkDecoder.hpp>
#include <rukh/http/ErrorFactory.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/HttpStreamResponse.hpp>
#include <rukh/http/HttpTypes.hpp>
#include <rukh/http/Router.hpp>
#include <rukh/http/httpUtils.hpp>
#include <rukh/net/ConnectionIO.hpp>
#include <rukh/net/StreamResults.hpp>

namespace rukh::http {

/// HTTP connection with the entire HTTP lifecycle
template <typename Stream> class HttpConnection {
public:
  HttpConnection(std::unique_ptr<Stream> stream, Router &router, ErrorFactory &errorFactory,
                 std::atomic<bool> &shutdown, std::atomic<int> &globalConnectionCount)
      : io_(std::move(stream)), router_(router), errorFactory_(errorFactory), shutdown_(shutdown),
        ConnGuard_(globalConnectionCount) {
    request_.setIp(io_.getIp());
    request_.setPort(io_.getPort());
  }

  /**
   * @brief The entire HTTP lifecycle in one coroutine.
   *
   * - handshake
   * - read request headers
   * - create body stream
   * - dispatch to middleware and handlers
   * - drain body stream just in case.
   * - send or stream response
   */
  core::Task<void> run() {
    // This is intentionally one flat coroutine rather than a chain of sub-coroutines
    // (readHeaders(), readBody(), etc.). Keeping everything in one coroutine means
    // one frame allocation for the entire lifetime of the connection (atleast on the happy path).

    resetInactivity();

    //=== TLS Handshake ===
    for (bool done = false; not done;) {
      net::HandshakeResult result = io_.handshake();
      switch (result) {
      case net::HandshakeResult::DONE:
      case net::HandshakeResult::NO_TLS:
        done = true;
        break;
      case net::HandshakeResult::WANT_READ:
        co_await core::ReadAwaitable{io_.getFd(), inactivityDeadline_};
        if (core::tl_timed_out)
          co_return;
        break;
      case net::HandshakeResult::WANT_WRITE:
        co_await core::WriteAwaitable{io_.getFd(), inactivityDeadline_};
        if (core::tl_timed_out)
          co_return;
        break;
      case net::HandshakeResult::ERROR:
        co_return;
      }
    }

    //=== Per-request loop ===
    for (;;) {
      if (not keepAlive_)
        co_return;

      formationDeadline_ = now() + std::chrono::seconds(ServerConfig::FORMATION_TIMEOUT_S);

      //=== Read headers ===
      {
        size_t headerSize = 0;
        while (headerSize == 0) {
          // Strip leading bare CRLF — HTTP/1.1 allows clients to send \r\n
          // between pipelined requests as a robustness measure (RFC 9112 §2.2)
          while (io_.getReadBufferSize() >= 2 && *(io_.readBufferBegin()) == '\r' &&
                 *(io_.readBufferBegin() + 1) == '\n') {
            io_.eraseFromReadBuffer(2);
          }

          // Search for the end-of-headers marker (\r\n\r\n)
          auto it = std::search(io_.readBufferBegin(), io_.readBufferEnd(), crlf2.begin(), crlf2.end());

          if (it != io_.readBufferEnd()) {
            headerSize = std::distance(io_.readBufferBegin(), it);
            break;
          }

          // Marker not found yet — suspend until the socket is readable or a deadline fires
          auto readResult = co_await io_.read(ServerConfig::MAX_HEADER_BYTES, activeDeadline());
          if (readResult == net::ReadResult::DATA) {
            auto timeNow = now();
            resetInactivity(timeNow);
          } else if (readResult == net::ReadResult::CLOSED || readResult == net::ReadResult::ERROR) {
            co_return;
          } else if (it == io_.readBufferEnd() && readResult == net::ReadResult::BUFFER_LIMIT_EXCEEDED) {
            co_await sendErrorResponseAndClose(431);
            co_return;
          } else if (readResult == net::ReadResult::TIMED_OUT) {
            co_await sendErrorResponseAndClose(408);
            co_return;
          }
        }

        //=== Parse + validate headers ===
        if (headerSize > ServerConfig::MAX_HEADER_BYTES) {
          co_await sendErrorResponseAndClose(431);
          co_return;
        }

        std::string_view headerView(reinterpret_cast<const char *>(io_.readBufferData()), headerSize);

        if (not request_.parseRequestHeader(headerView)) {
          SPDLOG_ERROR("PARSE ERROR... {}",
                       std::string_view(reinterpret_cast<const char *>(io_.readBufferData()), headerSize));
          co_await sendErrorResponseAndClose(400, "Malformed Header");
          co_return;
        }

        if (request_.getVersion() != "HTTP/1.1") {
          SPDLOG_ERROR("VERSION ERROR... {}", request_.getVersion());
          co_await sendErrorResponseAndClose(505);
          co_return;
        }

        if (request_.getHeaderLower("host") == "") {
          SPDLOG_ERROR("HOST ERROR... {}", headerView);
          co_await sendErrorResponseAndClose(400, "No Host Header Provided");
          co_return;
        }

        // Consume the header bytes + the \r\n\r\n terminator from the read buffer
        io_.eraseFromReadBuffer(headerSize + 4);
      }

      //=== Expect: 100-continue ===
      // Client is asking permission to send a body — validate the route exists
      // before committing to receiving potentially large data
      auto expect = request_.getHeaderLower("expect");

      if (not expect.empty()) {
        if (toLowerCase(expect) == "100-continue") {
          auto res = request_.getContentLength();
          if (not res) {
            if (res.error() == ContentLengthError::INVALID_CONTENT_LENGTH) {
              co_await sendErrorResponseAndClose(400, "Invalid content-length header");
              co_return;
            } else if (res.error() == ContentLengthError::NO_CONTENT_LENGTH_HEADER) {
              if (toLowerCase(request_.getHeaderLower("transfer-encoding")).find("chunked") == std::string::npos) {
                co_await sendErrorResponseAndClose(400, "Missing content-length header");
                co_return;
              }
            }
          }

          if (res.value() > ServerConfig::MAX_CONTENT_LENGTH) {
            co_await sendErrorResponseAndClose(413);
            co_return;
          }

          // TODO: better checks
          RouterResponse result = router_.validate(request_);
          switch (result) {
          case RouterResponse::NOT_FOUND:
            co_await sendErrorResponseAndClose(404);
            co_return;
          case RouterResponse::METHOD_NOT_ALLOWED:
            co_await sendErrorResponseAndClose(405);
            co_return;
          case RouterResponse::OK: {
            std::string response = "HTTP/1.1 100 Continue\r\n\r\n";
            io_.enqueue(std::vector<unsigned char>(response.begin(), response.end()));
            while (io_.hasPendingWrites()) {
              if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK)
                co_return;
            }
          }
          }
        } else {
          SPDLOG_ERROR("Expect value not supported: {}", expect);
          co_await sendErrorResponseAndClose(417);
          co_return;
        }
      }

      formationDeadline_ = std::chrono::steady_clock::time_point::max();

      { // Reading Body
        bool hasContentLengthHeader = request_.getHeaderLower("content-length") != "";
        auto transferEncodingHeader = request_.getHeaderLower("transfer-encoding");

        if (hasContentLengthHeader && transferEncodingHeader != "") {
          SPDLOG_ERROR("Content-Length and Transfer-Encoding headers both found");
          co_await sendErrorResponseAndClose(400, "Request cannot contain both Content-Length "
                                                  "and Transfer-Encoding headers");
          co_return;
        }

        if (transferEncodingHeader != "") {
          if (not icontains(transferEncodingHeader, "chunked")) {
            SPDLOG_ERROR("Transfer-Encoding header not supported: {}", transferEncodingHeader);
            co_await sendErrorResponseAndClose(501);
            co_return;
          }

          BodyReadFn chunkReadFn = [this](std::span<unsigned char> buf,
                                          size_t /*unused*/) mutable -> core::Task<size_t> {
            auto result = co_await chunkDecoder_.readSome(io_, buf, activeDeadline());
            if (!result) {
              switch (result.error()) {
              case ChunkError::MALFORMED:
                throw ServerException("Malformed chunk encoding", 400);
              case ChunkError::CHUNK_TOO_LARGE:
                throw ServerException("Chunk too large", 413);
              case ChunkError::REQUEST_SIZE_LIMIT_EXCEEDED:
                throw ServerException("Request body size limit exceeded", 413);
              }
            }

            if (chunkDecoder_.isDone())
              request_.setAttribute("chunkTrailers", chunkDecoder_.getTrailers());

            if (*result > 0)
              resetInactivity();
            co_return *result;
          };

          BodyDrainFn chunkDrainFn = [this](size_t /*unused*/) mutable -> core::Task<void> {
            std::array<unsigned char, 4096> scratch;
            std::span<unsigned char> buf{scratch};
            while (true) {
              auto result = co_await chunkDecoder_.readSome(io_, buf, activeDeadline());
              if (!result) {
                switch (result.error()) {
                case ChunkError::MALFORMED:
                  throw ServerException("Malformed chunk encoding", 400);
                case ChunkError::CHUNK_TOO_LARGE:
                  throw ServerException("Chunk too large", 413);
                case ChunkError::REQUEST_SIZE_LIMIT_EXCEEDED:
                  throw ServerException("Request body size limit exceeded", 413);
                }
              }
              if (*result == 0)
                co_return;
              resetInactivity();
            }
          };

          request_.attachBodyStream(std::make_unique<BodyStream>(0, std::move(chunkReadFn), std::move(chunkDrainFn)));

        } else if (hasContentLengthHeader) {
          auto contentLengthResult = request_.getContentLength();
          if (!contentLengthResult) {
            switch (contentLengthResult.error()) {
            case ContentLengthError::NO_CONTENT_LENGTH_HEADER:
              std::unreachable();
            case ContentLengthError::INVALID_CONTENT_LENGTH:
              co_await sendErrorResponseAndClose(400, "Invalid Content-Length header");
              co_return;
            }
          }
          size_t contentLength = contentLengthResult.value();
          if (contentLength > ServerConfig::MAX_CONTENT_LENGTH) {
            co_await sendErrorResponseAndClose(413);
            co_return;
          }

          BodyReadFn readFn = [this](std::span<unsigned char> buf, size_t remaining) mutable -> core::Task<size_t> {
            if (io_.getReadBufferSize() >= buf.size()) {
              std::memcpy(buf.data(), io_.readBufferData(), buf.size());
              io_.eraseFromReadBuffer(buf.size());
              co_return buf.size();
            }

            if (io_.getReadBufferSize() >= remaining) {
              std::memcpy(buf.data(), io_.readBufferData(), remaining);
              io_.eraseFromReadBuffer(remaining);
              co_return remaining;
            }

            net::ReadResult readResult = co_await io_.read(ServerConfig::MAX_CONTENT_LENGTH, activeDeadline());
            if (readResult == net::ReadResult::DATA) {
              resetInactivity();
            } else if (readResult == net::ReadResult::BUFFER_LIMIT_EXCEEDED) {
              throw ServerException("Buffer limit exceeded", 500);
            } else if (readResult == net::ReadResult::CLOSED || readResult == net::ReadResult::ERROR) {
              throw ConnectionException("Connection closed");
            } else if (readResult == net::ReadResult::TIMED_OUT) {
              throw ConnectionException("Read timed out");
            }

            size_t n = std::min(io_.getReadBufferSize(), std::min(buf.size(), remaining));
            std::memcpy(buf.data(), io_.readBufferData(), n);
            io_.eraseFromReadBuffer(n);
            co_return n;
          };

          BodyDrainFn drainFn = [this](size_t remaining) mutable -> core::Task<void> {
            while (io_.getReadBufferSize() < remaining) {
              size_t oldSize = io_.getReadBufferSize();
              net::ReadResult readResult = co_await io_.read(ServerConfig::MAX_CONTENT_LENGTH, activeDeadline());
              if (readResult == net::ReadResult::DATA) {
                resetInactivity();
              } else if (readResult == net::ReadResult::BUFFER_LIMIT_EXCEEDED) {
                throw ServerException("Buffer limit exceeded", 500);
              } else if (readResult == net::ReadResult::CLOSED || readResult == net::ReadResult::ERROR) {
                throw ConnectionException("Connection closed");
              } else if (readResult == net::ReadResult::TIMED_OUT) {
                throw ConnectionException("Read timed out");
              }
            }
            io_.eraseFromReadBuffer(remaining);
          };

          request_.attachBodyStream(std::make_unique<BodyStream>(contentLength, std::move(readFn), std::move(drainFn)));
        } else {

          BodyReadFn readFn = [this](std::span<unsigned char> /* unused */,
                                     size_t /* unused */) mutable -> core::Task<size_t> { co_return 0; };

          BodyDrainFn drainFn = [this](size_t /* unused */) mutable -> core::Task<void> { co_return; };
          request_.attachBodyStream(std::make_unique<BodyStream>(0, std::move(readFn), std::move(drainFn)));
        }
      }

      //=== Dispatch ===
      Response response;

      try {
        response = co_await router_.dispatch(request_);
      } catch (ServerException &e) {
        SPDLOG_ERROR("Server threw exception: {}", e.what());
        response = buildErrorResponse(e.status_code, e.what());
        keepAlive_ = not e.fatal;
      } catch (HandlerException &e) {
        SPDLOG_ERROR("Handler threw exception: {}", e.what());
        response = buildErrorResponse(e.status_code, e.what());
        keepAlive_ = not e.fatal;
      } catch (std::exception &e) {
        SPDLOG_ERROR("Exception: {}", e.what());
        response = buildErrorResponse(500, e.what());
        keepAlive_ = false;
      } catch (...) {
        SPDLOG_CRITICAL("HttpConnection: Unknown Exception");
        response = buildErrorResponse(500);
        keepAlive_ = false;
      }

      co_await request_.bodyStream()->drain();

      //=== Set Connection header ===
      std::visit(overloads{[this](auto &res) {
                   if (shutdown_) {
                     keepAlive_ = false;
                     res.headers.setHeaderLower("connection", "close");
                   } else {
                     if (not keepAlive_ || not shouldKeepAlive())
                       res.headers.setHeaderLower("connection", "close");
                   }
                 }},
                 response);

      //=== Send: plain response ===
      if (HttpResponse *res = std::get_if<HttpResponse>(&response)) {
        if (res->getStatusCode() < 0) {
          SPDLOG_CRITICAL("Prevented: Trying to send response with negative status code");
          co_return;
        }

        if (not res->serializeInto(io_.getWriteBuffer()))
          co_return;

        logRequest(request_, *res);
        while (io_.hasPendingWrites()) {
          if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK)
            co_return;
        }
        resetForNextRequest();
        continue;

        //=== Send: streaming response ===
      } else if (HttpStreamResponse *responseStream = std::get_if<HttpStreamResponse>(&response)) {
        // Send headers immediately — chunked body follows as chunks become available
        if (not responseStream->serializeHeaderInto(io_.getWriteBuffer()))
          co_return;

        while (io_.hasPendingWrites()) {
          if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK)
            co_return;
        }

        std::optional<std::string> chunkOpt = "init";
        bool error = false;
        while (chunkOpt.has_value()) {
          try {
            chunkOpt = co_await responseStream->getNextChunk();
          } catch (const std::exception &e) {
            SPDLOG_ERROR("Stream handler threw exception: {}", e.what());
            if (not responseStream->serializeBlockInto("Internal Server Error: " + std::string(e.what()),
                                                       io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }

            if (not responseStream->serializeBlockInto("", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            error = true;
          } catch (...) {
            SPDLOG_ERROR("Stream handler threw unknown exception");
            if (not responseStream->serializeBlockInto("Internal Server Error", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            if (not responseStream->serializeBlockInto("", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            error = true;
          }

          if (error) {
            while (io_.hasPendingWrites()) {
              if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK)
                co_return;
            }
            co_return;
          }

          if (not chunkOpt.has_value()) {
            // nullopt returned — send the terminal zero-length chunk to close the stream
            if (not responseStream->serializeBlockInto("", io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
            logRequest(request_, *responseStream);
            while (io_.hasPendingWrites()) {
              if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK)
                co_return;
            }
            break;
          } else {
            if (not responseStream->serializeBlockInto(*chunkOpt, io_.getWriteBuffer())) {
              io_.resetConnection();
              co_return;
            }
          }

          while (io_.hasPendingWrites()) {
            if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK) {
              io_.resetConnection();
              co_return;
            }
          }
        }
      }

      resetForNextRequest();
    }
  }

  int getFd() const { return io_.getFd(); }
  std::string getIp() const { return io_.getIp(); }
  uint16_t getPort() const { return io_.getPort(); }

private:
  struct ConnGuard {
    std::atomic<int> &c;
    ConnGuard(std::atomic<int> &c) : c(c) {}
    ~ConnGuard() { c.fetch_sub(1, std::memory_order_relaxed); }
  };

  HttpRequest request_;
  Router &router_;
  ErrorFactory &errorFactory_;

  ChunkDecoder<Stream> chunkDecoder_;

  net::ConnectionIO<Stream> io_;

  bool keepAlive_ = true;
  std::atomic<bool> &shutdown_;
  ConnGuard ConnGuard_;

  std::chrono::steady_clock::time_point inactivityDeadline_ = std::chrono::steady_clock::time_point::max();
  std::chrono::steady_clock::time_point formationDeadline_ = std::chrono::steady_clock::time_point::max();

  void resetInactivity(std::chrono::steady_clock::time_point timeNow = now()) {
    inactivityDeadline_ = timeNow + std::chrono::seconds(ServerConfig::INACTIVITY_TIMEOUT_S);
  }

  std::chrono::steady_clock::time_point activeDeadline() const {
    return formationDeadline_ != std::chrono::steady_clock::time_point::max()
               ? std::min(inactivityDeadline_, formationDeadline_)
               : inactivityDeadline_;
  }

  bool shouldKeepAlive() const { return not icontains(request_.getHeaderLower("connection"), "close"); }

  core::Task<void> sendErrorResponseAndClose(int statusCode, const std::string &message = "") {
    HttpResponse response = buildErrorResponse(statusCode, message);
    keepAlive_ = false;
    response.headers.setHeaderLower("connection", "close");
    if (response.getStatusCode() == -1) {
      SPDLOG_CRITICAL("Prevented: Trying to send response with status code -1");
      co_return;
    }

    if (not response.serializeInto(io_.getWriteBuffer()))
      co_return;
    logRequest(request_, response);
    do {
      if (auto r = co_await io_.write(ServerConfig::INACTIVITY_TIMEOUT_S); r != net::WriteResult::OK)
        co_return;
    } while (io_.hasPendingWrites());
  }

  HttpResponse buildErrorResponse(int statusCode, const std::string &message = "") {
    HttpResponse response = errorFactory_.build(request_, statusCode, message);
    if (request_.getMethod() == "HEAD")
      response.stripBody();
    if (statusCode == 405)
      response.headers.setHeaderLower("allow", router_.getAllowedMethodsString(request_));
    return response;
  }

  void resetForNextRequest() {
    request_.reset(io_.getIp(), io_.getPort());
    formationDeadline_ = std::chrono::steady_clock::time_point::max();
    chunkDecoder_.reset();
  }

  void logRequest(const HttpRequest &req, const HttpResponse &res) {
    int status = res.getStatusCode();
    if (status >= 500) {
      SPDLOG_ERROR("{}  {:<8} {:<20}  {:<16}:{:<6}", status, req.getMethod(), req.getPath(), req.getIp(),
                   req.getPort());
    } else if (status >= 400) {
      SPDLOG_WARN("{}  {:<8} {:<20}  {:<16}:{:<6}", status, req.getMethod(), req.getPath(), req.getIp(), req.getPort());
    } else {
      SPDLOG_INFO("{}  {:<8} {:<20}  {:<16}:{:<6}", status, req.getMethod(), req.getPath(), req.getIp(), req.getPort());
    }
  }

  void logRequest(const HttpRequest &req, const HttpStreamResponse &res) {
    int status = res.getStatusCode();
    if (status >= 500) {
      SPDLOG_ERROR("{}  {:<8} {:<20}  {:<16}:{:<6}", status, req.getMethod(), req.getPath(), req.getIp(),
                   req.getPort());
    } else if (status >= 400) {
      SPDLOG_WARN("{}  {:<8} {:<20}  {:<16}:{:<6}", status, req.getMethod(), req.getPath(), req.getIp(), req.getPort());
    } else {
      SPDLOG_INFO("{}  {:<8} {:<20}  {:<16}:{:<6}", status, req.getMethod(), req.getPath(), req.getIp(), req.getPort());
    }
  }
};
} // namespace rukh::http
