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
  HttpFileResponse(int statusCode, std::filesystem::path filePath);
  HttpFileResponse(int statusCode, const std::string &contentType, std::filesystem::path filePath);

  HttpFileResponse(HttpFileResponse &&other) = default;
  HttpFileResponse &operator=(HttpFileResponse &&other) = default;

  HttpFileResponse(HttpFileResponse const &) = delete;
  HttpFileResponse &operator=(HttpFileResponse const &) = delete;

  ~HttpFileResponse();

  void setOffset(size_t offset);
  size_t getOffset() const;

  void setContentLength(size_t contentLength);
  std::optional<size_t> getContentLength() const;

  void setFd(int fd, bool owns = false);
  int getFd() const;
  bool ownsFd() const;

  std::filesystem::path getFilePath() const;
  void setFilePath(const std::filesystem::path &filePath);

  std::string getContentType() const;
  void setContentType(const std::string &contentType);

  std::string getVersion() const;
  int getStatusCode() const;
  void setStatusCode(int statusCode);

  bool isCached() const;
  std::shared_ptr<std::vector<unsigned char>> &getCachedFileContent();
  void setCachedFileContent(const std::shared_ptr<std::vector<unsigned char>> content);

  /// @internalGroup @{

  /// @internalMethod
  bool serializeHeaderInto(std::vector<unsigned char> &buf) const;

  /// @internalMethod
  std::optional<core::FileOpenError> openFile();

  /// @}

private:
  int statusCode_ = -1;
  std::string version_ = "HTTP/1.1";
  std::filesystem::path filePath_;
  int fd_ = -1;
  bool ownsFd_ = true;
  size_t offset_ = 0;
  size_t contentLength_; // The length of the content to be sent, not the file's size

  std::shared_ptr<std::vector<unsigned char>> cachedFileContent_;
};

} // namespace rukh::http
