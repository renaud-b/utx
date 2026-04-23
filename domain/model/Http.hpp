#pragma once

#include <string>

#include "AtomicBlock.hpp"
#include "Chain.hpp"

namespace utx::domain::model {
    /** Struct representing an HTTP error with status code, error code, and message. */
    struct HttpError {
        int http_status = 500;
        std::string code;
        std::string message;

        std::string to_string() const {
            return "HttpError{status=" + std::to_string(http_status) + ", code=" + code + ", message=" + message + "}";
        }
    };
    /** Helper function to create an HttpError instance. */
    static HttpError make_err(const int status, std::string code, std::string msg) {
        return HttpError{ .http_status = status, .code = std::move(code), .message = std::move(msg) };
    }


    /** Structure to represent the status of a chain,
     * including its address, synchronization status, and last block index.
     */
    struct ChainStatus {
        std::string address;
        ChainSyncStatus status;
        uint64_t last_index;

    };

}
