// CEP:FILE: tools/cep_lint/src/linter.hpp
// CEP:WHAT: Lint orchestration: target expansion, per-file scanning, rule execution, document coverage, and issue collection.
// CEP:WHY: One place decides what is scanned and in which order so runs are deterministic (CEP&CC 6.5).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for unreadable or malformed targets; scanning stops at the first lexical failure.
// CEP:ASSUMES: Directory iteration is sorted before use; exclude patterns are searched against slash-normalized relative paths.
// CEP:COST: Offline tool; linear in total source size times rule count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#pragma once

#include <expected>
#include <string>
#include <vector>

#include "checks.hpp"
#include "config.hpp"
#include "diagnostics.hpp"

namespace cep::lint {

// CEP:WHAT: Result of one lint run: sorted issues and the file count.
// CEP:WHY: The reporter and exit logic consume one value.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Issues are already sorted by file, line, rule, message.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct LintResult {
    std::vector<Issue> issues;
    std::size_t files{0};
};

// CEP:WHAT: Lints the given targets against the configuration.
// CEP:WHY: Entry point used by both the CLI and the self-test runner.
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for target resolution, reading, or scanning failures.
// CEP:ASSUMES: Targets are files or directories; directory walks are sorted and filtered by extension and excludes.
// CEP:COST: Offline tool; linear in matched sources.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::expected<LintResult, BootError> run_lint(const Config& config,
                                                            const std::vector<std::string>& targets);

}  // namespace cep::lint
