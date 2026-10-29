#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>
#include <vr/FrameProfiler.h>

namespace vr{
// Names the measured stages and identifies CPU-only work.
const std::array<FrameProfiler::Description, FrameProfiler::StageCount>&
FrameProfiler::descriptions(){
    static const std::array<Description, StageCount> stages{
        {{"Frame incl. swap", false},
         {"Scene update total", false},
         {"Render total", true},
         {"Shadow maps", true},
         {"Portal traversal", false},
         {"Light collection", false},
         {"Scene graph culling", false},
         {"G-buffer geometry", true},
         {"Deferred lighting", true},
         {"Transparent surfaces", true},
         {"Bloom (7 draws)", true},
         {"DoF (3 draws)", true},
         {"Composite / tone map", true},
         {"Present / previews", true},
         {"Debug volumes", true},
         {"HUD / performance view", true},
         {"Swap / VSync wait", false},
         {"Forward surfaces", true},
         {"Ambient occlusion", true},
         {"Window events / commands", false},
         {"Model raycast / BVH", false},
         {"CPU subdivision", false},
         {"MSAA surface resolve", true},
         {"AA filter / color resolve", true},
         {"Sky / environment lighting", true},
         {"Planar mirror capture", true},
         {"Local cubemap capture / filter", true},
         {"Input / camera", false},
         {"Animation track sampling", false},
         {"Skeleton world transforms", false},
         {"Skin palette matrices", false},
         {"CPU vertex skinning", false},
         {"GPU skinning (once)", true},
         {"Skin positions / bounds", false},
         {"Room portal textures", true},
         {"Render setup / submission", false},
         {"Profiler query collection", false},
         {"Skinning transform feedback", true}}};
    return stages;
}

// Releases query objects while the owning context is still current.
FrameProfiler::~FrameProfiler(){
    if (m_allocated){
        for (const auto& queries : m_queries){
            glDeleteQueries(static_cast<GLsizei>(queries.ids.size()), queries.ids.data());
        }
    }
}

// Starts a fresh measurement history when the view is opened or closed.
void FrameProfiler::setEnabled(bool enabled){
    if (m_enabled == enabled){
        return;
    }
    if (enabled && m_gpuQueries && !m_allocated){
        GLint bits = 0;
        glGetQueryiv(GL_TIMESTAMP, GL_QUERY_COUNTER_BITS, &bits);
        m_gpuSupported = bits > 0;
        if (m_gpuSupported){
            for (auto& queries : m_queries){
                glGenQueries(static_cast<GLsizei>(queries.ids.size()), queries.ids.data());
            }
            m_allocated = true;
        }
    }
    m_enabled = enabled;
    resetAverages();
}

// Resets between frames without changing settings or waiting for GPU results.
void FrameProfiler::resetAverages(){
    m_active = false;
    m_recording = nullptr;
    m_scopeTop = nullptr;
    m_history.clear();
    m_current ={};
    m_skipped = 0;
    // Unique frame IDs prevent pending results from entering the new history.
}

// Groups nonoverlapping scopes and accounts for remaining time without counting totals twice.
std::array<FrameProfiler::Slice, FrameProfiler::SliceCount>
FrameProfiler::breakdown(const Summary& summary){
    std::array<Slice, SliceCount> slices{{{"Shadows"},
                                          {"Visibility / LOD", false},
                                          {"G-buffer geometry"},
                                          {"Lighting / transparency"},
                                          {"Post-processing / output"},
                                          {"HUD / debug"},
                                          {"Swap / VSync wait", false},
                                          {"Ambient occlusion"},
                                          {"Model raycast / BVH", false},
                                          {"Window events / commands", false},
                                          {"Subdivision", false},
                                          {"Planar mirror"},
                                          {"Local cubemaps"},
                                          {"Input / camera", false},
                                          {"Animation tracks", false},
                                          {"Skeleton transforms", false},
                                          {"Skin palettes", false},
                                          {"CPU vertex skinning", false},
                                          {"Scene graph / callbacks", false},
                                          {"Deformation setup / wait"},
                                          {"Skin positions / bounds", false},
                                          {"Room portal textures"},
                                          {"Render setup / submission", false},
                                          {"Profiler query collection", false},
                                          {"Vertex deformation (GPU)"},
                                          {"Other / gaps"}}};
    const auto add = [&](std::size_t slice, std::initializer_list<Stage> stages){
        for (const auto stage : stages){
            const auto index = static_cast<std::size_t>(stage);
            slices[slice].cpuMs += summary.hasExclusiveTimings ? summary.cpuExclusive[index] : summary.cpu[index];
            slices[slice].gpuMs += summary.hasExclusiveTimings ? summary.gpuExclusive[index] : summary.gpu[index];
        }
    };
    add(0,{Stage::Shadows});
    add(1,{Stage::Portals, Stage::Lights, Stage::Culling});
    add(2,{Stage::Geometry, Stage::Forward});
    add(3,{Stage::Lighting, Stage::Transparency, Stage::Environment});
    add(4,{Stage::Bloom, Stage::DepthOfField, Stage::Composite, Stage::Present,
            Stage::GBufferResolve, Stage::Antialiasing});
    add(5,{Stage::Debug, Stage::Overlay});
    add(6,{Stage::Swap});
    add(7,{Stage::Occlusion});
    add(8,{Stage::Picking});
    add(9,{Stage::Events});
    add(10,{Stage::Subdivision});
    add(11,{Stage::Reflection});
    add(12,{Stage::Cubemaps});
    add(13,{Stage::Input});
    add(14,{Stage::AnimationTracks});
    add(15,{Stage::AnimationHierarchy});
    add(16,{Stage::AnimationPalettes});
    add(17,{Stage::Skinning});
    add(18,{Stage::Update});
    add(19,{Stage::GpuSkinning});
    add(20,{Stage::SkinBounds});
    add(21,{Stage::PortalViews});
    add(22,{Stage::Submission});
    add(23,{Stage::Profiler});
    add(24,{Stage::SkinningDraw});
    // Retain support for manually constructed summaries with inclusive timings.
    if (!summary.hasExclusiveTimings){
        for (std::size_t i = 14; i <= 17; ++i){
            slices[18].cpuMs -= slices[i].cpuMs;
        }
        slices[9].cpuMs = std::max(0.0, slices[9].cpuMs - slices[10].cpuMs);
    }
    slices[18].cpuMs = std::max(0.0, slices[18].cpuMs);
    const double cpu = summary.cpu[static_cast<std::size_t>(Stage::Frame)];
    const double gpu = summary.gpu[static_cast<std::size_t>(Stage::Render)];
    double measuredCpu = 0, measuredGpu = 0;
    for (const auto& slice : slices){
        measuredCpu += slice.cpuMs;
        measuredGpu += slice.gpuMs;
    }
    slices.back().cpuMs = std::max(0.0, cpu - measuredCpu);
    slices.back().gpuMs = std::max(0.0, gpu - measuredGpu);
    for (auto& slice : slices){
        slice.cpuPercent = cpu > 0 ? slice.cpuMs / cpu * 100 : 0;
        slice.gpuPercent = gpu > 0 ? slice.gpuMs / gpu * 100 : 0;
    }
    return slices;
}

// Copies only results whose last timestamp is already available.
void FrameProfiler::collect(){
    for (auto& queries : m_queries){
        if (!queries.pending){
            continue;
        }
        GLint available = GL_FALSE;
        glGetQueryObjectiv(queries.last, GL_QUERY_RESULT_AVAILABLE, &available);
        if (!available){
            continue;
        }
        const auto sample = std::find_if(m_history.begin(), m_history.end(), [&](const auto& item){
            return item.sequence == queries.sequence;
        });
        if (sample != m_history.end()){
            std::array<double, MaxGpuScopes> duration{}, children{};
            for (std::size_t i = 0; i < queries.count; ++i){
                GLuint64 start = 0, end = 0;
                glGetQueryObjectui64v(queries.ids[i * 2], GL_QUERY_RESULT, &start);
                glGetQueryObjectui64v(queries.ids[i * 2 + 1], GL_QUERY_RESULT, &end);
                duration[i] = end >= start ? static_cast<double>(end - start) / 1e6 : 0;
                const auto parent = queries.intervals[i].parent;
                if (parent >= 0){
                    children[static_cast<std::size_t>(parent)] += duration[i];
                }
            }
            for (std::size_t i = 0; i < queries.count; ++i){
                const auto stage = static_cast<std::size_t>(queries.intervals[i].stage);
                sample->gpu[stage] += duration[i];
                sample->gpuExclusive[stage] += std::max(0.0, duration[i] - children[i]);
            }
            sample->gpuReady = true;
        }
        queries.pending = false;
    }
}

// Polls completed queries and starts recording a frame with its real interval.
void FrameProfiler::beginFrame(double intervalSeconds){
    if (!m_enabled){
        return;
    }
    const auto queryStart = std::chrono::steady_clock::now();
    collect();
    const auto queryMs = std::chrono::duration<double, std::milli>(
                             std::chrono::steady_clock::now() - queryStart).count();
    m_current ={};
    m_current.sequence = ++m_sequence;
    const auto queryStage = static_cast<std::size_t>(Stage::Profiler);
    m_current.cpu[queryStage] = m_current.cpuExclusive[queryStage] = queryMs;
    m_current.intervalMs = std::max(0.0, intervalSeconds * 1000);
    m_recording = nullptr;
    m_scopeTop = nullptr;
    if (m_gpuSupported){
        for (auto& queries : m_queries){
            if (!queries.pending){
                queries.count = 0;
                queries.last = 0;
                queries.sequence = m_sequence;
                m_recording = &queries;
                break;
            }
        }
        if (!m_recording){
            ++m_skipped;
        }
    }
    m_active = true;
}

// Stores this frame's wall time and leaves GPU results pending until a later frame.
void FrameProfiler::endFrame(double frameSeconds){
    if (!m_active){
        return;
    }
    if (frameSeconds > 0){
        m_current.intervalMs = frameSeconds * 1000;
        m_current.cpu[static_cast<std::size_t>(Stage::Frame)] = m_current.intervalMs;
    }
    if (m_recording){
        m_recording->pending = m_recording->last != 0;
    }
    m_history.push_back(m_current);
    if (m_history.size() > HistorySize){
        m_history.pop_front();
    }
    m_recording = nullptr;
    m_active = false;
}

// Updates live scene counters and includes them in the active frame's history.
void FrameProfiler::recordCounters(const Counters& counters){
    if (m_enabled){
        m_current.counters = counters;
        m_current.countersValid = true;
    }
}

// Starts an optional scope; disabled profiling performs no GL calls.
FrameProfiler::Scope::Scope(FrameProfiler* profiler, Stage stage)
    : m_profiler(profiler && profiler->m_active ? profiler : nullptr), m_stage(stage){
    if (!m_profiler){
        return;
    }
    m_start = std::chrono::steady_clock::now();
    m_sequence = m_profiler->m_sequence;
    m_parent = m_profiler->m_scopeTop;
    m_profiler->m_scopeTop = this;
    const auto i = static_cast<std::size_t>(stage);
    if (descriptions()[i].gpu && m_profiler->m_recording &&
        m_profiler->m_recording->count < MaxGpuScopes){
        auto& queries = *m_profiler->m_recording;
        m_gpuSlot = static_cast<int>(queries.count++);
        int parent = -1;
        for (auto scope = m_parent; scope; scope = scope->m_parent){
            if (scope->m_gpuSlot >= 0){
                parent = scope->m_gpuSlot;
                break;
            }
        }
        queries.intervals[m_gpuSlot] ={stage, parent};
        glQueryCounter(queries.ids[m_gpuSlot * 2], GL_TIMESTAMP);
    }
}

// Publishes CPU duration and queues the matching GPU timestamp.
FrameProfiler::Scope::~Scope(){
    if (!m_profiler || !m_profiler->m_active || m_sequence != m_profiler->m_sequence){
        return;
    }
    const auto i = static_cast<std::size_t>(m_stage);
    if (m_gpuSlot >= 0){
        auto& queries = *m_profiler->m_recording;
        queries.last = queries.ids[m_gpuSlot * 2 + 1];
        glQueryCounter(queries.last, GL_TIMESTAMP);
    }
    const auto duration = std::chrono::duration<double, std::milli>(
                              std::chrono::steady_clock::now() - m_start).count();
    m_profiler->m_current.cpu[i] += duration;
    m_profiler->m_current.cpuExclusive[i] += std::max(0.0, duration - m_childCpuMs);
    if (m_parent){
        m_parent->m_childCpuMs += duration;
    }
    m_profiler->m_scopeTop = m_parent;
}

// Averages the bounded history and computes the 95th-percentile frame interval.
FrameProfiler::Summary FrameProfiler::summarize() const{
    Summary result;
    result.samples = static_cast<unsigned>(m_history.size());
    result.hasExclusiveTimings = !m_history.empty();
    result.skipped = m_skipped;
    std::vector<double> intervals;
    for (const auto& sample : m_history){
        intervals.push_back(sample.intervalMs);
        result.frameMs += sample.intervalMs;
        result.peakMs = std::max(result.peakMs, sample.intervalMs);
        result.latestCpu = sample.cpu;
        for (std::size_t i = 0; i < StageCount; ++i){
            result.cpu[i] += sample.cpu[i];
            result.cpuExclusive[i] += sample.cpuExclusive[i];
            if (sample.gpuReady){
                result.gpu[i] += sample.gpu[i];
                result.gpuExclusive[i] += sample.gpuExclusive[i];
            }
        }
        if (sample.gpuReady){
            result.latestGpu = sample.gpu;
            ++result.gpuSamples;
            result.gpuLag = m_sequence - sample.sequence;
        }
        if (sample.countersValid){
            for (std::size_t i = 0; i < CounterCount; ++i){
                result.counters[i] += sample.counters[i];
            }
            ++result.counterSamples;
        }
    }
    if (!intervals.empty()){
        std::sort(intervals.begin(), intervals.end());
        result.p95Ms = intervals[static_cast<std::size_t>(std::ceil(intervals.size() * 0.95)) - 1];
        result.frameMs /= result.samples;
        for (auto& time : result.cpu){
            time /= result.samples;
        }
        for (auto& time : result.cpuExclusive){
            time /= result.samples;
        }
    }
    if (result.gpuSamples){
        for (auto& time : result.gpu){
            time /= result.gpuSamples;
        }
        for (auto& time : result.gpuExclusive){
            time /= result.gpuSamples;
        }
    }
    if (result.counterSamples){
        for (auto& value : result.counters){
            value /= result.counterSamples;
        }
    }
    return result;
}
} // namespace vr
