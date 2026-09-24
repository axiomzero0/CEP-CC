// CEP:FILE: tools/cep_lint/src/reporter.cpp
// CEP:WHAT: Implementation of template-based output rendering.
// CEP:WHY: Deterministic, configuration-driven output keeps CI parsing stable (CEP&CC 6.5, 38.15).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Placeholders are the documented set in the standard document's cep_lint chapter.
// CEP:COST: Offline tool; linear in issue count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#include "reporter.hpp"

#include <vector>

namespace cep::lint {
namespace {

constexpr std::size_t kDecimalBase = 10;
constexpr std::size_t kSeverityLevels = 4;

// CEP:WHAT: Substitutes '{placeholder}' fields into a template.
// CEP:WHY: One renderer for issue, summary, and scenario text keeps output consistent.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails; unknown placeholders stay visible.
// CEP:ASSUMES: Placeholders do not nest.
// CEP:COST: Linear in template size times field count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string render(std::string template_text,
                                 const std::vector<std::pair<std::string, std::string>>& fields) {
    for (const auto& [key, value] : fields) {
        const std::string token = "{" + key + "}";
        std::size_t position = template_text.find(token);
        while (position != std::string::npos) {
            template_text.replace(position, token.size(), value);
            position = template_text.find(token, position + value.size());
        }
    }
    return template_text;
}

// CEP:WHAT: Formats a non-negative size_t as decimal text.
// CEP:WHY: Output must not depend on locale (CEP&CC 6.5).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Value is non-negative by type.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
[[nodiscard]] std::string format_count(std::size_t value) {
    if (value == 0) {
        return std::string{"0"};
    }
    std::string digits;
    std::size_t remaining = value;
    while (remaining > 0) {
        const char digit = static_cast<char>('0' + remaining % kDecimalBase);
        digits.push_back(digit);
        remaining = remaining / kDecimalBase;
    }
    std::reverse(digits.begin(), digits.end());
    return digits;
}

// CEP:WHAT: Names a severity with fallback text.
// CEP:WHY: Output must stay stable even if configuration is terse.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails; unknown severities render as their number.
// CEP:ASSUMES: Severity names array is configured.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::string severity_name(const Config& config, int severity) {
    if (severity >= 0 && static_cast<std::size_t>(severity) < config.severity_names.size()) {
        return config.severity_names[static_cast<std::size_t>(severity)];
    }
    return "severity-" + format_count(static_cast<std::size_t>(severity < 0 ? 0 : severity));
}

}  // namespace

std::string render_issue(const Config& config, const Issue& issue) {
    return render(config.output.issue,
                  {{"file", issue.file},
                   {"line", format_count(issue.line)},
                   {"rule", issue.rule},
                   {"severity_name", severity_name(config, issue.severity)},
                   {"message", issue.message},
                   {"match", issue.match}});
}

// CEP:WHAT: Renders the run summary line.
// CEP:WHY: CI needs one stable line with counts (CEP&CC 34.3).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Issues were sorted before rendering.
// CEP:COST: Linear in issue count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::string render_summary(const Config& config, const LintResult& result) {
    std::vector<std::size_t> counts(kSeverityLevels, 0);
    for (const Issue& issue : result.issues) {
        if (issue.severity >= 0 && static_cast<std::size_t>(issue.severity) < counts.size()) {
            counts[static_cast<std::size_t>(issue.severity)] = counts[static_cast<std::size_t>(issue.severity)] + 1;
        }
    }
    std::vector<std::pair<std::string, std::string>> fields = {
        {"files", format_count(result.files)}, {"issues", format_count(result.issues.size())}};
    for (std::size_t severity = 0; severity < counts.size(); severity = severity + 1) {
        fields.emplace_back("sev" + format_count(severity), format_count(counts[severity]));
    }
    return render(config.output.summary, fields);
}

}  // namespace cep::lint
