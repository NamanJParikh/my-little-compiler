# SimpleASM Review

No changes have been made. The existing SimpleASM suites pass locally, but the following review items were found:

| Severity | Area | Finding and suggested change |
|---|---|---|
| Medium | Parser | [`parseImmediate_`](compiler.cpp) uses `std::stoll(..., nullptr)` and therefore accepts trailing garbage. For example, `d12junk` is currently parsed as `12`. Add full-token validation for decimal, binary, and hexadecimal payloads. |
| Medium | Parser | [`parseCode_`](compiler.cpp) splits operands on every comma, so text such as `t'Hello, world'` is misparsed. Introduce comma-aware operand splitting or tokenize commas outside text quotes. |
| Medium | Parser | [`parseRegister_`](compiler.cpp) and [`parseOperand_`](compiler.cpp) accept forms that should be rejected, including `R+1` and potentially `R01`. Validate the exact `R`/decimal format and label capitalization consistently. |
| Medium | Parser | Labels are not consistently validated. [`parseData_`](compiler.cpp) accepts lowercase or malformed labels, while code-label parsing has stricter ad hoc rules. Centralize the “all-caps label” validation, including underscore conventions. |
| Medium | Parser/CLI | Semantic validation occurs only in emitters. `--emit print` accepts unknown code labels, unknown data labels, and invalid operand counts. If printing is meant to be a parser-only mode, document that; otherwise add backend-independent semantic validation. |
| Medium | Python emitter | [`write_text`](compiler.cpp) accepts `address == len(memory)`, although that address is outside declared storage. Change the boundary check to `address >= len(memory)` and add a fixture for the exact end-of-memory address. |
| Low | Python emitter | [`signed_divide`](compiler.cpp) computes an overflowing positive quotient for `INT64_MIN / -1`, relying on later masking to preserve `INT64_MIN`. It is correct for the 64-bit result, but make the overflow case explicit for maintainability and add a Python fixture for it. |
| Low | x86 emitter | [`emitInstruction`](compiler.cpp) contains AI-era clarification/comments such as `signedMultiply is never used`. Remove dead parameters/comments and split overly large instruction-emission logic into clearer helpers if desired. |
| Medium | Tests | Parser tests lack malformed numeric suffixes, text containing commas, exact register syntax, lowercase/malformed labels, and semantic label/operand validation. |
| Medium | Tests | Python tests lack `INT64_MIN / -1`, exact-end text addresses, `0`-byte or invalid memory widths, and register-spilling coverage for all R9-R15 operations. |
| Low | Tests | x86 tests deliberately omit runtime negative-address and division-zero cases, but should retain them as explicit compile-time or code-generation rejection tests rather than `compile_only`/undocumented native faults. |
| Low | Tests | Add a cross-emitter differential harness for signed arithmetic, division boundaries, memory widths, control flow, and text output rather than maintaining separate, independent JSON/Python expectations only. |
