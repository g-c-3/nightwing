#pragma once
// src/eval/endgame.h
//
// Material-signature classifier (ROADMAP.md Phase 6's first item:
// "Material-signature classifier: detect endgame material buckets at
// each node, route to specialized endgame reasoning when matched").
// This file is deliberately scoped to CLASSIFICATION ONLY, not the
// actual specialized endgame reasoning itself: it answers "which
// recognized endgame material bucket, if any, does this position fall
// into," as a plain enum, and every subsequent Phase 6 item (King+pawn
// theory, rook endgame patterns, minor piece endgames, fortress
// detection, zugzwang-aware search shaping, the KPK/KRK/KBNK base
// heuristics) is expected to consult classify_endgame() and act on
// whichever EndgameSignature it returns -- none of THAT consuming code
// exists yet (there's nothing to route to until each of those items
// lands), so classify_endgame() is not yet called from eval::evaluate()
// or anywhere in search/. Building and thoroughly testing the
// classifier as its own, currently-unwired unit first mirrors this
// codebase's own established "self-contained piece before its
// consumer" precedent (docs/DECISIONS.md's tuner/selfplay.h entry, for
// a different pair of modules).
//
// SCOPE: every bucket below corresponds directly to a specific clause
// in ROADMAP.md's own Phase 6 item wording -- this enum is sized to
// exactly what the rest of Phase 6 already commits to building
// specialized reasoning for, not a speculative, open-ended taxonomy of
// every possible material configuration. This deliberately follows the
// same "narrowest version the item's own wording actually asks for"
// precedent eval/material_imbalance.h's own header comment already
// established for a different eval term. In particular, finer-grained
// distinctions WITHIN a bucket -- Lucena vs. Philidor vs. Vancura
// within RookEndgame, or "wrong bishop corner" within a future KBPK
// signature this file doesn't yet have -- are left to each later Phase
// 6 item's own positional judgment once it consults that bucket, not
// pre-built into the classifier now. A position that doesn't match any
// bucket below returns EndgameSignature::None and falls through to
// ordinary eval::evaluate() untouched, same as today.
//
// Classification is by PIECE COUNTS (board::popcount() over
// board::Position::pieces()), which side each piece belongs to, and,
// for OppositeColoredBishops specifically, each side's single bishop's
// square color -- no search, no attack tables, no positional judgment
// of any kind. "Which side" matters and is checked explicitly
// throughout classify_endgame()'s own implementation (endgame.cpp): a
// total piece count alone can't distinguish, say, "White has a bishop
// AND a knight, Black has bare king" (KBNK, the classical hardest
// basic mate) from "White has a bishop, Black has a knight, both bare
// otherwise" (a completely different, roughly balanced minor-piece
// endgame, KnightVsBishop below) -- both have exactly one total bishop
// and one total knight on the board.

#include "board/board.h"

namespace nightwing::eval {

/// A recognized endgame material bucket, or `None` if the position
/// doesn't match any of the specific patterns below -- see this file's
/// own header comment for the full scope rationale and why each
/// non-`None` value corresponds to a specific later Phase 6 item.
enum class EndgameSignature : std::uint8_t {
    /// No recognized special-case signature -- ordinary eval::evaluate()
    /// applies with no endgame-specific adjustment. The default for
    /// every position that isn't one of the specific patterns below,
    /// which is the overwhelming majority of positions ever classified.
    None = 0,

    /// King and exactly one pawn (either color, either side) vs. bare
    /// king -- no other material anywhere on the board. Feeds
    /// ROADMAP.md Phase 6's "King+pawn theory" item (opposition, key
    /// squares, corresponding squares, the rule of the square).
    KPK,

    /// King and exactly one rook (either color, either side) vs. bare
    /// king, no pawns anywhere -- the classical "basic checkmate"
    /// endgame. Feeds ROADMAP.md Phase 6's "Hand-built base heuristics
    /// carried over: KPK, KRK, KBNK exact-play rules" item.
    KRK,

    /// One side has exactly one bishop AND exactly one knight, the
    /// other side has bare king, no pawns anywhere -- the classical
    /// "hardest of the basic mates" endgame. Explicitly NOT the same as
    /// having one total bishop and one total knight split across both
    /// sides -- see KnightVsBishop below for that case, and this file's
    /// own header comment for why the distinction matters. Feeds the
    /// same ROADMAP.md item as KRK above.
    KBNK,

    /// Both sides have exactly one rook each and no other piece besides
    /// pawns (any pawn count, including zero) -- the broad "rook
    /// endgame" bucket ROADMAP.md Phase 6's "Rook endgame patterns" item
    /// (Lucena, Philidor, Vancura, rook-behind-passed-pawn) narrows
    /// further once matched; recognizing the bucket itself is this
    /// file's job, not distinguishing Lucena from Philidor, which needs
    /// actual pawn-structure/king-position judgment a later item adds.
    RookEndgame,

    /// One side has exactly one bishop and at least one pawn, the other
    /// side is completely bare (no pawns, no pieces of any kind) --
    /// zero knights/rooks/queens anywhere on the board. Feeds ROADMAP.md
    /// Phase 6's "Minor piece endgames" item, specifically its
    /// "wrong-bishop-corner draw detection" clause. Added Session 66,
    /// after the original six-bucket set (Session 64) turned out to
    /// have no bucket at all for this specific, already-anticipated
    /// case -- endgame.cpp's own is_light_square() comment named this
    /// exact gap in advance ("promote... if a later Phase 6 item (e.g.
    /// a future 'wrong bishop corner' signature) needs it more
    /// broadly"). Deliberately does NOT check pawn file(s) or count --
    /// whether the specific pawn(s) present are actually a same-file,
    /// promoting-on-the-bishop's-wrong-corner case is real positional
    /// judgment for the later "Minor piece endgames" consumer to make,
    /// the same "classification only, judgment later" split every
    /// other bucket here already follows (RookEndgame doesn't
    /// distinguish Lucena from Philidor internally either -- see this
    /// file's own header comment).
    KBPK,

    /// Each side has exactly one bishop, on OPPOSITE-colored squares,
    /// and neither side has any knight, rook, or queen (any pawn
    /// count) -- the classical "drawish tendency" bishop endgame. Feeds
    /// ROADMAP.md Phase 6's "Minor piece endgames" item, specifically
    /// its "opposite-colored bishop fortress/drawish-tendency eval
    /// adjustment" clause. Same-colored bishops are deliberately left
    /// as `None` here -- that pairing carries no such drawish tendency,
    /// and nothing in ROADMAP.md's Phase 6 wording calls for special
    /// handling of it.
    OppositeColoredBishops,

    /// One side has exactly one knight and no bishop, the other side
    /// has exactly one bishop and no knight, neither side has a rook or
    /// queen (any pawn count) -- feeds ROADMAP.md Phase 6's "Minor
    /// piece endgames" item, specifically its "knight vs. bishop
    /// endings weighted by pawn structure (open vs. closed)" clause.
    KnightVsBishop,
};

/// Classifies `pos` into one of the EndgameSignature buckets above,
/// purely from piece counts and which side each piece belongs to
/// (plus, for OppositeColoredBishops, the two single bishops' square
/// colors) -- this file's own header comment has the full scope
/// rationale. Returns EndgameSignature::None for any position that
/// doesn't match one of the specific patterns.
///
/// Precondition: none beyond what every other eval/*.h term already
/// requires as part of the mandatory startup sequence -- this function
/// only counts pieces already on the board and reads their squares, so
/// board::init_masks()/board::init_magic_bitboards() make no practical
/// difference to this function specifically (same situation as eval/
/// tempo.h's own tempo_value() and eval/material_imbalance.h's own
/// material_imbalance_value()).
[[nodiscard]] EndgameSignature classify_endgame(const board::Position& pos) noexcept;

} // namespace nightwing::eval
