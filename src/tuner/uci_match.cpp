// src/tuner/uci_match.cpp
//
// Implementation of the two-process UCI-vs-UCI match runner declared in
// uci_match.h (see that file's header comment for scope, referee rules,
// and failure policy).
//
// Layout of this file:
//   1. Process      — a minimal child-process wrapper with line-oriented,
//                     time-limited reads (POSIX and Windows variants).
//   2. Referee      — game-state helpers built on the board layer.
//   3. UciMatchSession / play_uci_match() / parse_bestmove_line().
//
// From-scratch implementation; no code copied from any existing match
// runner or process library.

#include "tuner/uci_match.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <sstream>

#include "board/board.h"
#include "board/move.h"
#include "board/movegen.h"
#include "support/rng.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace nightwing::tuner {
namespace {

using Clock = std::chrono::steady_clock;

/// Result of one time-limited line read.
enum class ReadStatus { Line, Timeout, Closed };

/// Milliseconds left until `deadline`, never negative.
[[nodiscard]] int remaining_ms(Clock::time_point deadline) {
    const auto left =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

// ---------------------------------------------------------------------------
// 1. Process
// ---------------------------------------------------------------------------

#if defined(_WIN32)

/// Child process with piped stdin/stdout (Windows variant).
class Process {
public:
    Process() = default;
    ~Process() { terminate(); }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    /// Launches `command` with `args`. Returns false and sets `error`
    /// on failure.
    bool launch(const std::string& command, const std::vector<std::string>& args,
                std::string& error) {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.bInheritHandle = TRUE;

        HANDLE child_out_r = nullptr;
        HANDLE child_out_w = nullptr;
        HANDLE child_in_r = nullptr;
        HANDLE child_in_w = nullptr;
        if (!CreatePipe(&child_out_r, &child_out_w, &sa, 0) ||
            !CreatePipe(&child_in_r, &child_in_w, &sa, 0)) {
            error = "CreatePipe failed";
            return false;
        }
        // The parent's ends must not be inherited by the child.
        SetHandleInformation(child_out_r, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(child_in_w, HANDLE_FLAG_INHERIT, 0);

        std::string cmdline = quote(command);
        for (const std::string& arg : args) {
            cmdline += ' ';
            cmdline += quote(arg);
        }

        STARTUPINFOA si{};
        si.cb = sizeof(si);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdInput = child_in_r;
        si.hStdOutput = child_out_w;
        si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

        PROCESS_INFORMATION pi{};
        const BOOL ok = CreateProcessA(nullptr, cmdline.data(), nullptr, nullptr, TRUE, 0, nullptr,
                                       nullptr, &si, &pi);
        // The child's own ends are closed in the parent either way.
        CloseHandle(child_out_w);
        CloseHandle(child_in_r);
        if (!ok) {
            CloseHandle(child_out_r);
            CloseHandle(child_in_w);
            error = "CreateProcess failed for '" + command + "'";
            return false;
        }
        CloseHandle(pi.hThread);
        process_ = pi.hProcess;
        out_read_ = child_out_r;
        in_write_ = child_in_w;
        return true;
    }

    /// Writes `line` plus a newline to the child's stdin.
    bool write_line(const std::string& line) {
        if (in_write_ == nullptr) {
            return false;
        }
        const std::string data = line + "\n";
        DWORD written = 0;
        return WriteFile(in_write_, data.data(), static_cast<DWORD>(data.size()), &written,
                         nullptr) != 0 &&
               written == data.size();
    }

    /// Reads one line (without its terminator) within `timeout_ms`.
    ReadStatus read_line(std::string& out, int timeout_ms) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            if (extract_line(out)) {
                return ReadStatus::Line;
            }
            if (out_read_ == nullptr) {
                return ReadStatus::Closed;
            }
            DWORD available = 0;
            if (!PeekNamedPipe(out_read_, nullptr, 0, nullptr, &available, nullptr)) {
                return flush_or_closed(out);
            }
            if (available > 0) {
                char chunk[4096];
                const DWORD want = std::min<DWORD>(available, sizeof(chunk));
                DWORD got = 0;
                if (!ReadFile(out_read_, chunk, want, &got, nullptr) || got == 0) {
                    return flush_or_closed(out);
                }
                buffer_.append(chunk, got);
                continue;
            }
            if (WaitForSingleObject(process_, 0) == WAIT_OBJECT_0) {
                // Exited with nothing left in the pipe.
                return flush_or_closed(out);
            }
            if (Clock::now() >= deadline) {
                return ReadStatus::Timeout;
            }
            Sleep(1);
        }
    }

    /// Asks the child to quit, then force-kills it if it does not exit.
    void terminate() {
        if (process_ == nullptr) {
            return;
        }
        write_line("quit");
        if (in_write_ != nullptr) {
            CloseHandle(in_write_);
            in_write_ = nullptr;
        }
        if (WaitForSingleObject(process_, 1000) != WAIT_OBJECT_0) {
            TerminateProcess(process_, 1);
            WaitForSingleObject(process_, 2000);
        }
        CloseHandle(process_);
        process_ = nullptr;
        if (out_read_ != nullptr) {
            CloseHandle(out_read_);
            out_read_ = nullptr;
        }
    }

private:
    static std::string quote(const std::string& s) {
        if (s.find_first_of(" \t\"") == std::string::npos && !s.empty()) {
            return s;
        }
        std::string r = "\"";
        for (const char c : s) {
            if (c == '"') {
                r += '\\';
            }
            r += c;
        }
        r += '"';
        return r;
    }

    bool extract_line(std::string& out) {
        const std::size_t pos = buffer_.find('\n');
        if (pos == std::string::npos) {
            return false;
        }
        out = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);
        if (!out.empty() && out.back() == '\r') {
            out.pop_back();
        }
        return true;
    }

    ReadStatus flush_or_closed(std::string& out) {
        if (!buffer_.empty()) {
            out = buffer_;
            buffer_.clear();
            return ReadStatus::Line;
        }
        return ReadStatus::Closed;
    }

    HANDLE process_ = nullptr;
    HANDLE in_write_ = nullptr;
    HANDLE out_read_ = nullptr;
    std::string buffer_;
};

#else // POSIX

/// Child process with piped stdin/stdout (POSIX variant).
class Process {
public:
    Process() = default;
    ~Process() { terminate(); }
    Process(const Process&) = delete;
    Process& operator=(const Process&) = delete;

    /// Launches `command` with `args`. Returns false and sets `error`
    /// on failure. An executable that cannot be started is detected by
    /// the child exiting with status 127, which shows up to the caller
    /// as a closed pipe during the handshake.
    bool launch(const std::string& command, const std::vector<std::string>& args,
                std::string& error) {
        // A write to a dead child's stdin must return EPIPE, not kill
        // this process.
        std::signal(SIGPIPE, SIG_IGN);

        int to_child[2] = {-1, -1};
        int from_child[2] = {-1, -1};
        if (pipe(to_child) != 0 || pipe(from_child) != 0) {
            error = "pipe() failed";
            return false;
        }
        // Parent-side ends must not leak into the child (or into any
        // later-spawned engine).
        fcntl(to_child[1], F_SETFD, FD_CLOEXEC);
        fcntl(from_child[0], F_SETFD, FD_CLOEXEC);

        std::vector<std::string> argv_storage;
        argv_storage.push_back(command);
        argv_storage.insert(argv_storage.end(), args.begin(), args.end());
        std::vector<char*> argv;
        for (std::string& s : argv_storage) {
            argv.push_back(s.data());
        }
        argv.push_back(nullptr);

        const pid_t pid = fork();
        if (pid < 0) {
            error = "fork() failed";
            close(to_child[0]);
            close(to_child[1]);
            close(from_child[0]);
            close(from_child[1]);
            return false;
        }
        if (pid == 0) {
            dup2(to_child[0], STDIN_FILENO);
            dup2(from_child[1], STDOUT_FILENO);
            // The original pipe fds are closed by exec (parent ends via
            // FD_CLOEXEC) or explicitly here (child ends).
            close(to_child[0]);
            close(from_child[1]);
            execvp(argv[0], argv.data());
            _exit(127);
        }
        close(to_child[0]);
        close(from_child[1]);
        pid_ = pid;
        in_fd_ = to_child[1];
        out_fd_ = from_child[0];
        return true;
    }

    /// Writes `line` plus a newline to the child's stdin.
    bool write_line(const std::string& line) {
        if (in_fd_ < 0) {
            return false;
        }
        const std::string data = line + "\n";
        std::size_t done = 0;
        while (done < data.size()) {
            const ssize_t n = write(in_fd_, data.data() + done, data.size() - done);
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return false;
            }
            done += static_cast<std::size_t>(n);
        }
        return true;
    }

    /// Reads one line (without its terminator) within `timeout_ms`.
    ReadStatus read_line(std::string& out, int timeout_ms) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        for (;;) {
            if (extract_line(out)) {
                return ReadStatus::Line;
            }
            if (out_fd_ < 0) {
                return ReadStatus::Closed;
            }
            struct pollfd pfd{};
            pfd.fd = out_fd_;
            pfd.events = POLLIN;
            const int rc = poll(&pfd, 1, remaining_ms(deadline));
            if (rc < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return flush_or_closed(out);
            }
            if (rc == 0) {
                return ReadStatus::Timeout;
            }
            char chunk[4096];
            const ssize_t n = read(out_fd_, chunk, sizeof(chunk));
            if (n < 0) {
                if (errno == EINTR) {
                    continue;
                }
                return flush_or_closed(out);
            }
            if (n == 0) {
                return flush_or_closed(out);
            }
            buffer_.append(chunk, static_cast<std::size_t>(n));
        }
    }

    /// Asks the child to quit, then force-kills it if it does not exit,
    /// and always reaps it (no zombies).
    void terminate() {
        if (pid_ <= 0) {
            return;
        }
        write_line("quit");
        if (in_fd_ >= 0) {
            close(in_fd_);
            in_fd_ = -1;
        }
        bool exited = false;
        for (int i = 0; i < 100; ++i) { // up to ~1 s
            int status = 0;
            const pid_t r = waitpid(pid_, &status, WNOHANG);
            if (r == pid_ || (r < 0 && errno != EINTR)) {
                exited = true;
                break;
            }
            usleep(10000);
        }
        if (!exited) {
            kill(pid_, SIGKILL);
            int status = 0;
            while (waitpid(pid_, &status, 0) < 0 && errno == EINTR) {
            }
        }
        pid_ = -1;
        if (out_fd_ >= 0) {
            close(out_fd_);
            out_fd_ = -1;
        }
    }

private:
    bool extract_line(std::string& out) {
        const std::size_t pos = buffer_.find('\n');
        if (pos == std::string::npos) {
            return false;
        }
        out = buffer_.substr(0, pos);
        buffer_.erase(0, pos + 1);
        if (!out.empty() && out.back() == '\r') {
            out.pop_back();
        }
        return true;
    }

    ReadStatus flush_or_closed(std::string& out) {
        if (!buffer_.empty()) {
            out = buffer_;
            buffer_.clear();
            return ReadStatus::Line;
        }
        return ReadStatus::Closed;
    }

    pid_t pid_ = -1;
    int in_fd_ = -1;
    int out_fd_ = -1;
    std::string buffer_;
};

#endif

/// Reads lines from `proc` until one equals `token` exactly, within
/// `timeout_ms` overall. Other lines (for example `id`/`option` lines
/// during the handshake) are discarded.
[[nodiscard]] ReadStatus wait_for_line(Process& proc, const std::string& token, int timeout_ms) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
    std::string line;
    for (;;) {
        const ReadStatus status = proc.read_line(line, remaining_ms(deadline));
        if (status != ReadStatus::Line) {
            return status;
        }
        if (line == token) {
            return ReadStatus::Line;
        }
    }
}

// ---------------------------------------------------------------------------
// 2. Referee
// ---------------------------------------------------------------------------

using board::Color;
using board::Move;
using board::MoveList;
using board::PieceType;
using board::Position;
using board::UndoInfo;

[[nodiscard]] bool side_in_check(const Position& pos) noexcept {
    const board::Bitboard king_bb = pos.pieces(pos.side_to_move, PieceType::King);
    const board::Square king_sq = board::bitscan_forward(king_bb);
    return board::is_square_attacked(pos, king_sq, board::opposite(pos.side_to_move));
}

/// True for the positions no legal sequence of moves can win from: bare
/// kings, or one lone minor piece (either side) against a bare king.
/// Deliberately conservative (it does not try to recognise blocked
/// same-colored-bishop endings); anything else is left to the other end
/// conditions and `max_plies`.
[[nodiscard]] bool insufficient_material(const Position& pos) noexcept {
    int minors = 0;
    for (const Color c : {Color::White, Color::Black}) {
        if (pos.pieces(c, PieceType::Pawn) != 0 || pos.pieces(c, PieceType::Rook) != 0 ||
            pos.pieces(c, PieceType::Queen) != 0) {
            return false;
        }
        minors += board::popcount(pos.pieces(c, PieceType::Knight)) +
                  board::popcount(pos.pieces(c, PieceType::Bishop));
    }
    return minors <= 1;
}

/// Finds the legal move whose UCI string equals `text`; returns false
/// when there is none (illegal or unparsable).
[[nodiscard]] bool find_legal_move(const MoveList& moves, const std::string& text, Move& out) {
    for (int i = 0; i < moves.size(); ++i) {
        if (moves[i].to_uci() == text) {
            out = moves[i];
            return true;
        }
    }
    return false;
}

/// How one refereed game ended.
struct GameOutcome {
    double white_score = 0.5; ///< 1 White won, 0 Black won, 0.5 draw.
    int illegal_by = -1;      ///< 0 White, 1 Black sent an illegal move, -1 none.
    bool aborted = false;     ///< An engine crashed/closed/timed out.
    std::string error;
};

} // namespace

// ---------------------------------------------------------------------------
// 3. Session
// ---------------------------------------------------------------------------

struct UciMatchSession::Impl {
    UciEngineSpec spec_a;
    UciEngineSpec spec_b;
    UciMatchConfig config;
    Process proc_a;
    Process proc_b;
    UciMatchResult result;
    bool started = false;

    int go_timeout_ms() const {
        if (config.move_timeout_ms > 0) {
            return config.move_timeout_ms;
        }
        return config.movetime_ms > 0 ? config.movetime_ms + 5000 : 120000;
    }

    std::string go_command() const {
        return uci_go_command(config);
    }

    bool fail(const std::string& message) {
        result.aborted = true;
        result.error = message;
        return false;
    }

    /// Launches one engine and runs its handshake and option setup.
    bool bring_up(Process& proc, const UciEngineSpec& spec) {
        std::string error;
        if (!proc.launch(spec.command, spec.args, error)) {
            return fail(spec.name + ": " + error);
        }
        if (!proc.write_line("uci") ||
            wait_for_line(proc, "uciok", config.handshake_timeout_ms) != ReadStatus::Line) {
            return fail(spec.name + ": no 'uciok' (executable missing, crashed, or not a UCI "
                                    "engine): " +
                        spec.command);
        }
        if (config.disable_own_book) {
            proc.write_line("setoption name OwnBook value false");
        }
        for (const auto& [name, value] : spec.options) {
            proc.write_line("setoption name " + name + " value " + value);
        }
        return sync(proc, spec);
    }

    /// `isready` / `readyok` round trip.
    bool sync(Process& proc, const UciEngineSpec& spec) {
        if (!proc.write_line("isready") ||
            wait_for_line(proc, "readyok", config.handshake_timeout_ms) != ReadStatus::Line) {
            return fail(spec.name + ": no 'readyok'");
        }
        return true;
    }

    /// Plays one refereed game; `white` and `black` are the engines'
    /// processes for this game.
    GameOutcome referee(Process& white, const UciEngineSpec& white_spec, Process& black,
                        const UciEngineSpec& black_spec, std::uint64_t seed) {
        GameOutcome outcome;
        support::Xorshift64Star rng(seed);
        Position pos = board::start_position();
        std::vector<std::uint64_t> history;
        std::vector<std::string> played;

        for (int ply = 0; ply < config.max_plies; ++ply) {
            MoveList moves;
            board::generate_legal_moves(pos, moves);

            if (moves.size() == 0) {
                if (side_in_check(pos)) {
                    outcome.white_score = (pos.side_to_move == Color::White) ? 0.0 : 1.0;
                } else {
                    outcome.white_score = 0.5; // stalemate
                }
                return outcome;
            }
            if (pos.halfmove_clock >= 100 || insufficient_material(pos)) {
                outcome.white_score = 0.5;
                return outcome;
            }
            int occurrences = 1;
            for (const std::uint64_t past : history) {
                if (past == pos.zobrist_hash) {
                    ++occurrences;
                }
            }
            if (occurrences >= 3) {
                outcome.white_score = 0.5;
                return outcome;
            }

            Move move;
            if (ply < config.random_opening_plies) {
                const std::size_t index = static_cast<std::size_t>(
                    rng.next() % static_cast<std::uint64_t>(moves.size()));
                move = moves[static_cast<int>(index)];
            } else {
                const bool white_to_move = (pos.side_to_move == Color::White);
                Process& mover = white_to_move ? white : black;
                const UciEngineSpec& mover_spec = white_to_move ? white_spec : black_spec;

                std::string position_cmd = "position startpos";
                if (!played.empty()) {
                    position_cmd += " moves";
                    for (const std::string& m : played) {
                        position_cmd += ' ';
                        position_cmd += m;
                    }
                }
                if (!mover.write_line(position_cmd) || !mover.write_line(go_command())) {
                    outcome.aborted = true;
                    outcome.error = mover_spec.name + ": could not write to the engine (crashed?)";
                    return outcome;
                }

                const auto deadline = Clock::now() + std::chrono::milliseconds(go_timeout_ms());
                std::string line;
                std::string text;
                for (;;) {
                    const ReadStatus status = mover.read_line(line, remaining_ms(deadline));
                    if (status == ReadStatus::Timeout) {
                        outcome.aborted = true;
                        outcome.error = mover_spec.name + ": timed out waiting for 'bestmove'";
                        return outcome;
                    }
                    if (status == ReadStatus::Closed) {
                        outcome.aborted = true;
                        outcome.error = mover_spec.name + ": closed its output (crashed?)";
                        return outcome;
                    }
                    if (line.rfind("bestmove", 0) == 0) {
                        text = parse_bestmove_line(line);
                        break;
                    }
                }

                if (!find_legal_move(moves, text, move)) {
                    outcome.illegal_by = white_to_move ? 0 : 1;
                    outcome.white_score = white_to_move ? 0.0 : 1.0;
                    return outcome;
                }
            }

            played.push_back(move.to_uci());
            history.push_back(pos.zobrist_hash);
            UndoInfo undo;
            board::make_move(pos, move, undo);
        }
        outcome.white_score = 0.5; // max_plies safety net
        return outcome;
    }
};

UciMatchSession::UciMatchSession(UciEngineSpec spec_a, UciEngineSpec spec_b, UciMatchConfig config)
    : impl_(std::make_unique<Impl>()) {
    impl_->spec_a = std::move(spec_a);
    impl_->spec_b = std::move(spec_b);
    impl_->config = config;
}

UciMatchSession::~UciMatchSession() {
    stop();
}

bool UciMatchSession::start() {
    if (impl_->started) {
        return true;
    }
    if (!impl_->bring_up(impl_->proc_a, impl_->spec_a) ||
        !impl_->bring_up(impl_->proc_b, impl_->spec_b)) {
        stop();
        return false;
    }
    impl_->started = true;
    return true;
}

bool UciMatchSession::play_game(int game_index, std::uint64_t seed) {
    Impl& s = *impl_;
    if (!s.started || s.result.aborted) {
        return false;
    }

    for (Process* proc : {&s.proc_a, &s.proc_b}) {
        const UciEngineSpec& spec = (proc == &s.proc_a) ? s.spec_a : s.spec_b;
        if (!proc->write_line("ucinewgame") || !s.sync(*proc, spec)) {
            return false; // fail() already recorded the abort
        }
    }

    const bool a_white = (game_index % 2 == 0);
    Process& white = a_white ? s.proc_a : s.proc_b;
    Process& black = a_white ? s.proc_b : s.proc_a;
    const UciEngineSpec& white_spec = a_white ? s.spec_a : s.spec_b;
    const UciEngineSpec& black_spec = a_white ? s.spec_b : s.spec_a;

    const GameOutcome outcome = s.referee(white, white_spec, black, black_spec, seed);
    if (outcome.aborted) {
        return s.fail(outcome.error);
    }

    const double score_a = a_white ? outcome.white_score : 1.0 - outcome.white_score;
    if (score_a == 1.0) {
        ++s.result.tally.wins_a;
    } else if (score_a == 0.0) {
        ++s.result.tally.wins_b;
    } else {
        ++s.result.tally.draws;
    }
    ++s.result.tally.games_played;

    if (outcome.illegal_by >= 0) {
        const bool a_offended = (outcome.illegal_by == 0) == a_white;
        if (a_offended) {
            ++s.result.illegal_moves_a;
        } else {
            ++s.result.illegal_moves_b;
        }
    }
    return true;
}

const UciMatchResult& UciMatchSession::result() const noexcept {
    return impl_->result;
}

void UciMatchSession::stop() {
    impl_->proc_a.terminate();
    impl_->proc_b.terminate();
    impl_->started = false;
}

UciMatchResult play_uci_match(const UciEngineSpec& spec_a, const UciEngineSpec& spec_b,
                              std::uint64_t base_seed, const UciMatchConfig& config) {
    UciMatchSession session(spec_a, spec_b, config);
    if (session.start()) {
        for (int i = 0; i < config.num_games; ++i) {
            if (!session.play_game(i, base_seed + static_cast<std::uint64_t>(i))) {
                break;
            }
        }
    }
    UciMatchResult result = session.result();
    session.stop();
    return result;
}

std::string uci_go_command(const UciMatchConfig& config) {
    if (config.movetime_ms > 0) {
        return "go movetime " + std::to_string(config.movetime_ms);
    }
    if (config.nodes > 0) {
        return "go nodes " + std::to_string(config.nodes);
    }
    return "go depth " + std::to_string(config.depth);
}

std::string parse_bestmove_line(const std::string& line) {
    std::istringstream in(line);
    std::string first;
    std::string move;
    if (!(in >> first) || first != "bestmove") {
        return {};
    }
    in >> move;
    return move;
}

} // namespace nightwing::tuner
