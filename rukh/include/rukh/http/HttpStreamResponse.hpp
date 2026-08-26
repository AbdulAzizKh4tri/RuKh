/**
 * @file HttpStreamResponse.hpp
 * @brief Rukh's HTTP Stream Response
 */
#pragma once

#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include <rukh/core/Task.hpp>
#include <rukh/http/CookieStore.hpp>
#include <rukh/http/HeaderStore.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http {

/**
 * @brief The function signature to return the next chunk of data for the stream response.
 *
 * @returns nullopt to end the stream.
 *
 */
using NextChunkFn = std::move_only_function<core::Task<std::optional<std::string>>()>;

/**
 * @brief Rukh's HTTP Stream Response
 *
 * @note Defaults to Transfer encoding: chunked. See @ref setChunked.
 *
 * @see HttpResponse
 * @see HeaderStore
 * @see CookieStore
 */
class HttpStreamResponse {
public:
  HeaderStore headers;
  CookieStore cookies;

  HttpStreamResponse();
  HttpStreamResponse(int statusCode);
  HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn);
  HttpStreamResponse(int statusCode, const std::string &contentType, NextChunkFn nextChunkFn);

  /// set transfer-encoding: chunked true/false
  void setChunked(bool chunked) {
    isChunked_ = chunked;
    if (not chunked)
      headers.removeHeader("transfer-encoding");
  }

  std::string getContentType() const;
  void setContentType(const std::string &contentType);

  /// set next chunk function. See @ref NextChunkFn
  void setNextChunkFn(NextChunkFn nextChunkFn);

  /// Moves the next chunk function out of the response
  NextChunkFn takeNextChunkFn();

  std::string getVersion() const;
  int getStatusCode() const;
  void setStatusCode(int statusCode);

  /// @internalGroup @{

  /// @internalMethod
  core::Task<std::optional<std::string>> getNextChunk();

  /// @internalMethod
  bool serializeHeaderInto(std::vector<unsigned char> &buf) const;

  /// @internalMethod
  bool serializeBlockInto(std::string_view chunk, std::vector<unsigned char> &buf, const std::string &mime = "");
  /// @}

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";
  bool isChunked_ = true;

  NextChunkFn nextChunkFn_;

  bool serializeChunkInto(std::string_view chunk, std::vector<unsigned char> &buf);
};
} // namespace rukh::http
