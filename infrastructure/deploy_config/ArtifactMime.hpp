#pragma once

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <string_view>

namespace utx::app::infrastructure::deploy {

// Deliberately filename-based: the source is an opaque compiled artifact.
// Keep values within the node project's MIME validation policy.
[[nodiscard]] inline std::string artifact_mime_for_path(
    const std::filesystem::path& path) {
    auto ext = path.extension().string();
    std::ranges::transform(ext, ext.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });

    if (ext == ".js" || ext == ".mjs" || ext == ".cjs") return "application/javascript";
    if (ext == ".css") return "text/css";
    if (ext == ".html" || ext == ".htm") return "text/html";
    if (ext == ".json" || ext == ".map") return "application/json";
    if (ext == ".svg") return "image/svg+xml";
    if (ext == ".png") return "image/png";
    if (ext == ".jpg" || ext == ".jpeg") return "image/jpeg";
    if (ext == ".gif") return "image/gif";
    if (ext == ".webp") return "image/webp";
    if (ext == ".avif") return "image/avif";
    if (ext == ".ico") return "image/x-icon";
    if (ext == ".woff2") return "font/woff2";
    if (ext == ".woff") return "font/woff";
    if (ext == ".ttf") return "font/ttf";
    if (ext == ".otf") return "font/otf";
    if (ext == ".eot") return "application/vnd.ms-fontobject";
    if (ext == ".wasm") return "application/wasm";
    if (ext == ".pdf") return "application/pdf";
    if (ext == ".txt") return "text/plain";
    if (ext == ".xml") return "application/xml";
    if (ext == ".zip") return "application/zip";
    if (ext == ".webm") return "video/webm";
    if (ext == ".mp4") return "video/mp4";
    return "application/octet-stream";
}

} // namespace utx::app::infrastructure::deploy
