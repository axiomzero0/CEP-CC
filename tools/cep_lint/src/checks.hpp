// CEP:FILE: tools/cep_lint/src/checks.hpp
// CEP:WHAT: The nine generic check primitives and the rule compiler that turns configuration data into executable checks.
// CEP:WHY: Engine code is mechanism; every pattern, threshold, and message comes from configuration (Law 7, CEP&CC 32.1).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: compile_rule returns bootstrap errors for invalid patterns or missing message templates; run_check appends issues to the context sink.
// CEP:ASSUMES: std::regex ECMAScript grammar; patterns come from the validated configuration.
// CEP:COST: Offline tool; per-file cost is linear in source size times rule count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#pragma once

#include <map>
#include <regex>
#include <string>
#include <string_view>
#include <vector>

#include "config.hpp"
#include "diagnostics.hpp"
#include "scanner.hpp"

namespace cep::lint {

// CEP:WHAT: A rule with its parameter patterns precompiled for execution.
// CEP:WHY: Regex compilation happens once per run, not once per file (CEP&CC 23.6.1 minimal work).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Built only by successful compile_rule calls.
// CEP:ASSUMES: Pattern keys ending in _pattern plus the token and exempt_context lists are regular expressions.
// CEP:COST: Offline tool; compilation is linear in pattern count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct CompiledRule {
    Rule rule;
    std::map<std::string, std::regex> patterns;
    std::map<std::string, std::vector<std::regex>> pattern_lists;
    std::map<std::string, std::vector<std::string>> pattern_sources;
};

// CEP:WHAT: Everything a per-file check needs: configuration, scanned views, file identity and class, and the issue sink.
// CEP:WHY: One context type keeps primitive signatures uniform and auditable.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Checks append issues; failures are bootstrap errors from compile_rule only.
// CEP:ASSUMES: view and file_path outlive the call.
// CEP:COST: Offline tool.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
struct CheckContext {
    const Config& config;
    const SourceView& view;
    std::string file_path;
    std::string file_class;
    std::vector<Issue>& issues;
};

// CEP:WHAT: Compiles one rule's parameter patterns and validates message templates.
// CEP:WHY: Invalid policy must fail loudly before any file is scanned (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for uncompilable patterns or missing message templates.
// CEP:ASSUMES: Rule check names were validated by load_config.
// CEP:COST: Linear in parameter count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<CompiledRule, BootError> compile_rule(const Rule& rule);

// CEP:WHAT: Runs one compiled rule against one file context.
// CEP:WHY: Uniform dispatch keeps the linter loop simple.
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap error only for the document coverage rule when the standard document is unreadable.
// CEP:ASSUMES: The rule was compiled from the same configuration.
// CEP:COST: Linear in the relevant view size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<bool, BootError> run_check(const CompiledRule& compiled, CheckContext& context);

// CEP:WHAT: Extracts the file's CEP:CLASS value from its header block.
// CEP:WHY: Class-scoped rules such as the hot-code token ban need the declared class (CEP&CC 33.3).
// CEP:STATUS: complete
// CEP:FAILURE: Returns an empty string when no header block or class field exists; the header rule reports that.
// CEP:ASSUMES: The class field is the first matching field in the first comment block.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
[[nodiscard]] std::string extract_file_class(const SourceView& view, const std::string& class_field);

}  // namespace cep::lint
