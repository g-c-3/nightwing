# Makefile for OpenBench and plain `make`. CMake remains the primary build.
#
# OpenBench invokes:  make -j EXE=<name>  (and may pass CXX=...). The binary
# is written to ./<name> (MinGW adds .exe on Windows).
#
#   make                         portable Release build, ./nightwing
#   make EXE=nightwing-abc123    name the output (OpenBench does this)
#   make CXX=clang++             pick the compiler
#   make ARCH=-march=x86-64-v3   AVX2 build (needs a Haswell-or-newer CPU)
#   make LTO=0                   disable link-time optimization
#   make VERSION=1.0.1234        version string reported by UCI `id name`
#
# Same source list and release flags as the CMake nightwing_lib / nightwing
# targets (-O3, -DNDEBUG, runtime-dispatched BMI2 path, -pthread, C++20,
# LTO). The default build is portable: no -march flag, so every OpenBench
# worker produces the same bench node count.

EXE      ?= nightwing
CXX      ?= g++
VERSION  ?= 1.0.0
ARCH     ?=
SRC_DIR   = src
GEN_DIR   = build-make/generated
OBJ_DIR   = build-make/obj

SRCS := $(addprefix $(SRC_DIR)/, \
  support/cpu_features.cpp board/bitboard.cpp board/attacks.cpp board/board.cpp \
  board/zobrist.cpp board/masks.cpp board/move.cpp board/movegen.cpp board/fen.cpp \
  board/perft.cpp eval/psqt.cpp eval/pawns.cpp eval/pawn_tt.cpp eval/mobility.cpp \
  eval/king_safety.cpp eval/piece_bonuses.cpp eval/knight_outposts.cpp eval/space.cpp \
  eval/threats.cpp eval/king_tropism.cpp eval/trapped_pieces.cpp eval/tempo.cpp \
  eval/material_imbalance.cpp eval/endgame.cpp eval/king_pawn_endgame.cpp \
  eval/rook_endgame.cpp eval/minor_piece_endgame.cpp eval/fortress.cpp \
  eval/basic_mates.cpp eval/eval_cache.cpp eval/incremental.cpp eval/eval.cpp \
  search/ordering.cpp search/quiescence.cpp search/search.cpp search/see.cpp \
  search/skill.cpp search/tt.cpp book/book.cpp uci/uci.cpp tuner/selfplay.cpp \
  tuner/tune.cpp tuner/match.cpp tuner/sprt.cpp tuner/spsa.cpp tuner/spsa_eval.cpp \
  main.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS))

# The BMI2/PEXT code path only exists on x86; CMake gates it the same way.
ifeq ($(OS),Windows_NT)
  MACHINE := x86_64
else
  MACHINE := $(shell uname -m)
endif
ifneq (,$(filter x86_64 amd64 AMD64 i386 i486 i586 i686,$(MACHINE)))
  BMI2_DEF = -DNIGHTWING_ENABLE_BMI2=1
endif

# LTO is on by default for GCC only: Clang's LTO needs a linker plugin that
# is not present on every machine, and it never changes the bench result.
LTO ?= $(if $(findstring clang,$(CXX)),0,1)
ifeq ($(LTO),1)
  LTO_FLAG = -flto
endif

# Windows (MinGW): link statically so the binary runs without extra DLLs.
ifeq ($(OS),Windows_NT)
  LDFLAGS += -static
endif

CXXFLAGS_ALL = -std=c++20 -O3 -DNDEBUG $(BMI2_DEF) $(ARCH) -pthread $(LTO_FLAG) \
               -I$(SRC_DIR) -I$(GEN_DIR) -MMD -MP $(CXXFLAGS)

all: $(EXE)

$(GEN_DIR)/nightwing/version.h: Makefile
	@mkdir -p $(dir $@)
	@printf '#pragma once\n#define NIGHTWING_VERSION_STRING "$(VERSION)"\n' > $@

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp $(GEN_DIR)/nightwing/version.h
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS_ALL) -c $< -o $@

$(EXE): $(OBJS)
	$(CXX) $(CXXFLAGS_ALL) $(OBJS) -o $@ $(LDFLAGS)

clean:
	rm -rf build-make $(EXE) $(EXE).exe

-include $(OBJS:.o=.d)

.PHONY: all clean
