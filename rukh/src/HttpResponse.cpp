#include <rukh/HttpResponse.hpp>

#include <spdlog/spdlog.h>

#include <rukh/ServerConfig.hpp>

namespace rukh {

HttpResponse::HttpResponse() : statusCode_(-1) {}

HttpResponse::HttpResponse(int statusCode) : statusCode_(statusCode) {
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
}

HttpResponse::HttpResponse(int statusCode, const std::string &body) : statusCode_(statusCode), body_(body) {
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
}

HttpResponse::HttpResponse(int statusCode, const std::string &contentType, const std::string &body)
    : statusCode_(statusCode), body_(body) {
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
  headers.setHeaderLower("content-type", contentType);
}

bool HttpResponse::serializeInto(std::vector<unsigned char> &buf) const {
  bool hasBody = !std::ranges::contains(noBody, statusCode_);

  const std::string_view statusLine = getStatusLine(statusCode_);
  size_t size = statusLine.size();

  for (auto &[k, v] : headers.getAllHeaders()) {
    if (!hasBody && k == "content-length")
      continue;
    size += k.size() + 2 + v.size() + 2;
  }

  size += ServerConfig::getServerLine().size();

  const auto &date = getCurrentHttpDate();
  size += (sizeof("date") - 1) + date.size() + 4;

  size += cookies.getSerializedSize();
  size += 2; // final \r\n

  size += hasBody ? body_.size() : 0;

  if (buf.size() + size > ServerConfig::MAX_WRITE_BUFFER_BYTES) {
    SPDLOG_WARN("Write buffer limit would be exceeded, Closing Connection");
    return false;
  }

  size_t oldSize = buf.size();
  buf.resize(oldSize + size);
  unsigned char *out = buf.data() + oldSize;

  auto write = [&out](std::string_view s) {
    std::memcpy(out, s.data(), s.size());
    out += s.size();
  };

  write(statusLine);

  for (auto &[k, v] : headers.getAllHeaders()) {
    if (!hasBody && k == "content-length")
      continue;
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

  if (hasBody)
    write(body_);

  assert(out == buf.data() + oldSize + size);
  return true;
}

std::string HttpResponse::getContentType() const {
  std::string header = headers.getHeaderLower("content-type");
  auto it = std::find(header.begin(), header.end(), ';');
  if (it != header.end())
    return std::string(header.begin(), it);
  else
    return header;
}

void HttpResponse::setContentType(const std::string &contentType) {
  headers.setHeaderLower("content-type", contentType);
}

void HttpResponse::setBody(const std::string &body) {
  body_ = body;
  headers.setHeaderLower("content-length", std::to_string(body_.size()));
}

void HttpResponse::stripBody() { body_ = ""; }

void HttpResponse::setVersion(const std::string &version) { version_ = version; }
void HttpResponse::setStatusCode(int statusCode) { statusCode_ = statusCode; }

std::string &HttpResponse::getBody() { return body_; }
size_t HttpResponse::getBodySize() const { return body_.size(); }

std::string HttpResponse::getVersion() const { return version_; }
int HttpResponse::getStatusCode() const { return statusCode_; }
} // namespace rukh
