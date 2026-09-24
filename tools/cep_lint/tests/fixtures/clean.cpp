// CEP:FILE: tools/cep_lint/tests/fixtures/clean.cpp
// CEP:WHAT: Fully compliant fixture proving the rules stay silent on correct code.
// CEP:WHY: A clean control file catches over-matching rules (CEP&CC 34.9 regression testing).
// CEP:CLASS: CEP-2
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; test data is never executed.
// CEP:ASSUMES: Scanned only by the self-test; excluded from normal repository scans.
// CEP:COST: Offline test data; runtime cost irrelevant.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario clean-file.

#include <cstdint>
#include <string_view>

namespace cep::lint::fixtures {

// CEP:WHAT: Width parameters for the compliant decoder fixture.
// CEP:WHY: Named constants are the sanctioned form for numeric policy (CEP&CC 11.3).
// CEP:STATUS: complete
// CEP:FAILURE: static_assert fires if the width does not match the fixture contract.
// CEP:ASSUMES: Fixture-only contract.
// CEP:COST: Compile-time only; no runtime instructions.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario clean-file.
constexpr std::uint32_t kFixtureFieldWidth = 512;
constexpr std::uint32_t kFixtureMask = 0xFF;
constexpr std::uint32_t kFixtureByteShift = 8;

// CEP:WHAT: Decodes a fixed-width field from a bounded view.
// CEP:WHY: Demonstrates compliant structure: named constants, computed results, explicit failure.
// CEP:STATUS: complete
// CEP:FAILURE: Returns empty optional when the view is too short.
// CEP:ASSUMES: Input is trusted fixture data.
// CEP:COST: Offline test data; constant-time decode.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario clean-file.
[[nodiscard]] auto decode_field(std::string_view bytes, std::size_t offset) -> std::optional<std::uint32_t> {
    if (offset + sizeof(std::uint32_t) > bytes.size()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    for (std::size_t index = 0; index < sizeof(std::uint32_t); index = index + 1) {
        value = (value << kFixtureByteShift) | static_cast<std::uint32_t>(bytes[offset + index]);
    }
    return value & kFixtureMask;
}

// CEP:WHAT: Computes the fixture checksum over decoded fields.
// CEP:WHY: Demonstrates a compliant multi-statement function with a computed result.
// CEP:STATUS: complete
// CEP:FAILURE: Never fails for bounded input.
// CEP:ASSUMES: Fields were decoded successfully by decode_field.
// CEP:COST: Offline test data; linear in field count.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario clean-file.
[[nodiscard]] auto checksum_of(std::string_view bytes) -> std::uint64_t {
    std::uint64_t total = 0;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const std::optional<std::uint32_t> field = decode_field(bytes, offset);
        if (!field.has_value()) {
            break;
        }
        total = total + field.value();
        offset = offset + kFixtureFieldWidth / kFixtureFieldWidth + sizeof(std::uint32_t);
    }
    return total;
}

// CEP:WHAT: Verifies the fixture width contract at compile time.
// CEP:WHY: static_assert is the sanctioned enforcement for compile-time assumptions (CEP&CC 8.21, 11.2).
// CEP:STATUS: complete
// CEP:FAILURE: Compilation fails if the width drifts.
// CEP:ASSUMES: None.
// CEP:COST: Compile-time only.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario clean-file.
static_assert(kFixtureFieldWidth > kFixtureMask, "fixture width must exceed its mask");

}  // namespace cep::lint::fixtures
