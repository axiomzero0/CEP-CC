// CEP:FILE: tools/cep_lint/src/reporter.hpp
// CEP:WHAT: Output rendering: issue lines, summaries, and scenario lines from configuration templates.
// CEP:WHY: All user-facing text is configuration data so wording evolves without code changes (Law 7, Law 8).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Rendering never fails; unknown placeholders remain visible in output.
// CEP:ASSUMES: Issues are sorted before rendering.
// CEP:COST: Offline tool; linear in output volume.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#pragma once

#include <string>

#include "config.hpp"
#include "diagnostics.hpp"
#include "linter.hpp"

namespace cep::lint {

// CEP:WHAT: Renders one issue line from the configured template.
// CEP:WHY: Issue output shape is policy (CEP&CC 38.15 machine-parseable diagnostics).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Severity names exist in configuration.
// CEP:COST: Linear in message size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string render_issue(const Config& config, const Issue& issue);

// CEP:WHAT: Renders the run summary line.
// CEP:WHY: CI needs one stable line with counts (CEP&CC 34.3 verdict stage).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: None.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::string render_summary(const Config& config, const LintResult& result);

}  // namespace cep::lint
