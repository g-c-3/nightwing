// src/eval/tempo.cpp
//
// See tempo.h.

#include "eval/tempo.h"

namespace nightwing::eval {

Score tempo_value(const board::Position& pos) noexcept {
    return pos.side_to_move == board::Color::White ? kTempoBonus : -kTempoBonus;
}

} // namespace nightwing::eval
