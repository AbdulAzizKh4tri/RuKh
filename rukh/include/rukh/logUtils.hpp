/**
 * @file logutils.hpp
 * @brief Utility functions for logging.
 */
#pragma once

#include <spdlog/async.h>
#include <string>

#include <rukh/HttpRequest.hpp>
#include <rukh/HttpResponse.hpp>
#include <rukh/HttpStreamResponse.hpp>

/**
 * @brief Logging namespace
 */
namespace rukh::logging {

/**
 * @brief Configure the application's default logger.
 *
 * @param on Enable/disable logging globally.
 * @param file Log file path. Empty to disable file logging.
 * @param console Enable console logging.
 *
 * @return The configured async logger, or nullptr if logging is disabled.
 *
 * The returned logger can be customized further by the caller.
 * Its sinks() contain the active sinks:
 * - stdout_color_sink_mt (if console == true)
 * - basic_file_sink_mt (if file is non-empty)
 */
std::shared_ptr<spdlog::async_logger> configureLog(bool on = true, const std::string &file = "", bool console = true);

void logRequest(const HttpRequest &req, const HttpResponse &res);

void logRequest(const HttpRequest &req, const HttpStreamResponse &res);

} // namespace rukh::logging
