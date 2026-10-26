#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <vector>
#include <vr/FrameProfiler.h>

namespace vr{
// Names the measured stages and identifies CPU-only work.
const std::array<FrameProfiler::Description, FrameProfiler::StageCount>&
FrameProfiler::descriptions(){
    static const std::array<Description, StageCount> stages{{{"Frame incl. swap", false},
                                                             {"Input / animation", false},
                                                             {"Render total", true},
                                                             {"Shadow maps", true},
                                                             {"Portal traversal", false},
                                                             {"Light collection", false},
                                                             {"Scene graph culling", false},
                                                             {"G-buffer geometry", true},
                                                             {"Deferred lighting", true},
                                                             {"Transparent surfaces", true},
                                                             {"Bloom (7 draws)", true},
                                                             {"DoF (2 draws)", true},
                                                             {"Composite / tone map", true},
                                                             {"Present / previews", true},
                                                             {"Debug volumes", true},
                                                             {"HUD / performance view", true},
                                                             {"Swap / VSync wait", false}}};
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
    if (enabled && !m_allocated){
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
    m_history.clear();
    m_current ={};
    m_skipped = 0;
    // Unique frame IDs prevent pending results from entering the new history.
}

// Groups nonoverlapping scopes and accounts for remaining time without counting totals twice.
std::array<FrameProfiler::Slice, FrameProfiler::SliceCount>
FrameProfiler::breakdown(const Summary& summary){
    std::array<Slice, SliceCount> slices{{{"Shadows"},
                                          {"Visibility"},
                                          {"G-buffer geometry"},
                                          {"Lighting / transparency"},
                                          {"Post-processing / output"},
                                          {"HUD / debug"},
                                          {"Swap / VSync wait"},
                                          {"Other / gaps"}}};
    const auto add = [&](std::size_t slice, std::initializer_list<Stage> stages){
        for (const auto stage : stages){
            const auto index = static_cast<std::size_t>(stage);
            slices[slice].cpuMs += summary.cpu[index];
            slices[slice].gpuMs += summary.gpu[index];
        }
    };
    add(0,{Stage::Shadows});
    add(1,{Stage::Portals, Stage::Lights, Stage::Culling});
    add(2,{Stage::Geometry});
    add(3,{Stage::Lighting, Stage::Transparency});
    add(4,{Stage::Bloom, Stage::DepthOfField, Stage::Composite, Stage::Present});
    add(5,{Stage::Debug, Stage::Overlay});
    add(6,{Stage::Swap});
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
            for (std::size_t i = 0; i < StageCount; ++i){
                if (!queries.used[i]){
                    continue;
                }
                GLuint64 start = 0, end = 0;
                glGetQueryObjectui64v(queries.ids[i * 2], GL_QUERY_RESULT, &start);
                glGetQueryObjectui64v(queries.ids[i * 2 + 1], GL_QUERY_RESULT, &end);
                sample->gpu[i] = end >= start ? static_cast<double>(end - start) / 1e6 : 0;
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
    collect();
    m_current ={};
    m_current.sequence = ++m_sequence;
    m_current.intervalMs = std::max(0.0, intervalSeconds * 1000);
    m_recording = nullptr;
    if (m_gpuSupported){
        for (auto& queries : m_queries){
            if (!queries.pending){
                queries.used.fill(false);
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
    const auto i = static_cast<std::size_t>(stage);
    m_gpu = descriptions()[i].gpu && m_profiler->m_recording;
    if (m_gpu){
        auto& queries = *m_profiler->m_recording;
        queries.used[i] = true;
        glQueryCounter(queries.ids[i * 2], GL_TIMESTAMP);
    }
}

// Publishes CPU duration and queues the matching GPU timestamp.
FrameProfiler::Scope::~Scope(){
    if (!m_profiler || !m_profiler->m_active){
        return;
    }
    const auto i = static_cast<std::size_t>(m_stage);
    if (m_gpu){
        auto& queries = *m_profiler->m_recording;
        queries.last = queries.ids[i * 2 + 1];
        glQueryCounter(queries.last, GL_TIMESTAMP);
    }
    m_profiler->m_current.cpu[i] +=
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - m_start)
            .count();
}

// Averages the bounded history and computes the 95th-percentile frame interval.
FrameProfiler::Summary FrameProfiler::summarize() const{
    Summary result;
    result.samples = static_cast<unsigned>(m_history.size());
    result.skipped = m_skipped;
    std::vector<double> intervals;
    for (const auto& sample : m_history){
        intervals.push_back(sample.intervalMs);
        result.frameMs += sample.intervalMs;
        result.latestCpu = sample.cpu;
        for (std::size_t i = 0; i < StageCount; ++i){
            result.cpu[i] += sample.cpu[i];
            if (sample.gpuReady){
                result.gpu[i] += sample.gpu[i];
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
    }
    if (result.gpuSamples){
        for (auto& time : result.gpu){
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
