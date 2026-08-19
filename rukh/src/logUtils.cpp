#include <rukh/logUtils.hpp>

#include <spdlog/async.h>
#include <spdlog/common.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

namespace rukh::logging {

/// ANSI escape sequences for logging colors
struct LogColors {
  static constexpr std::string_view trace = "\033[38;2;120;120;140m";
  static constexpr std::string_view debug = "\033[38;2;88;166;255m";
  static constexpr std::string_view info = "\033[38;2;80;200;120m";
  static constexpr std::string_view warn = "\033[38;2;255;196;80m";
  static constexpr std::string_view error = "\033[38;2;245;90;90m";
  static constexpr std::string_view critical = "\033[38;2;255;45;55m";
};

std::shared_ptr<spdlog::async_logger> configureLog(bool on, const std::string &file, bool console) {

  if (not on) {
    spdlog::set_level(spdlog::level::off);
    return {};
  }

  static constexpr std::string_view filePattern = "[%m-%d %H:%M] [T%t] [%l] %v";
  static constexpr std::string_view consolePattern = "\033[38;2;120;120;140m"
                                                     "[%m-%d %H:%M] [T%t] %^[%l] %v%$";

  spdlog::init_thread_pool(8192, 1); // queue size, 1 backend thread

  std::vector<spdlog::sink_ptr> sinks;

  if (console) {
    auto consoleSink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    consoleSink->set_pattern(std::string(consolePattern));

    // trace grey
    consoleSink->set_color(spdlog::level::trace, LogColors::trace);
    // debug blue
    consoleSink->set_color(spdlog::level::debug, LogColors::debug);
    // info green
    consoleSink->set_color(spdlog::level::info, LogColors::info);
    // warn yellow
    consoleSink->set_color(spdlog::level::warn, LogColors::warn);
    // error red
    consoleSink->set_color(spdlog::level::err, LogColors::error);
    // CRITICAL stronger red
    consoleSink->set_color(spdlog::level::critical, LogColors::critical);

    sinks.push_back(consoleSink);
  }

  if (not file.empty()) {
    auto fileSink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file, true);
    fileSink->set_pattern(std::string(filePattern));
    sinks.push_back(fileSink);
  }

  if (sinks.empty())
    throw std::runtime_error("configureLog: logging on but no sink (console=false and file empty)");

  auto logger = std::make_shared<spdlog::async_logger>("server", sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                       spdlog::async_overflow_policy::block);

  logger->set_level(spdlog::level::trace);
  logger->flush_on(spdlog::level::warn);
  spdlog::flush_every(std::chrono::seconds(3));
  spdlog::set_default_logger(logger);

  return logger;
}

} // namespace rukh::logging
