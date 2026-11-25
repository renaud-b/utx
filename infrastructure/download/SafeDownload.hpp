#pragma once

#include <cerrno>
#include <cstring>
#include <expected>
#include <filesystem>
#include <set>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include "domain/Types.hpp"

namespace utx::app::infrastructure::download {
namespace fs = std::filesystem;

inline bool safe_chain_id(std::string_view id) {
    if (id.empty() || id.size() > 128 || id == "." || id == "..") return false;
    for (unsigned char c : id)
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return false;
    return true;
}

// Reject, rather than normalize, remote paths. Keep CLI and Git metadata
// private; project configuration and CI files are ordinary downloadable files.
inline bool safe_target_path(std::string_view path) {
    if (path.empty() || path.size() > 4096) return false;
    for (unsigned char c : path)
        if (c < 32 || c == 127 || c == '\\' || c == ':') return false;
    while (true) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == ".." ||
            part == ".utx" || part == ".utx.deploy.json" ||
            part == ".utx.deploy.json.tmp" || part == ".git") return false;
        if (slash == std::string_view::npos) return true;
        path.remove_prefix(slash + 1);
    }
}

inline std::expected<void, std::string> validate_destinations(
    const fs::path& root, const domain::DeployConfig& config) {
    std::set<fs::path> files{".utx.deploy.json", ".utx/config.json"};
    for (const auto& target : config.targets) {
        if (!safe_chain_id(target.chain) || !safe_target_path(target.path))
            return std::unexpected("Manifest contains an invalid chain ID or path");
        if (!files.emplace(target.path).second)
            return std::unexpected("Manifest contains duplicate destination paths");
    }
    for (const auto& file : files) {
        for (auto parent = file.parent_path(); !parent.empty(); parent = parent.parent_path())
            if (files.contains(parent))
                return std::unexpected("Manifest contains a file/directory collision");
        fs::path current = root;
        for (auto it = file.begin(); it != file.end(); ++it) {
            current /= *it;
            std::error_code ec;
            const auto status = fs::symlink_status(current, ec);
            if (status.type() == fs::file_type::not_found &&
                (!ec || ec == std::errc::no_such_file_or_directory)) break;
            if (ec || fs::is_symlink(status))
                return std::unexpected("Destination contains an inaccessible path or symbolic link");
            if (std::next(it) == file.end() || !fs::is_directory(status))
                return std::unexpected("Destination already exists or is not a directory; use a fresh directory");
        }
    }
    return {};
}

// Linux/POSIX CLI: descriptor-relative traversal prevents following links
// even if a destination component changes after the read-only preflight.
class FileDescriptor {
public:
    explicit FileDescriptor(int fd) : fd_(fd) {}
    ~FileDescriptor() { if (fd_ >= 0) ::close(fd_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    FileDescriptor(FileDescriptor&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    FileDescriptor& operator=(FileDescriptor&& other) noexcept {
        if (this != &other) {
            if (fd_ >= 0) ::close(fd_);
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }
private:
    int fd_;
};

inline std::expected<void, std::string> write_new_file(
    const fs::path& root, const fs::path& relative, std::string_view bytes) {
    // Only validated manifest paths or these two CLI-owned paths reach here.
    const auto path = relative.generic_string();
    if (path != ".utx.deploy.json" && path != ".utx/config.json" && !safe_target_path(path))
        return std::unexpected("Unsafe destination path");
    FileDescriptor directory(::open(root.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW));
    if (!directory) return std::unexpected("Cannot open download directory");
    for (auto it = relative.begin(); it != relative.end(); ++it) {
        const auto part = it->string();
        if (std::next(it) == relative.end()) {
            FileDescriptor output(::openat(directory.get(), part.c_str(),
                O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0644));
            if (!output) return std::unexpected("Cannot create destination exclusively");
            while (!bytes.empty()) {
                const auto count = ::write(output.get(), bytes.data(), bytes.size());
                if (count < 0 && errno == EINTR) continue;
                if (count <= 0) {
                    ::unlinkat(directory.get(), part.c_str(), 0);
                    return std::unexpected("Cannot write destination");
                }
                bytes.remove_prefix(static_cast<std::size_t>(count));
            }
            if (::fsync(output.get()) != 0) {
                ::unlinkat(directory.get(), part.c_str(), 0);
                return std::unexpected("Cannot flush destination");
            }
            return {};
        }
        if (::mkdirat(directory.get(), part.c_str(), 0755) != 0 && errno != EEXIST)
            return std::unexpected("Cannot create destination directory");
        FileDescriptor child(::openat(directory.get(), part.c_str(),
            O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC));
        if (!child) return std::unexpected("Cannot open destination directory without following links");
        directory = std::move(child);
    }
    return std::unexpected("Empty destination");
}

} // namespace utx::app::infrastructure::download
