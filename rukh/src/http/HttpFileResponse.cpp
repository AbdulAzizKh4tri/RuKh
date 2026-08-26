#include <rukh/http/HttpFileResponse.hpp>

#include <spdlog/spdlog.h>

#include <rukh/ServerConfig.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http {

HttpFileResponse::HttpFileResponse() : statusCode_(-1) {}

HttpFileResponse::HttpFileResponse(int statusCode) : statusCode_(statusCode) {}

HttpFileResponse::HttpFileResponse(int statusCode, std::filesystem::path filePath)
    : statusCode_(statusCode), filePath_(filePath) {}

HttpFileResponse::HttpFileResponse(int statusCode, const std::string &contentType, std::filesystem::path filePath)
    : statusCode_(statusCode), filePath_(filePath) {
  headers.setHeaderLower("content-type", contentType);
}

HttpFileResponse::HttpFileResponse(HttpFileResponse &&other) {
  statusCode_ = other.statusCode_;
  version_ = other.version_;
  filePath_ = other.filePath_;
  fd_ = other.fd_;
  offset_ = other.offset_;
  contentLength_ = other.contentLength_;
  headers = std::move(other.headers);
  cookies = std::move(other.cookies);
  ownsFd_ = other.ownsFd_;

  other.fd_ = -1;
}

HttpFileResponse &HttpFileResponse::operator=(HttpFileResponse &&other) {
  statusCode_ = other.statusCode_;
  version_ = other.version_;
  filePath_ = other.filePath_;
  fd_ = other.fd_;
  offset_ = other.offset_;
  contentLength_ = other.contentLength_;
  ownsFd_ = other.ownsFd_;

  headers = std::move(other.headers);
  cookies = std::move(other.cookies);

  other.fd_ = -1;
  return *this;
}

HttpFileResponse::~HttpFileResponse() {
  if (ownsFd_ and fd_ != -1)
    ::close(fd_);
}

bool HttpFileResponse::serializeHeaderInto(std::vector<unsigned char> &buf) const {
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

std::string HttpFileResponse::getContentType() const {
  std::string header = headers.getHeaderLower("content-type");
  auto it = std::find(header.begin(), header.end(), ';');
  if (it != header.end())
    return std::string(header.begin(), it);
  else
    return header;
}

void HttpFileResponse::setContentType(const std::string &contentType) {
  headers.setHeaderLower("content-type", contentType);
}

std::string HttpFileResponse::getVersion() const { return version_; }
int HttpFileResponse::getStatusCode() const { return statusCode_; }
void HttpFileResponse::setStatusCode(int statusCode) { statusCode_ = statusCode; }

} // namespace rukh::http
