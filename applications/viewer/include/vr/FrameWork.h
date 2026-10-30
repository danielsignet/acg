#pragma once
#include <cstdlib>
#include <cstring>

namespace vr{
// Same-binary reference for frame-work benchmarks; normal runs use the optimizations.
inline bool frameWorkOptimized(){
    static const bool enabled=[] {
        const auto* reference=std::getenv("ACG_FRAME_WORK_REFERENCE");
        return !reference || std::strcmp(reference,"1")!=0;
    }();
    return enabled;
}
}
