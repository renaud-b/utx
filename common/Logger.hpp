#pragma once

#include <algorithm>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>

#include <unordered_map>
#include <memory>
#include <mutex>
#include <string>
#include <fstream>
#include <ranges>
#include <iostream>

namespace utx::common::logging {

    /** Convert string to spdlog level enum.
     * Defaults to info if unrecognized.
     * @param s Log level as string.
     * @returns Corresponding spdlog level enum.
     */
    inline spdlog::level::level_enum to_level(const std::string& s) {
        auto lower = s;
        std::ranges::transform(lower, lower.begin(), ::tolower);

        if (lower == "trace") return spdlog::level::trace;
        if (lower == "debug") return spdlog::level::debug;
        if (lower == "info")  return spdlog::level::info;
        if (lower == "warn")  return spdlog::level::warn;
        if (lower == "error") return spdlog::level::err;
        if (lower == "critical") return spdlog::level::critical;
        if (lower == "off") return spdlog::level::off;

        return spdlog::level::info;
    }

    /** Load logging configuration from a file.
     * Each line should be in the format: namespace=level
     * Lines starting with '#' are treated as comments.
     * @param path Path to the configuration file.
     * @returns Map of namespace to spdlog level enum.
     */
    inline std::unordered_map<std::string, spdlog::level::level_enum>
    load_logging_config(const std::string& path)
    {
        std::unordered_map<std::string, spdlog::level::level_enum> cfg;

        std::ifstream f(path);
        if (!f.is_open()) {
            std::cout << "logging config file not found: " << path << ", using defaults." << std::endl;
            return cfg;
        }

        std::string line;
        while (std::getline(f, line)) {
            if (line.empty() || line.starts_with('#'))
                continue;

            auto pos = line.find('=');
            if (pos == std::string::npos)
                continue;

            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);

            cfg[key] = to_level(val);
        }
        return cfg;
    }

    /** LoggerRegistry manages loggers for different namespaces (files).
     * It allows setting log levels per namespace and supports adding file sinks.
     */
    class LoggerRegistry {
    public:
        static LoggerRegistry& instance() {
            static LoggerRegistry inst;
            return inst;
        }

        /** Get or create a logger for the given namespace (file).
         * @param name Namespace or file name for the logger.
         * @returns Shared pointer to the spdlog logger.
         */
        std::shared_ptr<spdlog::logger> get(const std::string& name) {
            std::lock_guard lock(mutex_);

            // If exists → return
            if (auto it = loggers_.find(name); it != loggers_.end())
                return it->second;

            // Create logger
            auto logger = spdlog::stderr_color_mt(name);
            logger->set_pattern("%^[%Y-%m-%d %H:%M:%S.%e] [%n] [%l]%$ %v");

            // Apply level from configuration
            if (const auto it = per_logger_level_.find(name); it != per_logger_level_.end()) {
                logger->set_level(it->second);
            } else {
                // Not in config → OFF
                logger->set_level(default_level_);
            }

            loggers_[name] = logger;
            return logger;
        }

        /** Set the global log level for all loggers not overridden in config.
         * @param lvl Global log level to set.
         */
        void set_global_level(spdlog::level::level_enum lvl) {
            std::lock_guard lock(mutex_);
            default_level_ = lvl;

            for (auto& [name, logger] : loggers_) {
                // only apply to those not overridden
                if (!per_logger_level_.contains(name))
                    logger->set_level(lvl);
            }
        }

        /** Enable file output for all loggers.
         * @param filename Path to the log file.
         */
        void enable_file_output(const std::string& filename) {
            std::lock_guard lock(mutex_);

            const auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename, true);
            for (const auto &logger: loggers_ | std::views::values) {
                logger->sinks().push_back(sink);
            }
        }

        /** Apply logging configuration.
         * @param cfg Map of namespace to spdlog level enum.
         */
        void apply_config(const std::unordered_map<std::string, spdlog::level::level_enum>& cfg) {
            std::lock_guard lock(mutex_);
            per_logger_level_ = cfg;

            // Apply immediately to existing loggers
            for (auto& [name, logger] : loggers_) {
                if (auto it = per_logger_level_.find(name); it != per_logger_level_.end()) {
                    logger->set_level(it->second);
                } else {
                    logger->set_level(spdlog::level::off);
                }
            }
        }
        /** Set log level for a specific logger.
         * @param name File name of the logger.
         * @param level Log level to set.
         */
        void set_log_level(const std::string& name, spdlog::level::level_enum level) {
            std::lock_guard lock(mutex_);
            per_logger_level_[name] = level;

            if (auto it = loggers_.find(name); it != loggers_.end()) {
                it->second->set_level(level);
            }
        }


    private:
        LoggerRegistry() = default;

        std::mutex mutex_;
        std::unordered_map<std::string, std::shared_ptr<spdlog::logger>> loggers_;

        // Per-file overrides
        std::unordered_map<std::string, spdlog::level::level_enum> per_logger_level_;

        // Default global (used only if not in config)
        spdlog::level::level_enum default_level_ = spdlog::level::off;
    };

    // -----------------------------------------------------------------------------
    // Helper Macros
    // -----------------------------------------------------------------------------

    #define LOG_NS_NAME (::std::string_view(__FILE__).substr(::std::string_view(__FILE__).find_last_of("/\\") + 1))

    #define LOG_DEBUG(ns, fmt, ...) utx::common::logging::LoggerRegistry::instance().get(ns)->debug(fmt, ##__VA_ARGS__)
    #define LOG_INFO(ns, fmt, ...)  utx::common::logging::LoggerRegistry::instance().get(ns)->info(fmt, ##__VA_ARGS__)
    #define LOG_WARN(ns, fmt, ...)  utx::common::logging::LoggerRegistry::instance().get(ns)->warn(fmt, ##__VA_ARGS__)
    #define LOG_ERROR(ns, fmt, ...) utx::common::logging::LoggerRegistry::instance().get(ns)->error(fmt, ##__VA_ARGS__)
    #define LOG_CRITICAL(ns, fmt, ...) utx::common::logging::LoggerRegistry::instance().get(ns)->critical(fmt, ##__VA_ARGS__)

    // Convenience macros
    #define LOG_THIS_INFO(fmt, ...)  LOG_INFO(std::string(LOG_NS_NAME), fmt, ##__VA_ARGS__)
    #define LOG_THIS_WARN(fmt, ...)  LOG_WARN(std::string(LOG_NS_NAME), fmt, ##__VA_ARGS__)
    #define LOG_THIS_ERROR(fmt, ...) LOG_ERROR(std::string(LOG_NS_NAME), fmt, ##__VA_ARGS__)
    #define LOG_THIS_DEBUG(fmt, ...) LOG_DEBUG(std::string(LOG_NS_NAME), fmt, ##__VA_ARGS__)

} // namespace utx::common::logging
