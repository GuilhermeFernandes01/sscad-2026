#pragma once

#include <string>

namespace algosim::runner {

// Provenance of the binary that produced a run. git fields are captured at
// build time (cmake/GitInfo.cmake); simgrid_version must be filled by the
// caller that links against the backend.
struct BuildInfo {
    std::string git_sha         = "unknown";
    bool        git_dirty       = false;
    std::string simgrid_version = "unknown";
};

// Returns the build-time git provenance (simgrid_version left as "unknown").
[[nodiscard]] BuildInfo build_info();

}  // namespace algosim::runner
