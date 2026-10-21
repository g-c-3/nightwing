// src/support/cpu_features.cpp
//
// See cpu_features.h for contract. Detection uses compiler-provided
// intrinsics rather than hand-rolled CPUID parsing where available, per
// project performance-engineering standards (ARCHITECTURE.md).

#include "support/cpu_features.h"

#include <cstdio>

#if defined(_MSC_VER)
#include <intrin.h>
#elif (defined(__GNUC__) || defined(__clang__)) && (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
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

bool cpu_pext_is_slow(unsigned vendor_ebx, unsigned vendor_edx, unsigned vendor_ecx,
                      unsigned leaf1_eax) {
    // "AuthenticAMD" in CPUID leaf 0: EBX "Auth", EDX "enti", ECX "cAMD".
    const bool is_amd =
        vendor_ebx == 0x68747541u && vendor_edx == 0x69746e65u && vendor_ecx == 0x444d4163u;
    if (!is_amd) {
        return false;
    }
    unsigned family = (leaf1_eax >> 8) & 0xFu;
    if (family == 0xFu) {
        family += (leaf1_eax >> 20) & 0xFFu; // extended family
    }
    return family <= 0x17u; // Zen 1/2 are 0x17; Zen 3+ (0x19, 0x1A) have fast PEXT
}

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
    if (g_has_bmi2) {
        int v[4] = {0, 0, 0, 0};
        int l1[4] = {0, 0, 0, 0};
        __cpuid(v, 0);
        __cpuid(l1, 1);
        if (cpu_pext_is_slow(static_cast<unsigned>(v[1]), static_cast<unsigned>(v[3]),
                             static_cast<unsigned>(v[2]), static_cast<unsigned>(l1[0]))) {
            g_has_bmi2 = false;
        }
    }
#elif (defined(__GNUC__) || defined(__clang__)) && \
      (defined(__x86_64__) || defined(__i386__))
    __builtin_cpu_init();
    g_has_bmi2 = __builtin_cpu_supports("bmi2");
    g_has_popcnt = __builtin_cpu_supports("popcnt");
    if (g_has_bmi2) {
        unsigned a = 0, b = 0, c = 0, d = 0;
        unsigned eax1 = 0, unused_b = 0, unused_c = 0, unused_d = 0;
        if (__get_cpuid(0, &a, &b, &c, &d) && __get_cpuid(1, &eax1, &unused_b, &unused_c, &unused_d) &&
            cpu_pext_is_slow(b, d, c, eax1)) {
            g_has_bmi2 = false;
        }
    }
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
