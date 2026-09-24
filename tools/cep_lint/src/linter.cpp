// CEP:FILE: tools/cep_lint/src/linter.cpp
// CEP:WHAT: Implementation of lint orchestration: deterministic target expansion, file scanning, rule execution, and document coverage.
// CEP:WHY: Deterministic ordering and fail-loud target errors are CI requirements (CEP&CC 6.5, Law 6).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Bootstrap errors propagate for missing targets, unreadable files, or lexical failures.
// CEP:ASSUMES: std::filesystem iteration order is unspecified, so paths are sorted before processing (CEP&CC 28.3).
// CEP:COST: Offline tool; sorting is O(n log n) in file count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario repo-root-exclusions.
#include "linter.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <regex>

namespace cep::lint {
namespace {

constexpr int kBootFileIo = 1;

// CEP:WHAT: Finds the configured CEP:CLASS field name from the class-scoped rules.
// CEP:WHY: Class extraction must follow configuration, not hard-code the field (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional when no rule declares a class field; files then lint without class context.
// CEP:ASSUMES: The banned-token rule names the field.
// CEP:COST: Linear in rule count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
[[nodiscard]] std::optional<std::string> string_class_field(const std::vector<Rule>& rules) {
    for (const Rule& rule : rules) {
        if (rule.check != std::string{"banned_token_policy"}) {
            continue;
        }
        const std::optional<const Json*> member = rule.params.find("class_field");
        if (member.has_value()) {
            const std::optional<std::string_view> text = member.value()->as_string();
            if (text.has_value()) {
                return std::string{text.value()};
            }
        }
    }
    return std::nullopt;
}

// CEP:WHAT: Collects all files under a root, sorted, applying extension and exclude filters.
// CEP:WHY: Scans must be reproducible and must skip excluded trees such as fixtures and build output (CEP&CC 6.5, 32.13).
// CEP:STATUS: complete
// CEP:FAILURE: Unreadable directories are skipped conservatively; the walk never throws.
// CEP:ASSUMES: Excludes are searched against the path relative to the walk root with forward slashes.
// CEP:COST: Linear in directory entries plus sort.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario repo-root-exclusions.
[[nodiscard]] std::vector<std::string> collect_files(const std::filesystem::path& root,
                                                     const Config& config) {
    std::vector<std::string> files;
    std::error_code error;
    const bool is_directory = std::filesystem::is_directory(root, error);
    if (error || !is_directory) {
        return files;
    }
    std::vector<std::regex> excludes;
    for (const std::string& pattern : config.exclude_patterns) {
        try {
            excludes.emplace_back(pattern);
        } catch (const std::regex_error&) {
            continue;
        }
    }
    std::vector<std::filesystem::path> stack;
    stack.push_back(root);
    while (!stack.empty()) {
        const std::filesystem::path directory = stack.back();
        stack.pop_back();
        std::vector<std::filesystem::path> entries;
        std::error_code entry_error;
        for (const std::filesystem::directory_entry& entry :
             std::filesystem::directory_iterator(directory, entry_error)) {
            entries.push_back(entry.path());
        }
        if (entry_error) {
            continue;
        }
        std::sort(entries.begin(), entries.end());
        for (const std::filesystem::path& path : entries) {
            std::error_code classify_error;
            const bool entry_is_directory =
                std::filesystem::is_directory(path, classify_error) && !classify_error;
            const std::string relative =
                std::filesystem::relative(path, root, classify_error).generic_string();
            if (classify_error) {
                continue;
            }
            bool excluded = false;
            for (const std::regex& pattern : excludes) {
                if (std::regex_search(relative, pattern)) {
                    excluded = true;
                    break;
                }
            }
            if (excluded) {
                continue;
            }
            if (entry_is_directory) {
                stack.push_back(path);
                continue;
            }
            const std::string extension = path.extension().generic_string();
            if (std::find(config.extensions.begin(), config.extensions.end(), extension) ==
                config.extensions.end()) {
                continue;
            }
            std::error_code canonical_error;
            const std::filesystem::path canonical =
                std::filesystem::weakly_canonical(path, canonical_error);
            files.push_back(canonical_error ? path.generic_string() : canonical.generic_string());
        }
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());
    return files;
}

// CEP:WHAT: Lints one file: scan, extract class, run every per-file rule.
// CEP:WHY: Per-file work is isolated so failures point at one source (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for reading or scanning; rule failures are impossible after compile.
// CEP:ASSUMES: Rules were compiled once by the caller.
// CEP:COST: Linear in file size times rule count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<bool, BootError> lint_file(const std::vector<CompiledRule>& compiled_rules,
                                                       const Config& config, const std::string& path,
                                                       LintResult& result) {
    const std::expected<std::string, BootError> content = read_file(path);
    if (!content.has_value()) {
        return std::unexpected(content.error());
    }
    const std::expected<SourceView, ScanError> view = scan_source(content.value());
    if (!view.has_value()) {
        return std::unexpected(BootError{kBootFileIo, path + ":" + std::to_string(view.error().line) +
                                                        ": " + view.error().detail});
    }
    std::string file_class;
    const std::optional<std::string> class_field =
        string_class_field(config.rules);
    if (class_field.has_value()) {
        file_class = extract_file_class(view.value(), class_field.value());
    }
    CheckContext context{config, view.value(), path, std::move(file_class), result.issues};
    for (const CompiledRule& compiled : compiled_rules) {
        if (compiled.rule.check == std::string{"doc_rule_coverage"}) {
            continue;
        }
        const std::expected<bool, BootError> step = run_check(compiled, context);
        if (!step.has_value()) {
            return std::unexpected(step.error());
        }
    }
    result.files = result.files + 1;
    return true;
}

}  // namespace

// CEP:WHAT: Lints targets: expands files, compiles rules, scans, runs checks, and sorts issues.
// CEP:WHY: Public entry used by the CLI and the self-test runner.
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for missing targets, unreadable files, or lexical failures.
// CEP:ASSUMES: Targets are files or directories.
// CEP:COST: Linear in matched sources times rule count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
std::expected<LintResult, BootError> run_lint(const Config& config,
                                              const std::vector<std::string>& targets) {
    std::vector<std::string> files;
    for (const std::string& target : targets) {
        std::error_code error;
        const std::filesystem::path path{target};
        if (std::filesystem::is_directory(path, error)) {
            const std::vector<std::string> found = collect_files(path, config);
            files.insert(files.end(), found.begin(), found.end());
            continue;
        }
        if (std::filesystem::exists(path, error)) {
            std::error_code canonical_error;
            const std::filesystem::path canonical = std::filesystem::weakly_canonical(path, canonical_error);
            files.push_back(canonical_error ? path.generic_string() : canonical.generic_string());
            continue;
        }
        return std::unexpected(BootError{kBootFileIo, "target does not exist: " + target});
    }
    std::sort(files.begin(), files.end());
    files.erase(std::unique(files.begin(), files.end()), files.end());

    std::vector<CompiledRule> compiled_rules;
    compiled_rules.reserve(config.rules.size());
    for (const Rule& rule : config.rules) {
        if (!rule.enabled) {
            continue;
        }
        const std::expected<CompiledRule, BootError> compiled = compile_rule(rule);
        if (!compiled.has_value()) {
            return std::unexpected(compiled.error());
        }
        compiled_rules.push_back(compiled.value());
    }

    LintResult result;
    for (const std::string& file : files) {
        const std::expected<bool, BootError> step = lint_file(compiled_rules, config, file, result);
        if (!step.has_value()) {
            return std::unexpected(step.error());
        }
    }
    for (const CompiledRule& compiled : compiled_rules) {
        if (compiled.rule.check != std::string{"doc_rule_coverage"}) {
            continue;
        }
        SourceView empty_view;
        CheckContext context{config, empty_view, config.doc_path, std::string{}, result.issues};
        const std::expected<bool, BootError> step = run_check(compiled, context);
        if (!step.has_value()) {
            return std::unexpected(step.error());
        }
    }
    sort_issues(result.issues);
    return result;
}

}  // namespace cep::lint
