/**
 * @file HttpResponse.hpp
 * @brief Rukh's HTTP Response object
 */
#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include <rukh/CookieStore.hpp>
#include <rukh/HeaderStore.hpp>

namespace rukh {

/**
 * @brief Rukh's HTTP Response object
 *
 * @see HttpStreamResponse
 * @see HeaderStore
 * @see CookieStore
 */
class HttpResponse {
public:
  HeaderStore headers;
  CookieStore cookies;

  HttpResponse();
  HttpResponse(int statusCode);
  HttpResponse(int statusCode, const std::string &body);
  HttpResponse(int statusCode, const std::string &contentType, const std::string &body);

  void setStatusCode(int statusCode);
  void setBody(const std::string &body);
  void setContentType(const std::string &contentType);

  int getStatusCode() const;
  std::string &getBody();
  size_t getBodySize() const;
  std::string getContentType() const;

  std::string getVersion() const;

  /// @cond INTERNAL

  void setVersion(const std::string &version);
  void stripBody();
  static constexpr std::array noBody = {100, 101, 102, 103, 204, 304};
  static std::string statusText(int statusCode) { return getOrDefault(statusStrings_, statusCode, "Unknown"); };

  bool serializeInto(std::vector<unsigned char> &buf) const;

  static std::string_view getStatusLine(int code) noexcept {
    switch (code) {
    case 200:
      return "HTTP/1.1 200 OK\r\n";
    case 201:
      return "HTTP/1.1 201 Created\r\n";
    case 204:
      return "HTTP/1.1 204 No Content\r\n";
    case 206:
      return "HTTP/1.1 206 Partial Content\r\n";
    case 301:
      return "HTTP/1.1 301 Moved Permanently\r\n";
    case 302:
      return "HTTP/1.1 302 Found\r\n";
    case 304:
      return "HTTP/1.1 304 Not Modified\r\n";
    case 400:
      return "HTTP/1.1 400 Bad Request\r\n";
    case 401:
      return "HTTP/1.1 401 Unauthorized\r\n";
    case 403:
      return "HTTP/1.1 403 Forbidden\r\n";
    case 404:
      return "HTTP/1.1 404 Not Found\r\n";
    case 405:
      return "HTTP/1.1 405 Method Not Allowed\r\n";
    case 408:
      return "HTTP/1.1 408 Request Timeout\r\n";
    case 411:
      return "HTTP/1.1 411 Length Required";
    case 413:
      return "HTTP/1.1 413 Content Too Large\r\n";
    case 415:
      return "HTTP/1.1 415 Unsupported Media Type";
    case 416:
      return "HTTP/1.1 416 Range Not Satisfiable\r\n";
    case 417:
      return "HTTP/1.1 417 Expectation Failed\r\n";
    case 429:
      return "HTTP/1.1 429 Too Many Requests\r\n";
    case 431:
      return "HTTP/1.1 431 Request Header Fields Too Large\r\n";
    case 500:
      return "HTTP/1.1 500 Internal Server Error\r\n";
    case 501:
      return "HTTP/1.1 501 Not Implemented\r\n";
    case 503:
      return "HTTP/1.1 503 Service Unavailable\r\n";
    // fallback for anything not in the fast path:
    default: {
      static thread_local std::string fallback;
      fallback = "HTTP/1.1 " + std::to_string(code) + " " + std::string(HttpResponse::statusText(code)) + "\r\n";
      return fallback;
    }
    }
  }

  /// @endcond

private:
  std::string body_, version_ = "HTTP/1.1";
  int statusCode_;

  inline static const std::unordered_map<int, std::string> statusStrings_ = {
      // 1xx Informational
      {100, "Continue"},
      {101, "Switching Protocols"},
      {102, "Processing"},
      {103, "Early Hints"},

      // 2xx Success
      {200, "OK"},
      {201, "Created"},
      {202, "Accepted"},
      {203, "Non-Authoritative Information"},
      {204, "No Content"},
      {205, "Reset Content"},
      {206, "Partial Content"},
      {207, "Multi-Status"},
      {208, "Already Reported"},
      {226, "IM Used"},

      // 3xx Redirection
      {300, "Multiple Choices"},
      {301, "Moved Permanently"},
      {302, "Found"},
      {303, "See Other"},
      {304, "Not Modified"},
      {305, "Use Proxy"},
      {307, "Temporary Redirect"},
      {308, "Permanent Redirect"},

      // 4xx Client Error
      {400, "Bad Request"},
      {401, "Unauthorized"},
      {402, "Payment Required"},
      {403, "Forbidden"},
      {404, "Not Found"},
      {405, "Method Not Allowed"},
      {406, "Not Acceptable"},
      {407, "Proxy Authentication Required"},
      {408, "Request Timeout"},
      {409, "Conflict"},
      {410, "Gone"},
      {411, "Length Required"},
      {412, "Precondition Failed"},
      {413, "Content Too Large"},
      {414, "URI Too Long"},
      {415, "Unsupported Media Type"},
      {416, "Range Not Satisfiable"},
      {417, "Expectation Failed"},
      {418, "I'm a teapot"}, // :D
      {421, "Misdirected Request"},
      {422, "Unprocessable Content"},
      {423, "Locked"},
      {424, "Failed Dependency"},
      {425, "Too Early"},
      {426, "Upgrade Required"},
      {428, "Precondition Required"},
      {429, "Too Many Requests"},
      {431, "Request Header Fields Too Large"},
      {451, "Unavailable For Legal Reasons"},

      // 5xx Server Error
      {500, "Internal Server Error"},
      {501, "Not Implemented"},
      {502, "Bad Gateway"},
      {503, "Service Unavailable"},
      {504, "Gateway Timeout"},
      {505, "HTTP Version Not Supported"},
      {506, "Variant Also Negotiates"},
      {507, "Insufficient Storage"},
      {508, "Loop Detected"},
      {510, "Not Extended"},
      {511, "Network Authentication Required"}};
};
} // namespace rukh
