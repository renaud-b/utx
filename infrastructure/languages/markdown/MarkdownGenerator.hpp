#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../../../domain/graph/GraphElement.hpp"
#include "../../../domain/graph/IGraphVisitor.hpp"

namespace utx::infra::languages::markdown {

class MarkdownGenerator final : public domain::graph::IGraphVisitor {
public:
    MarkdownGenerator() = default;

    void visit(const std::shared_ptr<domain::graph::GraphElement>& element) override {
        if (!element) {
            return;
        }
        output_ += render_node(*element);
    }

    [[nodiscard]] std::string get_result() const override { return output_; }

private:
    static std::string trim(std::string s) {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char ch) { return !is_space(ch); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char ch) { return !is_space(ch); }).base(), s.end());
        return s;
    }

    static std::string escape_text(std::string_view text) {
        std::string out;
        out.reserve(text.size() * 2);
        for (const char ch : text) {
            switch (ch) {
                case '\\':
                case '`':
                case '*':
                case '_':
                case '[':
                case ']':
                case '!':
                case '#':
                case '>':
                    out.push_back('\\');
                    out.push_back(ch);
                    break;
                default:
                    out.push_back(ch);
                    break;
            }
        }
        return out;
    }

    static std::string render_inline_children(const std::vector<std::shared_ptr<domain::graph::GraphElement>>& children) {
        std::ostringstream oss;
        for (const auto& child : children) {
            if (!child) {
                continue;
            }
            oss << render_inline_node(*child);
        }
        return oss.str();
    }

    static std::string render_inline_node(const domain::graph::GraphElement& element) {
        const std::string type = element.get_property("type");

        if (type == "Text") {
            return escape_text(element.get_property("value"));
        }
        if (type == "InlineCode") {
            const std::string value = element.get_property("value");
            size_t longest = 0;
            size_t run = 0;
            for (const char ch : value) {
                if (ch == '`') {
                    ++run;
                    longest = std::max(longest, run);
                } else {
                    run = 0;
                }
            }
            const std::string fence(longest + 1, '`');
            return fence + value + fence;
        }
        if (type == "Emphasis") {
            return "*" + render_inline_children(element.children()) + "*";
        }
        if (type == "Strong") {
            return "**" + render_inline_children(element.children()) + "**";
        }
        if (type == "Link") {
            return "[" + render_inline_children(element.children()) + "](" + element.get_property("url") + ")";
        }
        if (type == "Image") {
            return "![" + escape_text(element.get_property("alt")) + "](" + element.get_property("src") + ")";
        }

        if (!element.children().empty()) {
            return render_inline_children(element.children());
        }
        return escape_text(element.get_property("value"));
    }

    static std::string render_block_children(const std::vector<std::shared_ptr<domain::graph::GraphElement>>& children) {
        std::vector<std::string> blocks;
        blocks.reserve(children.size());
        for (const auto& child : children) {
            if (!child) {
                continue;
            }
            const std::string rendered = render_node(*child);
            if (!rendered.empty()) {
                blocks.push_back(rendered);
            }
        }
        std::ostringstream oss;
        for (size_t i = 0; i < blocks.size(); ++i) {
            if (i > 0) {
                oss << "\n\n";
            }
            oss << blocks[i];
        }
        return oss.str();
    }

    static std::vector<std::string> split_lines(const std::string& text) {
        std::vector<std::string> lines;
        std::string current;
        for (char ch : text) {
            if (ch == '\n') {
                lines.push_back(current);
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        lines.push_back(current);
        return lines;
    }

    static std::string indent_block(const std::string& text, const std::string& prefix) {
        const auto lines = split_lines(text);
        std::ostringstream oss;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                oss << '\n';
            }
            oss << prefix << lines[i];
        }
        return oss.str();
    }

    static std::string render_code_block(const domain::graph::GraphElement& element) {
        const std::string fence = element.get_property("fence").empty() ? "```" : element.get_property("fence");
        const std::string language = trim(element.get_property("language"));
        const std::string body = element.get_property("value");

        std::ostringstream oss;
        oss << fence;
        if (!language.empty()) {
            oss << language;
        }
        oss << '\n' << body << '\n' << fence;
        return oss.str();
    }

    static std::string render_heading(const domain::graph::GraphElement& element) {
        size_t level = 1;
        const std::string level_prop = element.get_property("level");
        if (!level_prop.empty()) {
            level = std::max<size_t>(1, std::min<size_t>(6, static_cast<size_t>(std::stoul(level_prop))));
        }
        return std::string(level, '#') + " " + render_inline_children(element.children());
    }

    static std::string render_paragraph(const domain::graph::GraphElement& element) {
        return render_inline_children(element.children());
    }

    static std::string render_list_item(const domain::graph::GraphElement& element,
                                        const std::string& marker) {
        if (element.children().empty()) {
            return marker;
        }

        const auto& children = element.children();
        const std::string first = render_node(*children.front());
        std::ostringstream oss;
        oss << marker << first;
        const std::string padding(marker.size(), ' ');
        for (size_t i = 1; i < children.size(); ++i) {
            const std::string rendered = render_node(*children[i]);
            if (rendered.empty()) {
                continue;
            }
            oss << '\n' << indent_block(rendered, padding);
        }
        return oss.str();
    }

    static std::string render_list(const domain::graph::GraphElement& element) {
        const bool ordered = element.get_property("ordered") == "true";
        const std::string start_prop = element.get_property("start");
        size_t counter = 1;
        if (ordered && !start_prop.empty()) {
            counter = std::max<size_t>(1, static_cast<size_t>(std::stoul(start_prop)));
        }

        std::ostringstream oss;
        for (size_t i = 0; i < element.children().size(); ++i) {
            const auto& child = element.children()[i];
            if (!child) {
                continue;
            }
            if (i > 0) {
                oss << '\n';
            }
            const std::string marker = ordered ? std::to_string(counter++) + ". " : "- ";
            oss << render_list_item(*child, marker);
        }
        return oss.str();
    }

    static std::string render_blockquote(const domain::graph::GraphElement& element) {
        const std::string content = render_block_children(element.children());
        const auto lines = split_lines(content);
        std::ostringstream oss;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) {
                oss << '\n';
            }
            if (lines[i].empty()) {
                oss << '>';
            } else {
                oss << "> " << lines[i];
            }
        }
        return oss.str();
    }

    static std::string render_node(const domain::graph::GraphElement& element) {
        const std::string type = element.get_property("type");

        if (type == "Document") {
            return render_block_children(element.children());
        }
        if (type == "Paragraph") {
            return render_paragraph(element);
        }
        if (type == "Heading") {
            return render_heading(element);
        }
        if (type == "CodeBlock") {
            return render_code_block(element);
        }
        if (type == "List") {
            return render_list(element);
        }
        if (type == "ListItem") {
            return render_block_children(element.children());
        }
        if (type == "Blockquote") {
            return render_blockquote(element);
        }
        if (type == "ThematicBreak") {
            return "---";
        }
        if (type == "Text" || type == "InlineCode" || type == "Emphasis" || type == "Strong" || type == "Link" || type == "Image") {
            return render_inline_node(element);
        }

        if (!element.children().empty()) {
            return render_block_children(element.children());
        }
        return element.get_property("value");
    }

    std::string output_;
};

} // namespace utx::infra::languages::markdown
