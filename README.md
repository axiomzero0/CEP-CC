# CEP&CC

**Cycle-Exact Performance & Clean Code — Psychopathic Tier.**

The normative standard is [`standard.md`](standard.md). It is versioned, self-enforcing, and hostile to non-compliant code (section 34: extermination policy).

## Repository layout

```text
standard.md              The CEP&CC 0.1 standard (normative)
.cep/cep_lint.json       Lint policy: every rule, pattern, severity, and message as data (§32.1)
tools/cep_lint/          Mechanical enforcement tool: C++26, CEP-2 class
tools/cep_lint/src/      Engine sources (self-linted to zero findings)
tools/cep_lint/tests/    Self-test manifest, violation fixtures, and profiles
.github/workflows/       CI: build, self-lint, self-test, sanitizers (§16)
```

## Mechanical enforcement

The standard is enforced by `cep_lint`, a C++26 tool whose entire policy lives in `.cep/cep_lint.json`. Section 50 of the standard defines the rule catalog (18 rules) and the contract that keeps the document and the configuration synchronized: the `CEP-LINT-DOC-SYNC` rule fails the run if either side drifts (Law 8: no stale documentation).

```sh
# build (warnings are errors, CEP&CC 6.3 warning set)
tools/cep_lint/build.sh

# lint anything
tools/cep_lint/build/cep_lint <files or directories>

# self-lint gate: the tool's own sources must be clean
tools/cep_lint/build/cep_lint tools/cep_lint/src

# self-test gate: fixtures with exact expected counts
tools/cep_lint/build/cep_lint --self-test

# sanitizer gate (CEP&CC 6.6)
MODE=asan tools/cep_lint/build.sh && tools/cep_lint/build/cep_lint --self-test
```

Run commands from the repository root so the default configuration path `.cep/cep_lint.json` resolves.

## Changing the rules

Add or edit a rule in `.cep/cep_lint.json` and document it under section 50.3 of `standard.md` in the same change. `CEP-LINT-DOC-SYNC` rejects the split. Severity classes and required responses are defined in section 34.2.
