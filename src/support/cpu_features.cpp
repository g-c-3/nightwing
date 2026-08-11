// src/support/cpu_features.cpp
//
// See cpu_features.h for contract. Detection uses compiler-provided
// intrinsics rather than hand-rolled CPUID parsing where available, per
// project performance-engineering standards (ARCHITECTURE.md).

#include "support/cpu_features.h"

#include <cstdio>

#if defined(_MSC_VER)
#include <intrin.h>
#endif

namespace nightwing::support {

namespace {

bool g_has_bmi2 = false;
bool g_has_popcnt = false;
bool g_detected = false;
char g_summary[64] = "undetected";

void build_summary() {
    if (g_has_bmi2 && g_has_popcnt) {
        std::snprintf(g_summary, sizeof(g_summary), "BMI2 POPCNT");
    } else if (g_has_popcnt) {
        std::snprintf(g_summary, sizeof(g_summary), "POPCNT (no BMI2)");
    } else {
        std::snprintf(g_summary, sizeof(g_summary), "portable (no BMI2/POPCNT)");
    }
}

} // namespace

void detect_cpu_features() {
    if (g_detected) {
        return;
    }

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
    int info[4] = {0, 0, 0, 0};
    __cpuid(info, 0);
    int max_leaf = info[0];

    if (max_leaf >= 1) {
        __cpuid(info, 1);
        g_has_popcnt = (info[2] & (1 << 23)) != 0; // ECX bit 23
    }
    if (max_leaf >= 7) {
        int info7[4] = {0, 0, 0, 0};
        __cpuidex(info7, 7, 0);
        g_has_bmi2 = (info7[1] & (1 << 8)) != 0; // EBX bit 8
    }
#elif (defined(__GNUC__) || defined(__clang__)) && \
      (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    g_has_bmi2 = __builtin_cpu_supports("bmi2");
    g_has_popcnt = __builtin_cpu_supports("popcnt");
#else
    // Non-x86 target (e.g. ARM/Apple Silicon) or unknown compiler: BMI2 and
    // POPCNT are x86 ISA extensions and simply don't apply here — reporting
    // them absent is correct, not a fallback compromise. __builtin_cpu_supports
    // is itself an x86-only GCC/Clang builtin, so it must not be called here.
    g_has_bmi2 = false;
    g_has_popcnt = false;
#endif

    build_summary();
    g_detected = true;
}

bool cpu_has_bmi2() { return g_has_bmi2; }
bool cpu_has_popcnt() { return g_has_popcnt; }
const char* cpu_feature_summary() { return g_summary; }

} // namespace nightwing::support
