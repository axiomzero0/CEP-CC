// CEP:FILE: tools/cep_lint/src/config.hpp
// CEP:WHAT: Configuration model: rule table, scan policy, CLI options, output templates, exit codes, and self-test manifest location.
// CEP:WHY: All lint policy is data in .cep/cep_lint.json (CEP&CC 32.1); the engine contains mechanism only (Law 7).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: load_config returns positioned bootstrap errors for unreadable files, JSON errors, include cycles, missing keys, wrong types, unknown check primitives, and invalid severities.
// CEP:ASSUMES: JSON objects preserve order; include paths are relative to the including file; all relative paths resolve against the config file's directory.
// CEP:COST: Offline tool; linear in configuration size plus include depth.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#pragma once

#include <expected>
#include <map>
#include <string>
#include <vector>

#include "diagnostics.hpp"
#include "json_model.hpp"

namespace cep::lint {

// CEP:WHAT: One compiled-regex-free rule entry: identity, primitive, severity, parameters, and message templates.
// CEP:WHY: Checks consume rules uniformly, so policy changes never touch engine code.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Validated by load_config; malformed rules never reach checks.
// CEP:ASSUMES: params and messages are JSON objects.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct Rule {
    std::string id;
    std::string check;
    int severity{0};
    bool enabled{true};
    Json params;
    std::map<std::string, std::string> messages;
};

// CEP:WHAT: One CLI option specification.
// CEP:WHY: The command-line surface is configuration data, not engine policy (Law 7).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Validated by load_config.
// CEP:ASSUMES: short_name is empty or one character.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
struct Option {
    std::string long_name;
    std::string short_name;
    bool takes_value{false};
    std::string help;
};

// CEP:WHAT: Output message templates.
// CEP:WHY: Message text belongs to configuration so wording can evolve without code changes (Law 8).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Validated by load_config.
// CEP:ASSUMES: Placeholders are substituted by the reporter.
// CEP:COST: Offline tool; constant size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
struct OutputTemplates {
    std::string issue;
    std::string summary;
    std::string scenario;
    std::string suite;
};

// CEP:WHAT: The complete lint configuration.
// CEP:WHY: One value carries all policy; the engine and self-test share it.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Constructed only by successful load_config calls.
// CEP:ASSUMES: Paths are resolved relative to the config directory at load time.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct Config {
    std::string origin_path;
    std::string standard_name;
    std::string standard_version;
    std::string doc_path;
    std::string tool_name;
    std::string tool_version;
    std::vector<std::string> extensions;
    std::vector<std::string> exclude_patterns;
    std::vector<Option> options;
    std::string usage;
    OutputTemplates output;
    int exit_ok{0};
    int exit_violations{0};
    int exit_boot{0};
    int exit_usage{0};
    std::vector<std::string> severity_names;
    std::vector<int> fail_on;
    std::string manifest_path;
    std::vector<Rule> rules;
};

// CEP:WHAT: Reads a file completely.
// CEP:WHY: Config, manifest, and standard document loading share one failure-aware reader (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap error when the file cannot be opened or read.
// CEP:ASSUMES: Files fit in memory; encoding is UTF-8.
// CEP:COST: Offline tool; linear in file size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<std::string, BootError> read_file(const std::string& path);

// CEP:WHAT: Loads, include-merges, and validates the lint configuration.
// CEP:WHY: Policy must be validated before any check runs so misconfiguration fails loudly (Law 6, CEP&CC 34.3).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for I/O, parsing, cycles, schema violations, unknown primitives, or bad severities.
// CEP:ASSUMES: Includes are acyclic; merge semantics are deep for objects and replacing for arrays and scalars.
// CEP:COST: Offline tool; linear in total configuration size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<Config, BootError> load_config(const std::string& path);

}  // namespace cep::lint
