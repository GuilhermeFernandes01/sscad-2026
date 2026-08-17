#include "algosim/runner/build_info.hpp"
#include "algosim/runner/results_writer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace algosim;

namespace {

std::string read_file(const std::filesystem::path& p) {
    std::ifstream in{p};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

scenario::ScenarioSpec minimal_spec() {
    scenario::ScenarioSpec spec;
    spec.name               = "manifest_test";
    spec.source_config_path = "/nonexistent/manifest_test.yaml";
    spec.duration_seconds   = 600.0;
    spec.seed               = 42;
    return spec;
}

}  // namespace

TEST_CASE("manifest records build provenance", "[unit][runner][manifest]") {
    const auto out_root = std::filesystem::temp_directory_path()
                          / "algosim_results_writer_test";
    std::filesystem::remove_all(out_root);

    auto spec = minimal_spec();
    runner::ResultsWriter writer{out_root, "run_manifest_test", spec,
                                 "first_fit", "none"};

    runner::BuildInfo build;
    build.git_sha         = "abc123def";
    build.git_dirty       = true;
    build.simgrid_version = "4.1.1";
    writer.write_manifest(build);

    const auto manifest = read_file(writer.run_dir() / "manifest.json");
    CHECK(manifest.find("\"git_sha\": \"abc123def\"") != std::string::npos);
    CHECK(manifest.find("\"git_dirty\": true") != std::string::npos);
    CHECK(manifest.find("\"simgrid_version\": \"4.1.1\"") != std::string::npos);
}

TEST_CASE("build_info captures the git sha at build time",
          "[unit][runner][manifest]") {
    const auto info = runner::build_info();

    REQUIRE(!info.git_sha.empty());
    if (info.git_sha != "unknown") {
        CHECK(info.git_sha.size() == 40);
        CHECK(std::all_of(info.git_sha.begin(), info.git_sha.end(),
                          [](unsigned char c) { return std::isxdigit(c); }));
    }
}
