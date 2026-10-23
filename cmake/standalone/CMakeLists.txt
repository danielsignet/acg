cmake_minimum_required(VERSION 3.21)
project(vrlib VERSION 2.0 LANGUAGES C CXX)

get_filename_component(VRLIB_SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(VRLIB_MAJOR_VERSION ${PROJECT_VERSION_MAJOR})
set(VRLIB_MINOR_VERSION ${PROJECT_VERSION_MINOR})
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build configuration" FORCE)
endif()

# Build the bundled libraries together so their configurations stay compatible.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build static dependencies" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${VRLIB_SOURCE_DIR}/dependencies/glfw-3.3.9" "dependencies/glfw" EXCLUDE_FROM_ALL)

set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ASSIMP_TOOLS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
set(ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_ZLIB ON CACHE BOOL "" FORCE)
add_subdirectory("${VRLIB_SOURCE_DIR}/dependencies/assimp-5.3.1" "dependencies/assimp" EXCLUDE_FROM_ALL)
add_subdirectory("${VRLIB_SOURCE_DIR}/dependencies/freetype-2.3.11" "dependencies/freetype" EXCLUDE_FROM_ALL)

find_package(OpenGL REQUIRED)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  set(X_LIBS Threads::Threads ${CMAKE_DL_LIBS})
endif()

# Adapt dependency targets to the variables used by the original applications.
set(GLFW3_LIBRARY glfw)
set(ASSIMP_LIBRARIES_RELEASE assimp)
set(FREETYPE_LIBRARIES freetype)
add_compile_definitions(HAVE_FREETYPE)
include_directories(
  "${VRLIB_SOURCE_DIR}/dependencies/glm"
  "${VRLIB_SOURCE_DIR}/dependencies/stb"
  "${VRLIB_SOURCE_DIR}/dependencies/freetype-2.3.11/include"
  "${CMAKE_BINARY_DIR}/include"
)
file(COPY "${VRLIB_SOURCE_DIR}/dependencies/rapidxml-1.13/"
  DESTINATION "${CMAKE_BINARY_DIR}/include/rapidxml"
  FILES_MATCHING PATTERN "*.hpp")

# Reuse the existing application discovery and target definitions unchanged.
add_subdirectory("${VRLIB_SOURCE_DIR}/applications" "applications")
