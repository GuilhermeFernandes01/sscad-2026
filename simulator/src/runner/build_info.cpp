#include "algosim/runner/build_info.hpp"

#include "algosim_git_info.hpp"

namespace algosim::runner {

BuildInfo build_info() {
    BuildInfo info;
    info.git_sha   = ALGOSIM_GIT_SHA;
    info.git_dirty = ALGOSIM_GIT_DIRTY != 0;
    return info;
}

}  // namespace algosim::runner
