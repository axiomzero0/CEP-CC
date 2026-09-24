// CEP:FILE: tools/cep_lint/src/scanner.hpp
// CEP:WHAT: C++ source scanner producing comment fragments, comment blocks, string literals, and a normalized flat-code view with line mapping.
// CEP:WHY: Every lint rule operates on one of these views; one scanner keeps lexing consistent, tested, and honest about malformed input (Law 2).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Returns ScanError for unterminated strings, raw strings, char literals, or block comments; such files are refused rather than guessed at.
// CEP:ASSUMES: Source is UTF-8; preprocessor detection is line-based (first non-whitespace '#'); digit separators are disambiguated from char literals by lookahead.
// CEP:COST: Offline tool; single pass, linear in input size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#pragma once

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

namespace cep::lint {

// CEP:WHAT: One line of comment text with its source line and whether it covers the whole line.
// CEP:WHY: Line-scoped comment rules (work-marker ownership, marker text) need per-line text, and block assembly needs full-line flags.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: text has comment markers stripped and trailing carriage returns removed.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct CommentFragment {
    std::string text;
    std::size_t line{0};
    bool full_line{false};
};

// CEP:WHAT: A contiguous run of full-line comments, such as a CEP header or function block.
// CEP:WHY: The CEP comment schema (CEP&CC 10.2) is defined over blocks, not individual lines.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Lines are contiguous and 1-based.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct CommentBlock {
    std::string text;
    std::size_t start_line{0};
    std::size_t end_line{0};
};

// CEP:WHAT: A string literal's raw content and its starting source line.
// CEP:WHY: String-content rules (marker text) must see literal text with correct positions.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: text keeps escapes as written; raw string content is verbatim.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct StringLiteral {
    std::string text;
    std::size_t line{0};
};

// CEP:WHAT: Scan failure with position and detail.
// CEP:WHY: Lexically malformed input must fail loudly, never silently (Law 6).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: line is 1-based; zero means unknown.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct ScanError {
    std::string detail;
    std::size_t line{0};
};

// CEP:WHAT: The set of views the checks operate on.
// CEP:WHY: Rules need raw lines, comments, strings, and normalized code with a line map; sharing one scan keeps results consistent.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Constructed only by successful scans.
// CEP:ASSUMES: flat_line has one entry per flat character; lines are 1-based.
// CEP:COST: Offline tool; memory linear in source size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct SourceView {
    std::vector<std::string> lines;
    std::vector<CommentFragment> comment_fragments;
    std::vector<CommentBlock> comment_blocks;
    std::vector<StringLiteral> strings;
    std::string flat;
    std::vector<std::size_t> flat_line;

    // CEP:WHAT: Returns the source line of a flat-code offset.
    // CEP:WHY: Every structural match must be reported at a real source line (CEP&CC 38.15).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns 0 for out-of-range offsets; callers clamp before reporting.
    // CEP:ASSUMES: offset is a position in flat.
    // CEP:COST: Constant time.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::size_t line_at(std::size_t offset) const noexcept {
        if (offset < flat_line.size()) {
            return flat_line[offset];
        }
        return 0;
    }
};

// CEP:WHAT: Scans one C++ translation unit text.
// CEP:WHY: The single lexical pass feeding every rule.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected on unterminated literals or comments.
// CEP:ASSUMES: UTF-8 input; C++26 literal grammar (raw strings, digit separators).
// CEP:COST: Offline tool; single pass, linear time.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<SourceView, ScanError> scan_source(const std::string& text);

}  // namespace cep::lint
