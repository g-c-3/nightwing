include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
)
FetchContent_MakeAvailable(Catch2)

add_executable(nightwing_tests
    test_smoke.cpp
    bitboard_tests.cpp
    attacks_tests.cpp
    board_tests.cpp
    zobrist_tests.cpp
    masks_tests.cpp
    movegen_tests.cpp
    makemove_tests.cpp
    fen_tests.cpp
    perft_tests.cpp
    eval_tests.cpp
    pawns_tests.cpp
    mobility_tests.cpp
    king_safety_tests.cpp
    piece_bonuses_tests.cpp
    knight_outposts_tests.cpp
    space_tests.cpp
    threats_tests.cpp
    king_tropism_tests.cpp
    trapped_pieces_tests.cpp
    tempo_tests.cpp
    material_imbalance_tests.cpp
    endgame_tests.cpp
    king_pawn_endgame_tests.cpp
    rook_endgame_tests.cpp
    minor_piece_endgame_tests.cpp
    fortress_tests.cpp
    basic_mates_tests.cpp
    endgame_suite_tests.cpp
    book_tests.cpp
    pawn_tt_tests.cpp
    eval_cache_tests.cpp
    ordering_tests.cpp
    quiescence_tests.cpp
    search_tests.cpp
    see_tests.cpp
    tt_tests.cpp
    lazy_smp_tests.cpp
    uci_tests.cpp
    pondering_tests.cpp
    bench_tests.cpp
    selfplay_tests.cpp
    tune_tests.cpp
    match_tests.cpp
)
target_link_libraries(nightwing_tests PRIVATE nightwing_lib Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(nightwing_tests)
