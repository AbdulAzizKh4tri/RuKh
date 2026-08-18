#include <rukh/HttpStreamResponse.hpp>

#include <spdlog/spdlog.h>

#include <rukh/HttpResponse.hpp>
#include <rukh/ServerConfig.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/core/utils.hpp>

namespace rukh {

HttpStreamResponse::HttpStreamResponse() : statusCode_(-1) {}

HttpStreamResponse::HttpStreamResponse(int statusCode) : statusCode_(statusCode) {
  headers.setHeaderLower("transfer-encoding", "chunked");
}

HttpStreamResponse::HttpStreamResponse(int statusCode, NextChunkFn nextChunkFn)
    : statusCode_(statusCode), nextChunkFn_(std::move(nextChunkFn)) {
  headers.setHeaderLower("transfer-encoding", "chunked");
}

HttpStreamResponse::HttpStreamResponse(int statusCode, const std::string &contentType, NextChunkFn nextChunkFn)
    : statusCode_(statusCode), nextChunkFn_(std::move(nextChunkFn)) {
  headers.setHeaderLower("transfer-encoding", "chunked");
  headers.setHeaderLower("content-type", contentType);
}

core::Task<std::optional<std::string>> HttpStreamResponse::getNextChunk() { co_return co_await nextChunkFn_(); }

bool HttpStreamResponse::serializeHeaderInto(std::vector<unsigned char> &buf) const {
  const std::string_view statusLine = HttpResponse::getStatusLine(statusCode_);

  size_t size = statusLine.size();

  for (const auto &[k, v] : headers.getAllHeaders())
    size += k.size() + 2 + v.size() + 2;

  size += ServerConfig::getServerLine().size();

  const auto &date = getCurrentHttpDate();
  size += (sizeof("date") - 1) + date.size() + 4;

  size += cookies.getSerializedSize();

  size += 2; // final \r\n

  size_t oldSize = buf.size();

  if (oldSize + size > ServerConfig::MAX_WRITE_BUFFER_BYTES) {
    SPDLOG_WARN("Write buffer limit would be exceeded, Closing Connection");
    return false;
  }

  buf.resize(oldSize + size);
  unsigned char *out = buf.data() + oldSize;

  auto write = [&out](std::string_view s) {
    std::memcpy(out, s.data(), s.size());
    out += s.size();
  };

  write(statusLine);

  for (const auto &[k, v] : headers.getAllHeaders()) {
    write(k);
    write(": ");
    write(v);
    write("\r\n");
  }

  write(ServerConfig::getServerLine());

  write("date: ");
  write(date);
  write("\r\n");

  cookies.serializeUsing(write);
  write("\r\n");

  assert(out == buf.data() + oldSize + size);
  return true;
}

bool HttpStreamResponse::serializeBlockInto(std::string_view chunk, std::vector<unsigned char> &buf,
                                            const std::string &mime) {
  if (isChunked_)
    return serializeChunkInto(chunk, buf);

  size_t oldSize = buf.size();

  if (oldSize + chunk.size() > ServerConfig::MAX_WRITE_BUFFER_BYTES) {
    SPDLOG_WARN("Write buffer limit would be exceeded, Closing Connection");
    return false;
  }
  buf.resize(oldSize + chunk.size());
  std::memcpy(buf.data() + oldSize, chunk.data(), chunk.size());
  return true;
}

std::string HttpStreamResponse::getContentType() const {
  std::string header = headers.getHeaderLower("content-type");
  auto it = std::find(header.begin(), header.end(), ';');
  if (it != header.end())
    return std::string(header.begin(), it);
  else
    return header;
}

NextChunkFn HttpStreamResponse::takeNextChunkFn() { return std::move(nextChunkFn_); }
void HttpStreamResponse::setNextChunkFn(NextChunkFn nextChunkFn) { nextChunkFn_ = std::move(nextChunkFn); }

std::string HttpStreamResponse::getVersion() const { return version_; }
int HttpStreamResponse::getStatusCode() const { return statusCode_; }
void HttpStreamResponse::setStatusCode(int statusCode) { statusCode_ = statusCode; }

bool HttpStreamResponse::serializeChunkInto(std::string_view chunk, std::vector<unsigned char> &buf) {
  char hexBuf[16];
  auto [ptr, ec] = std::to_chars(hexBuf, hexBuf + sizeof(hexBuf), chunk.size(), 16);
  size_t hexLen = ptr - hexBuf;

  size_t oldSize = buf.size();

  if (oldSize + chunk.size() + hexLen + 4 > ServerConfig::MAX_WRITE_BUFFER_BYTES) {
    SPDLOG_WARN("Write buffer limit would be exceeded, Closing Connection");
    return false;
  }
  buf.resize(oldSize + chunk.size() + hexLen + 4);
  size_t offset = oldSize + hexLen;

  std::memcpy(buf.data() + oldSize, hexBuf, hexLen);
  buf[offset++] = '\r';
  buf[offset++] = '\n';
  std::memcpy(buf.data() + offset, chunk.data(), chunk.size());
  offset += chunk.size();
  buf[offset++] = '\r';
  buf[offset++] = '\n';
  return true;
}
} // namespace rukh
