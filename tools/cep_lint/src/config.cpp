// CEP:FILE: tools/cep_lint/src/config.cpp
// CEP:WHAT: Implementation of configuration loading: file reading, include resolution with deep merge, schema validation, and typed extraction.
// CEP:WHY: Policy lives in data (CEP&CC 32.1, Law 7); this module is the only bridge from JSON to engine types.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Every malformed input path returns a coded BootError; no exceptions escape this module.
// CEP:ASSUMES: std::filesystem is available for path normalization; objects preserve order.
// CEP:COST: Offline tool; linear in configuration size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
#include "config.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "json_parse.hpp"

namespace cep::lint {
namespace {

constexpr int kBootFileIo = 1;
constexpr int kBootJsonParse = 2;
constexpr int kBootIncludeCycle = 3;
constexpr int kBootMissingKey = 4;
constexpr int kBootBadType = 5;
constexpr int kBootUnknownPrimitive = 6;
constexpr int kBootBadSeverity = 7;
constexpr std::size_t kMaxIncludeDepth = 16;
constexpr std::size_t kPrimitiveCount = 9;

constexpr std::array<std::string_view, kPrimitiveCount> kKnownCheckPrimitives = {
    std::string_view{"flat_code_regex"},  std::string_view{"comment_regex"},
    std::string_view{"string_literal_regex"}, std::string_view{"numeric_literal_policy"},
    std::string_view{"macro_prefix_policy"}, std::string_view{"comment_block_schema"},
    std::string_view{"function_policy"},  std::string_view{"banned_token_policy"},
    std::string_view{"doc_rule_coverage"}};

// CEP:WHAT: Fails with a missing-key bootstrap error.
// CEP:WHY: Schema gaps must be reported precisely so configuration authors can repair them (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: This function is a failure constructor.
// CEP:ASSUMES: path is a dotted JSON key path.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] BootError missing_key(const std::string& path) {
    return BootError{kBootMissingKey, "missing key: " + path};
}

// CEP:WHAT: Fails with a wrong-type bootstrap error.
// CEP:WHY: Type confusion must be reported with the offending key (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: This function is a failure constructor.
// CEP:ASSUMES: path is a dotted JSON key path.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] BootError bad_type(const std::string& path) {
    return BootError{kBootBadType, "wrong type for key: " + path};
}

// CEP:WHAT: Requires a string member of an object.
// CEP:WHY: Typed access keeps validation and extraction in one audited place.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected when the key is absent or not a string.
// CEP:ASSUMES: object is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<std::string, BootError> require_string(const Json& object,
                                                                   const std::string& key,
                                                                   const std::string& path) {
    const std::optional<const Json*> member = object.find(key);
    if (!member.has_value()) {
        return std::unexpected(missing_key(path + "." + key));
    }
    const std::optional<std::string_view> text = member.value()->as_string();
    if (!text.has_value()) {
        return std::unexpected(bad_type(path + "." + key));
    }
    return std::string{text.value()};
}

// CEP:WHAT: Requires an integer member of an object.
// CEP:WHY: Severities and exit codes must be integers (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected when the key is absent or not an integer.
// CEP:ASSUMES: object is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<long long, BootError> require_integer(const Json& object,
                                                                  const std::string& key,
                                                                  const std::string& path) {
    const std::optional<const Json*> member = object.find(key);
    if (!member.has_value()) {
        return std::unexpected(missing_key(path + "." + key));
    }
    const std::optional<long long> number = member.value()->as_integer();
    if (!number.has_value()) {
        return std::unexpected(bad_type(path + "." + key));
    }
    return number.value();
}

// CEP:WHAT: Requires an object member of an object.
// CEP:WHY: Rule params and messages must be objects.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected when the key is absent or not an object.
// CEP:ASSUMES: object is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<const Json*, BootError> require_object(const Json& object,
                                                                   const std::string& key,
                                                                   const std::string& path) {
    const std::optional<const Json*> member = object.find(key);
    if (!member.has_value()) {
        return std::unexpected(missing_key(path + "." + key));
    }
    if (!member.value()->is_object()) {
        return std::unexpected(bad_type(path + "." + key));
    }
    return member.value();
}

// CEP:WHAT: Requires an array member of an object.
// CEP:WHY: Extensions, excludes, options, severities, and fail_on lists must be arrays.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected when the key is absent or not an array.
// CEP:ASSUMES: object is a JSON object.
// CEP:COST: Linear in object size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<const Json::Array*, BootError> require_array(const Json& object,
                                                                         const std::string& key,
                                                                         const std::string& path) {
    const std::optional<const Json*> member = object.find(key);
    if (!member.has_value()) {
        return std::unexpected(missing_key(path + "." + key));
    }
    const std::optional<const Json::Array*> items = member.value()->as_array();
    if (!items.has_value()) {
        return std::unexpected(bad_type(path + "." + key));
    }
    return items.value();
}

// CEP:WHAT: Requires a string element of an array.
// CEP:WHY: Homogeneous string arrays are validated element by element.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected when the element is not a string.
// CEP:ASSUMES: path names the element for diagnostics.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<std::string, BootError> array_string(const Json& element,
                                                                 const std::string& path) {
    const std::optional<std::string_view> text = element.as_string();
    if (!text.has_value()) {
        return std::unexpected(bad_type(path));
    }
    return std::string{text.value()};
}

// CEP:WHAT: Requires an integer element of an array.
// CEP:WHY: Severity and fail_on lists are integer arrays.
// CEP:STATUS: complete
// CEP:FAILURE: Returns unexpected when the element is not an integer.
// CEP:ASSUMES: path names the element for diagnostics.
// CEP:COST: Constant work.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<long long, BootError> array_integer(const Json& element,
                                                                const std::string& path) {
    const std::optional<long long> number = element.as_integer();
    if (!number.has_value()) {
        return std::unexpected(bad_type(path));
    }
    return number.value();
}

// CEP:WHAT: Deep-merges overlay onto base: objects merge recursively, everything else replaces.
// CEP:WHY: Included configurations must overlay defaults predictably (CEP&CC 34.7 layering).
// CEP:STATUS: complete
// CEP:FAILURE: Never fails.
// CEP:ASSUMES: Both values come from parsed JSON.
// CEP:COST: Linear in overlay size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario doc-sync-stale.
[[nodiscard]] Json deep_merge(const Json& base, const Json& overlay) {
    const std::optional<const Json::Object*> base_object = base.as_object();
    const std::optional<const Json::Object*> overlay_object = overlay.as_object();
    if (!base_object.has_value() || !overlay_object.has_value()) {
        return overlay;
    }
    Json::Object merged = *base_object.value();
    for (const Json::Member& member : *overlay_object.value()) {
        bool replaced = false;
        for (Json::Member& target : merged) {
            if (target.first == member.first) {
                target.second = deep_merge(target.second, member.second);
                replaced = true;
                break;
            }
        }
        if (!replaced) {
            merged.push_back(member);
        }
    }
    return Json{std::move(merged)};
}

// CEP:WHAT: Loads one configuration file and recursively merges its includes beneath it.
// CEP:WHY: Profiles derive from a base configuration without duplicating policy (Law 7).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for I/O, parsing, cycles, or excessive include depth.
// CEP:ASSUMES: Include paths are relative to the including file; visited paths are canonical.
// CEP:COST: Linear in total included size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario doc-sync-stale.
[[nodiscard]] std::expected<Json, BootError> load_merged(const std::string& path,
                                                         std::set<std::string>& visited,
                                                         std::size_t depth) {
    if (depth > kMaxIncludeDepth) {
        return std::unexpected(BootError{kBootIncludeCycle, "include depth exceeds limit: " + path});
    }
    std::error_code canonical_error;
    const std::filesystem::path canonical =
        std::filesystem::weakly_canonical(std::filesystem::path{path}, canonical_error);
    const std::string key = canonical_error ? path : canonical.generic_string();
    if (visited.contains(key)) {
        return std::unexpected(BootError{kBootIncludeCycle, "include cycle detected at: " + key});
    }
    visited.insert(key);
    const std::expected<std::string, BootError> content = read_file(path);
    if (!content.has_value()) {
        return std::unexpected(content.error());
    }
    const std::expected<Json, JsonError> parsed = parse_json(content.value());
    if (!parsed.has_value()) {
        const JsonError& error = parsed.error();
        std::ostringstream detail;
        detail << path << ':' << error.line << ':' << error.column << ": " << error.detail;
        return std::unexpected(BootError{kBootJsonParse, detail.str()});
    }
    const std::optional<const Json::Array*> includes =
        parsed.value().find("include").and_then([](const Json* value) { return value->as_array(); });
    Json result = parsed.value();
    if (includes.has_value()) {
        for (const Json& include : *includes.value()) {
            const std::optional<std::string_view> include_path = include.as_string();
            if (!include_path.has_value()) {
                return std::unexpected(BootError{kBootBadType, "include entries must be strings"});
            }
            std::error_code resolve_error;
            const std::filesystem::path base_directory = std::filesystem::path{path}.parent_path();
            const std::filesystem::path resolved =
                std::filesystem::weakly_canonical(base_directory / include_path.value(), resolve_error);
            if (resolve_error) {
                return std::unexpected(BootError{
                    kBootFileIo, "unresolvable include: " + std::string{include_path.value()}});
            }
            const std::expected<Json, BootError> included =
                load_merged(resolved.generic_string(), visited, depth + 1);
            if (!included.has_value()) {
                return std::unexpected(included.error());
            }
            result = deep_merge(included.value(), result);
        }
    }
    return result;
}

// CEP:WHAT: Validates and extracts one rule entry.
// CEP:WHY: Rules are the core policy unit; malformed rules must never reach checks (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for missing or mistyped fields, unknown primitives, or out-of-range severities.
// CEP:ASSUMES: The rules object member key is the rule identity.
// CEP:COST: Constant per rule.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
[[nodiscard]] std::expected<Rule, BootError> parse_rule(const std::string& id, const Json& entry) {
    const std::string path = "rules." + id;
    if (!entry.is_object()) {
        return std::unexpected(bad_type(path));
    }
    const std::expected<std::string, BootError> check = require_string(entry, "check", path);
    if (!check.has_value()) {
        return std::unexpected(check.error());
    }
    bool primitive_known = false;
    for (const std::string_view primitive : kKnownCheckPrimitives) {
        if (check.value() == primitive) {
            primitive_known = true;
            break;
        }
    }
    if (!primitive_known) {
        return std::unexpected(BootError{kBootUnknownPrimitive, "unknown check: " + check.value()});
    }
    const std::expected<long long, BootError> severity = require_integer(entry, "severity", path);
    if (!severity.has_value()) {
        return std::unexpected(severity.error());
    }
    constexpr long long kMaxSeverity = 3;
    if (severity.value() < 0 || severity.value() > kMaxSeverity) {
        return std::unexpected(BootError{kBootBadSeverity, "severity out of range for " + id});
    }
    const std::expected<const Json*, BootError> params = require_object(entry, "params", path);
    if (!params.has_value()) {
        return std::unexpected(params.error());
    }
    Rule rule;
    rule.id = id;
    rule.check = check.value();
    rule.severity = static_cast<int>(severity.value());
    rule.params = *params.value();
    const std::optional<const Json*> enabled = entry.find("enabled");
    if (enabled.has_value()) {
        const std::optional<bool> flag = enabled.value()->as_bool();
        if (!flag.has_value()) {
            return std::unexpected(bad_type(path + ".enabled"));
        }
        rule.enabled = flag.value();
    }
    const std::expected<const Json*, BootError> messages = require_object(entry, "messages", path);
    if (!messages.has_value()) {
        return std::unexpected(messages.error());
    }
    const std::optional<const Json::Object*> message_object = messages.value()->as_object();
    for (const Json::Member& member : *message_object.value()) {
        const std::optional<std::string_view> text = member.second.as_string();
        if (!text.has_value()) {
            return std::unexpected(bad_type(path + ".messages." + member.first));
        }
        rule.messages.emplace(member.first, std::string{text.value()});
    }
    return rule;
}

}  // namespace

// CEP:WHAT: Reads a file completely.
// CEP:WHY: Config, manifest, and standard document loading share one failure-aware reader (Law 6).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap error when the file cannot be opened or read.
// CEP:ASSUMES: Files fit in memory; encoding is UTF-8.
// CEP:COST: Offline tool; linear in file size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::expected<std::string, BootError> read_file(const std::string& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream.is_open()) {
        return std::unexpected(BootError{kBootFileIo, "cannot open file: " + path});
    }
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (stream.bad()) {
        return std::unexpected(BootError{kBootFileIo, "read failure: " + path});
    }
    return buffer.str();
}

// CEP:WHAT: Loads, include-merges, and validates the lint configuration.
// CEP:WHY: Policy must be validated before any check runs so misconfiguration fails loudly (Law 6, CEP&CC 34.3).
// CEP:STATUS: complete
// CEP:FAILURE: Returns bootstrap errors for I/O, parsing, cycles, schema violations, unknown primitives, or bad severities.
// CEP:ASSUMES: Includes are acyclic; merge semantics are deep for objects and replacing for arrays and scalars.
// CEP:COST: Offline tool; linear in total configuration size.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
std::expected<Config, BootError> load_config(const std::string& path) {
    std::set<std::string> visited;
    const std::expected<Json, BootError> merged = load_merged(path, visited, 0);
    if (!merged.has_value()) {
        return std::unexpected(merged.error());
    }
    const Json& root = merged.value();

    Config config;
    config.origin_path = path;
    const std::optional<const Json*> standard = root.find("standard");
    if (!standard.has_value() || !standard.value()->is_object()) {
        return std::unexpected(standard.has_value() ? bad_type("standard") : missing_key("standard"));
    }
    const std::expected<std::string, BootError> standard_name =
        require_string(*standard.value(), "name", "standard");
    if (!standard_name.has_value()) {
        return std::unexpected(standard_name.error());
    }
    config.standard_name = standard_name.value();
    const std::expected<std::string, BootError> standard_version =
        require_string(*standard.value(), "version", "standard");
    if (!standard_version.has_value()) {
        return std::unexpected(standard_version.error());
    }
    config.standard_version = standard_version.value();
    const std::expected<std::string, BootError> doc_path =
        require_string(*standard.value(), "doc", "standard");
    if (!doc_path.has_value()) {
        return std::unexpected(doc_path.error());
    }
    std::error_code doc_error;
    const std::filesystem::path config_directory = std::filesystem::path{path}.parent_path();
    const std::filesystem::path resolved_doc =
        std::filesystem::weakly_canonical(config_directory / doc_path.value(), doc_error);
    config.doc_path = doc_error ? doc_path.value() : resolved_doc.generic_string();

    const std::optional<const Json*> tool = root.find("tool");
    if (!tool.has_value() || !tool.value()->is_object()) {
        return std::unexpected(tool.has_value() ? bad_type("tool") : missing_key("tool"));
    }
    const std::expected<std::string, BootError> tool_name = require_string(*tool.value(), "name", "tool");
    if (!tool_name.has_value()) {
        return std::unexpected(tool_name.error());
    }
    config.tool_name = tool_name.value();
    const std::expected<std::string, BootError> tool_version =
        require_string(*tool.value(), "version", "tool");
    if (!tool_version.has_value()) {
        return std::unexpected(tool_version.error());
    }
    config.tool_version = tool_version.value();

    const std::expected<const Json*, BootError> scan = require_object(root, "scan", "root");
    if (!scan.has_value()) {
        return std::unexpected(scan.error());
    }
    const std::expected<const Json::Array*, BootError> extensions =
        require_array(*scan.value(), "extensions", "scan");
    if (!extensions.has_value()) {
        return std::unexpected(extensions.error());
    }
    for (const Json& element : *extensions.value()) {
        const std::expected<std::string, BootError> text = array_string(element, "scan.extensions");
        if (!text.has_value()) {
            return std::unexpected(text.error());
        }
        config.extensions.push_back(text.value());
    }
    const std::expected<const Json::Array*, BootError> excludes =
        require_array(*scan.value(), "exclude", "scan");
    if (!excludes.has_value()) {
        return std::unexpected(excludes.error());
    }
    for (const Json& element : *excludes.value()) {
        const std::expected<std::string, BootError> text = array_string(element, "scan.exclude");
        if (!text.has_value()) {
            return std::unexpected(text.error());
        }
        config.exclude_patterns.push_back(text.value());
    }

    const std::optional<const Json*> cli = root.find("cli");
    if (!cli.has_value() || !cli.value()->is_object()) {
        return std::unexpected(cli.has_value() ? bad_type("cli") : missing_key("cli"));
    }
    const std::expected<std::string, BootError> usage = require_string(*cli.value(), "usage", "cli");
    if (!usage.has_value()) {
        return std::unexpected(usage.error());
    }
    config.usage = usage.value();
    const std::expected<const Json::Array*, BootError> options =
        require_array(*cli.value(), "options", "cli");
    if (!options.has_value()) {
        return std::unexpected(options.error());
    }
    for (const Json& element : *options.value()) {
        if (!element.is_object()) {
            return std::unexpected(bad_type("cli.options element"));
        }
        Option option;
        const std::expected<std::string, BootError> long_name =
            require_string(element, "long", "cli.options element");
        if (!long_name.has_value()) {
            return std::unexpected(long_name.error());
        }
        option.long_name = long_name.value();
        const std::optional<const Json*> short_member = element.find("short");
        if (short_member.has_value()) {
            const std::expected<std::string, BootError> short_name =
                require_string(element, "short", "cli.options element");
            if (!short_name.has_value()) {
                return std::unexpected(short_name.error());
            }
            option.short_name = short_name.value();
        }
        const std::optional<const Json*> value_member = element.find("value");
        if (value_member.has_value()) {
            const std::optional<bool> flag = value_member.value()->as_bool();
            if (!flag.has_value()) {
                return std::unexpected(bad_type("cli.options element value"));
            }
            option.takes_value = flag.value();
        }
        const std::expected<std::string, BootError> help =
            require_string(element, "help", "cli.options element");
        if (!help.has_value()) {
            return std::unexpected(help.error());
        }
        option.help = help.value();
        config.options.push_back(std::move(option));
    }

    const std::expected<const Json*, BootError> output = require_object(root, "output", "root");
    if (!output.has_value()) {
        return std::unexpected(output.error());
    }
    const std::expected<std::string, BootError> issue =
        require_string(*output.value(), "issue", "output");
    if (!issue.has_value()) {
        return std::unexpected(issue.error());
    }
    config.output.issue = issue.value();
    const std::expected<std::string, BootError> summary =
        require_string(*output.value(), "summary", "output");
    if (!summary.has_value()) {
        return std::unexpected(summary.error());
    }
    config.output.summary = summary.value();
    const std::expected<std::string, BootError> scenario =
        require_string(*output.value(), "scenario", "output");
    if (!scenario.has_value()) {
        return std::unexpected(scenario.error());
    }
    config.output.scenario = scenario.value();
    const std::expected<std::string, BootError> suite =
        require_string(*output.value(), "suite", "output");
    if (!suite.has_value()) {
        return std::unexpected(suite.error());
    }
    config.output.suite = suite.value();

    const std::expected<const Json*, BootError> exit_codes = require_object(root, "exit_codes", "root");
    if (!exit_codes.has_value()) {
        return std::unexpected(exit_codes.error());
    }
    const std::expected<long long, BootError> exit_ok =
        require_integer(*exit_codes.value(), "ok", "exit_codes");
    if (!exit_ok.has_value()) {
        return std::unexpected(exit_ok.error());
    }
    config.exit_ok = static_cast<int>(exit_ok.value());
    const std::expected<long long, BootError> exit_violations =
        require_integer(*exit_codes.value(), "violations", "exit_codes");
    if (!exit_violations.has_value()) {
        return std::unexpected(exit_violations.error());
    }
    config.exit_violations = static_cast<int>(exit_violations.value());
    const std::expected<long long, BootError> exit_boot =
        require_integer(*exit_codes.value(), "boot", "exit_codes");
    if (!exit_boot.has_value()) {
        return std::unexpected(exit_boot.error());
    }
    config.exit_boot = static_cast<int>(exit_boot.value());
    const std::expected<long long, BootError> exit_usage =
        require_integer(*exit_codes.value(), "usage", "exit_codes");
    if (!exit_usage.has_value()) {
        return std::unexpected(exit_usage.error());
    }
    config.exit_usage = static_cast<int>(exit_usage.value());

    const std::expected<const Json::Array*, BootError> severity_names =
        require_array(root, "severity_names", "root");
    if (!severity_names.has_value()) {
        return std::unexpected(severity_names.error());
    }
    for (const Json& element : *severity_names.value()) {
        const std::expected<std::string, BootError> text =
            array_string(element, "severity_names element");
        if (!text.has_value()) {
            return std::unexpected(text.error());
        }
        config.severity_names.push_back(text.value());
    }

    const std::expected<const Json::Array*, BootError> fail_on = require_array(root, "fail_on", "root");
    if (!fail_on.has_value()) {
        return std::unexpected(fail_on.error());
    }
    for (const Json& element : *fail_on.value()) {
        const std::expected<long long, BootError> number = array_integer(element, "fail_on element");
        if (!number.has_value()) {
            return std::unexpected(number.error());
        }
        config.fail_on.push_back(static_cast<int>(number.value()));
    }

    const std::expected<const Json*, BootError> self_test = require_object(root, "self_test", "root");
    if (!self_test.has_value()) {
        return std::unexpected(self_test.error());
    }
    const std::expected<std::string, BootError> manifest =
        require_string(*self_test.value(), "manifest", "self_test");
    if (!manifest.has_value()) {
        return std::unexpected(manifest.error());
    }
    std::error_code manifest_error;
    const std::filesystem::path resolved_manifest =
        std::filesystem::weakly_canonical(config_directory / manifest.value(), manifest_error);
    config.manifest_path = manifest_error ? manifest.value() : resolved_manifest.generic_string();

    const std::expected<const Json*, BootError> rules = require_object(root, "rules", "root");
    if (!rules.has_value()) {
        return std::unexpected(rules.error());
    }
    const std::optional<const Json::Object*> rule_table = rules.value()->as_object();
    if (rule_table.value()->empty()) {
        return std::unexpected(missing_key("rules entries"));
    }
    std::map<std::string, const Json*> ordered_rules;
    for (const Json::Member& member : *rule_table.value()) {
        ordered_rules.emplace(member.first, &member.second);
    }
    for (const auto& [id, entry] : ordered_rules) {
        const std::expected<Rule, BootError> rule = parse_rule(id, *entry);
        if (!rule.has_value()) {
            return std::unexpected(rule.error());
        }
        config.rules.push_back(rule.value());
    }
    return config;
}

}  // namespace cep::lint
