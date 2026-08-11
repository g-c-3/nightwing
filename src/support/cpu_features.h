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

/// Returns true if the running CPU supports BMI2 (PEXT/PDEP instructions).
/// Only meaningful after detect_cpu_features() has been called.
bool cpu_has_bmi2();

/// Returns true if the running CPU supports the POPCNT instruction.
/// Only meaningful after detect_cpu_features() has been called.
bool cpu_has_popcnt();

/// Returns a short human-readable summary of detected features, e.g.
/// "BMI2 POPCNT" or "portable (no BMI2/POPCNT)". Used for UCI id/debug output.
const char* cpu_feature_summary();

} // namespace nightwing::support
