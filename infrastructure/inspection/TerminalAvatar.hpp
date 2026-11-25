#pragma once

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <expected>
#include <optional>
#include <string>
#include <string_view>

#include <httplib.h>

#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace utx::app::infrastructure::inspection {

// Wallet profile pictures are stored as /ipfs/<cid>. Accept the equivalent
// ipfs:// form and bare CIDs, but never follow arbitrary profile-supplied URLs.
inline std::optional<std::string> picture_cid(std::string_view picture) {
    if (picture.starts_with("/ipfs/")) {
        picture.remove_prefix(6);
    } else if (picture.starts_with("ipfs://")) {
        picture.remove_prefix(7);
    }

    if (picture.empty() || picture.size() > 128) return std::nullopt;
    for (const unsigned char c : picture) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9'))) {
            return std::nullopt;
        }
    }
    return std::string(picture);
}

// Read the image from the same node API selected for chain inspection.
// The response is capped in memory and never creates a local file.
inline std::expected<std::string, std::string> fetch_ipfs_avatar(
    const std::string& endpoint, std::string_view picture) {
    constexpr std::size_t kMaxAvatarBytes = 2 * 1024 * 1024;
    const auto cid = picture_cid(picture);
    if (!cid) return std::unexpected("Picture has no usable IPFS CID");

    httplib::Client client(endpoint);
    client.set_connection_timeout(3);
    client.set_read_timeout(5);
    client.set_follow_location(false);

    std::string bytes;
    const auto response = client.Get("/ipfs/" + *cid,
        [&](const char* chunk, std::size_t length) {
            if (length > kMaxAvatarBytes - bytes.size()) return false;
            bytes.append(chunk, length);
            return true;
        });

    if (!response || response->status != 200 || bytes.empty())
        return std::unexpected("IPFS avatar unavailable or larger than 2 MiB");
    return bytes;
}

// Pipe untrusted image bytes to the optional chafa renderer. A socketpair with
// MSG_NOSIGNAL avoids SIGPIPE if chafa is not installed or exits prematurely.
// The decoder runs in a child process; no shell, temporary file, or curl.
inline bool render_avatar(std::string_view bytes) {
    if (!::isatty(STDOUT_FILENO) || bytes.empty()) return false;
    if (const char* term = std::getenv("TERM"); !term || std::string_view(term) == "dumb")
        return false;

    int sockets[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) return false;

    const pid_t child = ::fork();
    if (child == 0) {
        ::close(sockets[0]);
        if (::dup2(sockets[1], STDIN_FILENO) < 0) ::_exit(127);
        ::close(sockets[1]);
        ::alarm(5);
        ::execlp("chafa", "chafa", "--format", "symbols", "--size", "20x10",
                 "-", static_cast<char*>(nullptr));
        ::_exit(127);
    }

    ::close(sockets[1]);
    if (child < 0) {
        ::close(sockets[0]);
        return false;
    }

    // Never block indefinitely if an image decoder stalls.
    timeval timeout{.tv_sec = 5, .tv_usec = 0};
    ::setsockopt(sockets[0], SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));

    std::size_t sent = 0;
    while (sent < bytes.size()) {
        const auto n = ::send(sockets[0], bytes.data() + sent, bytes.size() - sent, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        sent += static_cast<std::size_t>(n);
    }
    ::shutdown(sockets[0], SHUT_WR);
    ::close(sockets[0]);

    int status = 0;
    while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {}
    return sent == bytes.size() && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

inline bool preview_avatar(const std::string& endpoint, std::string_view picture) {
    if (!::isatty(STDOUT_FILENO)) return false;
    const auto bytes = fetch_ipfs_avatar(endpoint, picture);
    return bytes && render_avatar(*bytes);
}

} // namespace utx::app::infrastructure::inspection
