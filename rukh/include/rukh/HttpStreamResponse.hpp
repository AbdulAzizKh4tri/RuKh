#pragma once

#include <optional>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include <rukh/CookieStore.hpp>
#include <rukh/HeaderStore.hpp>
#include <rukh/core/Task.hpp>

namespace rukh {

using NextChunkFn = std::move_only_function<core::Task<std::optional<std::string>>()>;

class HttpStreamResponse {
public:
  HeaderStore headers;
  CookieStore cookies;

  HttpStreamResponse();

  HttpStreamResponse(int statusCode);
  HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn);

  HttpStreamResponse(int statusCode, const std::string &contentType, NextChunkFn nextChunkFn);

  core::Task<std::optional<std::string>> getNextChunk();

  bool serializeHeaderInto(std::vector<unsigned char> &buf) const;

  bool serializeBlockInto(std::string_view chunk, std::vector<unsigned char> &buf, const std::string &mime = "");

  std::string getContentType() const;

  NextChunkFn takeNextChunkFn();
  void setNextChunkFn(NextChunkFn nextChunkFn);

  void setChunked(bool chunked) {
    isChunked_ = chunked;
    headers.removeHeader("transfer-encoding");
  }

  std::string getVersion() const;
  int getStatusCode() const;
  void setStatusCode(int statusCode);

private:
  int statusCode_;
  std::string version_ = "HTTP/1.1";
  bool isChunked_ = true;

  NextChunkFn nextChunkFn_;

  bool serializeChunkInto(std::string_view chunk, std::vector<unsigned char> &buf);
};
} // namespace rukh
