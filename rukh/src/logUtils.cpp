#include <rukh/logUtils.hpp>

#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

namespace rukh {

void configureLog(bool on, std::string file) {
  if (!on) {
    spdlog::set_level(spdlog::level::off);
    return;
  }

  spdlog::set_level(spdlog::level::trace);

  if (file != "") {
    auto fileLogger = spdlog::basic_logger_mt("server", file, true);
    //[thread %t]
    fileLogger->set_pattern("[%m-%d %H:%M] [%^%l%$] %v");
    spdlog::set_default_logger(fileLogger);
    return;
  }

  //[thread %t]
  spdlog::set_pattern("[%m-%d %H:%M] [%^%l%$] %v");
}

std::string_view statusColor(int statusCode) {
  if (statusCode < 300)
    return Color::Green;
  if (statusCode < 500)
    return Color::Yellow;
  return Color::Red;
}

void logRequest(const HttpRequest &req, const HttpResponse &res) {
  int status = res.getStatusCode();
  if (status >= 500) {
    SPDLOG_ERROR("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status, Color::Reset, req.getMethod(),
                 req.getPath(), req.getIp(), req.getPort());
  } else if (status >= 400) {
    SPDLOG_WARN("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status, Color::Reset, req.getMethod(),
                req.getPath(), req.getIp(), req.getPort());
  } else {
    SPDLOG_INFO("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status, Color::Reset, req.getMethod(),
                req.getPath(), req.getIp(), req.getPort());
  }
}

void logRequest(const HttpRequest &req, const HttpStreamResponse &res) {
  int status = res.getStatusCode();
  if (status >= 500) {
    SPDLOG_ERROR("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status, Color::Reset, req.getMethod(),
                 req.getPath(), req.getIp(), req.getPort());
  } else if (status >= 400) {
    SPDLOG_WARN("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status, Color::Reset, req.getMethod(),
                req.getPath(), req.getIp(), req.getPort());
  } else {
    SPDLOG_INFO("{}{}{}  {:<8} {:<20}  {:<16}:{:<6}", statusColor(status), status, Color::Reset, req.getMethod(),
                req.getPath(), req.getIp(), req.getPort());
  }
}
} // namespace rukh
