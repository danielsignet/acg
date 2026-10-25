#include <cstdlib>
#include <stdexcept>
#include <vector>
#include <vr/ResourceLocator.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace vr{
// Checks for the shader that identifies a complete resource directory.
bool ResourceLocator::isResourceRoot(const std::filesystem::path& path){
    return std::filesystem::is_regular_file(path / "shaders/phong-shading.vs");
}

// Resolves the running executable through the platform API when available.
std::filesystem::path ResourceLocator::executablePath(const char* executable){
#ifdef _WIN32
    std::vector<wchar_t> buffer(32768);
    const auto length =
        GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length){
        return std::wstring(buffer.data(), length);
    }
#else
    char buffer[4096];
    const auto length = readlink("/proc/self/exe", buffer, sizeof(buffer));
    if (length > 0){
        return std::string(buffer, static_cast<std::size_t>(length));
    }
#endif
    return std::filesystem::absolute(executable);
}

// Finds resources using an explicit root, VR_PATH, executable location or source tree.
std::filesystem::path ResourceLocator::findRoot(const char* executable,
                                                const std::filesystem::path& requested){
    if (!requested.empty()){
        if (!isResourceRoot(requested)){
            throw std::runtime_error("Invalid resource root: " + requested.string());
        }
        return std::filesystem::absolute(requested);
    }
    if (const auto* environment = std::getenv("VR_PATH")){
        if (isResourceRoot(environment)){
            return environment;
        }
    }
    auto path = executablePath(executable).parent_path();
    for (int level = 0; level < 4; ++level, path = path.parent_path()){
        if (isResourceRoot(path)){
            return path;
        }
    }
    if (isResourceRoot(VR_SOURCE_ROOT)){
        return VR_SOURCE_ROOT;
    }
    throw std::runtime_error("Assets not found; use --root <resource directory>");
}

// Returns a diagnostic output location in the operating system's temporary directory.
std::filesystem::path ResourceLocator::temporaryOutput(const std::string& filename){
    return std::filesystem::temp_directory_path() / "acg-viewer" / filename;
}
} // namespace vr
