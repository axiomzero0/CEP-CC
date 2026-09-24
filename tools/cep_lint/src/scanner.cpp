// CEP:FILE: tools/cep_lint/src/scanner.cpp
// CEP:WHAT: Implementation of the C++ source scanner: comment and string extraction, flat-code construction, and comment-block assembly.
// CEP:WHY: All rules consume the views built here, so lexical correctness is concentrated in one audited place (CEP&CC 14.6).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Unterminated literals or comments return positioned errors; malformed files are never guessed at (Law 6).
// CEP:ASSUMES: C++26 literal grammar; preprocessor lines start with '#'; raw string prefixes are the five standard forms.
// CEP:COST: Offline tool; single pass, linear time and memory.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#include "scanner.hpp"

#include <array>
#include <string_view>

namespace cep::lint {
namespace {

constexpr std::size_t kMaxRawDelimiterLength = 16;
constexpr std::size_t kMaxRawPrefixLength = 8;
constexpr std::size_t kTwoCharTokenLength = 2;
constexpr std::array kRawStringPrefixes = {
    std::string_view{"R"}, std::string_view{"LR"}, std::string_view{"uR"},
    std::string_view{"UR"}, std::string_view{"u8R"}};

// CEP:WHAT: Decides whether a character can appear in an identifier.
// CEP:WHY: Raw string prefix detection and name extraction need one definition.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: ASCII input.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] bool is_identifier_char(char value) noexcept {
    const bool alpha = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    return alpha || value == '_' || (value >= '0' && value <= '9');
}

[[nodiscard]] bool is_hex_digit(char value) noexcept {
    return (value >= '0' && value <= '9') || (value >= 'a' && value <= 'f') || (value >= 'A' && value <= 'F');
}

[[nodiscard]] bool is_space(char value) noexcept {
    return value == ' ' || value == '\t' || value == '\r' || value == '\n';
}

// CEP:WHAT: Splits text into lines on '\n', keeping a final unterminated line.
// CEP:WHY: Line-based rules and preprocessor detection need the raw line table.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: '\r' remains attached to line ends and is treated as whitespace later.
// CEP:COST: Linear in text size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> result;
    std::string current;
    for (const char value : text) {
        if (value == '\n') {
            result.push_back(current);
            current.clear();
            continue;
        }
        current.push_back(value);
    }
    result.push_back(current);
    return result;
}

// CEP:WHAT: Determines whether a raw line is a preprocessor directive.
// CEP:WHY: String and char literal rules differ on preprocessor lines (header names, error text).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails; blank lines return false.
// CEP:ASSUMES: A directive line's first non-whitespace character is '#'.
// CEP:COST: Constant per line.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] bool line_is_directive(const std::string& line) noexcept {
    for (const char value : line) {
        if (value != ' ' && value != '\t') {
            return value == '#';
        }
    }
    return false;
}

// CEP:WHAT: Extracts the identifier token immediately before a position.
// CEP:WHY: Raw string literals are recognized by their R, LR, uR, UR, or u8R prefix.
// CEP:STATUS: complete
// CEP:FAILURE: Returns an empty view when no identifier precedes the position.
// CEP:ASSUMES: The token is at most kMaxRawPrefixLength characters.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string_view identifier_before(const std::string& text, std::size_t position) noexcept {
    std::size_t begin = position;
    while (begin > 0 && is_identifier_char(text[begin - 1]) && position - begin < kMaxRawPrefixLength) {
        begin = begin - 1;
    }
    return std::string_view{text}.substr(begin, position - begin);
}

// CEP:WHY: Raw string recognition must not treat arbitrary identifiers as prefixes.
// CEP:WHAT: Decides whether a token is a raw string literal prefix.
// CEP:WHY: Only the five standard R-forms start raw strings.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Token length is bounded by kMaxRawPrefixLength.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] bool is_raw_string_prefix(std::string_view token) noexcept {
    for (const std::string_view prefix : kRawStringPrefixes) {
        if (token == prefix) {
            return true;
        }
    }
    return false;
}

// CEP:WHAT: Scanner state shared across the single pass.
// CEP:WHY: Keeping position, line, and whitespace context in one struct makes the pass explicit and reviewable (Law 5).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Errors are reported by the owning pass function.
// CEP:ASSUMES: text outlives the scan.
// CEP:COST: Offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
class SourceScanner {
public:
    explicit SourceScanner(const std::string& text) noexcept : text_{text} {}

    // CEP:WHAT: Runs the full scan and assembles comment blocks.
    // CEP:WHY: This is the only entry point; all views are produced atomically.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on unterminated literals or comments.
    // CEP:ASSUMES: Input is a well-formed C++ file at the lexical level.
    // CEP:COST: Single pass, linear time.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<SourceView, ScanError> run() && {
        view_.lines = split_lines(text_);
        directives_.reserve(view_.lines.size());
        for (const std::string& line : view_.lines) {
            directives_.push_back(line_is_directive(line));
        }
        while (position_ < text_.size()) {
            const std::expected<bool, ScanError> step = consume_one();
            if (!step.has_value()) {
                return std::unexpected(step.error());
            }
        }
        assemble_blocks();
        return std::move(view_);
    }

private:
    [[nodiscard]] ScanError fail(const std::string& detail) const noexcept {
        return ScanError{detail, line_};
    }

    [[nodiscard]] bool at(std::size_t offset) const noexcept { return position_ + offset < text_.size(); }

    [[nodiscard]] char peek(std::size_t offset) const noexcept { return text_[position_ + offset]; }

    [[nodiscard]] bool on_directive_line() const noexcept {
        if (line_ <= directives_.size()) {
            return directives_[line_ - 1];
        }
        return false;
    }

    // CEP:WHAT: Emits one code character to the flat view.
    // CEP:WHY: The flat view is the shared structural input for all rules.
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails.
    // CEP:ASSUMES: The character came from code state.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    
    void emit(char value) {
        view_.flat.push_back(value);
        view_.flat_line.push_back(line_);
    }

    // CEP:WHAT: Emits one space for a removed comment or literal.
    // CEP:WHY: Removed regions must occupy flat positions so later code keeps its offset.
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails.
    // CEP:ASSUMES: The current line is the region's start line.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    
    void emit_space() {
        view_.flat.push_back(' ');
        view_.flat_line.push_back(line_);
    }

    // CEP:WHAT: Consumes one lexical unit: comment, string, char literal, or one code character.
    // CEP:WHY: Unit-at-a-time dispatch keeps every literal form explicit (Law 5).
    // CEP:STATUS: complete
    // CEP:FAILURE: Delegates failures from literal scanning.
    // CEP:ASSUMES: position_ is valid.
    // CEP:COST: Constant per unit except literal spans.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, ScanError> consume_one() {
        const char current = text_[position_];
        if (current == '/' && at(1) && peek(1) == '/') {
            consume_line_comment();
            return true;
        }
        if (current == '/' && at(1) && peek(1) == '*') {
            return consume_block_comment();
        }
        if (current == '"') {
            return consume_string();
        }
        if (current == '\'') {
            return consume_char_or_separator();
        }
        emit(current);
        if (current == '\n') {
            line_ = line_ + 1;
            only_whitespace_so_far_ = true;
        } else if (!is_space(current)) {
            only_whitespace_so_far_ = false;
        }
        position_ = position_ + 1;
        return true;
    }

    // CEP:WHAT: Consumes a '//' comment and records its fragment.
    // CEP:WHY: Comment text feeds marker rules and block assembly.
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails; ends at newline or end of input.
    // CEP:ASSUMES: The '//' marker was matched.
    // CEP:COST: Linear in comment length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    void consume_line_comment() {
        const bool full_line = only_whitespace_so_far_;
        std::string content;
        std::size_t scan = position_ + kTwoCharTokenLength;
        while (scan < text_.size() && text_[scan] != '\n') {
            content.push_back(text_[scan]);
            scan = scan + 1;
        }
        if (!content.empty() && content.back() == '\r') {
            content.pop_back();
        }
        view_.comment_fragments.push_back(CommentFragment{std::move(content), line_, full_line});
        emit_space();
        position_ = scan;
    }

    // CEP:WHAT: Consumes a '/* */' comment, recording one fragment per covered line.
    // CEP:WHY: Block comments must map to exact lines for schema checks.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected when the comment is unterminated.
    // CEP:ASSUMES: The '/*' marker was matched.
    // CEP:COST: Linear in comment length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, ScanError> consume_block_comment() {
        const std::size_t start_line = line_;
        const bool starts_full = only_whitespace_so_far_;
        std::string content;
        std::size_t scan = position_ + kTwoCharTokenLength;
        while (scan < text_.size() && !(text_[scan] == '*' && scan + 1 < text_.size() && text_[scan + 1] == '/')) {
            content.push_back(text_[scan]);
            if (text_[scan] == '\n') {
                line_ = line_ + 1;
            }
            scan = scan + 1;
        }
        if (scan + 1 >= text_.size()) {
            return std::unexpected(fail("unterminated block comment"));
        }
        scan = scan + kTwoCharTokenLength;
        bool ends_clean = true;
        for (std::size_t rest = scan; rest < text_.size() && text_[rest] != '\n'; rest = rest + 1) {
            if (text_[rest] != ' ' && text_[rest] != '\t' && text_[rest] != '\r') {
                ends_clean = false;
                break;
            }
        }
        const std::size_t saved_line = line_;
        std::vector<std::string> comment_lines = split_lines(content);
        for (std::size_t index = 0; index < comment_lines.size(); index = index + 1) {
            const bool first = index == 0;
            const bool last = index + 1 == comment_lines.size();
            bool full = true;
            if (first && last) {
                full = starts_full && ends_clean;
            } else if (first) {
                full = starts_full;
            } else if (last) {
                full = ends_clean;
            }
            std::string fragment_text = comment_lines[index];
            if (!fragment_text.empty() && fragment_text.back() == '\r') {
                fragment_text.pop_back();
            }
            view_.comment_fragments.push_back(
                CommentFragment{std::move(fragment_text), start_line + index, full});
        }
        line_ = saved_line;
        emit_space();
        position_ = scan;
        return true;
    }

    // CEP:WHAT: Consumes a string literal, a raw string, or a preprocessor-line quoted span.
    // CEP:WHY: Literal content feeds string rules; preprocessor quotes are skipped as header names or message text.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected for unterminated strings or malformed raw strings.
    // CEP:ASSUMES: Raw string prefixes are the five standard forms; ordinary strings do not span lines.
    // CEP:COST: Linear in literal length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, ScanError> consume_string() {
        if (on_directive_line()) {
            std::size_t scan = position_ + 1;
            while (scan < text_.size() && text_[scan] != '"' && text_[scan] != '\n') {
                scan = scan + 1;
            }
            position_ = scan < text_.size() && text_[scan] == '"' ? scan + 1 : scan;
            emit_space();
            only_whitespace_so_far_ = false;
            return true;
        }
        const std::string_view prefix = identifier_before(text_, position_);
        if (is_raw_string_prefix(prefix)) {
            return consume_raw_string();
        }
        const std::size_t start_line = line_;
        std::string content;
        std::size_t scan = position_ + 1;
        while (scan < text_.size() && text_[scan] != '"') {
            if (text_[scan] == '\\' && scan + 1 < text_.size()) {
                content.push_back(text_[scan]);
                content.push_back(text_[scan + 1]);
                scan = scan + kTwoCharTokenLength;
                continue;
            }
            if (text_[scan] == '\n') {
                return std::unexpected(fail("unterminated string literal"));
            }
            content.push_back(text_[scan]);
            scan = scan + 1;
        }
        if (scan >= text_.size()) {
            return std::unexpected(fail("unterminated string literal"));
        }
        view_.strings.push_back(StringLiteral{std::move(content), start_line});
        emit_space();
        only_whitespace_so_far_ = false;
        position_ = scan + 1;
        return true;
    }

    // CEP:WHAT: Consumes a raw string literal with its delimiter.
    // CEP:WHY: Raw strings hide comment markers and braces from structural checks; they must be removed as one unit.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected for a malformed opener or unterminated raw string.
    // CEP:ASSUMES: The R-form prefix was recognized; the closer is ')delimiter"'.
    // CEP:COST: Linear in literal length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, ScanError> consume_raw_string() {
        const std::size_t start_line = line_;
        std::string delimiter;
        std::size_t scan = position_ + 1;
        while (scan < text_.size() && text_[scan] != '(' && delimiter.size() < kMaxRawDelimiterLength) {
            delimiter.push_back(text_[scan]);
            scan = scan + 1;
        }
        if (scan >= text_.size() || text_[scan] != '(') {
            return std::unexpected(fail("malformed raw string opener"));
        }
        scan = scan + 1;
        std::string closer;
        closer.push_back(')');
        closer.append(delimiter);
        closer.push_back('"');
        const std::size_t content_begin = scan;
        const std::size_t content_end = text_.find(closer, scan);
        if (content_end == std::string::npos) {
            return std::unexpected(fail("unterminated raw string literal"));
        }
        std::string content = text_.substr(content_begin, content_end - content_begin);
        for (const char value : content) {
            if (value == '\n') {
                line_ = line_ + 1;
            }
        }
        view_.strings.push_back(StringLiteral{std::move(content), start_line});
        emit_space();
        only_whitespace_so_far_ = false;
        position_ = content_end + closer.size();
        return true;
    }

    // CEP:WHAT: Consumes a char literal, or passes a digit separator through.
    // CEP:WHY: Digit separators ('1'000') and char literals ('a') must be disambiguated so both flat code and numeric rules stay correct.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected for an unterminated char literal.
    // CEP:ASSUMES: A quote followed by a hex digit and then a non-quote is a separator.
    // CEP:COST: Constant or literal length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, ScanError> consume_char_or_separator() {
        if (on_directive_line()) {
            emit(text_[position_]);
            only_whitespace_so_far_ = false;
            position_ = position_ + 1;
            return true;
        }
        const char next = at(1) ? peek(1) : '\0';
        const char after = at(2) ? peek(2) : '\0';
        if (is_hex_digit(next) && after != '\'') {
            emit(text_[position_]);
            position_ = position_ + 1;
            return true;
        }
        std::size_t scan = position_ + 1;
        while (scan < text_.size() && text_[scan] != '\'') {
            if (text_[scan] == '\\' && scan + 1 < text_.size()) {
                scan = scan + kTwoCharTokenLength;
                continue;
            }
            if (text_[scan] == '\n') {
                return std::unexpected(fail("unterminated character literal"));
            }
            scan = scan + 1;
        }
        if (scan >= text_.size()) {
            return std::unexpected(fail("unterminated character literal"));
        }
        emit_space();
        only_whitespace_so_far_ = false;
        position_ = scan + 1;
        return true;
    }

    // CEP:WHAT: Merges contiguous full-line comment fragments into blocks.
    // CEP:WHY: The CEP comment schema (CEP&CC 10.2) is defined over blocks such as file headers and function documentation.
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails; fragments without full-line coverage are skipped.
    // CEP:ASSUMES: Fragments are recorded in source order.
    // CEP:COST: Linear in fragment count.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    void assemble_blocks() {
        std::size_t index = 0;
        while (index < view_.comment_fragments.size()) {
            const CommentFragment& first = view_.comment_fragments[index];
            if (!first.full_line) {
                index = index + 1;
                continue;
            }
            CommentBlock block;
            block.text = first.text;
            block.start_line = first.line;
            block.end_line = first.line;
            std::size_t next = index + 1;
            while (next < view_.comment_fragments.size() && view_.comment_fragments[next].full_line &&
                   view_.comment_fragments[next].line == block.end_line + 1) {
                block.text.push_back('\n');
                block.text.append(view_.comment_fragments[next].text);
                block.end_line = view_.comment_fragments[next].line;
                next = next + 1;
            }
            view_.comment_blocks.push_back(std::move(block));
            index = next;
        }
    }

    const std::string& text_;
    SourceView view_;
    std::vector<bool> directives_;
    std::size_t position_{0};
    std::size_t line_{1};
    bool only_whitespace_so_far_{true};
};

}  // namespace

// CEP:WHAT: Scans one C++ translation unit text.
// CEP:WHY: Public entry producing every view the checks consume.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected on unterminated literals or comments.
// CEP:ASSUMES: UTF-8 input with C++26 literal grammar.
// CEP:COST: Single pass, linear time.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::expected<SourceView, ScanError> scan_source(const std::string& text) {
    SourceScanner scanner{text};
    return std::move(scanner).run();
}

}  // namespace cep::lint
