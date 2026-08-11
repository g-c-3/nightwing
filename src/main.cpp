// src/main.cpp
//
// Nightwing entry point. Phase 0: just enough to prove the build/CI
// pipeline works end-to-end (empty-engine skeleton). The body of main()
// is replaced by the UCI loop (uci/uci.cpp) in Phase 2.

#include <cstdio>

#include "support/cpu_features.h"

int main() {
    nightwing::support::detect_cpu_features();
    std::printf("Nightwing (dev build) - CPU features: %s\n",
                nightwing::support::cpu_feature_summary());
    return 0;
}
