#pragma once
#include "GraphElement.hpp"
#include <string>

namespace utx::domain::graph {
    class IGraphVisitor {
    public:
        virtual ~IGraphVisitor() = default;
        virtual void visit(const std::shared_ptr<GraphElement>& element) = 0;
        [[nodiscard]] virtual std::string get_result() const = 0;
    };
}