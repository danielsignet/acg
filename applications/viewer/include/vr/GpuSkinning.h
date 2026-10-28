#pragma once
#include "Program.h"
#include "StreamBuffer.h"
#include <cstdint>
#include <vector>

namespace vr{
class Geometry;
class FrameProfiler;

struct GpuSkinData{
    ~GpuSkinData();
    GLuint inputArray = 0, inputBuffer = 0, jointBuffer = 0, weightBuffer = 0;
    GLuint joints = 0, weights = 0, palette = 0;
    StreamBuffer output, paletteStream;
    std::uint64_t revision = 0;
};

class GpuSkinning final{
public:
    struct Stats{
        std::size_t vertices = 0, cpuPositions = 0, draws = 0, paletteBytes = 0;
    };

    explicit GpuSkinning(const std::filesystem::path& root, FrameProfiler* profiler = nullptr);
    ~GpuSkinning();
    void beginFrame();
    bool enqueue(Geometry& geometry);
    void prepare();
    void endFrame();

    const Stats& getStats() const{
        return m_stats;
    }

    FrameProfiler* getProfiler() const{
        return m_profiler;
    }

    void recordCpuPositions(std::size_t count){
        m_stats.cpuPositions += count;
    }

private:
    void skin(Geometry& geometry);
    std::shared_ptr<Program> m_program;
    std::vector<Geometry*> m_pending;
    std::vector<glm::vec4> m_palette;
    FrameProfiler* m_profiler;
    GLint m_maxTexels = 0;
    GLuint m_feedback = 0;
    Stats m_stats;
};
} // namespace vr
