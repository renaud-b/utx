#pragma once

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>
#include <httplib.h>
#include <unistd.h>
#ifdef UTX_HAS_PNG
#include <png.h>
#endif

namespace utx::app::infrastructure::download {

inline bool public_picture_path(std::string_view path) {
    if (!path.starts_with("/ipfs/")) return false;
    path.remove_prefix(6);
    return path.size() >= 10 && path.size() <= 128 &&
        std::ranges::all_of(path, [](unsigned char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                   (c >= '0' && c <= '9');
        });
}

inline std::optional<std::string> png_terminal_preview(const std::string& bytes) {
#ifdef UTX_HAS_PNG
    png_image image{};
    image.version = PNG_IMAGE_VERSION;
    struct Cleanup { png_image& image; ~Cleanup() { png_image_free(&image); } } cleanup{image};
    if (!png_image_begin_read_from_memory(&image, bytes.data(), bytes.size()))
        return std::nullopt;
    if (image.width == 0 || image.height == 0 || image.width > 2048 || image.height > 2048)
        return std::nullopt;
    image.format = PNG_FORMAT_RGBA;
    std::vector<unsigned char> pixels(PNG_IMAGE_SIZE(image));
    if (!png_image_finish_read(&image, nullptr, pixels.data(), 0, nullptr))
        return std::nullopt;

    constexpr unsigned cols = 20, rows = 10;
    std::ostringstream out;
    auto color = [&](unsigned x, unsigned y, bool background) {
        const auto sx = x * image.width / cols;
        const auto sy = y * image.height / (rows * 2);
        const auto offset = (static_cast<std::size_t>(sy) * image.width + sx) * 4;
        const unsigned alpha = pixels[offset + 3];
        out << "\033[" << (background ? 48 : 38) << ";2;";
        for (unsigned i = 0; i < 3; ++i) {
            if (i) out << ';';
            out << (static_cast<unsigned>(pixels[offset + i]) * alpha / 255);
        }
        out << 'm';
    };
    for (unsigned y = 0; y < rows; ++y) {
        for (unsigned x = 0; x < cols; ++x) {
            color(x, y * 2, false);
            color(x, y * 2 + 1, true);
            out << "\xe2\x96\x80"; // Unicode upper half block
        }
        out << "\033[0m\n";
    }
    return out.str();
#else
    (void)bytes;
    return std::nullopt;
#endif
}

inline bool supports_avatar_preview() {
#ifdef UTX_HAS_PNG
    const char* color = std::getenv("COLORTERM");
    return ::isatty(STDOUT_FILENO) && !std::getenv("NO_COLOR") && color &&
        (std::string_view(color) == "truecolor" || std::string_view(color) == "24bit");
#else
    return false;
#endif
}

// Never follow an identity-controlled external URL or write a temporary image.
// Use only the selected node/gateway and its public IPFS route.
inline std::optional<std::string> fetch_avatar_preview(
    const std::string& gateway, const std::string& path) {
    if (!supports_avatar_preview() || !public_picture_path(path)) return std::nullopt;
    constexpr std::size_t max_bytes = 2 * 1024 * 1024;
    httplib::Client client(gateway);
    client.set_connection_timeout(3, 0);
    client.set_read_timeout(3, 0);
    std::string bytes;
    auto response = client.Get(path, [&](const char* data, std::size_t length) {
        if (length > max_bytes - bytes.size()) return false;
        bytes.append(data, length);
        return true;
    });
    if (!response || response->status != 200) return std::nullopt;
    return png_terminal_preview(bytes);
}

} // namespace utx::app::infrastructure::download
