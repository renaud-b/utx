#pragma once

#include <chrono>
#include <string>
#include <utility>
#include <source_location>

#include "Logger.hpp"

namespace utx::common {

class ScopedTimer {
public:
    using Clock = std::chrono::steady_clock;

    ScopedTimer(std::string label,
                std::chrono::milliseconds threshold,
                std::source_location location = std::source_location::current())
        : label_(std::move(label))
        , threshold_(threshold)
        , location_(location)
        , start_(Clock::now())
    {}

    ~ScopedTimer() {
        log_if_slow(Clock::now() - start_);
    }

    // 🔥 Static helper pour lambda
    template <typename Fn>
    static auto measure(std::string label,
                        std::chrono::milliseconds threshold,
                        Fn&& fn,
                        std::source_location location = std::source_location::current(),
                        bool verbose = false)
        -> decltype(fn())
    {
        const auto start = Clock::now();

        if constexpr (std::is_void_v<decltype(fn())>) {
            std::forward<Fn>(fn)();
            const auto duration = Clock::now() - start;
            log_static(label, threshold, duration, location, verbose);
        } else {
            auto result = std::forward<Fn>(fn)();
            const auto duration = Clock::now() - start;
            log_static(label, threshold, duration, location, verbose);
            return result;
        }
        return {};
    }

    // non copyable
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    // movable
    ScopedTimer(ScopedTimer&&) = default;
    ScopedTimer& operator=(ScopedTimer&&) = default;

private:
    static void log_static(const std::string& label,
                           std::chrono::milliseconds threshold,
                           std::chrono::steady_clock::duration duration,
                           const std::source_location& location,
                           const bool verbose = false)
    {
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(duration);

        if (ms >= threshold) {
            LOG_THIS_WARN(
                "[SLOW] {} took {} ms (threshold: {} ms)",
                label,
                ms.count(),
                threshold.count());
            if (!verbose) {
                return;
            }

            LOG_THIS_WARN(
                "at {}:{}",
                location.file_name(),
                location.line());
            LOG_THIS_WARN(
                "in function {}",
                location.function_name()
            );
        }
    }

    void log_if_slow(std::chrono::steady_clock::duration duration) const {
        log_static(label_, threshold_, duration, location_);
    }

private:
    std::string label_;
    std::chrono::milliseconds threshold_;
    std::source_location location_;
    Clock::time_point start_;
};

} // namespace utx::common