// CEP:FILE: tools/cep_lint/src/diagnostics.cpp
// CEP:WHAT: Implementation of deterministic issue ordering.
// CEP:WHY: Reproducible output is required for CI gates and golden tests (CEP&CC 6.5).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Issue fields are fully populated.
// CEP:COST: Offline tool; std::sort is O(n log n).
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#include "diagnostics.hpp"

#include <algorithm>
#include <vector>

namespace cep::lint {

// CEP:WHAT: Sorts issues deterministically by file, line, rule, and message.
// CEP:WHY: Output must be reproducible regardless of scan order (CEP&CC 6.5).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Issue fields are fully populated.
// CEP:COST: Comparison-sorted, offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
void sort_issues(std::vector<Issue>& issues) {
    std::sort(issues.begin(), issues.end(), [](const Issue& left, const Issue& right) {
        if (left.file != right.file) {
            return left.file < right.file;
        }
        if (left.line != right.line) {
            return left.line < right.line;
        }
        if (left.rule != right.rule) {
            return left.rule < right.rule;
        }
        return left.message < right.message;
    });
}

}  // namespace cep::lint
