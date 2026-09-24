// CEP:FILE: tools/cep_lint/src/cli.hpp
// CEP:WHAT: Command-line interface: bootstrap configuration discovery and configuration-driven option parsing.
// CEP:WHY: The CLI surface is policy data; only the bootstrap contract (default path and environment variable) is mechanism (Law 7).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Unknown options and missing values produce usage bootstrap errors; no exception escapes.
// CEP:ASSUMES: The bootstrap default is .cep/cep_lint.json overridable by CEP_LINT_CONFIG or --config.
// CEP:COST: Offline tool; linear in argument count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#pragma once

#include <expected>
#include <string>
#include <vector>

#include "config.hpp"
#include "diagnostics.hpp"

namespace cep::lint {

constexpr std::string_view kBootstrapConfigDefault = ".cep/cep_lint.json";
constexpr std::string_view kBootstrapConfigEnv = "CEP_LINT_CONFIG";
constexpr int kBootstrapExitBoot = 2;

// CEP:WHAT: Parsed command line: selected options and positional targets.
// CEP:WHY: main() needs one value describing the requested action.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a payload type.
// CEP:ASSUMES: Option presence is tracked by name.
// CEP:COST: Offline tool; linear storage.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
struct CommandLine {
    std::vector<std::string> flags;
    std::vector<std::string> targets;
    std::string config_path;
};

// CEP:WHAT: Finds the bootstrap configuration path from arguments and environment.
// CEP:WHY: Configuration must be located before the CLI vocabulary itself is loaded (CEP&CC 32.1).
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty string when no override is present; the default is used then.
// CEP:ASSUMES: Arguments are the raw argv values.
// CEP:COST: Linear in argument count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::string bootstrap_config_path(int argc, char* argv[]);

// CEP:WHAT: Parses arguments against the configured option table.
// CEP:WHY: Option names, help text, and value-taking flags are configuration (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Returns usage bootstrap errors for unknown options or missing values.
// CEP:ASSUMES: The configuration was loaded successfully.
// CEP:COST: Linear in argument count times option count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::expected<CommandLine, BootError> parse_arguments(const Config& config, int argc,
                                                                    char* argv[]);

// CEP:WHAT: Renders the usage text with all configured options.
// CEP:WHY: Help output is policy data (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: None.
// CEP:COST: Linear in option count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::string render_usage(const Config& config);

}  // namespace cep::lint
