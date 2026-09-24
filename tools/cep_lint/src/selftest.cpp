// CEP:FILE: tools/cep_lint/src/selftest.cpp
// CEP:WHAT: Implementation of the self-test manifest runner.
// CEP:WHY: Rules are proven by fixtures with exact expectations; exact counts keep regressions loud (CEP&CC 34.9).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Bootstrap errors cover manifest I/O, parsing, and schema; expectation mismatches mark scenarios failed.
// CEP:ASSUMES: Scenario configuration paths are relative to the manifest directory.
// CEP:COST: Offline tool; one lint run per scenario.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
#include "selftest.hpp"

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <sstream>

#include "json_parse.hpp"
#include "linter.hpp"
#include "reporter.hpp"

namespace cep::lint {
namespace {

constexpr int kBootFileIo = 1;
constexpr int kBootJsonParse = 2;
constexpr int kBootMissingKey = 4;
constexpr int kBootBadType = 5;

// CEP:WHAT: Formats a count without locale dependence.
// CEP:WHY: Scenario lines must be stable across environments (CEP&CC 6.5).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Value is non-negative by type.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
[[nodiscard]] std::string format_count(std::size_t value) {
    if (value == 0) {
        return std::string{"0"};
    }
    constexpr std::size_t kDecimalBase = 10;
    std::string digits;
    std::size_t remaining = value;
    while (remaining > 0) {
        digits.push_back(static_cast<char>('0' + remaining % kDecimalBase));
        remaining = remaining / kDecimalBase;
    }
    std::reverse(digits.begin(), digits.end());
    return digits;
}

// CEP:WHAT: Renders one scenario result line from the configured template.
// CEP:WHY: Self-test output is configuration text like all other output (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Placeholders are status, name, issues, and expected.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
[[nodiscard]] std::string render_scenario(const Config& config, const std::string& status,
                                          const std::string& name, std::size_t issues,
                                          const std::string& expected) {
    std::string text = config.output.scenario;
    const std::vector<std::pair<std::string, std::string>> fields = {
        {"status", status}, {"name", name}, {"issues", format_count(issues)}, {"expected", expected}};
    for (const auto& [key, value] : fields) {
        const std::string token = "{" + key + "}";
        std::size_t position = text.find(token);
        while (position != std::string::npos) {
            text.replace(position, token.size(), value);
            position = text.find(token, position + value.size());
        }
    }
    return text;
}

// CEP:WHAT: Loads a scenario-specific configuration layered over the base configuration.
// CEP:WHY: Profiles override rule parameters without duplicating policy (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Delegates to load_config failures.
// CEP:ASSUMES: Include paths inside the profile are relative to the profile file.
// CEP:COST: One configuration load.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario doc-sync-stale.
[[nodiscard]] std::expected<Config, BootError> scenario_config(const std::string& manifest_path,
                                                               const std::string& relative) {
    std::error_code error;
    const std::filesystem::path resolved =
        std::filesystem::weakly_canonical(std::filesystem::path{manifest_path}.parent_path() /
                                              std::filesystem::path{relative},
                                          error);
    if (error) {
        return std::unexpected(BootError{kBootFileIo, "unresolvable scenario config: " + relative});
    }
    return load_config(resolved.generic_string());
}

}  // namespace

// CEP:WHAT: Runs every scenario in the configured manifest.
// CEP:WHY: CI requires a single entry point that fails when any expectation breaks (CEP&CC 16).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for unreadable manifests or schema violations.
// CEP:ASSUMES: Scenario paths resolve against the manifest directory.
// CEP:COST: One lint run per scenario.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json all scenarios.
std::expected<std::vector<ScenarioResult>, BootError> run_self_test(const Config& base_config) {
    const std::expected<std::string, BootError> content = read_file(base_config.manifest_path);
    if (!content.has_value()) {
        return std::unexpected(content.error());
    }
    const std::expected<Json, JsonError> parsed = parse_json(content.value());
    if (!parsed.has_value()) {
        const JsonError& error = parsed.error();
        std::ostringstream detail;
        detail << base_config.manifest_path << ':' << error.line << ':' << error.column << ": "
               << error.detail;
        return std::unexpected(BootError{kBootJsonParse, detail.str()});
    }
    const std::optional<const Json::Array*> scenarios =
        parsed.value().find("scenarios").and_then([](const Json* value) { return value->as_array(); });
    if (!scenarios.has_value()) {
        return std::unexpected(BootError{kBootMissingKey, "manifest missing scenarios array"});
    }
    const std::filesystem::path manifest_directory =
        std::filesystem::path{base_config.manifest_path}.parent_path();

    std::vector<ScenarioResult> results;
    for (const Json& scenario : *scenarios.value()) {
        const std::optional<const Json::Object*> object = scenario.as_object();
        if (!object.has_value()) {
            return std::unexpected(BootError{kBootBadType, "scenario entries must be objects"});
        }
        std::string name;
        for (const Json::Member& member : *object.value()) {
            if (member.first == std::string{"name"}) {
                name = std::string{member.second.as_string().value_or(std::string_view{})};
            }
        }
        std::vector<std::string> targets;
        const std::optional<const Json::Array*> target_array =
            scenario.find("targets").and_then([](const Json* value) { return value->as_array(); });
        if (!target_array.has_value()) {
            return std::unexpected(BootError{kBootMissingKey, "scenario " + name + " missing targets"});
        }
        for (const Json& target : *target_array.value()) {
            const std::optional<std::string_view> text = target.as_string();
            if (!text.has_value()) {
                return std::unexpected(BootError{kBootBadType, "scenario " + name + " bad target"});
            }
            std::error_code error;
            const std::filesystem::path resolved =
                std::filesystem::weakly_canonical(manifest_directory / std::filesystem::path{text.value()},
                                                  error);
            targets.push_back(error ? std::string{text.value()} : resolved.generic_string());
        }
        Config active = base_config;
        const std::optional<std::string_view> config_override = [&scenario]() {
            const std::optional<const Json*> member = scenario.find("config");
            if (!member.has_value()) {
                return std::optional<std::string_view>{};
            }
            return member.value()->as_string();
        }();
        if (config_override.has_value()) {
            const std::expected<Config, BootError> loaded =
                scenario_config(base_config.manifest_path, std::string{config_override.value()});
            if (!loaded.has_value()) {
                return std::unexpected(loaded.error());
            }
            active = loaded.value();
        }
        const std::expected<LintResult, BootError> run = run_lint(active, targets);
        if (!run.has_value()) {
            return std::unexpected(run.error());
        }
        std::map<std::string, std::size_t> by_rule;
        for (const Issue& issue : run.value().issues) {
            by_rule[issue.rule] = by_rule[issue.rule] + 1;
        }
        bool passed = true;
        std::string expected_text;
        const std::optional<const Json*> total = scenario.find("expect_total");
        if (total.has_value()) {
            const std::optional<long long> count = total.value()->as_integer();
            if (!count.has_value()) {
                return std::unexpected(BootError{kBootBadType, "scenario " + name + " bad expect_total"});
            }
            passed = static_cast<long long>(run.value().issues.size()) == count.value();
            expected_text = format_count(static_cast<std::size_t>(count.value()));
        } else {
            const std::optional<const Json::Object*> expected_rules =
                scenario.find("expect_by_rule").and_then([](const Json* value) { return value->as_object(); });
            if (!expected_rules.has_value()) {
                return std::unexpected(
                    BootError{kBootMissingKey, "scenario " + name + " missing expectations"});
            }
            for (const Json::Member& member : *expected_rules.value()) {
                const std::optional<long long> count = member.second.as_integer();
                if (!count.has_value()) {
                    return std::unexpected(BootError{kBootBadType, "scenario " + name + " bad count"});
                }
                const std::size_t actual = by_rule.contains(member.first) ? by_rule.at(member.first) : 0;
                if (actual != static_cast<std::size_t>(count.value())) {
                    passed = false;
                }
                if (!expected_text.empty()) {
                    expected_text.append(",");
                }
                expected_text.append(member.first + "=" + format_count(static_cast<std::size_t>(count.value())));
            }
            for (const auto& entry : by_rule) {
                bool known = false;
                for (const Json::Member& member : *expected_rules.value()) {
                    if (member.first == entry.first) {
                        known = true;
                        break;
                    }
                }
                if (!known) {
                    passed = false;
                }
            }
        }
        ScenarioResult result;
        result.name = name;
        result.passed = passed;
        result.line = render_scenario(active, passed ? "PASS" : "FAIL", name, run.value().issues.size(),
                                       expected_text);
        results.push_back(std::move(result));
    }
    return results;
}

}  // namespace cep::lint
