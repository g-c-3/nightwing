// src/main.cpp
//
// Nightwing entry point. Phase 0/1: enough to prove the build/CI pipeline
// works end-to-end and that the board subsystem's mandatory startup
// sequence (ARCHITECTURE.md "Startup Sequence") is wired up correctly.
// The body of main() is replaced by the UCI loop (uci/uci.cpp) in Phase 2.

#include <cstdio>

#include "board/attacks.h"
#include "board/board.h"
#include "board/masks.h"
#include "board/zobrist.h"
#include "support/cpu_features.h"

int main() {
    nightwing::support::detect_cpu_features();

    // Mandatory order per ARCHITECTURE.md: init_masks() ->
    // init_magic_bitboards() -> init_zobrist_keys(). init_magic_bitboards()
    // also runs CPU feature detection itself (see board/attacks.cpp), so
    // the explicit call above isn't strictly required for correctness,
    // but keeping it first and explicit here means the feature summary
    // below is guaranteed accurate regardless of internal implementation
    // details of the steps that follow.
    nightwing::board::init_masks();
    nightwing::board::init_magic_bitboards();
    nightwing::board::init_zobrist_keys();

    const nightwing::board::Position pos = nightwing::board::start_position();

    std::printf("Nightwing (dev build) - CPU features: %s\n",
                nightwing::support::cpu_feature_summary());
    std::printf("Startup sequence OK - start position zobrist hash: 0x%016llx\n",
                static_cast<unsigned long long>(pos.zobrist_hash));
    std::printf("%s", nightwing::board::to_string(pos).c_str());

    return 0;
}
