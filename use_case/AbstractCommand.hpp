#pragma once

#include <vector>
#include <string>

namespace utx::app::use_case {
    /** Base class for all command implementations in the application domain. */
    class AbstractCommand {
    public:
        virtual ~AbstractCommand() = default;
        /** Execute the command with the given arguments.
         * @param args The command-line arguments passed to the command.
         * @return An integer status code (0 for success, non-zero for errors).
         */
        [[nodiscard]]
        virtual int execute(const std::vector<std::string>& args) = 0;
    };
}