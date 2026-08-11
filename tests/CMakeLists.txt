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
)
target_link_libraries(nightwing_tests PRIVATE nightwing_lib Catch2::Catch2WithMain)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(CTest)
include(Catch)
catch_discover_tests(nightwing_tests)
