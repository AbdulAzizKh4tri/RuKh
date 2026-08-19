/**
 * @file logutils.hpp
 * @brief Logging utilities and configuration
 */

#pragma once

#include <spdlog/async.h>
#include <string>

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
 * @returns The configured async logger, or nullptr if logging is disabled.
 *
 * The returned logger can be customized further by the caller.
 * Its sinks() contain the active sinks:
 * - stdout_color_sink_mt (if console == true)
 * - basic_file_sink_mt (if file is non-empty)
 */
std::shared_ptr<spdlog::async_logger> configureLog(bool on = true, const std::string &file = "", bool console = true);

} // namespace rukh::logging
