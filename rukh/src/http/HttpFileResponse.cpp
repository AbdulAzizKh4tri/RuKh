#include <rukh/http/HttpFileResponse.hpp>

#include <spdlog/spdlog.h>

#include <rukh/ServerConfig.hpp>
#include <rukh/core/Task.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http {

HttpFileResponse::HttpFileResponse() : statusCode_(-1) {}

HttpFileResponse::HttpFileResponse(int statusCode, std::filesystem::path filePath)
    : statusCode_(statusCode), filePath_(filePath) {}

HttpFileResponse::HttpFileResponse(int statusCode, const std::string &contentType, std::filesystem::path filePath)
    : statusCode_(statusCode), filePath_(filePath) {
  headers.setHeaderLower("content-type", contentType);
}

HttpFileResponse::~HttpFileResponse() {
  if (ownsFd_ and fd_ != -1)
    ::close(fd_);
}

void HttpFileResponse::setOffset(size_t offset) { offset_ = offset; }
size_t HttpFileResponse::getOffset() const { return offset_; }

void HttpFileResponse::setContentLength(size_t contentLength) {
  HttpFileResponse::contentLength_ = contentLength;
  HttpFileResponse::headers.setHeaderLower("content-length", std::to_string(contentLength));
}
std::optional<size_t> HttpFileResponse::getContentLength() const { return contentLength_; }

void HttpFileResponse::setFd(int fd, bool owns) {
  HttpFileResponse::fd_ = fd;
  HttpFileResponse::ownsFd_ = owns;
}

int HttpFileResponse::getFd() const { return fd_; }
bool HttpFileResponse::ownsFd() const { return ownsFd_; }

std::filesystem::path HttpFileResponse::getFilePath() const { return filePath_; }
void HttpFileResponse::setFilePath(const std::filesystem::path &filePath) { filePath_ = filePath; }

std::optional<core::FileOpenError> HttpFileResponse::openFile() {

  fd_ = ::open(filePath_.c_str(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
  if (fd_ >= 0)
    return std::nullopt;

  core::FileOpenError error;
  SPDLOG_ERROR("Failed to open file: {}", strerror(errno));
  switch (errno) {
  case ENOENT:
  case ENOTDIR:
    error = core::FileOpenError::NotFound;
    break;
  case EACCES:
  case EPERM:
    error = core::FileOpenError::Forbidden;
    break;
  case ENAMETOOLONG:
  case ELOOP:
    error = core::FileOpenError::Malformed;
    break;
  case EMFILE:
  case ENFILE:
  case ENOMEM:
    error = core::FileOpenError::ResourceExhausted;
    break;
  default:
    error = core::FileOpenError::Unexpected;
    break;
  }
  return error;
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

bool HttpFileResponse::isCached() const { return cachedFileContent_ != nullptr; }

std::shared_ptr<std::vector<unsigned char>> &HttpFileResponse::getCachedFileContent() { return cachedFileContent_; }

void HttpFileResponse::setCachedFileContent(const std::shared_ptr<std::vector<unsigned char>> content) {
  cachedFileContent_ = content;
}

} // namespace rukh::http
