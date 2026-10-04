// tests/spsa_tests.cpp
//
// Unit tests for src/tuner/spsa.h/.cpp (ROADMAP.md "SPSA tuner" item).
// The optimizer core is engine-agnostic, so every test drives it with a
// synthetic, instant match callback; no search or board code is involved
// except the ParameterRef adapter round-trip at the end.

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

#include "eval/mobility.h"
#include "tuner/spsa.h"
#include "tuner/tune.h"

using namespace nightwing::tuner;

namespace {

// Synthetic "strength" f(theta) = -sum((theta_i - target_i)^2); theta+ scores
// above 0.5 exactly when it is closer to the target than theta-.
SpsaMatchFn quadratic_match(std::vector<double> target) {
    return [target](const std::vector<double>& plus, const std::vector<double>& minus, std::uint64_t) {
        double f_plus = 0.0;
        double f_minus = 0.0;
        for (std::size_t i = 0; i < target.size(); ++i) {
            f_plus -= (plus[i] - target[i]) * (plus[i] - target[i]);
            f_minus -= (minus[i] - target[i]) * (minus[i] - target[i]);
        }
        return 1.0 / (1.0 + std::exp(-(f_plus - f_minus) / 8.0));
    };
}

} // namespace

TEST_CASE("spsa: converges toward the optimum of a synthetic strength function", "[tuner][spsa]") {
    const std::vector<SpsaParam> params = {{"a", 0.0, 8.0, -100.0, 100.0}, {"b", 40.0, 8.0, -100.0, 100.0}};
    SpsaConfig cfg;
    cfg.iterations = 400;
    cfg.r0 = 4.0;
    const SpsaResult r = run_spsa(params, cfg, quadratic_match({20.0, 10.0}));
    REQUIRE(r.iterations_run == 400);
    REQUIRE(r.theta.size() == 2);
    // Started 20 and 30 away from the target; must end much closer.
    REQUIRE(std::abs(r.theta[0] - 20.0) < 8.0);
    REQUIRE(std::abs(r.theta[1] - 10.0) < 12.0);
}

TEST_CASE("spsa: a drawn match (0.5) never moves the parameters", "[tuner][spsa]") {
    const std::vector<SpsaParam> params = {{"a", 7.0, 3.0, -50.0, 50.0}};
    SpsaConfig cfg;
    cfg.iterations = 25;
    const SpsaResult r = run_spsa(params, cfg, [](const auto&, const auto&, std::uint64_t) { return 0.5; });
    REQUIRE(r.theta[0] == 7.0);
}

TEST_CASE("spsa: results stay inside the bounds even with extreme scores", "[tuner][spsa]") {
    const std::vector<SpsaParam> params = {{"a", 0.0, 50.0, -3.0, 3.0}};
    SpsaConfig cfg;
    cfg.iterations = 50;
    cfg.r0 = 100.0;
    // Out-of-range scores are clamped to [0, 1]; plus always "wins".
    const SpsaResult r = run_spsa(params, cfg, [](const auto&, const auto&, std::uint64_t) { return 7.5; });
    REQUIRE(r.theta[0] >= -3.0);
    REQUIRE(r.theta[0] <= 3.0);
    for (double s : r.plus_scores) {
        REQUIRE(s == 1.0);
    }
}

TEST_CASE("spsa: same seed and deterministic callback give identical results", "[tuner][spsa]") {
    const std::vector<SpsaParam> params = {{"a", 1.0, 4.0}, {"b", -2.0, 4.0}};
    SpsaConfig cfg;
    cfg.iterations = 60;
    cfg.seed = 99;
    const auto match = quadratic_match({5.0, -5.0});
    const SpsaResult r1 = run_spsa(params, cfg, match);
    const SpsaResult r2 = run_spsa(params, cfg, match);
    REQUIRE(r1.theta == r2.theta);
    cfg.seed = 100;
    const SpsaResult r3 = run_spsa(params, cfg, match);
    REQUIRE(r3.theta != r1.theta);
}

TEST_CASE("spsa: empty parameter list and zero iterations are harmless", "[tuner][spsa]") {
    const SpsaResult empty = run_spsa({}, SpsaConfig{}, [](const auto&, const auto&, std::uint64_t) { return 0.5; });
    REQUIRE(empty.iterations_run == 0);
    REQUIRE(empty.theta.empty());

    SpsaConfig cfg;
    cfg.iterations = 0;
    const SpsaResult none = run_spsa({{"a", 3.0, 1.0}}, cfg, [](const auto&, const auto&, std::uint64_t) { return 1.0; });
    REQUIRE(none.iterations_run == 0);
    REQUIRE(none.theta[0] == 3.0);
}

TEST_CASE("spsa: callback sees plus/minus symmetric about the current theta", "[tuner][spsa]") {
    const std::vector<SpsaParam> params = {{"a", 10.0, 2.0, -100.0, 100.0}};
    SpsaConfig cfg;
    cfg.iterations = 1;
    bool called = false;
    (void)run_spsa(params, cfg, [&](const std::vector<double>& p, const std::vector<double>& m, std::uint64_t) {
        called = true;
        REQUIRE(std::abs((p[0] + m[0]) / 2.0 - 10.0) < 1e-9);
        REQUIRE(std::abs(std::abs(p[0] - m[0]) - 4.0) < 1e-9); // 2 * c at k = 0
        return 0.5;
    });
    REQUIRE(called);
}

TEST_CASE("spsa: ParameterRef adapter round-trips MobilityWeights", "[tuner][spsa]") {
    const nightwing::eval::MobilityWeights base{};
    const auto params = make_spsa_params(kMobilityParameters, base, 2.0, -200.0, 200.0);
    REQUIRE(params.size() == kMobilityParameters.size());
    std::vector<double> theta;
    for (const auto& p : params) {
        theta.push_back(p.start);
    }
    // Unchanged theta reproduces the base exactly.
    const auto same = apply_spsa_theta(kMobilityParameters, base, theta);
    for (const auto& ref : kMobilityParameters) {
        REQUIRE(ref.get(same) == ref.get(base));
    }
    // Changing one entry changes exactly that field.
    theta[0] += 5.0;
    const auto changed = apply_spsa_theta(kMobilityParameters, base, theta);
    REQUIRE(kMobilityParameters[0].get(changed) == kMobilityParameters[0].get(base) + 5.0);
    for (std::size_t i = 1; i < kMobilityParameters.size(); ++i) {
        REQUIRE(kMobilityParameters[i].get(changed) == kMobilityParameters[i].get(base));
    }
}
