#pragma once

#include <cerrno>
#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

namespace utx::app::infrastructure::inspection {

// chafa and curl are optional. Do not invoke a shell with profile-controlled URLs.
// Only fetch small HTTPS images, and only for an interactive terminal.
inline bool safe_avatar_url(std::string_view url) {
    if (!url.starts_with("https://") || url.size() > 2048) return false;
    for (const unsigned char c : url) {
        if (c <= 32 || c >= 127) return false;
    }
    return true;
}

inline void preview_avatar(std::string_view picture) {
    if (!::isatty(STDOUT_FILENO) || !safe_avatar_url(picture)) return;
    if (const char* term = std::getenv("TERM"); !term || std::string_view(term) == "dumb") return;

    const std::string url(picture);
    int pipe_fds[2];
    if (::pipe(pipe_fds) != 0) return;

    std::cout.flush();
    const pid_t downloader = ::fork();
    if (downloader == 0) {
        ::dup2(pipe_fds[1], STDOUT_FILENO);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execlp("curl", "curl", "--fail", "--silent", "--location",
                 "--proto", "=https", "--proto-redir", "=https",
                 "--connect-timeout", "3", "--max-time", "5",
                 "--max-filesize", "524288", "--", url.c_str(),
                 static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (downloader < 0) {
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        return;
    }

    const pid_t renderer = ::fork();
    if (renderer == 0) {
        ::dup2(pipe_fds[0], STDIN_FILENO);
        ::close(pipe_fds[0]);
        ::close(pipe_fds[1]);
        const int devnull = ::open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            ::dup2(devnull, STDERR_FILENO);
            ::close(devnull);
        }
        ::execlp("chafa", "chafa", "--format", "symbols", "--size", "16x8",
                 "-", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    ::close(pipe_fds[0]);
    ::close(pipe_fds[1]);
    int status = 0;
    while (::waitpid(downloader, &status, 0) < 0 && errno == EINTR) {}
    if (renderer > 0) {
        while (::waitpid(renderer, &status, 0) < 0 && errno == EINTR) {}
    }
}

} // namespace utx::app::infrastructure::inspection
