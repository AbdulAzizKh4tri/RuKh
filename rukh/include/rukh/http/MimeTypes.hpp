/**
 * @file MimeTypes.hpp
 * @brief File extensions and their Http mime types
 */

#pragma once

namespace rukh::http {

static const std::unordered_map<std::string, std::string> MIME_TYPES = {
    // Web / HTML / scripts / styles
    {".html", "text/html"},
    {".htm", "text/html"},
    {".xhtml", "application/xhtml+xml"},
    {".css", "text/css"},
    {".js", "application/javascript"},
    {".mjs", "application/javascript"},
    {".json", "application/json"},
    {".jsonld", "application/ld+json"},
    {".xml", "application/xml"},
    {".xsl", "application/xml"},
    {".xslt", "application/xslt+xml"},
    {".rss", "application/rss+xml"},
    {".atom", "application/atom+xml"},

    // Images
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".jpe", "image/jpeg"},
    {".png", "image/png"},
    {".gif", "image/gif"},
    {".bmp", "image/bmp"},
    {".webp", "image/webp"},
    {".svg", "image/svg+xml"},
    {".svgz", "image/svg+xml"},
    {".ico", "image/x-icon"},
    {".cur", "image/x-icon"},
    {".tif", "image/tiff"},
    {".tiff", "image/tiff"},
    {".avif", "image/avif"},
    {".heic", "image/heic"},
    {".heif", "image/heif"},
    {".apng", "image/apng"},

    // Fonts
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".ttf", "font/ttf"},
    {".otf", "font/otf"},
    {".eot", "application/vnd.ms-fontobject"},

    // Documents / Office
    {".txt", "text/plain"},
    {".csv", "text/csv"},
    {".pdf", "application/pdf"},
    {".rtf", "application/rtf"},
    {".doc", "application/msword"},
    {".dot", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".dotx", "application/vnd.openxmlformats-officedocument.wordprocessingml.template"},
    {".xls", "application/vnd.ms-excel"},
    {".xlt", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xltx", "application/vnd.openxmlformats-officedocument.spreadsheetml.template"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pot", "application/vnd.ms-powerpoint"},
    {".pps", "application/vnd.ms-powerpoint"},
    {".pptx", "application/"
              "vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".potx", "application/vnd.openxmlformats-officedocument.presentationml.template"},
    {".ppsx", "application/vnd.openxmlformats-officedocument.presentationml.slideshow"},
    {".pages", "application/vnd.apple.pages"},

    // Archives / compressed
    {".zip", "application/zip"},
    {".gz", "application/gzip"},
    {".tgz", "application/gzip"},
    {".tar", "application/x-tar"},
    {".rar", "application/vnd.rar"},
    {".7z", "application/x-7z-compressed"},
    {".bz2", "application/x-bzip2"},
    {".xz", "application/x-xz"},

    // Audio
    {".mp3", "audio/mpeg"},
    {".mpega", "audio/mpeg"},
    {".mpga", "audio/mpeg"},
    {".wav", "audio/wav"},
    {".wave", "audio/wav"},
    {".ogg", "audio/ogg"},
    {".oga", "audio/ogg"},
    {".opus", "audio/opus"},
    {".flac", "audio/flac"},
    {".aac", "audio/aac"},
    {".m4a", "audio/mp4"},
    {".m4b", "audio/mp4"},
    {".m4r", "audio/mp4"},
    {".amr", "audio/amr"},
    {".weba", "audio/webm"},
    {".mid", "audio/midi"},
    {".midi", "audio/midi"},

    // Video
    {".mp4", "video/mp4"},
    {".m4v", "video/mp4"},
    {".webm", "video/webm"},
    {".ogv", "video/ogg"},
    {".mov", "video/quicktime"},
    {".qt", "video/quicktime"},
    {".avi", "video/x-msvideo"},
    {".wmv", "video/x-ms-wmv"},
    {".flv", "video/x-flv"},
    {".mkv", "video/x-matroska"},
    {".mk3d", "video/x-matroska"},
    {".mks", "video/x-matroska"},
    {".mpg", "video/mpeg"},
    {".mpeg", "video/mpeg"},
    {".mpe", "video/mpeg"},
    {".ts", "video/mp2t"},
    {".3gp", "video/3gpp"},
    {".3g2", "video/3gpp2"},

    // WebAssembly & others
    {".wasm", "application/wasm"},

    // Application / binary fallback
    {".bin", "application/octet-stream"},
    {".exe", "application/octet-stream"},
    {".dll", "application/octet-stream"},
    {".dat", "application/octet-stream"},
    {".iso", "application/octet-stream"},

    // Others that appear frequently
    {".yaml", "text/yaml"},
    {".yml", "text/yaml"},
    {".toml", "application/toml"},
    {".markdown", "text/markdown"},
    {".md", "text/markdown"},
    {".epub", "application/epub+zip"},
    {".azw3", "application/vnd.amazon.ebook"},
    {".kml", "application/vnd.google-earth.kml+xml"},
    {".kmz", "application/vnd.google-earth.kmz"},
    {".gpx", "application/gpx+xml"},
    {".geojson", "application/geo+json"},
};
}
