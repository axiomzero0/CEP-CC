// CEP:FILE: tools/cep_lint/tests/fixtures/violations.cpp
// CEP:WHAT: Deliberate violation fixture exercising every comment, literal, structure, and naming rule.
// CEP:WHY: The self-test manifest needs exact expected counts so regressions are loud (CEP&CC 34.9).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; test data is never executed.
// CEP:ASSUMES: Scanned only by the self-test; excluded from normal repository scans.
// CEP:COST: Offline test data; runtime cost irrelevant.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario violation-sweep.

// TODO: fix the decoder eventually
// fixme: this whole file is intentionally wrong

#define LEGACY_MAX_PACKET 4096

#include <cstdint>
#include <string>

struct bad_name_with_underscores {
    std::uint32_t value;
};

// CEP:STATUS: finished

enum class broken_status : std::uint8_t {
    first,
};

// CEP:STATUS: stub

constexpr std::uint32_t kNamedLimit = 512;

[[nodiscard]] auto compute_total(std::uint32_t first, std::uint32_t second) -> std::uint32_t {
    const std::uint32_t threshold = 47;
    const std::uint32_t wide = 0x1000;
    const double ratio = 3.5;
    const std::uint32_t total = first + second + threshold + wide + kNamedLimit;
    return total > static_cast<std::uint32_t>(ratio) ? total : first;
}

auto GiveMeCamelCase(std::uint32_t input) -> std::uint32_t {
    const std::uint32_t doubled = input + input;
    const std::uint32_t tripled = doubled + input;
    return tripled;
}

auto consume_packet(const std::string& packet) -> bool {
    try {
        return packet.size() > kNamedLimit;
    } catch (const std::exception&) {
    }
    return false;
}

auto clamp_packet(std::uint32_t value) -> std::uint32_t {
    if (value > 4096) {
        return 4096;
    }
    if (value < 16) {
        return 16;
    }
    return value;
}

void drain_queue() {
    for (std::uint32_t attempt = 0; attempt < 1; ++attempt) {}
}

auto AlwaysTrue(std::uint32_t value) -> bool { return true; }

void EmptyStub() {}

auto callback_factory() {
    return []() {};
}

auto report_status() -> const char* { return "not implemented"; }

auto unused_helper() -> std::uint32_t {
    // return old_value;
    return kNamedLimit;
}
