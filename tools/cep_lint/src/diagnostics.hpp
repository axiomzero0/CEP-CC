// CEP:FILE: tools/cep_lint/src/diagnostics.hpp
// CEP:WHAT: Violation records, severity model, and deterministic ordering for lint findings.
// CEP:WHY: Findings are the tool's output contract; stable ordering and explicit severity are required for reproducible CI (CEP&CC 6.5, 34.2).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; these are value types with no failure paths.
// CEP:ASSUMES: Severity values are 0-3 as defined by CEP&CC 34.2.
// CEP:COST: Offline tool; constant size per finding.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace cep::lint {

// CEP:WHAT: One rule violation at a source position.
// CEP:WHY: The reporter, self-test, and exit logic all consume the same finding shape.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: line is 1-based; match is the offending text evidence.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct Issue {
    std::string rule;
    int severity{0};
    std::string file;
    std::size_t line{0};
    std::string message;
    std::string match;
};

// CEP:WHAT: Bootstrap failure before configuration is available.
// CEP:WHY: Failures that occur before the configuration loads cannot use configuration message templates (Law 6).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Codes are documented in the standard document's cep_lint chapter.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct BootError {
    int code{0};
    std::string detail;
};

// CEP:WHAT: Sorts issues by file, line, rule, and message.
// CEP:WHY: Output must be deterministic regardless of scan or map iteration order (CEP&CC 6.5, 28.3).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Issues come from a completed run.
// CEP:COST: Comparison-sorted, offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
void sort_issues(std::vector<Issue>& issues);

}  // namespace cep::lint
