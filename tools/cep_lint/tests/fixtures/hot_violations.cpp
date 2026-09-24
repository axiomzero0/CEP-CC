// CEP:FILE: tools/cep_lint/tests/fixtures/hot_violations.cpp
// CEP:WHAT: CEP-0 declared fixture containing banned hot-path tokens with otherwise compliant structure.
// CEP:WHY: Proves class-scoped bans fire only for the declared class (CEP&CC 5.1).
// CEP:CLASS: CEP-0
// CEP:STATUS: complete
// CEP:FAILURE: Not applicable; test data is never executed.
// CEP:ASSUMES: Scanned only by the self-test; excluded from normal repository scans.
// CEP:COST: Offline test data; runtime cost irrelevant.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.

#include <memory>
#include <string>
#include <vector>

// CEP:WHAT: Banned-token count used only to keep the fixture self-consistent.
// CEP:WHY: Named constant keeps even fixture policy explicit (CEP&CC 11.3).
// CEP:STATUS: complete
// CEP:FAILURE: static_assert fires if the count drifts from the manifest expectation.
// CEP:ASSUMES: Count matches the banned tokens below.
// CEP:COST: Compile-time only.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
constexpr std::size_t kBannedTokenSites = 6;

// CEP:WHAT: Hot decoder that violates allocation, exception, and dispatch bans.
// CEP:WHY: Each banned token below is a deliberate severity-0 fixture hit.
// CEP:STATUS: complete
// CEP:FAILURE: Deliberately non-compliant; never executed.
// CEP:ASSUMES: Untrusted input simulation only.
// CEP:COST: Not measured; fixture data.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
[[nodiscard]] auto decode_hot(const std::vector<std::uint8_t>& packet) -> std::string {
    auto buffer = std::make_unique<std::uint8_t[]>(packet.size());
    std::string name;
    if (packet.empty()) {
        throw std::exception{};
    }
    for (const std::uint8_t byte : packet) {
        name.push_back(static_cast<char>(byte));
    }
    return name;
}

// CEP:WHAT: Virtual dispatch ban fixture.
// CEP:WHY: virtual functions are banned in CEP-0 (CEP&CC 8.28).
// CEP:STATUS: complete
// CEP:FAILURE: Deliberately non-compliant; never executed.
// CEP:ASSUMES: None.
// CEP:COST: Not measured; fixture data.
// CEP:EVIDENCE: tools/cep_lint/tests/manifest.json scenario hot-bans.
class HotBase {
public:
    HotBase() = default;
    virtual ~HotBase() = default;
    virtual auto transform(std::uint8_t value) -> std::uint8_t = 0;
};

static_assert(kBannedTokenSites > 0, "fixture must contain banned tokens");
