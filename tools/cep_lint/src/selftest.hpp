// CEP:FILE: tools/cep_lint/src/selftest.hpp
// CEP:WHAT: Self-test runner executing the configured manifest of scenarios.
// CEP:WHY: The tool must prove its own rules fire and stay silent exactly as expected (CEP&CC 34.3, 34.9).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for manifest problems; scenario mismatches produce failures in the returned report.
// CEP:ASSUMES: Manifest paths resolve against the manifest file's directory; scenarios may override the configuration.
// CEP:COST: Offline tool; each scenario runs a full lint.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
#pragma once

#include <expected>
#include <string>
#include <vector>

#include "config.hpp"
#include "diagnostics.hpp"

namespace cep::lint {

// CEP:WHAT: One executed scenario with its outcome.
// CEP:WHY: The suite summary and exit decision need per-scenario facts.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Scenario names are unique in the manifest.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
struct ScenarioResult {
    std::string name;
    bool passed{false};
    std::string line;
};

// CEP:WHAT: Runs every scenario in the configured manifest.
// CEP:WHY: CI requires a single entry point that fails when any expectation breaks (CEP&CC 16).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for unreadable manifests or scenario schema violations.
// CEP:ASSUMES: The base configuration was loaded successfully before calling.
// CEP:COST: One lint run per scenario.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
[[nodiscard]] std::expected<std::vector<ScenarioResult>, BootError> run_self_test(const Config& config);

}  // namespace cep::lint
