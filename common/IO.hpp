#pragma once

#include <fstream>
#include <stdexcept>
#include <string>

namespace utx::common::io {
    [[nodiscard]]
    inline std::string read_file(const std::string &file_path) {
        std::ifstream file_stream(file_path, std::ios::in | std::ios::binary);
        if (!file_stream) {
            throw std::runtime_error("Cannot open file: " + file_path);
        }
        std::string content;
        file_stream.seekg(0, std::ios::end);
        content.resize(file_stream.tellg());
        file_stream.seekg(0, std::ios::beg);
        file_stream.read(&content[0], content.size());
        file_stream.close();

        return content;
    }

    inline void write_file(const std::string &file_path, const std::string &content) {
        std::ofstream file_stream(file_path, std::ios::out | std::ios::binary);
        if (!file_stream) {
            throw std::runtime_error("Cannot open file for writing: " + file_path);
        }

        file_stream.write(content.data(), content.size());
        file_stream.close();
    }
} // namespace utx::common::io
