#pragma once
#include <atomic>
#include <cstdint>

namespace vr{
inline std::uint64_t nextRevision(){
    static std::atomic<std::uint64_t> revision{1};
    return revision.fetch_add(1, std::memory_order_relaxed);
}
} // namespace vr
