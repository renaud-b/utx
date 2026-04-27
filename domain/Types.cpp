#include <string>
#include <nlohmann/json.hpp>

#include "Types.hpp"

using json = nlohmann::json;

namespace utx::app::domain {

std::string to_string(TargetKind k) {
    switch (k) {
        case TargetKind::Html: return "html";
        case TargetKind::Js:   return "js";
        case TargetKind::Css:  return "css";
        case TargetKind::Markdown: return "markdown";
        case TargetKind::Go: return "go";
        case TargetKind::Graph: return "graph";
        case TargetKind::Cpp: return "cpp";
        case TargetKind::Identity: return "identity";
        default: return "unknown";
    }
}

std::optional<TargetKind> parse_kind(std::string s) {
    std::ranges::transform(s, s.begin(), [](unsigned char c) { return std::tolower(c); });
    if (s == "html") return TargetKind::Html;
    if (s == "cpp" || s == "hpp") return TargetKind::Cpp;
    if (s == "js" || s == "javascript") return TargetKind::Js;
    if (s == "css") return TargetKind::Css;
    if (s == "md" || s == "markdown") return TargetKind::Markdown;
    if (s == "go" || s == "golang") return TargetKind::Go;
    if (s == "graph") return TargetKind::Graph;
    if (s == "identity") return TargetKind::Identity;
    return std::nullopt;
}

}
