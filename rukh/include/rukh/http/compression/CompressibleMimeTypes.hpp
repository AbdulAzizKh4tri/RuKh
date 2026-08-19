#pragma once

#include <string>
#include <unordered_set>

namespace rukh::http::compression  {

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
}
