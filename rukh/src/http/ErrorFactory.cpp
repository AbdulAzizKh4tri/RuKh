#include <rukh/http/ErrorFactory.hpp>

#include <functional>
#include <nlohmann/json.hpp>
#include <vector>

#include <rukh/http/HttpRequest.hpp>
#include <rukh/http/HttpResponse.hpp>
#include <rukh/http/httpUtils.hpp>

namespace rukh::http {
using json = nlohmann::json;

ErrorFactory::ErrorFactory() {
  auto fallbackFormatter = [](int statusCode, const std::string_view &message = "") {
    HttpResponse response(statusCode);

    json body = {{"errorCode", statusCode},
                 {"errorMessage", message == "" ? HttpResponse::statusText(statusCode) : message}};

    response.headers.setHeaderLower("content-type", "application/json");
    response.headers.setCacheControl("no-store");
    response.setBody(body.dump());
    return response;
  };
  fallbackFormatterPair_ = {"application/json", fallbackFormatter};
}

void ErrorFactory::setFallbackFormatter(std::string type, Formatter formatter) {
  fallbackFormatterPair_ = {type, formatter};
}

std::pair<std::string, Formatter> ErrorFactory::getFallbackFormatter() { return fallbackFormatterPair_; }

void ErrorFactory::setFormatter(std::string type, Formatter formatter) {
  registeredFormatters_.emplace_back(toLowerCase(type), formatter);
}

HttpResponse ErrorFactory::build(const HttpRequest &req, int statusCode, const std::string_view &message) const {
  const auto &acceptVector = req.getHeaders("Accept");
  if (acceptVector.empty())
    return fallbackFormatterPair_.second(statusCode, message);

  std::string acceptString;
  for (const auto &accept : acceptVector)
    acceptString += accept + ",";

  std::vector<std::pair<std::string, float>> typePrefs;
  std::vector<std::string> excluded;

  parseQValues(acceptString, typePrefs, excluded);

  if (typePrefs.empty() && excluded.empty())
    return fallbackFormatterPair_.second(statusCode, message);

  if (typePrefs.empty()) {
    auto formatter =
        std::find_if(registeredFormatters_.begin(), registeredFormatters_.end(), [&excluded](const auto &p) {
          return std::none_of(excluded.begin(), excluded.end(),
                              [&p](const auto &ex) { return mime_match(ex, p.first); });
        });
    if (formatter != registeredFormatters_.end())
      return formatter->second(statusCode, message);
    return fallbackFormatterPair_.second(406, "");
  }

  std::sort(typePrefs.begin(), typePrefs.end(), [](const auto &a, const auto &b) { return a.second > b.second; });

  for (const auto &type : typePrefs) {
    auto formatter = std::find_if(registeredFormatters_.begin(), registeredFormatters_.end(),
                                  [&type](const auto &p) { return mime_match(p.first, type.first); });
    if (formatter != registeredFormatters_.end())
      return formatter->second(statusCode, message);
  }

  return fallbackFormatterPair_.second(406, "");
}
} // namespace rukh::http
