// tests/fake_uci_engine.cpp
//
// A deliberately misbehaving UCI "engine" used only by
// tests/uci_match_tests.cpp to exercise tuner::UciMatchSession's failure
// policy (uci_match.h) without depending on a real engine bug. It speaks
// just enough UCI to pass the handshake, then misbehaves on `go`
// according to its first command-line argument:
//
//   illegal  replies `bestmove a1a1`, a move that is never legal
//   crash    exits with status 1 as soon as `go` arrives
//   hang     never answers `go` (still exits cleanly on `quit`)
//   slow     waits 600 ms, then replies `bestmove a1a1` (used by the
//            clock-mode tests: the delay alone must cost the game on time,
//            before the illegal move is ever judged)
//
// From-scratch test helper.

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

int main(int argc, char** argv) {
    const std::string mode = (argc > 1) ? argv[1] : "illegal";
    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line == "uci") {
            std::cout << "id name fake\nuciok" << std::endl;
        } else if (line == "isready") {
            std::cout << "readyok" << std::endl;
        } else if (line.rfind("go", 0) == 0) {
            if (mode == "illegal") {
                std::cout << "bestmove a1a1" << std::endl;
            } else if (mode == "crash") {
                std::exit(1);
            } else if (mode == "slow") {
                std::this_thread::sleep_for(std::chrono::milliseconds(600));
                std::cout << "bestmove a1a1" << std::endl;
            }
            // "hang": say nothing.
        } else if (line == "quit") {
            break;
        }
    }
    return 0;
}
