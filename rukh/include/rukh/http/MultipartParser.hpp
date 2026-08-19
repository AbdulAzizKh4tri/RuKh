/**
 * @file MultipartParser.hpp
 * @brief Multipart form data parser
 */

#pragma once

#include <functional>
#include <iterator>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>
#include <vector>

#include <rukh/Exceptions.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/http/BodyStream.hpp>
#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/httpUtils.hpp>
#include <rukh/stringUtils.hpp>

namespace rukh::http {

/**
 * @brief Multipart form data parser
 *
 * This class is used to parse multipart/form-data requests
 * Sample usage:
 *
 * @code
 *  MultipartParser mp(request);
 *  std::string username, password;
 *  std::vector<std::string> terms;
 *
 *  mp.onField("username", [&username](std::string v) -> core::Task<void> { // without convenience wrapper
 *    username = v;
 *    co_return;
 *  });
 *
 *  mp.storeFieldValue("password", password); // with wrappers
 *  mp.storeFieldValues("terms", terms);
 *
 *  std::optional<core::AsyncFileWriter> writerOpt = core::AsyncFileWriter::open("./app/public/test.bin");
 *
 *  if (not writerOpt)
 *    throw std::runtime_error("File issue");
 *  mp.onFile("file", [&writerOpt](std::span<unsigned char> data) -> core::Task<void> {
 *    co_await writerOpt->writeChunk(std::string_view(reinterpret_cast<char *>(data.data()), data.size()));
 *  });
 *
 *  co_await mp.go(); // MUST CALL THIS
 *
 * @endcode
 */
class MultipartParser {

  using PartReadResult = std::tuple<size_t /*totalConsumed*/, bool /*end*/>;

  class BufferedReader {
  public:
    BufferedReader(BodyStream *bodyStream) : bodyStream_(bodyStream) {
      buffer_.resize(ServerConfig::MULTIPART_BUFFER_SIZE);
    }

    core::Task<std::pair<std::span<unsigned char>, size_t>> read() {
      auto span = std::span<unsigned char>(buffer_.data() + validBytes_, buffer_.size() - validBytes_);
      auto n = co_await bodyStream_->read(span);
      validBytes_ += n;
      co_return {std::span<unsigned char>(buffer_.data(), validBytes_), n};
    }

    std::span<unsigned char> consumeBytes(size_t n) {
      std::memmove(buffer_.data(), buffer_.data() + n, validBytes_ - n);
      validBytes_ -= n;
      return std::span<unsigned char>(buffer_.data(), validBytes_);
    }

  private:
    std::vector<unsigned char> buffer_;
    BodyStream *bodyStream_;

    size_t validBytes_ = 0;
  };

  class PartReader {
  public:
    PartReader(std::vector<std::pair<std::string, std::string>> headers, std::string &boundary, BufferedReader &reader,
               size_t maxSize)
        : headers_(headers), boundary_(boundary), reader_(reader), maxSize_(maxSize) {}

    core::Task<PartReadResult> readAndDo(std::move_only_function<core::Task<void>(std::span<unsigned char>)> doPart) {
      size_t fullBoundarySize = boundary_.size() + 2;
      size_t totalConsumed = 0;
      for (;;) {
        auto [span, _] = co_await reader_.read();
        auto [it, end] = findBoundary(span);
        if (it == span.end()) {
          totalConsumed += span.size() - fullBoundarySize;
          if (totalConsumed > maxSize_)
            throw ServerException("Request size limit exceeded", 413);

          co_await doPart(span.subspan(0, span.size() - fullBoundarySize));
          reader_.consumeBytes(span.size() - fullBoundarySize);

        } else {
          size_t endOfPart = it - span.begin();
          size_t endOfBoundary;
          endOfBoundary = endOfPart + fullBoundarySize;
          totalConsumed += endOfBoundary;
          if (totalConsumed > maxSize_)
            throw ServerException("Request size limit exceeded", 413);

          co_await doPart(span.subspan(0, endOfPart));
          reader_.consumeBytes(endOfBoundary);
          co_return {totalConsumed, end};
        }
      }
    }

    core::Task<PartReadResult> drainTillBoundary() {
      size_t fullBoundarySize = boundary_.size() + 2;
      size_t totalConsumed = 0;
      for (;;) {
        auto [span, n] = co_await reader_.read();
        auto [it, end] = findBoundary(span);
        if (it == span.end()) {
          reader_.consumeBytes(span.size() - fullBoundarySize);
          totalConsumed += span.size() - fullBoundarySize;
          if (totalConsumed > maxSize_)
            throw ServerException("Request size limit exceeded", 413);
        } else {
          size_t endOfPart = it - span.begin();
          size_t endOfBoundary;
          endOfBoundary = endOfPart + fullBoundarySize;
          totalConsumed += endOfBoundary;
          if (totalConsumed > maxSize_)
            throw ServerException("Request size limit exceeded", 413);
          reader_.consumeBytes(endOfBoundary);
          co_return {totalConsumed, end};
        }
      }
    }

  private:
    std::vector<std::pair<std::string, std::string>> headers_;
    std::string &boundary_;
    BufferedReader &reader_;
    size_t maxSize_;

    std::pair<std::span<unsigned char>::iterator, bool> findBoundary(std::span<unsigned char> &span) {

      auto it = std::search(span.begin(), span.end(), boundary_.begin(), boundary_.end());
      if (it == span.end())
        return {span.end(), false};

      auto boundaryEnd = it + boundary_.size();
      if (std::distance(boundaryEnd, span.end()) < 2)
        return {span.end(), false};
      if (*(boundaryEnd) == '-' and *(boundaryEnd + 1) == '-') {
        return std::make_pair(it, true);
      } else if (*(boundaryEnd) == '\r' and *(boundaryEnd + 1) == '\n') {
        return std::make_pair(it, false);
      } else {
        return {span.end(), false};
      }
    }
  };

public:
  MultipartParser(HttpRequest &request) : reader_(request.bodyStream()) {
    auto contentLengthResult = request.getContentLength();
    if (not contentLengthResult) {
      if (contentLengthResult.error() == ContentLengthError::INVALID_CONTENT_LENGTH) {
        throw ServerException("Invalid Content-Length", 400);
      } else if (contentLengthResult.error() == ContentLengthError::NO_CONTENT_LENGTH_HEADER) {
        if (toLowerCase(request.getHeaderLower("transfer-encoding")).find("chunked") == std::string::npos) {
          throw ServerException("Content length or TE chunked required", 411);
        }
      }
    }

    if (contentLengthResult) {
      contentLength_ = *contentLengthResult;
      if (contentLength_ > ServerConfig::MAX_MULTIPART_CONTENT_LENGTH) {
        throw ServerException("Request size limit exceeded", 413);
      }
    } else {
      contentLength_ = ServerConfig::MAX_MULTIPART_TE_LENGTH;
    }

    auto contentType = request.getHeaderLower("content-type");
    if (not contentType.starts_with("multipart/form-data"))
      throw ServerException("Request is not multipart/form-data", 415);

    auto boundaryIt = contentType.find("boundary=");
    auto boundaryEndIt = contentType.find(';', boundaryIt + 9);
    auto boundarySize = boundaryEndIt - (boundaryIt + 9);

    boundary_ = "\r\n--" + std::string(contentType.substr(boundaryIt + 9, boundarySize));

    if (boundary_.empty())
      throw ServerException("Boundary for multipart not found", 400);
    else if (boundary_.size() > ServerConfig::MAX_MULTIPART_BOUNDARY_LENGTH)
      throw ServerException("Boundary for multipart is too long", 400);
  }

  /**
   * @brief Convenience method to store the value of a field
   * @param name The name of the field
   * @param value The variable to store the value in
   */
  void storeFieldValue(const std::string &name, std::string &value) {
    onField(name, [&value](std::string v) -> core::Task<void> {
      value = v;
      co_return;
    });
  }

  /**
   * @brief Convenience method to store the values of a field
   * @param name The name of the field
   * @param values The vector to store the values in
   */
  void storeFieldValues(const std::string &name, std::vector<std::string> &values) {
    onMultiField(name, [&values](std::vector<std::string> v) -> core::Task<void> {
      for (auto value : v)
        values.push_back(value);
      co_return;
    });
  }

  /**
   * @brief Register a handler for a MultiField type
   * @param name The name of the field
   * @param handler The handler function
   */
  void onMultiField(const std::string &name,
                    std::move_only_function<core::Task<void>(std::vector<std::string>)> handler) {
    MultiFieldHandlers_.emplace_back(name, std::move(handler));
  }

  /**
   * @brief Register a handler for a Field type
   * @param name The name of the field
   * @param handler The handler function
   */
  void onField(const std::string &name, std::move_only_function<core::Task<void>(std::string)> handler) {
    fieldHandlers_.emplace_back(name, std::move(handler));
  }

  /**
   * @brief Register a handler for a File type
   * @param name The name of the field
   * @param handler The handler function
   *
   * @note The handler's parameter is a chunk of the File, not the full thing.
   *
   */
  void onFile(const std::string &name, std::move_only_function<core::Task<void>(std::span<unsigned char>)> handler) {
    fileHandlers_.emplace_back(name, std::move(handler));
  }

  /**
   * @brief Parse the multipart request and run the registered handlers. Must be called to get data into the fields
   */
  core::Task<void> go() {
    size_t fullBoundarySize = boundary_.size() + 2;
    size_t firstBoundarySize = boundary_.size();
    std::span<unsigned char> span;

    while (span.size() < firstBoundarySize) {
      auto [newSpan, n] = co_await reader_.read();
      span = newSpan;
    }

    if (span[firstBoundarySize - 1] != '\n' or span[firstBoundarySize - 2] != '\r')
      throw ServerException("Malformed multipart request: no initial boundary found", 400);

    bool found =
        std::memcmp(span.data(), reinterpret_cast<unsigned char *>(boundary_.data() + 2), firstBoundarySize - 2) == 0;

    if (not found)
      throw ServerException("Malformed multipart request: no initial boundary found", 400);

    reader_.consumeBytes(firstBoundarySize);
    contentConsumed_ += firstBoundarySize;

    std::vector<std::pair<std::string, std::string>> headersVec;
    std::unordered_map<std::string, std::vector<std::string>> multiFieldValues;

    while (contentConsumed_ < contentLength_) {
      auto [span, n] = co_await reader_.read();

      auto headerEndIt = std::search(span.begin(), span.end(), crlf2.begin(), crlf2.end());
      while (headerEndIt == span.end()) {
        auto [newSpan, n] = co_await reader_.read();
        span = newSpan;
        if (n == 0)
          throw ServerException("Malformed multipart request: Part headers not found or too large", 400);
        headerEndIt = std::search(span.begin(), span.end(), crlf2.begin(), crlf2.end());
      }

      std::string headers = std::string(span.begin(), headerEndIt + 4);
      reader_.consumeBytes(headers.size());
      contentConsumed_ += headers.size();

      if (headers.empty())
        throw ServerException("Malformed multipart: No part headers", 400);

      std::string_view headerView = headers;

      while (not headerView.empty()) {
        auto crlfIt = headerView.find("\r\n");
        std::string_view header = headerView.substr(0, crlfIt);
        headerView.remove_prefix(crlfIt + 2);
        if (header.empty())
          continue;

        auto colonIt = header.find(':');
        if (colonIt == std::string_view::npos)
          throw ServerException("Malformed part header", 400);

        std::string name = toLowerCase(header.substr(0, colonIt));
        std::string_view value = header.substr(colonIt + 1);
        trim(name);
        trim(value);

        if (name.empty() || value.empty())
          throw ServerException("Malformed part header", 400);

        headersVec.emplace_back(name, value);
      }

      auto cdHeaderIt =
          std::find_if(headersVec.begin(), headersVec.end(),
                       [](const std::pair<std::string, std::string> &h) { return h.first == "content-disposition"; });

      if (cdHeaderIt == headersVec.end())
        throw ServerException("Malformed part header: No Content-Disposition", 400);

      std::string_view cdValue = cdHeaderIt->second;

      bool formData = false;
      std::string partName;
      bool isFile = false;

      while (not cdValue.empty()) {
        auto semicolonIt = cdValue.find(';');
        std::string_view part;
        if (semicolonIt == std::string_view::npos) {
          part = cdValue;
          cdValue.remove_prefix(cdValue.size());
        } else {
          part = cdValue.substr(0, semicolonIt);
          cdValue.remove_prefix(semicolonIt + 1);
        }

        trim(part);
        if (part == "form-data")
          formData = true;
        else if (auto equalsIt = part.find('='); equalsIt != std::string_view::npos) {
          std::string_view paramName = part.substr(0, equalsIt);
          std::string_view paramValue = part.substr(equalsIt + 1);
          trim(paramName);
          trim(paramValue);

          if (paramName == "name") {
            partName = paramValue.substr(1, paramValue.size() - 2);
          } else if (paramName == "filename") {
            isFile = true;
          }
        }
      }

      if (not formData)
        throw ServerException("Content-Disposition must be form-data, others are not implemented", 501);
      if (partName.empty())
        throw ServerException("Multipart: No part name provided", 400);

      size_t maxSize = isFile ? ServerConfig::MAX_MULTIPART_FILE_SIZE : ServerConfig::MAX_MULTIPART_FIELD_SIZE;

      PartReader partReader(std::move(headersVec), boundary_, reader_, maxSize);
      headersVec.clear();

      if (isFile) {
        auto handlerIt = std::find_if(fileHandlers_.begin(), fileHandlers_.end(),
                                      [&partName](const auto &p) { return p.first == partName; });
        if (handlerIt != fileHandlers_.end()) {
          auto [totalConsumed, end] = co_await partReader.readAndDo(std::move(handlerIt->second));
          contentConsumed_ += totalConsumed;
          if (end)
            break;
        } else {
          SPDLOG_WARN("Multipart: File '{}' not handled", partName);
          auto [totalConsumed, end] = co_await partReader.drainTillBoundary();
          if (end)
            break;
          contentConsumed_ += totalConsumed;
        }
      } else {
        std::string value;
        auto valueReader = [&value](std::span<unsigned char> span) -> core::Task<void> {
          value += std::string(span.begin(), span.end());
          co_return;
        };

        auto [totalConsumed, end] = co_await partReader.readAndDo(std::move(valueReader));
        contentConsumed_ += totalConsumed;

        auto handlerIt = std::find_if(fieldHandlers_.begin(), fieldHandlers_.end(),
                                      [&partName](const auto &p) { return p.first == partName; });
        auto multiHandlerIt = std::find_if(MultiFieldHandlers_.begin(), MultiFieldHandlers_.end(),
                                           [&partName](const auto &p) { return p.first == partName; });

        if (handlerIt == fieldHandlers_.end() && multiHandlerIt == MultiFieldHandlers_.end())
          SPDLOG_WARN("Multipart: Field '{}' not handled", partName);

        if (handlerIt != fieldHandlers_.end()) {
          co_await handlerIt->second(std::move(value));
        } else if (multiHandlerIt != MultiFieldHandlers_.end()) {
          multiFieldValues[partName].push_back(std::move(value));
        }

        if (end)
          break;
      }
    }

    if (contentConsumed_ > contentLength_)
      throw ServerException("Request size limit exceeded", 413);

    for (auto &[key, val] : multiFieldValues) {
      auto handlerIt = std::find_if(MultiFieldHandlers_.begin(), MultiFieldHandlers_.end(),
                                    [&key](const auto &p) { return p.first == key; });
      if (handlerIt != MultiFieldHandlers_.end())
        co_await handlerIt->second(val);
      else
        SPDLOG_WARN("Multipart: MultiField '{}' not handled", key);
    }
  }

private:
  size_t contentLength_;
  size_t contentConsumed_ = 0;
  BufferedReader reader_;
  std::string boundary_;

  std::vector<std::pair<std::string, std::move_only_function<core::Task<void>(std::string)>>> fieldHandlers_;
  std::vector<std::pair<std::string, std::move_only_function<core::Task<void>(std::vector<std::string>)>>>
      MultiFieldHandlers_;
  std::vector<std::pair<std::string, std::move_only_function<core::Task<void>(std::span<unsigned char>)>>>
      fileHandlers_;
};
} // namespace rukh::http
