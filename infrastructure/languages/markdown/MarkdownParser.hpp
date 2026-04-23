#pragma once

#include <algorithm>
#include <cctype>
#include <memory>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "../../../domain/graph/GraphElement.hpp"

namespace utx::infra::languages::markdown {

class MarkdownParser final {
public:
    [[nodiscard]]
    std::shared_ptr<utx::domain::graph::GraphElement> parse(const std::string& source) const {
        auto document = make_node("Document");
        document->set_property("tag", "markdown");

        const auto lines = split_lines(source);
        size_t index = 0;
        parse_blocks(lines, index, document);
        return document;
    }

private:
    static std::shared_ptr<utx::domain::graph::GraphElement> make_node(const std::string& type) {
        auto node = std::make_shared<utx::domain::graph::GraphElement>(type);
        node->set_property("type", type);
        return node;
    }

    static std::string trim(std::string s) {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char ch) { return !is_space(ch); }));
        s.erase(std::find_if(s.rbegin(), s.rend(), [&](unsigned char ch) { return !is_space(ch); }).base(), s.end());
        return s;
    }

    static std::string ltrim(std::string s) {
        auto is_space = [](unsigned char c) { return std::isspace(c) != 0; };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), [&](unsigned char ch) { return !is_space(ch); }));
        return s;
    }

    static bool is_blank(std::string_view line) {
        for (const unsigned char c : line) {
            if (!std::isspace(c)) {
                return false;
            }
        }
        return true;
    }

    static size_t leading_spaces(std::string_view line) {
        size_t count = 0;
        while (count < line.size() && line[count] == ' ') {
            ++count;
        }
        return count;
    }

    static std::vector<std::string> split_lines(const std::string& source) {
        std::vector<std::string> lines;
        std::string current;
        for (char ch : source) {
            if (ch == '\n') {
                if (!current.empty() && current.back() == '\r') {
                    current.pop_back();
                }
                lines.push_back(current);
                current.clear();
            } else {
                current.push_back(ch);
            }
        }
        if (!current.empty() || (!source.empty() && source.back() == '\n')) {
            if (!current.empty() && current.back() == '\r') {
                current.pop_back();
            }
            lines.push_back(current);
        }
        return lines;
    }

    static bool starts_with(std::string_view s, std::string_view prefix) {
        return s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix;
    }

    static bool is_heading_line(std::string_view line, size_t& level, std::string& content) {
        const std::string trimmed = ltrim(std::string(line));
        if (trimmed.empty() || trimmed.front() != '#') {
            return false;
        }

        size_t count = 0;
        while (count < trimmed.size() && trimmed[count] == '#') {
            ++count;
        }
        if (count == 0 || count > 6) {
            return false;
        }
        if (count >= trimmed.size() || trimmed[count] != ' ') {
            return false;
        }

        level = count;
        content = trimmed.substr(count + 1);
        return true;
    }

    static bool is_thematic_break(std::string_view line) {
        const std::string trimmed = trim(std::string(line));
        if (trimmed == "---" || trimmed == "***" || trimmed == "___") {
            return true;
        }
        return false;
    }

    static bool is_fence_line(std::string_view line, std::string& fence, std::string& info) {
        const std::string trimmed = ltrim(std::string(line));
        if (starts_with(trimmed, "```")) {
            fence = "```";
            info = trim(trimmed.substr(3));
            return true;
        }
        if (starts_with(trimmed, "~~~")) {
            fence = "~~~";
            info = trim(trimmed.substr(3));
            return true;
        }
        return false;
    }

    struct ListMarker {
        bool ordered = false;
        std::string marker;
        std::string content;
    };

    static bool parse_list_marker(std::string_view line, ListMarker& marker) {
        const std::string trimmed = ltrim(std::string(line));
        if (trimmed.size() >= 2 && (trimmed.starts_with("- ") || trimmed.starts_with("* ") || trimmed.starts_with("+ "))) {
            marker.ordered = false;
            marker.marker = std::string(1, trimmed[0]);
            marker.content = trimmed.substr(2);
            return true;
        }

        size_t i = 0;
        while (i < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
            ++i;
        }
        if (i == 0 || i + 1 >= trimmed.size()) {
            return false;
        }
        if ((trimmed[i] != '.' && trimmed[i] != ')') || trimmed[i + 1] != ' ') {
            return false;
        }

        marker.ordered = true;
        marker.marker = trimmed.substr(0, i + 2);
        marker.content = trimmed.substr(i + 2);
        return true;
    }

    static size_t find_next_single_asterisk(std::string_view text, size_t start) {
        for (size_t i = start; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (text[i] == '*') {
                if (i + 1 < text.size() && text[i + 1] == '*') {
                    ++i;
                    continue;
                }
                return i;
            }
        }
        return std::string_view::npos;
    }

    static size_t find_next_double_asterisk(std::string_view text, size_t start) {
        for (size_t i = start; i + 1 < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (text.compare(i, 2, "**") == 0) {
                return i;
            }
        }
        return std::string_view::npos;
    }

    static size_t find_link_close(std::string_view text, size_t start) {
        for (size_t i = start; i + 1 < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (text[i] == ']' && text[i + 1] == '(') {
                return i;
            }
        }
        return std::string_view::npos;
    }

    static size_t find_matching_paren(std::string_view text, size_t start) {
        int depth = 0;
        for (size_t i = start; i < text.size(); ++i) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                ++i;
                continue;
            }
            if (text[i] == '(') {
                ++depth;
            } else if (text[i] == ')') {
                if (depth == 0) {
                    return i;
                }
                --depth;
            }
        }
        return std::string_view::npos;
    }

    static std::vector<std::shared_ptr<utx::domain::graph::GraphElement>>
    parse_inline_children(std::string_view text) {
        std::vector<std::shared_ptr<utx::domain::graph::GraphElement>> children;
        std::string buffer;

        auto flush_text = [&]() {
            if (buffer.empty()) {
                return;
            }
            auto text_node = make_node("Text");
            text_node->set_property("value", buffer);
            children.push_back(std::move(text_node));
            buffer.clear();
        };

        for (size_t i = 0; i < text.size();) {
            if (text[i] == '\\' && i + 1 < text.size()) {
                buffer.push_back(text[i + 1]);
                i += 2;
                continue;
            }

            if (i + 1 < text.size() && text.compare(i, 2, "**") == 0) {
                const size_t close = find_next_double_asterisk(text, i + 2);
                if (close != std::string_view::npos) {
                    flush_text();
                    auto strong = make_node("Strong");
                    for (const auto& child : parse_inline_children(text.substr(i + 2, close - (i + 2)))) {
                        strong->add_child(child);
                    }
                    children.push_back(std::move(strong));
                    i = close + 2;
                    continue;
                }
            }

            if (text[i] == '*') {
                const size_t close = find_next_single_asterisk(text, i + 1);
                if (close != std::string_view::npos) {
                    flush_text();
                    auto emphasis = make_node("Emphasis");
                    for (const auto& child : parse_inline_children(text.substr(i + 1, close - (i + 1)))) {
                        emphasis->add_child(child);
                    }
                    children.push_back(std::move(emphasis));
                    i = close + 1;
                    continue;
                }
            }

            if (text[i] == '`') {
                size_t close = i + 1;
                while (close < text.size() && text[close] != '`') {
                    if (text[close] == '\\' && close + 1 < text.size()) {
                        close += 2;
                        continue;
                    }
                    ++close;
                }
                if (close < text.size() && text[close] == '`') {
                    flush_text();
                    auto code = make_node("InlineCode");
                    code->set_property("value", std::string(text.substr(i + 1, close - (i + 1))));
                    children.push_back(std::move(code));
                    i = close + 1;
                    continue;
                }
            }

            if (text[i] == '!' && i + 1 < text.size() && text[i + 1] == '[') {
                const size_t label_close = find_link_close(text, i + 2);
                if (label_close != std::string_view::npos && label_close + 1 < text.size() && text[label_close + 1] == '(') {
                    const size_t url_close = find_matching_paren(text, label_close + 2);
                    if (url_close != std::string_view::npos) {
                        flush_text();
                        auto image = make_node("Image");
                        const std::string alt = std::string(text.substr(i + 2, label_close - (i + 2)));
                        const std::string src = std::string(text.substr(label_close + 2, url_close - (label_close + 2)));
                        image->set_property("alt", alt);
                        image->set_property("src", src);
                        children.push_back(std::move(image));
                        i = url_close + 1;
                        continue;
                    }
                }
            }

            if (text[i] == '[') {
                const size_t label_close = find_link_close(text, i + 1);
                if (label_close != std::string_view::npos && label_close + 1 < text.size() && text[label_close + 1] == '(') {
                    const size_t url_close = find_matching_paren(text, label_close + 2);
                    if (url_close != std::string_view::npos) {
                        flush_text();
                        auto link = make_node("Link");
                        link->set_property("url", std::string(text.substr(label_close + 2, url_close - (label_close + 2))));
                        for (const auto& child : parse_inline_children(text.substr(i + 1, label_close - (i + 1)))) {
                            link->add_child(child);
                        }
                        children.push_back(std::move(link));
                        i = url_close + 1;
                        continue;
                    }
                }
            }

            buffer.push_back(text[i]);
            ++i;
        }

        flush_text();
        return children;
    }

    static void append_inline_text(const std::string& text,
                                   const std::shared_ptr<utx::domain::graph::GraphElement>& parent) {
        for (const auto& child : parse_inline_children(text)) {
            parent->add_child(child);
        }
    }

    static std::string join_lines(const std::vector<std::string>& lines, size_t begin, size_t end) {
        std::ostringstream oss;
        for (size_t i = begin; i < end; ++i) {
            if (i > begin) {
                oss << '\n';
            }
            oss << lines[i];
        }
        return oss.str();
    }

    static std::vector<std::string> strip_blockquote_prefix(const std::vector<std::string>& lines, size_t begin, size_t end) {
        std::vector<std::string> inner;
        inner.reserve(end - begin);
        for (size_t i = begin; i < end; ++i) {
            std::string line = lines[i];
            std::string trimmed = ltrim(line);
            if (!trimmed.empty() && trimmed.front() == '>') {
                trimmed.erase(trimmed.begin());
                if (!trimmed.empty() && trimmed.front() == ' ') {
                    trimmed.erase(trimmed.begin());
                }
                inner.push_back(std::move(trimmed));
            } else {
                inner.push_back(std::move(line));
            }
        }
        return inner;
    }

    static void parse_paragraph(const std::vector<std::string>& lines,
                                size_t begin,
                                size_t end,
                                const std::shared_ptr<utx::domain::graph::GraphElement>& parent) {
        std::ostringstream oss;
        for (size_t i = begin; i < end; ++i) {
            const std::string line = trim(lines[i]);
            if (line.empty()) {
                continue;
            }
            if (oss.tellp() > 0) {
                oss << ' ';
            }
            oss << line;
        }

        const std::string paragraph_text = trim(oss.str());
        if (paragraph_text.empty()) {
            return;
        }

        auto paragraph = make_node("Paragraph");
        append_inline_text(paragraph_text, paragraph);
        parent->add_child(paragraph);
    }

    static void parse_fenced_code(const std::vector<std::string>& lines,
                                  size_t& index,
                                  const std::shared_ptr<utx::domain::graph::GraphElement>& parent) {
        std::string fence;
        std::string info;
        if (!is_fence_line(lines[index], fence, info)) {
            return;
        }

        ++index;
        std::ostringstream body;
        while (index < lines.size()) {
            const std::string trimmed = ltrim(lines[index]);
            if (starts_with(trimmed, fence)) {
                break;
            }
            if (body.tellp() > 0) {
                body << '\n';
            }
            body << lines[index];
            ++index;
        }
        if (index < lines.size()) {
            ++index;
        }

        auto code_block = make_node("CodeBlock");
        code_block->set_property("fence", fence);
        code_block->set_property("language", info);
        code_block->set_property("value", body.str());
        parent->add_child(code_block);
    }

    static void parse_heading_block(const std::string& line,
                                    const std::shared_ptr<utx::domain::graph::GraphElement>& parent) {
        size_t level = 0;
        std::string content;
        if (!is_heading_line(line, level, content)) {
            return;
        }

        auto heading = make_node("Heading");
        heading->set_property("level", std::to_string(level));
        append_inline_text(content, heading);
        parent->add_child(heading);
    }

    static void parse_blockquote(const std::vector<std::string>& lines,
                                 size_t& index,
                                 const std::shared_ptr<utx::domain::graph::GraphElement>& parent) {
        const size_t begin = index;
        bool saw_quote = false;
        while (index < lines.size()) {
            const std::string trimmed = ltrim(lines[index]);
            if (!trimmed.empty() && trimmed.front() == '>') {
                saw_quote = true;
                ++index;
                continue;
            }
            if (is_blank(lines[index]) && saw_quote) {
                ++index;
                continue;
            }
            break;
        }

        const auto inner_lines = strip_blockquote_prefix(lines, begin, index);
        auto quote = make_node("Blockquote");
        size_t inner_index = 0;
        parse_blocks(inner_lines, inner_index, quote);
        parent->add_child(quote);
    }

    static void parse_list(const std::vector<std::string>& lines,
                           size_t& index,
                           const std::shared_ptr<utx::domain::graph::GraphElement>& parent,
                           size_t list_indent = 0) {
        ListMarker first{};
        const std::string first_line = lines[index];
        if (leading_spaces(first_line) != list_indent || !parse_list_marker(first_line, first)) {
            return;
        }

        auto list = make_node("List");
        list->set_property("ordered", first.ordered ? "true" : "false");
        if (first.ordered) {
            const std::string trimmed = ltrim(std::string(lines[index]));
            size_t i = 0;
            while (i < trimmed.size() && std::isdigit(static_cast<unsigned char>(trimmed[i]))) {
                ++i;
            }
            list->set_property("start", trimmed.substr(0, i));
        }

        while (index < lines.size()) {
            ListMarker marker{};
            const size_t current_indent = leading_spaces(lines[index]);
            if (current_indent != list_indent || !parse_list_marker(lines[index], marker) || marker.ordered != first.ordered) {
                break;
            }

            auto item = make_node("ListItem");
            std::vector<std::string> paragraph_lines;
            paragraph_lines.push_back(marker.content);
            bool paragraph_emitted = false;
            auto emit_paragraph = [&]() {
                if (paragraph_emitted) {
                    return;
                }
                parse_paragraph(paragraph_lines, 0, paragraph_lines.size(), item);
                paragraph_emitted = true;
            };
            ++index;

            while (index < lines.size()) {
                if (is_blank(lines[index])) {
                    ++index;
                    emit_paragraph();
                    break;
                }
                const size_t next_indent = leading_spaces(lines[index]);
                if (next_indent < list_indent) {
                    emit_paragraph();
                    break;
                }
                ListMarker continuation_marker{};
                if (next_indent == list_indent && parse_list_marker(lines[index], continuation_marker)) {
                    emit_paragraph();
                    break;
                }
                if (next_indent > list_indent) {
                    const std::string stripped = ltrim(lines[index]);
                    if (parse_list_marker(stripped, continuation_marker)) {
                        emit_paragraph();
                        parse_list(lines, index, item, next_indent);
                        continue;
                    }
                }
                paragraph_lines.push_back(trim(lines[index]));
                ++index;
            }

            emit_paragraph();
            list->add_child(item);
        }

        parent->add_child(list);
    }

    static void parse_blocks(const std::vector<std::string>& lines,
                             size_t& index,
                             const std::shared_ptr<utx::domain::graph::GraphElement>& parent) {
        while (index < lines.size()) {
            if (is_blank(lines[index])) {
                ++index;
                continue;
            }

            size_t heading_level = 0;
            std::string heading_content;
            if (is_heading_line(lines[index], heading_level, heading_content)) {
                parse_heading_block(lines[index], parent);
                ++index;
                continue;
            }

            std::string fence;
            std::string info;
            if (is_fence_line(lines[index], fence, info)) {
                parse_fenced_code(lines, index, parent);
                continue;
            }

            if (is_thematic_break(lines[index])) {
                parent->add_child(make_node("ThematicBreak"));
                ++index;
                continue;
            }

            const std::string trimmed = ltrim(lines[index]);
            if (!trimmed.empty() && trimmed.front() == '>') {
                parse_blockquote(lines, index, parent);
                continue;
            }

            ListMarker list_marker{};
            if (parse_list_marker(lines[index], list_marker)) {
                parse_list(lines, index, parent, leading_spaces(lines[index]));
                continue;
            }

            const size_t begin = index;
            while (index < lines.size()) {
                if (is_blank(lines[index])) {
                    break;
                }
                size_t maybe_level = 0;
                std::string maybe_content;
                std::string maybe_fence;
                std::string maybe_info;
                ListMarker maybe_marker{};
                const bool break_block =
                    is_heading_line(lines[index], maybe_level, maybe_content) ||
                    is_fence_line(lines[index], maybe_fence, maybe_info) ||
                    is_thematic_break(lines[index]) ||
                    (!ltrim(lines[index]).empty() && ltrim(lines[index]).front() == '>') ||
                    parse_list_marker(lines[index], maybe_marker);
                if (break_block && index != begin) {
                    break;
                }
                if (break_block) {
                    break;
                }
                ++index;
            }
            parse_paragraph(lines, begin, index, parent);
        }
    }
};

} // namespace utx::infra::languages::markdown
