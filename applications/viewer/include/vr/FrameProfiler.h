#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <deque>
#include <glad/glad.h>

namespace vr{
// Records CPU scopes and delayed GPU timestamps without waiting for the driver.
class FrameProfiler final{
public:
    enum class Stage{
        Frame,
        Update,
        Render,
        Shadows,
        Portals,
        Lights,
        Culling,
        Geometry,
        Lighting,
        Transparency,
        Bloom,
        DepthOfField,
        Composite,
        Present,
        Debug,
        Overlay,
        Swap,
        Forward,
        Occlusion,
        Events,
        Picking,
        Subdivision,
        GBufferResolve,
        Antialiasing,
        Environment,
        Reflection,
        Cubemaps,
        Input,
        AnimationTracks,
        AnimationHierarchy,
        AnimationPalettes,
        Skinning,
        Count
    };
    static constexpr std::size_t StageCount = static_cast<std::size_t>(Stage::Count);
    static constexpr std::size_t HistorySize = 120;

    enum class Counter{
        ColorDraws,
        ColorTriangles,
        TransparentDraws,
        ShadowDraws,
        ShadowTriangles,
        ShadowPasses,
        TotalDraws,
        TotalTriangles,
        ScreenDraws,
        Lights,
        VisitedNodes,
        CameraBounds,
        FrustumRejected,
        PortalRejected,
        ShadowBounds,
        ShadowRejected,
        VisibleRooms,
        PortalsTested,
        GBufferMiB,
        LodFadeDraws,
        MirrorDraws,
        ScreenCopies,
        ProbeDraws,
        ProbeFaces,
        ReadyProbes,
        SkinnedVertices,
        SkinInfluences,
        Count
    };
    static constexpr std::size_t CounterCount = static_cast<std::size_t>(Counter::Count);
    using Counters = std::array<double, CounterCount>;

    struct Description{
        const char* name;
        bool gpu;
    };

    struct Sample{
        std::uint64_t sequence = 0;
        double intervalMs = 0;
        std::array<double, StageCount> cpu{}, gpu{};
        Counters counters{};
        bool countersValid = false;
        bool gpuReady = false;
    };

    struct Summary{
        std::array<double, StageCount> cpu{}, gpu{};
        std::array<double, StageCount> latestCpu{}, latestGpu{};
        Counters counters{};
        double frameMs = 0, p95Ms = 0, peakMs = 0;
        unsigned samples = 0, gpuSamples = 0, counterSamples = 0;
        std::uint64_t gpuLag = 0, skipped = 0;
    };

    struct Slice{
        const char* name;
        bool gpu = true;
        double cpuMs = 0, gpuMs = 0;
        double cpuPercent = 0, gpuPercent = 0;
    };

    static constexpr std::size_t SliceCount = 20;

    class Scope final{
    public:
        // Starts an optional scope; disabled profiling performs no GL calls.
        Scope(FrameProfiler* profiler, Stage stage);
        // Publishes CPU duration and queues the matching GPU timestamp.
        ~Scope();
        Scope(const Scope&) = delete;
        Scope& operator=(const Scope&) = delete;

    private:
        FrameProfiler* m_profiler;
        Stage m_stage;
        std::chrono::steady_clock::time_point m_start;
        bool m_gpu = false;
    };

    // Defers allocating queries until profiling is first enabled.
    explicit FrameProfiler(bool gpuQueries = true) : m_gpuQueries(gpuQueries){
    }

    // Releases query objects while the owning context is still current.
    ~FrameProfiler();
    FrameProfiler(const FrameProfiler&) = delete;
    FrameProfiler& operator=(const FrameProfiler&) = delete;
    // Starts a fresh measurement history when the view is opened or closed.
    void setEnabled(bool enabled);
    // Resets between frames without changing settings or waiting for GPU results.
    void resetAverages();

    // Reports whether the performance view is collecting measurements.
    bool getEnabled() const{
        return m_enabled;
    }

    // Reports whether the context provides meaningful timestamp queries.
    bool getGpuSupported() const{
        return m_gpuSupported;
    }

    // Polls completed queries and starts recording a frame with its real interval.
    void beginFrame(double intervalSeconds);
    // Stores this frame's wall time and leaves GPU results pending until a later frame.
    void endFrame(double frameSeconds = 0);
    // Updates live scene counters and includes them in the active frame's history.
    void recordCounters(const Counters& counters);

    // Returns counters from the currently displayed scene frame.
    const Counters& getCounters() const{
        return m_current.counters;
    }

    // Averages the bounded history and computes the 95th-percentile frame interval.
    Summary summarize() const;
    // Groups nonoverlapping scopes and accounts for remaining time without counting totals twice.
    static std::array<Slice, SliceCount> breakdown(const Summary& summary);

    // Exposes completed frames in chronological order.
    const std::deque<Sample>& getHistory() const{
        return m_history;
    }

    // Names the measured stages and identifies CPU-only work.
    static const std::array<Description, StageCount>& descriptions();

private:
    struct Queries{
        std::array<GLuint, StageCount * 2> ids{};
        std::array<bool, StageCount> used{};
        std::uint64_t sequence = 0;
        GLuint last = 0;
        bool pending = false;
    };

    // Copies only results whose last timestamp is already available.
    void collect();
    std::array<Queries, 8> m_queries;
    Queries* m_recording = nullptr;
    std::deque<Sample> m_history;
    Sample m_current;
    std::uint64_t m_sequence = 0, m_skipped = 0;
    bool m_enabled = false, m_active = false, m_allocated = false, m_gpuSupported = false;
    bool m_gpuQueries;
};
} // namespace vr
