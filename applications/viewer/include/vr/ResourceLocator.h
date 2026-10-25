#pragma once
#include <filesystem>

namespace vr{
class ResourceLocator final{
public:
    // Returns a diagnostic output location in the operating system's temporary directory.
    static std::filesystem::path temporaryOutput(const std::string& filename);

    // Finds resources using an explicit root, VR_PATH, executable location or source tree.
    static std::filesystem::path findRoot(const char* executable,
                                          const std::filesystem::path& requested ={});

private:
    // Checks for the shader that identifies a complete resource directory.
    static bool isResourceRoot(const std::filesystem::path& path);

    // Resolves the running executable through the platform API when available.
    static std::filesystem::path executablePath(const char* executable);
};
} // namespace vr
