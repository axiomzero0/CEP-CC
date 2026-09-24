// CEP:FILE: tools/cep_lint/src/json_model.cpp
// CEP:WHAT: Implementation of the JSON value model accessors.
// CEP:WHY: Keeps the DOM interface out of the parser and makes value access failure-aware.
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: All accessors report absence with optionals; no failure path exists here.
// CEP:ASSUMES: The stored variant is the only state; no aliasing exists.
// CEP:COST: Offline tool; runtime cost irrelevant.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario self-source-clean.
#include "json_model.hpp"

namespace cep::lint {

Json::Json(bool value) noexcept : value_{value} {}

Json::Json(long long value) noexcept : value_{value} {}

Json::Json(double value) noexcept : value_{value} {}

Json::Json(std::string value) noexcept : value_{std::move(value)} {}

Json::Json(Array value) noexcept : value_{std::move(value)} {}

Json::Json(Object value) noexcept : value_{std::move(value)} {}

bool Json::is_null() const noexcept { return std::holds_alternative<std::nullptr_t>(value_); }

bool Json::is_bool() const noexcept { return std::holds_alternative<bool>(value_); }

bool Json::is_number() const noexcept {
    return std::holds_alternative<long long>(value_) || std::holds_alternative<double>(value_);
}

bool Json::is_string() const noexcept { return std::holds_alternative<std::string>(value_); }

bool Json::is_array() const noexcept { return std::holds_alternative<Array>(value_); }

bool Json::is_object() const noexcept { return std::holds_alternative<Object>(value_); }

std::optional<bool> Json::as_bool() const noexcept {
    if (const bool* value = std::get_if<bool>(&value_)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<long long> Json::as_integer() const noexcept {
    if (const long long* value = std::get_if<long long>(&value_)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<double> Json::as_floating() const noexcept {
    if (const double* value = std::get_if<double>(&value_)) {
        return *value;
    }
    return std::nullopt;
}

std::optional<std::string_view> Json::as_string() const noexcept {
    if (const std::string* value = std::get_if<std::string>(&value_)) {
        return std::string_view{*value};
    }
    return std::nullopt;
}

std::optional<const Json::Array*> Json::as_array() const noexcept {
    if (const Array* value = std::get_if<Array>(&value_)) {
        return value;
    }
    return std::nullopt;
}

std::optional<const Json::Object*> Json::as_object() const noexcept {
    if (const Object* value = std::get_if<Object>(&value_)) {
        return value;
    }
    return std::nullopt;
}

std::optional<const Json*> Json::find(std::string_view key) const noexcept {
    if (const Object* object = std::get_if<Object>(&value_)) {
        for (const Member& member : *object) {
            if (member.first == key) {
                return &member.second;
            }
        }
    }
    return std::nullopt;
}

}  // namespace cep::lint
