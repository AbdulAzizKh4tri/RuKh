#pragma once

#include <nlohmann/json.hpp>
#include <rukh/http/ErrorFactory.hpp>

inline rukh::http::ErrorFactory &getErrorFactory() {
  using namespace rukh;
  using namespace rukh::http;
  using json = nlohmann::json;
  static ErrorFactory errorFactory;
  static bool called = false;

  if (called)
    return errorFactory;

  called = true;

  auto jsonFormatter = [](int statusCode, const std::string_view &message = "") {
    HttpResponse response(statusCode);
    json body = {{"errorCode", statusCode},
                 {"errorMessage", message == "" ? HttpResponse::statusText(statusCode) : message}};

    response.headers.setHeaderLower("content-type", "application/json");
    response.headers.setCacheControl("no-store");
    response.setBody(body.dump());
    return response;
  };

  auto htmlFormatter = [](int statusCode, const std::string_view &message = "") {
    HttpResponse response(statusCode);
    std::string statusText = HttpResponse::statusText(statusCode);
    std::string msg = message.empty() ? statusText : std::string(message);
    std::string body = "<!DOCTYPE html>"
                       "<html><head><title>" +
                       std::to_string(statusCode) + " " + statusText +
                       "</title></head>"
                       "<body>"
                       "<h1>" +
                       std::to_string(statusCode) + " " + statusText +
                       "</h1>"
                       "<p>" +
                       msg +
                       "</p>"
                       "</body></html>";
    response.headers.setHeaderLower("content-type", "text/html");
    response.headers.setCacheControl("no-store");
    response.setBody(body);
    return response;
  };

  errorFactory.setFormatter("application/json", jsonFormatter);
  errorFactory.setFormatter("text/html", htmlFormatter);

  return errorFactory;
}
