#include "algosim/algorithms/registry.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace algosim;

TEST_CASE("Built-in algorithms register themselves", "[unit][registry]") {
    algorithms::register_builtin_algorithms();

    const auto names = algorithms::AlgorithmRegistry::placement_names();
    CHECK(std::find(names.begin(), names.end(), "first_fit")   != names.end());
    CHECK(std::find(names.begin(), names.end(), "best_fit")    != names.end());
    CHECK(std::find(names.begin(), names.end(), "worst_fit")   != names.end());
    CHECK(std::find(names.begin(), names.end(), "round_robin") != names.end());
    CHECK(std::find(names.begin(), names.end(), "wsnb")        != names.end());
    CHECK(std::find(names.begin(), names.end(), "followme_s")  != names.end());
    CHECK(std::find(names.begin(), names.end(), "cnemesis")    != names.end());

    const auto migs = algorithms::AlgorithmRegistry::migration_names();
    CHECK(std::find(migs.begin(), migs.end(), "followme_s") != migs.end());
    CHECK(std::find(migs.begin(), migs.end(), "cnemesis")   != migs.end());
}

TEST_CASE("Registry factory returns a usable algorithm", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    auto placement = algorithms::AlgorithmRegistry::make_placement("best_fit");
    REQUIRE(placement != nullptr);
    CHECK(placement->name() == "best_fit");
}

TEST_CASE("Registry throws on unknown algorithm", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    CHECK_THROWS_AS(algorithms::AlgorithmRegistry::make_placement("nonexistent"),
                    std::out_of_range);
}

TEST_CASE("Parameterized make_placement for cnemesis", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    algorithms::AlgorithmRegistry::AlgoParams params{
        {"min_benefit_ratio", "0.7"},
        {"max_concurrent", "10"},
    };
    auto algo = algorithms::AlgorithmRegistry::make_placement("cnemesis", params);
    REQUIRE(algo != nullptr);
    CHECK(algo->name() == "cnemesis");
}

TEST_CASE("Parameterized make_placement for wsnb", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    algorithms::AlgorithmRegistry::AlgoParams params{
        {"carbon_threshold", "200.0"},
    };
    auto algo = algorithms::AlgorithmRegistry::make_placement("wsnb", params);
    REQUIRE(algo != nullptr);
    CHECK(algo->name() == "wsnb");
}

TEST_CASE("Parameterized make_placement falls through for non-parameterized algo", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    algorithms::AlgorithmRegistry::AlgoParams params{{"irrelevant", "42"}};
    auto algo = algorithms::AlgorithmRegistry::make_placement("first_fit", params);
    REQUIRE(algo != nullptr);
    CHECK(algo->name() == "first_fit");
}

TEST_CASE("Parameterized make_migration for cnemesis", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    algorithms::AlgorithmRegistry::AlgoParams params{
        {"min_benefit_ratio", "1.0"},
        {"under_threshold", "0.30"},
    };
    auto algo = algorithms::AlgorithmRegistry::make_migration("cnemesis", params);
    REQUIRE(algo != nullptr);
}

TEST_CASE("Parameterized make with empty params uses defaults", "[unit][registry]") {
    algorithms::register_builtin_algorithms();
    algorithms::AlgorithmRegistry::AlgoParams empty;
    auto algo = algorithms::AlgorithmRegistry::make_placement("cnemesis", empty);
    REQUIRE(algo != nullptr);
    CHECK(algo->name() == "cnemesis");
}
