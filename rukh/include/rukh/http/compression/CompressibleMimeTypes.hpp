/**
 * @file CompressibleMimeTypes.hpp
 * @brief List of compressible mime types
 */

#pragma once

#include <string>
#include <unordered_set>

namespace rukh::http::compression {

/**
 * @brief List of compressible mime types.
 * @attention This list is not exhaustive and may need to be extended in the future.
 */
static std::unordered_set<std::string> compressibleMimeTypes = {
    // Text
    "text/html",
    "text/css",
    "text/plain",
    "text/csv",
    "text/xml",
    "text/yaml",
    "text/markdown",
    // JavaScript
    "application/javascript",
    "application/x-javascript",
    // JSON / structured data
    "application/json",
    "application/ld+json",
    "application/manifest+json",
    // XML variants
    "application/xml",
    "application/xhtml+xml",
    "application/rss+xml",
    "application/atom+xml",
    // SVG (text-based)
    "image/svg+xml",
    // WebAssembly
    "application/wasm",
    // Fonts (woff/woff2 are already compressed, skip those)
    "font/ttf",
    "font/otf",
    "application/vnd.ms-fontobject",
};
} // namespace rukh::http::compression
