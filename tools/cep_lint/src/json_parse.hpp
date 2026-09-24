// CEP:FILE: tools/cep_lint/src/json_parse.hpp
// CEP:WHAT: Recursive-descent JSON parser producing the ordered Json DOM.
// CEP:WHY: The lint policy is configuration data (CEP&CC 32.1), so the tool must parse its own standard JSON without external dependencies.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Returns std::unexpected with line, column, and detail for any malformed document; never throws.
// CEP:ASSUMES: Input is UTF-8; nesting is bounded by kMaxNestingDepth; numbers use '.' as decimal separator (locale-independent via std::from_chars).
// CEP:COST: Offline tool; single pass, linear in input size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#pragma once

#include <cstddef>
#include <expected>
#include <string>

#include "json_model.hpp"

namespace cep::lint {

// CEP:WHAT: Parse failure with source position and machine-readable detail.
// CEP:WHY: Failure behavior must be explicit and diagnosable (Law 6, CEP&CC 38.15).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Positions are 1-based line and column numbers.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
struct JsonError {
    std::size_t line{};
    std::size_t column{};
    std::string detail;
};

// CEP:WHAT: Parses a complete JSON document.
// CEP:WHY: Configuration and manifests are JSON; parsing is the first failure-aware stage (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns std::unexpected<JsonError> on syntax errors, duplicate keys, bad escapes, lone surrogates, out-of-range integers, or trailing content.
// CEP:ASSUMES: Depth is bounded by kMaxNestingDepth to keep recursion bounded (Law 6).
// CEP:COST: Offline tool; linear in document size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<Json, JsonError> parse_json(const std::string& text);

}  // namespace cep::lint
