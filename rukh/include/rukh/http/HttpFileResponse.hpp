/**
 * @file HttpFileResponse.hpp
 * @brief Rukh's HTTP File Response
 * \todo cleanup and docs
 */
#pragma once

#include <cstring>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

#include <rukh/core/FileIoHelpers.hpp>
#include <rukh/http/CookieStore.hpp>
#include <rukh/http/HeaderStore.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http {

/**
 * @brief Rukh's HTTP File Response
 *
 * @see HttpResponse
 * @see HeaderStore
 * @see CookieStore
 */
class HttpFileResponse {
public:
  using Range = std::pair<std::optional<size_t>, std::optional<size_t>>;

  HeaderStore headers;
  CookieStore cookies;

  HttpFileResponse();
  HttpFileResponse(int statusCode);
  HttpFileResponse(int statusCode, std::filesystem::path filePath);
  HttpFileResponse(int statusCode, const std::string &contentType, std::filesystem::path filePath);

  HttpFileResponse(HttpFileResponse &&other);
  HttpFileResponse &operator=(HttpFileResponse &&other);

  HttpFileResponse(HttpFileResponse const &) = delete;
  HttpFileResponse &operator=(HttpFileResponse const &) = delete;

  ~HttpFileResponse();

  void setOffset(size_t offset) { offset_ = offset; }
  size_t getOffset() const { return offset_; }

  void setContentLength(size_t contentLength) {
    contentLength_ = contentLength;
    headers.setHeaderLower("content-length", std::to_string(contentLength));
  }
  std::optional<size_t> getContentLength() const { return contentLength_; }

  void setFd(int fd, bool owns = false) {
    fd_ = fd;
    ownsFd_ = owns;
  }

  int getFd() const { return fd_; }
  bool ownsFd() const { return ownsFd_; }

  std::filesystem::path getFilePath() const { return filePath_; }
  void setFilePath(const std::filesystem::path &filePath) { filePath_ = filePath; }

  std::string getContentType() const;
  void setContentType(const std::string &contentType);

  std::string getVersion() const;
  int getStatusCode() const;
  void setStatusCode(int statusCode);

  /// @internalGroup @{

  /// @internalMethod
  bool serializeHeaderInto(std::vector<unsigned char> &buf) const;

  std::optional<core::FileOpenError> openFile() {

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

  /// @}

private:
  int statusCode_ = -1;
  std::string version_ = "HTTP/1.1";
  std::filesystem::path filePath_;
  int fd_ = -1;
  bool ownsFd_ = true;
  size_t offset_ = 0;
  std::optional<size_t> contentLength_;
};

} // namespace rukh::http
