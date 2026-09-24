// CEP:FILE: tools/cep_lint/src/main.cpp
// CEP:WHAT: Tool entry point: bootstrap, configuration load, command dispatch, output, and exit code selection.
// CEP:WHY: A single audited entry keeps failure policy explicit end to end (Law 6, CEP&CC 34.3).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Exits with the configured boot code and a coded message for configuration failures; usage errors print usage text.
// CEP:ASSUMES: argv strings outlive the run; output goes to stdout and errors to stderr.
// CEP:COST: Offline tool; dominated by the lint run.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

#include "cli.hpp"
#include "config.hpp"
#include "diagnostics.hpp"
#include "linter.hpp"
#include "reporter.hpp"
#include "selftest.hpp"

namespace {

// CEP:WHAT: Prints a bootstrap failure with its code.
// CEP:WHY: Pre-configuration failures cannot use message templates; codes are the contract (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: This function is a failure reporter.
// CEP:ASSUMES: Codes are documented in the standard document's cep_lint chapter.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
void report_boot_error(const cep::lint::BootError& error) {
    std::cerr << "CEP-LINT-BOOT-" << error.code << ": " << error.detail << '\n';
}

}  // namespace

// CEP:WHAT: Tool entry point: bootstrap, configuration, dispatch, output, and exit code.
// CEP:WHY: A single audited entry keeps failure policy explicit end to end (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Exits with the boot code and coded message for configuration failures; usage errors print usage text.
// CEP:ASSUMES: argv strings outlive the run; output goes to stdout and errors to stderr.
// CEP:COST: Dominated by the lint run.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
int main(int argc, char* argv[]) {
    const std::string bootstrap_path = cep::lint::bootstrap_config_path(argc, argv);
    std::string config_path = bootstrap_path;
    if (config_path.empty()) {
        config_path = std::string{cep::lint::kBootstrapConfigDefault};
    }
    const std::expected<cep::lint::Config, cep::lint::BootError> loaded = cep::lint::load_config(config_path);
    if (!loaded.has_value()) {
        report_boot_error(loaded.error());
        return cep::lint::kBootstrapExitBoot;
    }
    const cep::lint::Config& config = loaded.value();

    const std::expected<cep::lint::CommandLine, cep::lint::BootError> parsed =
        cep::lint::parse_arguments(config, argc, argv);
    if (!parsed.has_value()) {
        report_boot_error(parsed.error());
        std::cerr << cep::lint::render_usage(config);
        return config.exit_usage;
    }
    const cep::lint::CommandLine& command = parsed.value();

    for (const std::string& flag : command.flags) {
        if (flag == std::string{"help"}) {
            std::cout << cep::lint::render_usage(config);
            return config.exit_ok;
        }
        if (flag == std::string{"version"}) {
            std::cout << config.tool_name << ' ' << config.tool_version << " enforcing "
                      << config.standard_name << ' ' << config.standard_version << '\n';
            return config.exit_ok;
        }
    }
    if (command.targets.empty() && std::find(command.flags.begin(), command.flags.end(),
                                             std::string{"self-test"}) == command.flags.end()) {
        std::cerr << cep::lint::render_usage(config);
        return config.exit_usage;
    }

    for (const std::string& flag : command.flags) {
        if (flag != std::string{"self-test"}) {
            continue;
        }
        const std::expected<std::vector<cep::lint::ScenarioResult>, cep::lint::BootError> results =
            cep::lint::run_self_test(config);
        if (!results.has_value()) {
            report_boot_error(results.error());
            return config.exit_boot;
        }
        std::size_t passed = 0;
        for (const cep::lint::ScenarioResult& result : results.value()) {
            std::cout << result.line << '\n';
            if (result.passed) {
                passed = passed + 1;
            }
        }
        std::string suite = config.output.suite;
        const std::vector<std::pair<std::string, std::string>> fields = {
            {"passed", std::to_string(passed)}, {"total", std::to_string(results.value().size())}};
        for (const auto& [key, value] : fields) {
            const std::string token = "{" + key + "}";
            std::size_t position = suite.find(token);
            while (position != std::string::npos) {
                suite.replace(position, token.size(), value);
                position = suite.find(token, position + value.size());
            }
        }
        std::cout << suite << '\n';
        return passed == results.value().size() ? config.exit_ok : config.exit_violations;
    }

    const std::expected<cep::lint::LintResult, cep::lint::BootError> result =
        cep::lint::run_lint(config, command.targets);
    if (!result.has_value()) {
        report_boot_error(result.error());
        return config.exit_boot;
    }
    for (const cep::lint::Issue& issue : result.value().issues) {
        std::cout << cep::lint::render_issue(config, issue) << '\n';
    }
    std::cout << cep::lint::render_summary(config, result.value()) << '\n';
    const bool failing = std::any_of(result.value().issues.begin(), result.value().issues.end(),
                                     [&config](const cep::lint::Issue& issue) {
                                         return std::find(config.fail_on.begin(), config.fail_on.end(),
                                                          issue.severity) != config.fail_on.end();
                                     });
    return failing ? config.exit_violations : config.exit_ok;
}
