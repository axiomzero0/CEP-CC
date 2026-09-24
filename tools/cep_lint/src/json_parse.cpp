// CEP:FILE: tools/cep_lint/src/json_parse.cpp
// CEP:WHAT: Implementation of the recursive-descent JSON parser.
// CEP:WHY: Self-contained, deterministic, exception-free parsing keeps the tool auditable and locale-independent (CEP&CC 6.5, 9.62).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Every malformed-input case returns a positioned JsonError; the only recursion is depth-bounded.
// CEP:ASSUMES: UTF-8 input; '\u' escapes follow RFC 8259 including surrogate pairs.
// CEP:COST: Offline tool; single pass, linear time, bounded stack.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#include "json_parse.hpp"

#include <charconv>
#include <limits>
#include <string_view>

namespace cep::lint {
namespace {

constexpr std::size_t kMaxNestingDepth = 128;
constexpr long long kDecimalBase = 10;
constexpr long long kHexadecimalBase = 16;
constexpr long long kUtf16HighSurrogateBegin = 0xD800;
constexpr long long kUtf16HighSurrogateEnd = 0xDBFF;
constexpr long long kUtf16LowSurrogateBegin = 0xDC00;
constexpr long long kUtf16LowSurrogateEnd = 0xDFFF;
constexpr long long kUtf16SurrogateOffset = 0x10000;
constexpr std::size_t kUnicodeEscapeLength = 4;

// CEP:WHAT: Cursor over the JSON text with 1-based line and column tracking.
// CEP:WHY: Positioned errors are required by Law 6 and CEP&CC 38.15.
// CEP:STATUS: complete
// CEP:FAILURE: Methods never fail; callers validate characters before consuming.
// CEP:ASSUMES: The text outlives the cursor.
// CEP:COST: Constant-time operations; offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
class JsonCursor {
public:
    explicit JsonCursor(std::string_view text) noexcept : text_{text} {}

    [[nodiscard]] bool at_end() const noexcept { return position_ >= text_.size(); }

    [[nodiscard]] char peek() const noexcept { return text_[position_]; }

    // CEP:WHAT: Advances the cursor one character, tracking line and column.
    // CEP:WHY: Positioned errors require exact line and column bookkeeping.
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails; out-of-range calls are no-ops.
    // CEP:ASSUMES: The cursor is inside the text.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    
    void advance() noexcept {
        if (position_ < text_.size()) {
            if (text_[position_] == '\n') {
                line_ = line_ + 1;
                column_ = 1;
            } else {
                column_ = column_ + 1;
            }
            position_ = position_ + 1;
        }
    }

    [[nodiscard]] std::size_t line() const noexcept { return line_; }

    [[nodiscard]] std::size_t column() const noexcept { return column_; }

    [[nodiscard]] std::size_t position() const noexcept { return position_; }

    // CEP:WHAT: Skips spaces, tabs, carriage returns, and newlines.
    // CEP:WHY: JSON whitespace must not affect parsing (RFC 8259).
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails.
    // CEP:ASSUMES: Whitespace set matches RFC 8259.
    // CEP:COST: Linear in skipped run length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
    void skip_whitespace() noexcept {
        while (position_ < text_.size()) {
            const char current = text_[position_];
            const bool is_space = current == ' ' || current == '\t' || current == '\n' || current == '\r';
            if (!is_space) {
                break;
            }
            advance();
        }
    }

    // CEP:WHAT: Computes a stable detail string containing the next few input characters.
    // CEP:WHY: Error context must be reviewable without leaking large input spans (Law 6).
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails.
    // CEP:ASSUMES: Detail length is bounded by kErrorContextLength.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
    [[nodiscard]] std::string context() const {
        constexpr std::size_t kErrorContextLength = 16;
        std::string detail;
        const std::size_t remaining = text_.size() - position_;
        const std::size_t length = remaining < kErrorContextLength ? remaining : kErrorContextLength;
        for (std::size_t index = 0; index < length; index = index + 1) {
            detail.push_back(text_[position_ + index]);
        }
        return detail;
    }

private:
    std::string_view text_;
    std::size_t position_{0};
    std::size_t line_{1};
    std::size_t column_{1};
};

// CEP:WHAT: Parser state: cursor plus current nesting depth.
// CEP:WHY: Depth bounding is required so recursion cannot exhaust the stack (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Depth violations produce positioned errors.
// CEP:ASSUMES: kMaxNestingDepth is far above any sane configuration size.
// CEP:COST: Offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
class JsonParser {
public:
    explicit JsonParser(std::string_view text) noexcept : cursor_text_{text}, cursor_{text} {}

    // CEP:WHAT: Parses one complete document and rejects trailing content.
    // CEP:WHY: Root-level entry point; partial parses would silently misconfigure the tool (Law 2).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on any syntax error or trailing content.
    // CEP:ASSUMES: Document is a single JSON value.
    // CEP:COST: Linear in document size.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<Json, JsonError> parse_document() {
        const std::expected<Json, JsonError> root = parse_value();
        if (!root.has_value()) {
            return std::unexpected(root.error());
        }
        cursor_.skip_whitespace();
        if (!cursor_.at_end()) {
            return std::unexpected(error("trailing content after JSON value"));
        }
        return root;
    }

private:
    [[nodiscard]] JsonError error(const std::string& detail) const noexcept {
        return JsonError{cursor_.line(), cursor_.column(), detail};
    }

    // CEP:WHAT: Parses a value based on its leading character.
    // CEP:WHY: JSON values are syntactically disjoint; dispatch keeps each case small and auditable.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected for an unrecognized leading character.
    // CEP:ASSUMES: Whitespace was already skipped by callers.
    // CEP:COST: Constant dispatch, then delegated parsing.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<Json, JsonError> parse_value() {
        if (depth_ > kMaxNestingDepth) {
            return std::unexpected(error("nesting depth exceeds limit"));
        }
        cursor_.skip_whitespace();
        if (cursor_.at_end()) {
            return std::unexpected(error("unexpected end of input"));
        }
        const char lead = cursor_.peek();
        if (lead == '{') {
            return parse_object();
        }
        if (lead == '[') {
            return parse_array();
        }
        if (lead == '"') {
            const std::expected<std::string, JsonError> text = parse_string();
            if (!text.has_value()) {
                return std::unexpected(text.error());
            }
            return Json{text.value()};
        }
        if (lead == 't' || lead == 'f' || lead == 'n') {
            return parse_keyword();
        }
        const bool numeric = lead == '-' || (lead >= '0' && lead <= '9');
        if (numeric) {
            return parse_number();
        }
        return std::unexpected(error("unexpected character: " + cursor_.context()));
    }

    // CEP:WHAT: Parses the literals true, false, and null.
    // CEP:WHY: Keyword literals are fixed-length; exact matching prevents silent acceptance of prefixes (Law 2).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on truncation or mismatch.
    // CEP:ASSUMES: None.
    // CEP:COST: Constant work per keyword.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
    [[nodiscard]] std::expected<Json, JsonError> parse_keyword() {
        for (const std::string_view keyword : {std::string_view{"true"}, std::string_view{"false"},
                                               std::string_view{"null"}}) {
            if (matches(keyword)) {
                if (keyword == std::string_view{"true"}) {
                    return Json{true};
                }
                if (keyword == std::string_view{"false"}) {
                    return Json{false};
                }
                return Json{};
            }
        }
        return std::unexpected(error("invalid literal: " + cursor_.context()));
    }

    // CEP:WHAT: Consumes the input if it equals the given keyword.
    // CEP:WHY: Literal matching must be exact so prefixes cannot silently pass (Law 2).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns false without consuming on mismatch.
    // CEP:ASSUMES: The cursor is positioned at the literal start.
    // CEP:COST: Linear in keyword length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
    [[nodiscard]] bool matches(std::string_view keyword) noexcept {
        for (const char expected : keyword) {
            if (cursor_.at_end() || cursor_.peek() != expected) {
                return false;
            }
            cursor_.advance();
        }
        return true;
    }

    // CEP:WHAT: Parses an object with duplicate-key detection.
    // CEP:WHY: Duplicate keys make configuration ambiguous; ambiguity is a silent assumption (Law 2).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on missing braces, colons, commas, or duplicate keys.
    // CEP:ASSUMES: Member order is preserved.
    // CEP:COST: Linear in object size; duplicate check is quadratic in member count (bounded by depth limit).
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<Json, JsonError> parse_object() {
        Json::Object members;
        cursor_.advance();
        cursor_.skip_whitespace();
        if (!cursor_.at_end() && cursor_.peek() == '}') {
            cursor_.advance();
            return Json{std::move(members)};
        }
        while (true) {
            cursor_.skip_whitespace();
            if (cursor_.at_end() || cursor_.peek() != '"') {
                return std::unexpected(error("expected object key string"));
            }
            const std::expected<std::string, JsonError> key = parse_string();
            if (!key.has_value()) {
                return std::unexpected(key.error());
            }
            for (const Json::Member& member : members) {
                if (member.first == key.value()) {
                    return std::unexpected(error("duplicate object key: " + key.value()));
                }
            }
            cursor_.skip_whitespace();
            if (cursor_.at_end() || cursor_.peek() != ':') {
                return std::unexpected(error("expected ':' after object key"));
            }
            cursor_.advance();
            depth_ = depth_ + 1;
            const std::expected<Json, JsonError> value = parse_value();
            depth_ = depth_ - 1;
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            members.emplace_back(key.value(), value.value());
            cursor_.skip_whitespace();
            if (cursor_.at_end()) {
                return std::unexpected(error("unterminated object"));
            }
            if (cursor_.peek() == ',') {
                cursor_.advance();
                continue;
            }
            if (cursor_.peek() == '}') {
                cursor_.advance();
                return Json{std::move(members)};
            }
            return std::unexpected(error("expected ',' or '}' in object"));
        }
    }

    // CEP:WHAT: Parses an array of values.
    // CEP:WHY: Rule tables and token lists are arrays; strict comma/bracket handling keeps them unambiguous.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on missing brackets, commas, or nested value errors.
    // CEP:ASSUMES: None.
    // CEP:COST: Linear in array size.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<Json, JsonError> parse_array() {
        Json::Array items;
        cursor_.advance();
        cursor_.skip_whitespace();
        if (!cursor_.at_end() && cursor_.peek() == ']') {
            cursor_.advance();
            return Json{std::move(items)};
        }
        while (true) {
            depth_ = depth_ + 1;
            const std::expected<Json, JsonError> value = parse_value();
            depth_ = depth_ - 1;
            if (!value.has_value()) {
                return std::unexpected(value.error());
            }
            items.push_back(value.value());
            cursor_.skip_whitespace();
            if (cursor_.at_end()) {
                return std::unexpected(error("unterminated array"));
            }
            if (cursor_.peek() == ',') {
                cursor_.advance();
                continue;
            }
            if (cursor_.peek() == ']') {
                cursor_.advance();
                return Json{std::move(items)};
            }
            return std::unexpected(error("expected ',' or ']' in array"));
        }
    }

    // CEP:WHAT: Parses a string with escape sequences including surrogate pairs.
    // CEP:WHY: Configuration messages contain arbitrary text; escape handling must be complete, not approximate (CEP&CC 28.3 determinism).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on raw control characters, invalid escapes, bad hex digits, or lone surrogates.
    // CEP:ASSUMES: UTF-8 output; surrogate pairs follow RFC 8259.
    // CEP:COST: Linear in string length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<std::string, JsonError> parse_string() {
        std::string result;
        cursor_.advance();
        while (true) {
            if (cursor_.at_end()) {
                return std::unexpected(error("unterminated string"));
            }
            const char current = cursor_.peek();
            if (current == '"') {
                cursor_.advance();
                return result;
            }
            if (current == '\\') {
                cursor_.advance();
                const std::expected<bool, JsonError> escape = parse_escape(result);
                if (!escape.has_value()) {
                    return std::unexpected(escape.error());
                }
                continue;
            }
            constexpr unsigned char kJsonControlLimit = 0x20;
            if (static_cast<unsigned char>(current) < kJsonControlLimit) {
                return std::unexpected(error("raw control character in string"));
            }
            result.push_back(current);
            cursor_.advance();
        }
    }

    // CEP:WHAT: Parses one escape sequence into the result buffer.
    // CEP:WHY: Escape handling is the classic parser defect source; full coverage is required by Law 6.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected (false) on unknown escapes or malformed '\u' sequences.
    // CEP:ASSUMES: The leading backslash was already consumed.
    // CEP:COST: Constant per escape; '\u' is bounded by kUnicodeEscapeLength hex digits.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, JsonError> parse_escape(std::string& result) {
        if (cursor_.at_end()) {
            return std::unexpected(error("unterminated escape"));
        }
        const char code = cursor_.peek();
        cursor_.advance();
        switch (code) {
            case '"':
                result.push_back('"');
                return true;
            case '\\':
                result.push_back('\\');
                return true;
            case '/':
                result.push_back('/');
                return true;
            case 'b':
                result.push_back('\b');
                return true;
            case 'f':
                result.push_back('\f');
                return true;
            case 'n':
                result.push_back('\n');
                return true;
            case 'r':
                result.push_back('\r');
                return true;
            case 't':
                result.push_back('\t');
                return true;
            case 'u':
                return parse_unicode_escape(result);
            default:
                return std::unexpected(error("invalid escape character"));
        }
    }

    // CEP:WHAT: Parses a '\u' escape and combines surrogate pairs.
    // CEP:WHY: Lone surrogates produce ill-formed UTF-8; rejecting them keeps output deterministic and valid.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on short hex runs, non-hex digits, or unpaired surrogates.
    // CEP:ASSUMES: The 'u' was consumed; exactly kUnicodeEscapeLength hex digits follow.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<bool, JsonError> parse_unicode_escape(std::string& result) {
        const std::expected<long long, JsonError> first = parse_hex_number();
        if (!first.has_value()) {
            return std::unexpected(first.error());
        }
        long long code_point = first.value();
        const bool is_high_surrogate =
            code_point >= kUtf16HighSurrogateBegin && code_point <= kUtf16HighSurrogateEnd;
        if (is_high_surrogate) {
            if (cursor_.at_end() || cursor_.peek() != '\\' ) {
                return std::unexpected(error("high surrogate without low surrogate"));
            }
            cursor_.advance();
            if (cursor_.at_end() || cursor_.peek() != 'u') {
                return std::unexpected(error("high surrogate without low surrogate"));
            }
            cursor_.advance();
            const std::expected<long long, JsonError> second = parse_hex_number();
            if (!second.has_value()) {
                return std::unexpected(second.error());
            }
            const long long low = second.value();
            const bool is_low_surrogate = low >= kUtf16LowSurrogateBegin && low <= kUtf16LowSurrogateEnd;
            if (!is_low_surrogate) {
                return std::unexpected(error("invalid low surrogate"));
            }
            code_point = kUtf16SurrogateOffset +
                         (code_point - kUtf16HighSurrogateBegin) * kHexadecimalBase * kHexadecimalBase +
                         (low - kUtf16LowSurrogateBegin);
        } else {
            const bool is_lone_low_surrogate =
                code_point >= kUtf16LowSurrogateBegin && code_point <= kUtf16LowSurrogateEnd;
            if (is_lone_low_surrogate) {
                return std::unexpected(error("lone low surrogate"));
            }
        }
        append_utf8(result, code_point);
        return true;
    }

    // CEP:WHAT: Reads exactly kUnicodeEscapeLength hexadecimal digits.
    // CEP:WHY: Fixed-length hex runs prevent run-together escape parsing errors.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected on a short run or non-hex digit.
    // CEP:ASSUMES: None.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<long long, JsonError> parse_hex_number() {
        long long value = 0;
        for (std::size_t index = 0; index < kUnicodeEscapeLength; index = index + 1) {
            if (cursor_.at_end()) {
                return std::unexpected(error("truncated unicode escape"));
            }
            const long long digit = hex_digit_value(cursor_.peek());
            if (digit < 0) {
                return std::unexpected(error("invalid hex digit in unicode escape"));
            }
            value = value * kHexadecimalBase + digit;
            cursor_.advance();
        }
        return value;
    }

    // CEP:WHAT: Converts one hexadecimal digit to its value.
    // CEP:WHY: Escape parsing needs digit values without locale dependence.
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns -1 for non-hex input; callers convert it to an error.
    // CEP:ASSUMES: ASCII input.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
    [[nodiscard]] static long long hex_digit_value(char digit) noexcept {
        if (digit >= '0' && digit <= '9') {
            return digit - '0';
        }
        if (digit >= 'a' && digit <= 'f') {
            return digit - 'a' + kDecimalBase;
        }
        if (digit >= 'A' && digit <= 'F') {
            return digit - 'A' + kDecimalBase;
        }
        return -1;
    }

    // CEP:WHAT: Appends a code point as UTF-8.
    // CEP:WHY: Output must be valid UTF-8 regardless of escape form (CEP&CC 8.44).
    // CEP:STATUS: complete
    // CEP:FAILURE: Never fails; code points are bounded by the surrogate math above.
    // CEP:ASSUMES: code_point is at most 0x10FFFF.
    // CEP:COST: Constant work.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    static void append_utf8(std::string& result, long long code_point) {
        constexpr long long kUtf8TwoByteBegin = 0x80;
        constexpr long long kUtf8ThreeByteBegin = 0x800;
        constexpr long long kUtf8FourByteBegin = 0x10000;
        constexpr long long kUtf8ContinuationMask = 0x3F;
        constexpr long long kUtf8TwoByteLead = 0xC0;
        constexpr long long kUtf8ThreeByteLead = 0xE0;
        constexpr long long kUtf8FourByteLead = 0xF0;
        constexpr long long kUtf8ContinuationLead = 0x80;
        constexpr int kUtf8ShiftOne = 6;
        constexpr int kUtf8ShiftTwo = 12;
        constexpr int kUtf8ShiftThree = 18;
        if (code_point < kUtf8TwoByteBegin) {
            result.push_back(static_cast<char>(code_point));
            return;
        }
        if (code_point < kUtf8ThreeByteBegin) {
            result.push_back(static_cast<char>(kUtf8TwoByteLead | (code_point >> kUtf8ShiftOne)));
            result.push_back(static_cast<char>(kUtf8ContinuationLead | (code_point & kUtf8ContinuationMask)));
            return;
        }
        if (code_point < kUtf8FourByteBegin) {
            result.push_back(static_cast<char>(kUtf8ThreeByteLead | (code_point >> kUtf8ShiftTwo)));
            result.push_back(static_cast<char>(kUtf8ContinuationLead |
                                              ((code_point >> kUtf8ShiftOne) & kUtf8ContinuationMask)));
            result.push_back(static_cast<char>(kUtf8ContinuationLead | (code_point & kUtf8ContinuationMask)));
            return;
        }
        result.push_back(static_cast<char>(kUtf8FourByteLead | (code_point >> kUtf8ShiftThree)));
        result.push_back(
            static_cast<char>(kUtf8ContinuationLead | ((code_point >> kUtf8ShiftTwo) & kUtf8ContinuationMask)));
        result.push_back(
            static_cast<char>(kUtf8ContinuationLead | ((code_point >> kUtf8ShiftOne) & kUtf8ContinuationMask)));
        result.push_back(static_cast<char>(kUtf8ContinuationLead | (code_point & kUtf8ContinuationMask)));
    }

    // CEP:WHAT: Parses a number using std::from_chars over the matched literal span.
    // CEP:WHY: from_chars is locale-independent and exception-free (CEP&CC 6.5, 9.62).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns unexpected when the literal is malformed or an integer exceeds long long range.
    // CEP:ASSUMES: Numbers with '.', 'e', or 'E' are floating point; others are integers.
    // CEP:COST: Linear in literal length.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::expected<Json, JsonError> parse_number() {
        const std::size_t begin = cursor_.position();
        if (!cursor_.at_end() && cursor_.peek() == '-') {
            cursor_.advance();
        }
        bool floating = false;
        while (!cursor_.at_end()) {
            const char current = cursor_.peek();
            const bool is_digit = current >= '0' && current <= '9';
            const bool is_float_marker = current == '.' || current == 'e' || current == 'E';
            const bool is_sign = current == '+' || current == '-';
            const bool after_exponent = exponent_seen_ && is_sign;
            if (is_digit) {
                cursor_.advance();
                continue;
            }
            if (is_float_marker) {
                floating = true;
                if (current == 'e' || current == 'E') {
                    exponent_seen_ = true;
                }
                cursor_.advance();
                continue;
            }
            if (after_exponent) {
                cursor_.advance();
                exponent_seen_ = false;
                continue;
            }
            break;
        }
        const std::size_t end = cursor_.position();
        const std::string_view literal = std::string_view{cursor_text_}.substr(begin, end - begin);
        if (literal.empty() || literal == std::string_view{"-"}) {
            return std::unexpected(error("invalid number"));
        }
        if (floating) {
            double value = 0.0;
            const char* first = literal.data();
            const char* last = literal.data() + literal.size();
            const std::from_chars_result parsed = std::from_chars(first, last, value);
            if (parsed.ec != std::errc{} || parsed.ptr != last) {
                return std::unexpected(error("invalid floating-point number"));
            }
            return Json{value};
        }
        long long value = 0;
        const char* first = literal.data();
        const char* last = literal.data() + literal.size();
        const std::from_chars_result parsed = std::from_chars(first, last, value);
        if (parsed.ec != std::errc{} || parsed.ptr != last) {
            return std::unexpected(error("integer out of range or malformed"));
        }
        return Json{value};
    }

private:
    std::string_view cursor_text_;
    JsonCursor cursor_;
    std::size_t depth_{0};
    bool exponent_seen_{false};
};

}  // namespace

// CEP:WHAT: Parses one complete JSON document.
// CEP:WHY: Public entry point for configuration and manifest loading.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected on any syntax error.
// CEP:ASSUMES: Depth is bounded by kMaxNestingDepth.
// CEP:COST: Linear in document size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::expected<Json, JsonError> parse_json(const std::string& text) {
    JsonParser parser{std::string_view{text}};
    return parser.parse_document();
}

}  // namespace cep::lint
