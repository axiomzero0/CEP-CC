// CEP:FILE: tools/cep_lint/src/json_model.hpp
// CEP:WHAT: Value model for JSON documents used by the lint configuration.
// CEP:WHY: The configuration is pure data (CEP&CC 32.1); a minimal ordered DOM keeps rule policy out of engine code.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: None at this layer; accessors return optionals instead of throwing.
// CEP:ASSUMES: Object member order is preserved as written; duplicate keys are rejected by the parser.
// CEP:COST: Offline tool; runtime cost irrelevant (CEP&CC 10.8 cold profile).
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace cep::lint {

// CEP:WHAT: A JSON value: null, boolean, integer, floating, string, array, or ordered object.
// CEP:WHY: The lint configuration is data-driven (Law 7); the engine must not hard-code policy.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; a value type.
// CEP:ASSUMES: Integers fit in long long; configuration floats are only used for documentation.
// CEP:COST: Offline tool; runtime cost irrelevant.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
class Json {
public:
    using Array = std::vector<Json>;
    using Member = std::pair<std::string, Json>;
    using Object = std::vector<Member>;

    Json() noexcept = default;
    Json(bool value) noexcept;
    Json(long long value) noexcept;
    Json(double value) noexcept;
    Json(std::string value) noexcept;
    Json(Array value) noexcept;
    Json(Object value) noexcept;

    [[nodiscard]] bool is_null() const noexcept;
    [[nodiscard]] bool is_bool() const noexcept;
    [[nodiscard]] bool is_number() const noexcept;
    [[nodiscard]] bool is_string() const noexcept;
    [[nodiscard]] bool is_array() const noexcept;
    [[nodiscard]] bool is_object() const noexcept;

    [[nodiscard]] std::optional<bool> as_bool() const noexcept;
    [[nodiscard]] std::optional<long long> as_integer() const noexcept;
    [[nodiscard]] std::optional<double> as_floating() const noexcept;
    [[nodiscard]] std::optional<std::string_view> as_string() const noexcept;
    [[nodiscard]] std::optional<const Array*> as_array() const noexcept;
    [[nodiscard]] std::optional<const Object*> as_object() const noexcept;

    // CEP:WHAT: Looks up an object member by key.
    // CEP:WHY: Configuration access must be explicit and failure-aware (Law 6).
    // CEP:STATUS: complete
    // CEP:FAILURE: Returns empty optional when the value is not an object or the key is absent.
    // CEP:ASSUMES: Objects are small; linear search is acceptable for a cold tool.
    // CEP:COST: Linear in object size; offline tool.
    // CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.
    [[nodiscard]] std::optional<const Json*> find(std::string_view key) const noexcept;

private:
    std::variant<std::nullptr_t, bool, long long, double, std::string, Array, Object> value_;
};

}  // namespace cep::lint
