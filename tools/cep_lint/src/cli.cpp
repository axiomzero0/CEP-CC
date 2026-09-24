// CEP:FILE: tools/cep_lint/src/cli.cpp
// CEP:WHAT: Implementation of bootstrap configuration discovery and configuration-driven argument parsing.
// CEP:WHY: The tool's interface stays policy-driven while bootstrap discovery remains a documented contract (Law 7).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Usage bootstrap errors carry the offending argument.
// CEP:ASSUMES: argv strings outlive parsing.
// CEP:COST: Offline tool; linear in arguments.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#include "cli.hpp"

#include <cstdlib>
#include <string_view>

namespace cep::lint {
namespace {

constexpr int kBootUsage = 3;
constexpr std::string_view kLongPrefix = "--";
constexpr std::string_view kShortPrefix = "-";
constexpr std::string_view kAssignSeparator = "=";

// CEP:WHAT: Reports a usage bootstrap error with the offending argument.
// CEP:WHY: CLI failures must name the input that caused them (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: This function is a failure constructor.
// CEP:ASSUMES: None.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] BootError usage_error(const std::string& detail) {
    return BootError{kBootUsage, detail};
}

}  // namespace

// CEP:WHAT: Finds the configuration path from arguments and environment.
// CEP:WHY: Configuration must be located before the CLI vocabulary is loaded.
// CEP:STATUS: complete
// CEP:FAILURE: Returns an empty string when no override exists; the default applies then.
// CEP:ASSUMES: argv strings outlive the call.
// CEP:COST: Linear in argument count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
std::string bootstrap_config_path(int argc, char* argv[]) {
    for (int index = 1; index < argc; index = index + 1) {
        const std::string_view argument{argv[index]};
        if (argument.starts_with(kLongPrefix) && argument.find(kAssignSeparator) != std::string_view::npos) {
            const std::size_t separator = argument.find(kAssignSeparator);
            if (argument.substr(kLongPrefix.size(), separator - kLongPrefix.size()) ==
                std::string_view{"config"}) {
                return std::string{argument.substr(separator + 1)};
            }
        }
        if (argument == std::string_view{"--config"} || argument == std::string_view{"-c"}) {
            if (index + 1 < argc) {
                return std::string{argv[index + 1]};
            }
        }
    }
    const char* environment = std::getenv(kBootstrapConfigEnv.data());
    if (environment != nullptr && environment[0] != '\0') {
        return std::string{environment};
    }
    return std::string{};
}

// CEP:WHAT: Parses arguments against the configured option table.
// CEP:WHY: The CLI surface is policy data (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Returns usage bootstrap errors for unknown options or missing values.
// CEP:ASSUMES: The configuration was loaded successfully.
// CEP:COST: Linear in argument count times option count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
std::expected<CommandLine, BootError> parse_arguments(const Config& config, int argc, char* argv[]) {
    CommandLine parsed;
    parsed.config_path = std::string{kBootstrapConfigDefault};
    for (int index = 1; index < argc; index = index + 1) {
        const std::string_view argument{argv[index]};
        std::string name;
        std::string inline_value;
        bool has_inline_value = false;
        if (argument.starts_with(kLongPrefix)) {
            const std::size_t separator = argument.find(kAssignSeparator);
            if (separator == std::string_view::npos) {
                name = std::string{argument.substr(kLongPrefix.size())};
            } else {
                name = std::string{argument.substr(kLongPrefix.size(), separator - kLongPrefix.size())};
                inline_value = std::string{argument.substr(separator + 1)};
                has_inline_value = true;
            }
        } else if (argument.starts_with(kShortPrefix) && argument.size() > kShortPrefix.size()) {
            name = std::string{argument.substr(kShortPrefix.size())};
        } else {
            parsed.targets.push_back(std::string{argument});
            continue;
        }
        const Option* matched = nullptr;
        for (const Option& option : config.options) {
            const bool long_match = argument.starts_with(kLongPrefix) && name == option.long_name;
            const bool short_match = !argument.starts_with(kLongPrefix) && name == option.short_name;
            if (long_match || short_match) {
                matched = &option;
                break;
            }
        }
        if (matched == nullptr) {
            return std::unexpected(usage_error("unknown option: " + std::string{argument}));
        }
        if (!matched->takes_value) {
            parsed.flags.push_back(matched->long_name);
            continue;
        }
        if (has_inline_value) {
            if (matched->long_name == std::string{"config"}) {
                parsed.config_path = inline_value;
            }
            continue;
        }
        if (index + 1 >= argc) {
            return std::unexpected(usage_error("option requires a value: " + std::string{argument}));
        }
        index = index + 1;
        if (matched->long_name == std::string{"config"}) {
            parsed.config_path = std::string{argv[index]};
        }
    }
    return parsed;
}

// CEP:WHAT: Renders usage text with all configured options.
// CEP:WHY: Help output is policy data (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Option tables are validated.
// CEP:COST: Linear in option count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
std::string render_usage(const Config& config) {
    std::string text = config.usage;
    text.push_back('\n');
    for (const Option& option : config.options) {
        text.append("  --");
        text.append(option.long_name);
        if (!option.short_name.empty()) {
            text.append(", -");
            text.append(option.short_name);
        }
        if (option.takes_value) {
            text.append(" <value>");
        }
        text.append("    ");
        text.append(option.help);
        text.push_back('\n');
    }
    return text;
}

}  // namespace cep::lint
