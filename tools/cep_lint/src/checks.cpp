// CEP:FILE: tools/cep_lint/src/checks.cpp
// CEP:WHAT: Implementation of the nine check primitives, the function-definition extractor, block association, and statement context joining.
// CEP:WHY: All policy is data; these mechanisms apply it uniformly so rule changes never require code changes (Law 7).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: compile_rule fails loudly on bad policy; run_check appends issues; the document rule propagates read failures.
// CEP:ASSUMES: std::regex ECMAScript grammar; heads of function definitions end with a parenthesized parameter list.
// CEP:COST: Offline tool; linear per file and rule.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#include "checks.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <optional>

namespace cep::lint {
namespace {

constexpr std::string_view kNumericLiteralPattern =
    "\\b(?:0[xX][0-9a-fA-F']+|[0-9][0-9']*(?:\\.[0-9']*)?|\\.[0-9][0-9']*)(?:[eE][+-]?[0-9]+)?[uUlLfFzZ]{0,3}\\b";
constexpr std::string_view kDefinePattern = "^\\s*#\\s*define\\s+([A-Za-z_][A-Za-z0-9_]*)";
constexpr std::string_view kDefaultGapPattern = "^\\s*[^;{}()#]*$";
constexpr std::string_view kFileHeaderField = "CEP:FILE:";
constexpr std::size_t kMaxAssociationGapLines = 8;
constexpr std::size_t kMaxStatementJoinLines = 12;
constexpr std::size_t kMaxSuffixLength = 3;
constexpr std::size_t kTwoCharTokenLength = 2;
constexpr int kBootInvalidPattern = 8;
constexpr int kBootMissingMessage = 4;

// CEP:WHAT: One recognized function definition in flat code.
// CEP:WHY: Block, stub, and naming rules need name, position, and body facts from one extractor.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Offsets index into the flat view; lines are 1-based.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct FunctionDefinition {
    std::string name;
    bool is_lambda{false};
    bool is_destructor{false};
    bool is_operator{false};
    std::size_t head_line{0};
    std::size_t body_begin{0};
    std::size_t body_end{0};
    std::size_t statement_count{0};
};

// CEP:WHAT: Trims leading and trailing whitespace from a fragment.
// CEP:WHY: Head and line classification must not depend on surrounding whitespace.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Whitespace is the standard set.
// CEP:COST: Linear in fragment length.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string_view trim(std::string_view text) noexcept {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        begin = begin + 1;
    }
    while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        end = end - 1;
    }
    return text.substr(begin, end - begin);
}

// CEP:WHAT: Trims trailing whitespace from a fragment.
// CEP:WHY: Statement boundaries are detected from line endings.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Whitespace is the standard set.
// CEP:COST: Linear in fragment length.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string_view rtrim(std::string_view text) noexcept {
    std::size_t end = text.size();
    while (end > 0 && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
        end = end - 1;
    }
    return text.substr(0, end);
}

// CEP:WHAT: Collapses whitespace runs so multi-line evidence fits one line.
// CEP:WHY: One-line findings keep output machine-parseable (CEP&CC 38.15).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: None.
// CEP:COST: Linear in text size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string squeeze_whitespace(std::string_view text) {
    std::string result;
    bool pending_space = false;
    for (const char value : text) {
        const bool space = std::isspace(static_cast<unsigned char>(value)) != 0;
        if (space) {
            pending_space = !result.empty();
            continue;
        }
        if (pending_space) {
            result.push_back(' ');
            pending_space = false;
        }
        result.push_back(value);
    }
    return result;
}

// CEP:WHAT: Substitutes '{placeholder}' fields into a message template.
// CEP:WHY: All message text is configuration data (Law 7, Law 8).
// CEP:STATUS: complete
// CEP:FAILURE: Unknown placeholders are left untouched and surface visibly in output.
// CEP:ASSUMES: Placeholders do not nest.
// CEP:COST: Linear in template size times field count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string render_template(std::string template_text,
                                          const std::vector<std::pair<std::string, std::string>>& fields) {
    for (const auto& [key, value] : fields) {
        const std::string token = "{" + key + "}";
        std::size_t position = template_text.find(token);
        while (position != std::string::npos) {
            template_text.replace(position, token.size(), value);
            position = template_text.find(token, position + value.size());
        }
    }
    return template_text;
}

// CEP:WHAT: Reads a string parameter from rule params.
// CEP:WHY: Rule parameters are data; access must be explicit and failure-aware (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional when the key is absent or not a string.
// CEP:ASSUMES: params is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::optional<std::string> string_param(const Json& params, std::string_view key) {
    const std::optional<const Json*> member = params.find(key);
    if (!member.has_value()) {
        return std::nullopt;
    }
    const std::optional<std::string_view> text = member.value()->as_string();
    if (!text.has_value()) {
        return std::nullopt;
    }
    return std::string{text.value()};
}

// CEP:WHAT: Reads an integer parameter from rule params.
// CEP:WHY: Thresholds such as line groups and statement minimums are data.
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional when the key is absent or not an integer.
// CEP:ASSUMES: params is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::optional<long long> int_param(const Json& params, std::string_view key) {
    const std::optional<const Json*> member = params.find(key);
    if (!member.has_value()) {
        return std::nullopt;
    }
    return member.value()->as_integer();
}

// CEP:WHAT: Reads a boolean parameter with a fallback.
// CEP:WHY: Flags such as icase and lambda coverage are optional data.
// CEP:STATUS: complete
// CEP:FAILURE: Absent or mistyped entries fall back to the documented default.
// CEP:ASSUMES: params is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] bool bool_param(const Json& params, std::string_view key, bool fallback) {
    const std::optional<const Json*> member = params.find(key);
    if (!member.has_value()) {
        return fallback;
    }
    const std::optional<bool> flag = member.value()->as_bool();
    return flag.has_value() ? flag.value() : fallback;
}

// CEP:WHAT: Reads a string-array parameter.
// CEP:WHY: Field lists, classes, statuses, and tokens come from configuration.
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional when the key is absent or not an array of strings.
// CEP:ASSUMES: None.
// CEP:COST: Linear in array size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::optional<std::vector<std::string>> string_array_param(const Json& params,
                                                                         std::string_view key) {
    const std::optional<const Json*> member = params.find(key);
    if (!member.has_value()) {
        return std::nullopt;
    }
    const std::optional<const Json::Array*> items = member.value()->as_array();
    if (!items.has_value()) {
        return std::nullopt;
    }
    std::vector<std::string> result;
    for (const Json& element : *items.value()) {
        const std::optional<std::string_view> text = element.as_string();
        if (!text.has_value()) {
            return std::nullopt;
        }
        result.push_back(std::string{text.value()});
    }
    return result;
}

// CEP:WHAT: Reads an integer-array parameter from rule params.
// CEP:WHY: Allow-lists such as permitted literal values are data.
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional when the key is absent or any element is not an integer.
// CEP:ASSUMES: params is a JSON object.
// CEP:COST: Linear in array size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::optional<std::vector<long long>> int_array_param(const Json& params,
                                                                     std::string_view key) {
    const std::optional<const Json*> member = params.find(key);
    if (!member.has_value()) {
        return std::nullopt;
    }
    const std::optional<const Json::Array*> items = member.value()->as_array();
    if (!items.has_value()) {
        return std::nullopt;
    }
    std::vector<long long> result;
    for (const Json& element : *items.value()) {
        const std::optional<long long> number = element.as_integer();
        if (!number.has_value()) {
            return std::nullopt;
        }
        result.push_back(number.value());
    }
    return result;
}

// CEP:WHAT: Compiles one regular expression, converting std::regex_error into a bootstrap error.
// CEP:WHY: This is the sole exception boundary in the tool; failures must be loud and coded (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap error with the pattern text on compile failure.
// CEP:ASSUMES: ECMAScript grammar.
// CEP:COST: Compilation cost per pattern, once per run.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<std::regex, BootError> compile_pattern(std::string_view pattern, bool icase) {
    try {
        const auto flags = icase ? std::regex_constants::icase : std::regex_constants::ECMAScript;
        return std::regex{pattern.begin(), pattern.end(), flags};
    } catch (const std::regex_error& error) {
        return std::unexpected(BootError{kBootInvalidPattern,
                                         "invalid pattern: " + std::string{pattern} + ": " +
                                             error.what()});
    }
}

// CEP:WHAT: Builds a field-value regex such as 'CEP:STATUS:\s*(word)' from a field name and value list.
// CEP:WHY: Field names and status vocabularies are configuration data.
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap error when the assembled pattern is invalid.
// CEP:ASSUMES: Values are plain words without regex metacharacters.
// CEP:COST: One compilation.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<std::regex, BootError> field_value_regex(const std::string& field,
                                                                     const std::vector<std::string>& values) {
    std::string pattern = field + ":\\s*(";
    for (std::size_t index = 0; index < values.size(); index = index + 1) {
        if (index > 0) {
            pattern.append("|");
        }
        pattern.append(values[index]);
    }
    pattern.append(")");
    return compile_pattern(pattern, false);
}

// CEP:WHAT: Extracts the last identifier token of a code fragment.
// CEP:WHY: Function names are the last identifier before the parameter list.
// CEP:STATUS: complete
// CEP:FAILURE: Returns an empty view when no identifier exists.
// CEP:ASSUMES: Fragment is trimmed.
// CEP:COST: Linear in fragment size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
// CEP:WHAT: Decides whether a character can appear in an identifier.
// CEP:WHY: Identifier extraction for raw string prefixes and names needs one definition.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: ASCII input.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] bool is_identifier_char(char value) noexcept {
    const bool alpha = (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z');
    return alpha || value == '_' || (value >= '0' && value <= '9');
}

// CEP:WHAT: Extracts the trailing identifier token of a code fragment.
// CEP:WHY: Function names are the last identifier before the parameter list.
// CEP:STATUS: complete
// CEP:FAILURE: Returns an empty view when no identifier exists.
// CEP:ASSUMES: Fragment is trimmed.
// CEP:COST: Linear in fragment length.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string_view last_identifier(std::string_view fragment) noexcept {
    std::size_t end = fragment.size();
    while (end > 0 && !is_identifier_char(fragment[end - 1])) {
        end = end - 1;
    }
    std::size_t begin = end;
    while (begin > 0 && is_identifier_char(fragment[begin - 1])) {
        begin = begin - 1;
    }
    return fragment.substr(begin, end - begin);
}

// CEP:WHAT: Head classification result for brace candidates.
// CEP:WHY: The extractor must distinguish functions, lambdas, destructors, and operators before rules run.
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: None.
// CEP:COST: Offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct HeadInfo {
    bool is_function{false};
    bool is_lambda{false};
    bool is_destructor{false};
    bool is_operator{false};
    std::string name;
};

constexpr std::array kLeadingSpecifiers = {
    std::string_view{"constexpr"}, std::string_view{"consteval"}, std::string_view{"constinit"},
    std::string_view{"static"},    std::string_view{"inline"},    std::string_view{"extern"},
    std::string_view{"virtual"},   std::string_view{"explicit"},  std::string_view{"friend"},
    std::string_view{"const"},     std::string_view{"unsigned"},  std::string_view{"signed"},
    std::string_view{"typename"}};

constexpr std::array kHeadKeywords = {
    std::string_view{"if"},       std::string_view{"else"},     std::string_view{"for"},
    std::string_view{"while"},    std::string_view{"do"},       std::string_view{"switch"},
    std::string_view{"try"},      std::string_view{"catch"},    std::string_view{"return"},
    std::string_view{"throw"},    std::string_view{"new"},      std::string_view{"delete"},
    std::string_view{"namespace"}, std::string_view{"using"},   std::string_view{"typedef"},
    std::string_view{"struct"},   std::string_view{"class"},    std::string_view{"enum"},
    std::string_view{"union"},    std::string_view{"concept"},  std::string_view{"requires"},
    std::string_view{"static_assert"}, std::string_view{"decltype"}, std::string_view{"sizeof"},
    std::string_view{"alignof"},  std::string_view{"asm"},      std::string_view{"case"},
    std::string_view{"break"},    std::string_view{"continue"}, std::string_view{"goto"},
    std::string_view{"co_await"}, std::string_view{"co_yield"}, std::string_view{"co_return"},
    std::string_view{"operator"}};

template <std::size_t Count>
[[nodiscard]] bool word_in_list(std::string_view word, const std::array<std::string_view, Count>& list) {
    return std::find(list.begin(), list.end(), word) != list.end();
}

[[nodiscard]] bool word_in_specifiers(std::string_view word) {
    return std::find(kLeadingSpecifiers.begin(), kLeadingSpecifiers.end(), word) != kLeadingSpecifiers.end();
}

// CEP:WHAT: Strips leading attribute spans, specifiers, and a template header from a head fragment.
// CEP:WHY: Classification must see the essential signature, not decorations (Law 5).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails; returns what remains.
// CEP:ASSUMES: Attributes and templates appear before the return type.
// CEP:COST: Linear in fragment size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string_view strip_head_decorations(std::string_view fragment) noexcept {
    std::string_view head = fragment;
    bool changed = true;
    while (changed && !head.empty()) {
        changed = false;
        head = trim(head);
        if (head.starts_with(std::string_view{"[["})) {
            const std::size_t close = head.find("]]");
            if (close == std::string_view::npos) {
                break;
            }
            head = head.substr(close + kTwoCharTokenLength);
            changed = true;
            continue;
        }
        const std::size_t word_end = head.find_first_of(" \t\r\n");
        const std::string_view first_word =
            word_end == std::string_view::npos ? head : head.substr(0, word_end);
        if (word_in_specifiers(first_word)) {
            head = word_end == std::string_view::npos ? std::string_view{} : head.substr(word_end);
            changed = true;
            continue;
        }
        if (first_word == std::string_view{"template"} && head.find('<') != std::string_view::npos) {
            std::size_t depth = 0;
            std::size_t scan = head.find('<');
            std::size_t cut = std::string_view::npos;
            for (std::size_t index = scan; index < head.size(); index = index + 1) {
                if (head[index] == '<') {
                    depth = depth + 1;
                } else if (head[index] == '>') {
                    depth = depth - 1;
                    if (depth == 0) {
                        cut = index + 1;
                        break;
                    }
                }
            }
            if (cut != std::string_view::npos) {
                head = head.substr(cut);
                changed = true;
                continue;
            }
        }
    }
    return head;
}

// CEP:WHAT: Classifies a statement head as a function, lambda, destructor, or operator definition.
// CEP:WHY: The extractor feeds block, stub, and naming rules that need exactly these facts.
// CEP:STATUS: complete
// CEP:FAILURE: Returns is_function=false for anything that is not a definition head.
// CEP:ASSUMES: Constructor initializer lists follow the first parameter list after ':'.
// CEP:COST: Linear in head size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] HeadInfo classify_head(std::string_view raw_head) noexcept {
    HeadInfo info;
    std::string_view head = strip_head_decorations(raw_head);
    if (head.empty()) {
        return info;
    }
    const std::size_t word_end = head.find_first_of(" \t\r\n");
    const std::string_view first_word =
        word_end == std::string_view::npos ? head : head.substr(0, word_end);
    if (word_in_list(first_word, kHeadKeywords)) {
        return info;
    }
    std::size_t paren_depth = 0;
    std::string cut = std::string{head};
    for (std::size_t index = 0; index < cut.size(); index = index + 1) {
        if (cut[index] == '(') {
            paren_depth = paren_depth + 1;
        } else if (cut[index] == ')') {
            paren_depth = paren_depth - 1;
        } else if (cut[index] == ':' && paren_depth == 0 && index + 1 < cut.size() &&
                   cut[index + 1] != ':' && index > 0) {
            std::size_t back = index;
            while (back > 0 && (cut[back - 1] == ' ' || cut[back - 1] == '\t' || cut[back - 1] == '\r' ||
                                cut[back - 1] == '\n')) {
                back = back - 1;
            }
            if (back > 0 && cut[back - 1] == ')') {
                cut.resize(index);
                break;
            }
        }
    }
    head = trim(std::string_view{cut});
    std::size_t last_close = head.rfind(')');
    if (last_close == std::string_view::npos) {
        return info;
    }
    std::string_view tail = trim(head.substr(last_close + 1));
    bool tail_ok = true;
    while (!tail.empty()) {
        if (tail.starts_with(std::string_view{"->"})) {
            break;
        }
        const std::size_t tail_word_end = tail.find_first_of(" \t\r\n");
        const std::string_view tail_word =
            tail_word_end == std::string_view::npos ? tail : tail.substr(0, tail_word_end);
        if (tail_word == std::string_view{"const"} || tail_word == std::string_view{"noexcept"} ||
            tail_word == std::string_view{"override"} || tail_word == std::string_view{"final"}) {
            tail = tail_word_end == std::string_view::npos ? std::string_view{} : tail.substr(tail_word_end);
            continue;
        }
        tail_ok = false;
        break;
    }
    if (!tail_ok) {
        return info;
    }
    std::size_t open = last_close;
    std::size_t depth = 0;
    while (open > 0) {
        if (head[open] == ')') {
            depth = depth + 1;
        } else if (head[open] == '(') {
            depth = depth - 1;
            if (depth == 0) {
                break;
            }
        }
        open = open - 1;
    }
    if (open == 0 && head[0] != '(') {
        return info;
    }
    const std::string_view pre_paren = rtrim(head.substr(0, open));
    if (pre_paren.empty()) {
        return info;
    }
    if (pre_paren.back() == ']') {
        info.is_function = true;
        info.is_lambda = true;
        return info;
    }
    const std::string_view identifier = last_identifier(pre_paren);
    if (identifier.empty()) {
        return info;
    }
    info.is_function = true;
    if (word_in_list(identifier, kHeadKeywords)) {
        info.is_function = false;
        return info;
    }
    if (pre_paren.find("operator") != std::string_view::npos) {
        info.is_operator = true;
        return info;
    }
    const std::size_t identifier_offset = pre_paren.size() - identifier.size();
    if (identifier_offset > 0 && pre_paren[identifier_offset - 1] == '~') {
        info.is_destructor = true;
    }
    info.name = std::string{identifier};
    return info;
}

// CEP:WHAT: Walks flat code and records every brace candidate that classifies as a function or lambda definition.
// CEP:WHY: Block, stub, and naming rules all consume the same definition facts; one extractor prevents divergent parses (Law 5).
// CEP:STATUS: complete
// CEP:FAILURE: Unclosed braces are skipped rather than crashing; the file is reported by the scanner if malformed.
// CEP:ASSUMES: Parameter lists are balanced; lambdas inside call arguments are not recorded because they sit at paren depth above zero.
// CEP:COST: Linear in flat size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::vector<FunctionDefinition> extract_function_definitions(const SourceView& view) {
    std::vector<FunctionDefinition> definitions;
    const std::string& flat = view.flat;
    std::size_t boundary = 0;
    std::size_t paren_depth = 0;
    std::size_t index = 0;
    while (index < flat.size()) {
        const char current = flat[index];
        if (current == '(') {
            paren_depth = paren_depth + 1;
            index = index + 1;
            continue;
        }
        if (current == ')') {
            if (paren_depth > 0) {
                paren_depth = paren_depth - 1;
            }
            index = index + 1;
            continue;
        }
        const bool boundary_char = current == ';' || current == '{' || current == '}';
        if (boundary_char && paren_depth == 0) {
            if (current == '{') {
                const std::string_view head = trim(std::string_view{flat}.substr(boundary, index - boundary));
                const HeadInfo info = classify_head(head);
                std::size_t depth = 1;
                std::size_t close = index + 1;
                while (close < flat.size() && depth > 0) {
                    if (flat[close] == '{') {
                        depth = depth + 1;
                    } else if (flat[close] == '}') {
                        depth = depth - 1;
                    }
                    close = close + 1;
                }
                if (info.is_function && depth == 0) {
                    std::size_t head_begin = boundary;
                    while (head_begin < index && std::isspace(static_cast<unsigned char>(flat[head_begin])) != 0) {
                        head_begin = head_begin + 1;
                    }
                    FunctionDefinition definition;
                    definition.name = info.name;
                    definition.is_lambda = info.is_lambda;
                    definition.is_destructor = info.is_destructor;
                    definition.is_operator = info.is_operator;
                    definition.head_line = view.line_at(head_begin);
                    definition.body_begin = index + 1;
                    definition.body_end = close - 1;
                    std::size_t statements = 0;
                    for (std::size_t scan = definition.body_begin; scan < definition.body_end;
                         scan = scan + 1) {
                        if (flat[scan] == ';') {
                            statements = statements + 1;
                        }
                    }
                    definition.statement_count = statements;
                    definitions.push_back(std::move(definition));
                }
            }
            boundary = index + 1;
        }
        index = index + 1;
    }
    return definitions;
}

// CEP:WHAT: Finds the comment block immediately above a definition, allowing only punctuation-free gap lines.
// CEP:WHY: CEP blocks document functions; association must skip attributes and template headers but reject code (CEP&CC 10.2).
// CEP:STATUS: complete
// CEP:FAILURE: Returns null when no block qualifies or the gap is too large.
// CEP:ASSUMES: Blocks are ordered by line; file headers (CEP:FILE) are never function blocks.
// CEP:COST: Linear in block count plus gap size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] const CommentBlock* find_associated_block(const SourceView& view, std::size_t head_line,
                                                        const std::regex& gap) {
    const CommentBlock* closest = nullptr;
    for (const CommentBlock& block : view.comment_blocks) {
        if (block.end_line >= head_line) {
            break;
        }
        closest = &block;
    }
    if (closest == nullptr) {
        return nullptr;
    }
    if (closest->text.find(kFileHeaderField) != std::string::npos) {
        return nullptr;
    }
    if (head_line - closest->end_line - 1 > kMaxAssociationGapLines) {
        return nullptr;
    }
    for (std::size_t line = closest->end_line + 1; line < head_line; line = line + 1) {
        if (line > view.lines.size() || !std::regex_match(view.lines[line - 1], gap)) {
            return nullptr;
        }
    }
    return closest;
}

// CEP:WHAT: Joins the statement lines above a literal for exemption-context matching.
// CEP:WHY: Declared constants may wrap across lines; exemptions must see the whole statement (CEP&CC 11.2).
// CEP:STATUS: complete
// CEP:FAILURE: Returns the literal's own line when nothing above joins.
// CEP:ASSUMES: Statement boundaries are semicolons, braces, blank lines, directives, and comment lines.
// CEP:COST: Linear in join size, bounded by kMaxStatementJoinLines.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string statement_context(const SourceView& view, std::size_t line) {
    if (line == 0 || line > view.lines.size()) {
        return std::string{};
    }
    std::string joined = view.lines[line - 1];
    std::size_t current = line;
    std::size_t count = 1;
    while (current > 1 && count < kMaxStatementJoinLines) {
        const std::string_view above = rtrim(view.lines[current - 2]);
        if (above.empty()) {
            break;
        }
        if (above.front() == '#' || above.starts_with(std::string_view{"//"})) {
            break;
        }
        if (above.back() == ';' || above.back() == '{' || above.back() == '}') {
            break;
        }
        joined = std::string{above} + ' ' + joined;
        current = current - 1;
        count = count + 1;
    }
    return joined;
}

// CEP:WHAT: Parses a numeric literal token into a double for allow-list comparison.
// CEP:WHY: Values, not spellings, decide whether 0x0 and 0 are the same allowed value (CEP&CC 11.3).
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional for unparseable tokens; callers treat that as a violation.
// CEP:ASSUMES: Token matches the numeric literal grammar; suffixes are at most kMaxSuffixLength characters.
// CEP:COST: Linear in token size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::optional<double> parse_numeric_value(std::string_view token) {
    while (!token.empty() && token.size() >= kMaxSuffixLength &&
           std::string_view{"uUlLfFzZ"}.find(token.back()) != std::string_view::npos) {
        token.remove_suffix(1);
    }
    std::string digits;
    for (const char value : token) {
        if (value != '\'') {
            digits.push_back(value);
        }
    }
    if (digits.rfind("0x", 0) == 0 || digits.rfind("0X", 0) == 0) {
        long long value = 0;
        const char* first = digits.data() + 2;
        const char* last = digits.data() + digits.size();
        const std::from_chars_result parsed = std::from_chars(first, last, value, 16);
        if (parsed.ec == std::errc{} && parsed.ptr == last) {
            return static_cast<double>(value);
        }
        return std::nullopt;
    }
    const bool floating = digits.find('.') != std::string::npos || digits.find('e') != std::string::npos ||
                          digits.find('E') != std::string::npos;
    if (floating) {
        double value = 0.0;
        const char* first = digits.data();
        const char* last = digits.data() + digits.size();
        const std::from_chars_result parsed = std::from_chars(first, last, value);
        if (parsed.ec == std::errc{} && parsed.ptr == last) {
            return value;
        }
        return std::nullopt;
    }
    long long value = 0;
    const char* first = digits.data();
    const char* last = digits.data() + digits.size();
    const std::from_chars_result parsed = std::from_chars(first, last, value);
    if (parsed.ec == std::errc{} && parsed.ptr == last) {
        return static_cast<double>(value);
    }
    return std::nullopt;
}

// CEP:WHAT: Looks up a message template with a default fallback.
// CEP:WHY: Message selection is data-driven; missing keys degrade visibly, never silently.
// CEP:STATUS: complete
// CEP:FAILURE: Falls back to the default template, then to empty text.
// CEP:ASSUMES: Rule messages were validated at compile time.
// CEP:COST: Logarithmic in message count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string message_for(const Rule& rule, const std::string& key) {
    const auto entry = rule.messages.find(key);
    if (entry != rule.messages.end()) {
        return entry->second;
    }
    const auto fallback = rule.messages.find("default");
    return fallback != rule.messages.end() ? fallback->second : std::string{};
}

// CEP:WHAT: Emits one issue with a rule-specific rendered message.
// CEP:WHY: Issue construction must be uniform so output shape never varies by rule (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Extra placeholders are rule-specific; general ones are added by the reporter.
// CEP:COST: Linear in message size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void emit_issue(const CompiledRule& compiled, CheckContext& context, const std::string& key,
                std::size_t line, const std::string& match,
                const std::vector<std::pair<std::string, std::string>>& extra) {
    Issue issue;
    issue.rule = compiled.rule.id;
    issue.severity = compiled.rule.severity;
    issue.file = context.file_path;
    issue.line = line == 0 ? 1 : line;
    issue.match = match;
    issue.message = render_template(message_for(compiled.rule, key), extra);
    context.issues.push_back(std::move(issue));
}

// CEP:WHAT: Reports matches of a structural pattern over flat code.
// CEP:WHY: Empty handlers, empty branches, literal-only returns, and type naming are flat structural facts.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails; patterns were compiled by compile_rule.
// CEP:ASSUMES: line_group, when present, names a capture group holding the reportable position.
// CEP:COST: Linear in flat size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void run_flat_code_regex(const CompiledRule& compiled, CheckContext& context) {
    const std::regex& pattern = compiled.patterns.at("pattern");
    const std::optional<long long> line_group = int_param(compiled.rule.params, "line_group");
    const bool has_naming = compiled.patterns.contains("name_pattern");
    for (std::sregex_iterator it(context.view.flat.begin(), context.view.flat.end(), pattern);
         it != std::sregex_iterator{}; ++it) {
        const std::smatch& match = *it;
        std::size_t position = static_cast<std::size_t>(match.position());
        if (line_group.has_value() && match.size() > static_cast<std::size_t>(line_group.value()) &&
            match[static_cast<std::size_t>(line_group.value())].matched) {
            position = static_cast<std::size_t>(match.position(static_cast<std::size_t>(line_group.value())));
        }
        std::string evidence = squeeze_whitespace(match.str());
        if (match.size() > 1 && match[1].matched) {
            evidence = squeeze_whitespace(match[1].str());
        }
        if (has_naming) {
            const std::regex& naming = compiled.patterns.at("name_pattern");
            if (std::regex_match(evidence, naming)) {
                continue;
            }
        }
        emit_issue(compiled, context, "default", context.view.line_at(position), evidence, {});
    }
}

// CEP:WHAT: Reports pattern matches in comment lines with optional suppression.
// CEP:WHY: Work-marker ownership, commented-out code, and marker text are comment-line facts (CEP&CC 10.10, 10.14).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Suppression is matched against the raw fragment text.
// CEP:COST: Linear in comment volume.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void run_comment_regex(const CompiledRule& compiled, CheckContext& context) {
    const std::regex& pattern = compiled.patterns.at("pattern");
    const std::regex* suppression = nullptr;
    if (compiled.patterns.contains("suppression_pattern")) {
        suppression = &compiled.patterns.at("suppression_pattern");
    }
    for (const CommentFragment& fragment : context.view.comment_fragments) {
        if (suppression != nullptr && std::regex_search(fragment.text, *suppression)) {
            continue;
        }
        std::smatch match;
        if (std::regex_search(fragment.text, match, pattern)) {
            std::string evidence = squeeze_whitespace(match.str());
            if (match.size() > 1 && match[1].matched) {
                evidence = squeeze_whitespace(match[1].str());
            }
            emit_issue(compiled, context, "default", fragment.line, evidence, {});
        }
    }
}

// CEP:WHAT: Reports pattern matches inside string literals.
// CEP:WHY: Marker text in strings is how placeholders leak into runtime messages (CEP&CC 10.5).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Match lines are computed from newlines inside the literal.
// CEP:COST: Linear in literal volume.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void run_string_literal_regex(const CompiledRule& compiled, CheckContext& context) {
    const std::regex& pattern = compiled.patterns.at("pattern");
    for (const StringLiteral& literal : context.view.strings) {
        for (std::sregex_iterator it(literal.text.begin(), literal.text.end(), pattern);
             it != std::sregex_iterator{}; ++it) {
            const std::smatch& match = *it;
            std::size_t newlines = 0;
            for (std::size_t scan = 0; scan < static_cast<std::size_t>(match.position()); scan = scan + 1) {
                if (literal.text[scan] == '\n') {
                    newlines = newlines + 1;
                }
            }
            std::string evidence = squeeze_whitespace(match.str());
            if (match.size() > 1 && match[1].matched) {
                evidence = squeeze_whitespace(match[1].str());
            }
            emit_issue(compiled, context, "default", literal.line + newlines, evidence, {});
        }
    }
}

// CEP:WHAT: Reports numeric literals outside the allow-list whose statement context matches no exemption.
// CEP:WHY: Magic numbers are banned; named constants, enums, assertions, and directives are the sanctioned forms (CEP&CC 11).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: The numeric literal grammar is mechanism, not policy.
// CEP:COST: Linear in flat size plus context joins.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void run_numeric_literal_policy(const CompiledRule& compiled, CheckContext& context) {
    static const std::regex literal_pattern{std::string{kNumericLiteralPattern}};
    const std::vector<long long> allowed_values =
        int_array_param(compiled.rule.params, "allowed_values").value_or(std::vector<long long>{});
    const auto exempt = compiled.pattern_lists.find("exempt_contexts");
    for (std::sregex_iterator it(context.view.flat.begin(), context.view.flat.end(), literal_pattern);
         it != std::sregex_iterator{}; ++it) {
        const std::smatch& match = *it;
        const std::string token = match.str();
        const std::optional<double> value = parse_numeric_value(token);
        bool allowed = false;
        if (value.has_value()) {
            for (const long long candidate : allowed_values) {
                if (static_cast<double>(candidate) == value.value()) {
                    allowed = true;
                    break;
                }
            }
        }
        if (allowed) {
            continue;
        }
        const std::size_t line = context.view.line_at(static_cast<std::size_t>(match.position()));
        const std::string joined = statement_context(context.view, line);
        if (exempt != compiled.pattern_lists.end()) {
            bool exempted = false;
            for (const std::regex& pattern : exempt->second) {
                if (std::regex_search(joined, pattern)) {
                    exempted = true;
                    break;
                }
            }
            if (exempted) {
                continue;
            }
        }
        emit_issue(compiled, context, "default", line, token, {});
    }
}

// CEP:WHAT: Reports object-like macro definitions whose names violate the prefix or guard patterns.
// CEP:WHY: Macros must be CEP_-prefixed or include guards (CEP&CC 33.16, 8.4).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: The define grammar is mechanism.
// CEP:COST: Linear in line count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void run_macro_prefix_policy(const CompiledRule& compiled, CheckContext& context) {
    static const std::regex define_pattern{std::string{kDefinePattern}};
    const std::regex& name_pattern = compiled.patterns.at("name_pattern");
    const std::regex& guard_pattern = compiled.patterns.at("guard_pattern");
    for (std::size_t index = 0; index < context.view.lines.size(); index = index + 1) {
        std::smatch match;
        if (std::regex_search(context.view.lines[index], match, define_pattern)) {
            const std::string name = match[1].str();
            if (std::regex_match(name, name_pattern) || std::regex_match(name, guard_pattern)) {
                continue;
            }
            emit_issue(compiled, context, "default", index + 1, name, {});
        }
    }
}

// CEP:WHAT: Validates the file header block: presence, required fields, and class value.
// CEP:WHY: Every first-party file must begin with a CEP block (CEP&CC 35).
// CEP:STATUS: complete
// CEP:FAILURE: Emits no_block, missing, or bad_class issues.
// CEP:ASSUMES: The first comment block is the header when it starts early enough.
// CEP:COST: Constant plus block size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario no-header.
void run_comment_block_schema(const CompiledRule& compiled, CheckContext& context) {
    const std::string mode = string_param(compiled.rule.params, "mode").value_or(std::string{});
    if (mode == std::string{"file_header"}) {
        const long long max_start = int_param(compiled.rule.params, "max_start_line").value_or(1);
        const std::vector<std::string> fields =
            string_array_param(compiled.rule.params, "required_fields").value_or(std::vector<std::string>{});
        const std::string class_field =
            string_param(compiled.rule.params, "class_field").value_or(std::string{"CEP:CLASS"});
        const std::vector<std::string> allowed =
            string_array_param(compiled.rule.params, "allowed_classes").value_or(std::vector<std::string>{});
        const CommentBlock* header = nullptr;
        if (!context.view.comment_blocks.empty() &&
            context.view.comment_blocks.front().start_line <= static_cast<std::size_t>(max_start)) {
            header = &context.view.comment_blocks.front();
        }
        if (header == nullptr) {
            emit_issue(compiled, context, "no_block", 1, std::string{}, {});
            return;
        }
        std::string missing;
        for (const std::string& field : fields) {
            if (header->text.find(field + ":") == std::string::npos) {
                if (!missing.empty()) {
                    missing.append(", ");
                }
                missing.append(field);
            }
        }
        if (!missing.empty()) {
            emit_issue(compiled, context, "missing", header->start_line, missing, {{"missing", missing}});
        }
        const std::expected<std::regex, BootError> class_regex = field_value_regex(class_field, {std::string{"\\S+"}});
        if (class_regex.has_value()) {
            std::smatch match;
            if (std::regex_search(header->text, match, class_regex.value())) {
                const std::string value = match[1].str();
                if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
                    emit_issue(compiled, context, "bad_class", header->start_line, value, {});
                }
            }
        }
        return;
    }
    if (mode == std::string{"status_value"}) {
        const std::string status_field =
            string_param(compiled.rule.params, "status_field").value_or(std::string{"CEP:STATUS"});
        const std::vector<std::string> allowed =
            string_array_param(compiled.rule.params, "allowed_values").value_or(std::vector<std::string>{});
        const std::expected<std::regex, BootError> status_regex =
            field_value_regex(status_field, {std::string{"[A-Za-z]+"}});
        if (!status_regex.has_value()) {
            return;
        }
        for (const CommentFragment& fragment : context.view.comment_fragments) {
            std::smatch match;
            if (std::regex_search(fragment.text, match, status_regex.value())) {
                const std::string value = match[1].str();
                if (std::find(allowed.begin(), allowed.end(), value) == allowed.end()) {
                    emit_issue(compiled, context, "bad_value", fragment.line, value, {});
                }
            }
        }
        return;
    }
    if (mode == std::string{"stub_requirements"}) {
        const std::string status_field =
            string_param(compiled.rule.params, "status_field").value_or(std::string{"CEP:STATUS"});
        const std::vector<std::string> statuses =
            string_array_param(compiled.rule.params, "statuses").value_or(std::vector<std::string>{});
        const std::vector<std::string> required =
            string_array_param(compiled.rule.params, "required_fields").value_or(std::vector<std::string>{});
        if (statuses.empty()) {
            return;
        }
        const std::expected<std::regex, BootError> status_regex = field_value_regex(status_field, statuses);
        if (!status_regex.has_value()) {
            return;
        }
        for (const CommentBlock& block : context.view.comment_blocks) {
            std::smatch match;
            if (std::regex_search(block.text, match, status_regex.value())) {
                const std::string value = match[1].str();
                for (const std::string& field : required) {
                    if (block.text.find(field + ":") == std::string::npos) {
                        emit_issue(compiled, context, "unowned", block.start_line, value, {});
                        break;
                    }
                }
            }
        }
    }
}

// CEP:WHAT: Validates function definitions: CEP blocks for nontrivial bodies, empty-body stubs, and naming.
// CEP:WHY: Functions are the primary unit of the comment schema, the stub policy, and the naming law (CEP&CC 10.2, 10.5, 33.13).
// CEP:STATUS: complete
// CEP:FAILURE: Emits per-definition issues as configured.
// CEP:ASSUMES: Lambdas are exempt from blocks unless configured; destructors and operators are exempt from naming.
// CEP:COST: Linear in definition count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void run_function_policy(const CompiledRule& compiled, CheckContext& context) {
    const std::string mode = string_param(compiled.rule.params, "mode").value_or(std::string{});
    const std::vector<FunctionDefinition> definitions = extract_function_definitions(context.view);
    const std::regex* gap = nullptr;
    static const std::regex default_gap{std::string{kDefaultGapPattern}};
    if (compiled.patterns.contains("gap_pattern")) {
        gap = &compiled.patterns.at("gap_pattern");
    }
    if (mode == std::string{"block"}) {
        const std::vector<std::string> fields =
            string_array_param(compiled.rule.params, "required_fields").value_or(std::vector<std::string>{});
        const long long min_statements = int_param(compiled.rule.params, "min_body_statements").value_or(2);
        const bool lambdas_need_blocks = bool_param(compiled.rule.params, "require_block_for_lambdas", false);
        for (const FunctionDefinition& definition : definitions) {
            if (definition.is_lambda && !lambdas_need_blocks) {
                continue;
            }
            if (definition.statement_count < static_cast<std::size_t>(min_statements)) {
                continue;
            }
            const std::string display_name =
                definition.name.empty() ? std::string{"(lambda)"} : definition.name;
            const CommentBlock* block =
                find_associated_block(context.view, definition.head_line, gap != nullptr ? *gap : default_gap);
            std::string missing = std::string{"no CEP comment block"};
            if (block != nullptr) {
                missing.clear();
                for (const std::string& field : fields) {
                    if (block->text.find(field + ":") == std::string::npos) {
                        if (!missing.empty()) {
                            missing.append(", ");
                        }
                        missing.append(field);
                    }
                }
            }
            if (!missing.empty()) {
                emit_issue(compiled, context, "missing", definition.head_line, display_name,
                           {{"missing", missing}});
            }
        }
        return;
    }
    if (mode == std::string{"stub_body"}) {
        const std::string status_field = string_param(compiled.rule.params, "suppress_status_field")
                                             .value_or(std::string{"CEP:STATUS"});
        const std::vector<std::string> statuses =
            string_array_param(compiled.rule.params, "suppress_statuses").value_or(std::vector<std::string>{});
        std::optional<std::regex> status_regex;
        if (!statuses.empty()) {
            const std::expected<std::regex, BootError> assembled = field_value_regex(status_field, statuses);
            if (assembled.has_value()) {
                status_regex = assembled.value();
            }
        }
        for (const FunctionDefinition& definition : definitions) {
            const std::string_view body = std::string_view{context.view.flat}
                                              .substr(definition.body_begin,
                                                      definition.body_end - definition.body_begin);
            if (!trim(body).empty()) {
                continue;
            }
            const CommentBlock* block = find_associated_block(
                context.view, definition.head_line, gap != nullptr ? *gap : default_gap);
            if (block != nullptr && status_regex.has_value() &&
                std::regex_search(block->text, *status_regex)) {
                continue;
            }
            const std::string display_name =
                definition.name.empty() ? std::string{"(lambda)"} : definition.name;
            emit_issue(compiled, context, "default", definition.head_line, display_name, {});
        }
        return;
    }
    if (mode == std::string{"constant_return"}) {
        const std::regex& body_pattern = compiled.patterns.at("pattern");
        for (const FunctionDefinition& definition : definitions) {
            if (definition.statement_count != 1) {
                continue;
            }
            const std::string_view body = std::string_view{context.view.flat}
                                              .substr(definition.body_begin,
                                                      definition.body_end - definition.body_begin);
            std::smatch match;
            const std::string body_text{trim(body)};
            if (!std::regex_match(body_text, match, body_pattern)) {
                continue;
            }
            emit_issue(compiled, context, "default", definition.head_line,
                       squeeze_whitespace(trim(body)), {});
        }
        return;
    }
    if (mode == std::string{"naming"}) {
        const std::regex& name_pattern = compiled.patterns.at("name_pattern");
        for (const FunctionDefinition& definition : definitions) {
            if (definition.is_lambda || definition.is_destructor || definition.is_operator ||
                definition.name.empty()) {
                continue;
            }
            if (std::regex_match(definition.name, name_pattern)) {
                continue;
            }
            emit_issue(compiled, context, "default", definition.head_line, definition.name, {});
        }
    }
}

// CEP:WHAT: Reports banned tokens in files whose declared CEP:CLASS is a hot class.
// CEP:WHY: CEP-0 hot code bans allocation, exceptions, RTTI, virtual dispatch, I/O, and more (CEP&CC 5.1).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails; headerless files are handled by the header rule.
// CEP:ASSUMES: The class list and token list come from configuration.
// CEP:COST: Linear in flat size times token count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
void run_banned_token_policy(const CompiledRule& compiled, CheckContext& context) {
    const std::vector<std::string> classes =
        string_array_param(compiled.rule.params, "applies_to_classes").value_or(std::vector<std::string>{});
    if (std::find(classes.begin(), classes.end(), context.file_class) == classes.end()) {
        return;
    }
    const auto tokens = compiled.pattern_lists.find("tokens");
    if (tokens == compiled.pattern_lists.end()) {
        return;
    }
    for (const std::regex& token : tokens->second) {
        for (std::sregex_iterator it(context.view.flat.begin(), context.view.flat.end(), token);
             it != std::sregex_iterator{}; ++it) {
            const std::smatch& match = *it;
            emit_issue(compiled, context, "default", context.view.line_at(static_cast<std::size_t>(match.position())),
                       squeeze_whitespace(match.str()), {{"class", context.file_class}});
        }
    }
}

}  // namespace

// CEP:WHAT: Verifies that every configured rule is documented in the standard document and versions agree.
// CEP:WHY: Law 8 bans stale documentation; this rule makes doc drift a lint failure (CEP&CC 34.3).
// CEP:STATUS: complete
// CEP:FAILURE: Propagates a bootstrap error when the document cannot be read; otherwise emits sync issues.
// CEP:ASSUMES: Rule headings match the configured heading pattern; the version line matches the version pattern.
// CEP:COST: Linear in document size plus rule count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario doc-sync-stale.
[[nodiscard]] std::expected<bool, BootError> run_doc_rule_coverage(const CompiledRule& compiled,
                                                                   CheckContext& context) {
    const std::string doc_param =
        string_param(compiled.rule.params, "doc_path").value_or(std::string{});
    std::string doc_path = doc_param;
    if (!doc_param.empty() && doc_param.front() != '/') {
        const std::filesystem::path base =
            std::filesystem::path{context.config.origin_path}.parent_path();
        doc_path = (base / std::filesystem::path{doc_param}).lexically_normal().generic_string();
    }
    const std::expected<std::string, BootError> content = read_file(doc_path);
    if (!content.has_value()) {
        return std::unexpected(content.error());
    }
    context.file_path = doc_path;
    const std::regex& heading_pattern = compiled.patterns.at("heading_pattern");
    const std::regex& version_pattern = compiled.patterns.at("version_pattern");
    std::vector<std::string> lines = [&content]() {
        std::vector<std::string> result;
        std::string current;
        for (const char value : content.value()) {
            if (value == '\n') {
                result.push_back(current);
                current.clear();
                continue;
            }
            current.push_back(value);
        }
        result.push_back(current);
        return result;
    }();
    std::map<std::string, std::size_t> documented;
    std::vector<std::pair<std::string, std::size_t>> duplicates;
    std::string doc_version;
    std::size_t version_line = 1;
    for (std::size_t index = 0; index < lines.size(); index = index + 1) {
        std::smatch match;
        if (std::regex_search(lines[index], match, heading_pattern)) {
            std::string id = squeeze_whitespace(match.str());
            if (match.size() > 1 && match[1].matched) {
                id = match[1].str();
            }
            if (documented.contains(id)) {
                duplicates.emplace_back(id, index + 1);
                continue;
            }
            documented.emplace(id, index + 1);
        }
        if (doc_version.empty() && std::regex_search(lines[index], match, version_pattern)) {
            if (match.size() > 1 && match[1].matched) {
                doc_version = match[1].str();
                version_line = index + 1;
            }
        }
    }
    for (const Rule& rule : context.config.rules) {
        if (!documented.contains(rule.id)) {
            emit_issue(compiled, context, "missing_in_doc", 1, rule.id, {});
        }
    }
    std::map<std::string, bool> configured;
    for (const Rule& rule : context.config.rules) {
        configured.emplace(rule.id, true);
    }
    for (const auto& [id, line] : documented) {
        if (!configured.contains(id)) {
            emit_issue(compiled, context, "missing_in_config", line, id, {});
        }
    }
    for (const auto& [id, line] : duplicates) {
        emit_issue(compiled, context, "duplicate", line, id, {});
    }
    if (!doc_version.empty() && doc_version != context.config.standard_version) {
        emit_issue(compiled, context, "version_mismatch", version_line, doc_version,
                   {{"doc_version", doc_version}, {"config_version", context.config.standard_version}});
    }
    return true;
}

// CEP:WHAT: Compiles a rule's parameter patterns and validates required keys and message templates.
// CEP:WHY: Bad policy must fail before scanning so failures are actionable, not per-file noise (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for missing keys, invalid patterns, or missing message templates.
// CEP:ASSUMES: Parameter keys ending in _pattern, plus tokens and exempt_contexts, are regular expressions.
// CEP:COST: Linear in parameter count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<CompiledRule, BootError> compile_rule_impl(const Rule& rule) {
    CompiledRule compiled;
    compiled.rule = rule;
    const bool icase = bool_param(rule.params, "icase", false);
    const std::optional<const Json::Object*> object = rule.params.as_object();
    if (object.has_value()) {
        for (const Json::Member& member : *object.value()) {
            const std::string& key = member.first;
            const bool is_single_pattern =
                key == std::string{"pattern"} || key == std::string{"suppression_pattern"} ||
                key == std::string{"name_pattern"} || key == std::string{"guard_pattern"} ||
                key == std::string{"gap_pattern"} || key == std::string{"heading_pattern"} ||
                key == std::string{"version_pattern"};
            const bool is_pattern_list = key == std::string{"tokens"} || key == std::string{"exempt_contexts"};
            if (is_single_pattern) {
                const std::optional<std::string_view> text = member.second.as_string();
                if (!text.has_value()) {
                    return std::unexpected(BootError{kBootMissingMessage,
                                                     "rule " + rule.id + " param " + key +
                                                         " must be a pattern string"});
                }
                const std::expected<std::regex, BootError> pattern =
                    compile_pattern(text.value(), icase && key == std::string{"pattern"});
                if (!pattern.has_value()) {
                    return std::unexpected(pattern.error());
                }
                compiled.patterns.emplace(key, pattern.value());
                continue;
            }
            if (is_pattern_list) {
                const std::optional<const Json::Array*> items = member.second.as_array();
                if (!items.has_value()) {
                    return std::unexpected(BootError{kBootMissingMessage,
                                                     "rule " + rule.id + " param " + key +
                                                         " must be a pattern array"});
                }
                std::vector<std::regex> patterns;
                std::vector<std::string> sources;
                for (const Json& element : *items.value()) {
                    const std::optional<std::string_view> text = element.as_string();
                    if (!text.has_value()) {
                        return std::unexpected(BootError{kBootMissingMessage,
                                                         "rule " + rule.id + " param " + key +
                                                             " entries must be strings"});
                    }
                    const std::expected<std::regex, BootError> pattern = compile_pattern(text.value(), false);
                    if (!pattern.has_value()) {
                        return std::unexpected(pattern.error());
                    }
                    patterns.push_back(pattern.value());
                    sources.push_back(std::string{text.value()});
                }
                compiled.pattern_lists.emplace(key, std::move(patterns));
                compiled.pattern_sources.emplace(key, std::move(sources));
            }
        }
    }
    struct Required {
        std::string check;
        std::vector<std::string> patterns;
        std::vector<std::string> messages;
    };
    std::vector<std::string> required_messages;
    if (rule.check == std::string{"comment_block_schema"}) {
        const std::string mode = string_param(rule.params, "mode").value_or(std::string{});
        if (mode == std::string{"file_header"}) {
            required_messages = {"no_block", "missing", "bad_class"};
        } else if (mode == std::string{"status_value"}) {
            required_messages = {"bad_value"};
        } else if (mode == std::string{"stub_requirements"}) {
            required_messages = {"unowned"};
        } else {
            return std::unexpected(
                BootError{kBootMissingMessage, "rule " + rule.id + " unknown mode: " + mode});
        }
    } else if (rule.check == std::string{"function_policy"}) {
        const std::string mode = string_param(rule.params, "mode").value_or(std::string{});
        if (mode == std::string{"block"}) {
            required_messages = {"missing"};
        } else if (mode == std::string{"stub_body"} || mode == std::string{"naming"} ||
                   mode == std::string{"constant_return"}) {
            required_messages = {"default"};
        } else {
            return std::unexpected(
                BootError{kBootMissingMessage, "rule " + rule.id + " unknown mode: " + mode});
        }
    }
    const std::vector<Required> requirements = {
        Required{"flat_code_regex", {"pattern"}, {"default"}},
        Required{"comment_regex", {"pattern"}, {"default"}},
        Required{"string_literal_regex", {"pattern"}, {"default"}},
        Required{"numeric_literal_policy", {}, {"default"}},
        Required{"macro_prefix_policy", {"name_pattern", "guard_pattern"}, {"default"}},
        Required{"comment_block_schema", {}, required_messages},
        Required{"function_policy", {}, required_messages},
        Required{"banned_token_policy", {"tokens"}, {"default"}},
        Required{"doc_rule_coverage", {"heading_pattern", "version_pattern", "doc_path"},
                 {"missing_in_doc", "missing_in_config", "version_mismatch", "duplicate"}}};
    for (const Required& requirement : requirements) {
        if (requirement.check != rule.check) {
            continue;
        }
        for (const std::string& key : requirement.patterns) {
            if (!compiled.patterns.contains(key) && !compiled.pattern_lists.contains(key) &&
                !string_param(rule.params, key).has_value()) {
                return std::unexpected(
                    BootError{kBootMissingMessage, "rule " + rule.id + " missing param: " + key});
            }
        }
        for (const std::string& key : requirement.messages) {
            if (!rule.messages.contains(key)) {
                return std::unexpected(
                    BootError{kBootMissingMessage, "rule " + rule.id + " missing message: " + key});
            }
        }
        break;
    }
    return compiled;
}

// CEP:WHAT: Compiles one rule for execution.
// CEP:WHY: Public entry so the linter compiles each rule exactly once per run.
// CEP:STATUS: complete
// CEP:FAILURE: Delegates to the internal compiler's bootstrap errors.
// CEP:ASSUMES: Rules were validated by load_config.
// CEP:COST: Linear in parameter count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::expected<CompiledRule, BootError> compile_rule(const Rule& rule) {
    return compile_rule_impl(rule);
}

// CEP:WHAT: Extracts the declared CEP:CLASS from a file's header block.
// CEP:WHY: Class-scoped rules need the declared class (CEP&CC 33.3).
// CEP:STATUS: complete
// CEP:FAILURE: Returns an empty string when no header or field exists.
// CEP:ASSUMES: The first block is the header.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
std::string extract_file_class(const SourceView& view, const std::string& class_field) {
    if (view.comment_blocks.empty()) {
        return std::string{};
    }
    const std::expected<std::regex, BootError> pattern =
        field_value_regex(class_field, {std::string{"\\S+"}});
    if (!pattern.has_value()) {
        return std::string{};
    }
    std::smatch match;
    if (std::regex_search(view.comment_blocks.front().text, match, pattern.value())) {
        return match[1].str();
    }
    return std::string{};
}

// CEP:WHAT: Dispatches one compiled rule to its primitive.
// CEP:WHY: Uniform dispatch keeps the linter loop independent of primitive details (Law 5).
// CEP:STATUS: complete
// CEP:FAILURE: Only the document coverage rule can fail, on unreadable documents.
// CEP:ASSUMES: The rule was compiled from the same configuration as the context.
// CEP:COST: Delegated to primitives.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::expected<bool, BootError> run_check(const CompiledRule& compiled, CheckContext& context) {
    const std::string& check = compiled.rule.check;
    if (check == std::string{"flat_code_regex"}) {
        run_flat_code_regex(compiled, context);
        return true;
    }
    if (check == std::string{"comment_regex"}) {
        run_comment_regex(compiled, context);
        return true;
    }
    if (check == std::string{"string_literal_regex"}) {
        run_string_literal_regex(compiled, context);
        return true;
    }
    if (check == std::string{"numeric_literal_policy"}) {
        run_numeric_literal_policy(compiled, context);
        return true;
    }
    if (check == std::string{"macro_prefix_policy"}) {
        run_macro_prefix_policy(compiled, context);
        return true;
    }
    if (check == std::string{"comment_block_schema"}) {
        run_comment_block_schema(compiled, context);
        return true;
    }
    if (check == std::string{"function_policy"}) {
        run_function_policy(compiled, context);
        return true;
    }
    if (check == std::string{"banned_token_policy"}) {
        run_banned_token_policy(compiled, context);
        return true;
    }
    if (check == std::string{"doc_rule_coverage"}) {
        return run_doc_rule_coverage(compiled, context);
    }
    return true;
}

}  // namespace cep::lint
