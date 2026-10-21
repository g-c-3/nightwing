#pragma once
// src/support/cpu_features.h
//
// Runtime CPU feature detection (BMI2 PEXT/PDEP, POPCNT), used to select
// between the BMI2 fast-path and the portable magic-bitboard fallback at
// startup. Detection is done once and cached; callers should use the
// cached accessors (cpu_has_bmi2(), cpu_has_popcnt()) rather than
// re-probing per call.
//
// Compile-time availability of the BMI2 fast-path code (gated by the
// NIGHTWING_ENABLE_BMI2 build option — see root CMakeLists.txt) is
// independent of this: this header answers "can the CPU this binary is
// actually running on execute BMI2 instructions right now", which matters
// even on a BMI2-enabled build if it's later run on older hardware.

namespace nightwing::support {

/// Detects and caches CPU feature flags relevant to Nightwing's hot paths.
/// Must be called once during startup, before any code queries cpu_has_*().
/// Safe to call more than once (idempotent).
void detect_cpu_features();

/// Returns true if the running CPU supports BMI2 (PEXT/PDEP instructions)
/// AND PEXT is worth using on it: AMD Zen 1/2 (family 0x17) implement PEXT
/// in microcode, where it is slower than magic bitboards, so this reports
/// false there (the portable magic path is used instead; search results are
/// identical). Idea credit: Stockfish's build notes and CPW ("BMI2",
/// "Magic Bitboards"). Only meaningful after detect_cpu_features().
bool cpu_has_bmi2();

/// Pure helper for the Zen 1/2 rule above, taking raw CPUID values:
/// leaf 0 vendor registers (`vendor_ebx`/`vendor_edx`/`vendor_ecx`, the
/// "AuthenticAMD" string) and leaf 1 `eax` (family/model/stepping). Returns
/// true for AMD CPUs with display family 0x17 (Zen 1, Zen+, Zen 2) or any
/// earlier AMD family; false for Intel, Zen 3 and later (family >= 0x19) and
/// unknown vendors. Exposed for unit testing.
bool cpu_pext_is_slow(unsigned vendor_ebx, unsigned vendor_edx, unsigned vendor_ecx,
                      unsigned leaf1_eax);

/// Returns true if the running CPU supports the POPCNT instruction.
/// Only meaningful after detect_cpu_features() has been called.
bool cpu_has_popcnt();

/// Returns a short human-readable summary of detected features, e.g.
/// "BMI2 POPCNT" or "portable (no BMI2/POPCNT)". Used for UCI id/debug output.
const char* cpu_feature_summary();

} // namespace nightwing::support
