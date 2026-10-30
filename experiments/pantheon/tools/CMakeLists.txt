cmake_minimum_required(VERSION 3.21)
project(pantheon_capture LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(ACG "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(BASE "${ACG}/build/windows")
add_executable(pantheon_capture capture.cpp)
add_executable(pantheon_rooms_fixed_viewer gyro_main.cpp)
add_executable(gyroscope_tests gyroscope_tests.cpp)
add_executable(sky_environment_tests sky_environment_tests.cpp)
add_executable(room_isolation_tests room_isolation_tests.cpp)
add_executable(picking_audit picking_audit.cpp)
add_executable(room_work_tests room_work_tests.cpp)
add_executable(reflection_transition_tests reflection_transition_tests.cpp)
enable_testing()
add_test(NAME gyroscopes COMMAND gyroscope_tests "${ACG}")
add_test(NAME room_skies COMMAND sky_environment_tests "${ACG}")
add_test(NAME room_isolation COMMAND room_isolation_tests)
add_test(NAME room_work COMMAND room_work_tests "${ACG}")
add_test(NAME reflection_transitions COMMAND reflection_transition_tests "${ACG}")
foreach(target pantheon_capture pantheon_rooms_fixed_viewer gyroscope_tests sky_environment_tests room_isolation_tests picking_audit room_work_tests reflection_transition_tests)
target_include_directories(${target} PRIVATE
  "${ACG}/applications/viewer/include" "${ACG}/dependencies/glm"
  "${ACG}/dependencies/glfw-3.3.9/include")
target_compile_definitions(${target} PRIVATE GLFW_INCLUDE_NONE GLM_FORCE_RADIANS)
if(MSVC)
  target_compile_options(${target} PRIVATE /W4 /utf-8 /permissive-)
endif()
target_link_libraries(${target} PRIVATE
  "${BASE}/applications/viewer/$<CONFIG>/vrlib$<$<CONFIG:Debug>:d>.lib"
  "${BASE}/dependencies/glfw-3.3.9/src/$<CONFIG>/glfw3$<$<CONFIG:Debug>:d>.lib"
  "${BASE}/dependencies/assimp-5.3.1/lib/$<CONFIG>/assimp-vc143-mt$<$<CONFIG:Debug>:d>.lib"
  "${BASE}/dependencies/freetype-2.3.11/$<CONFIG>/freetype$<$<CONFIG:Debug>:d>.lib"
  "${BASE}/dependencies/assimp-5.3.1/contrib/zlib/$<CONFIG>/zlibstatic$<$<CONFIG:Debug>:d>.lib"
  opengl32)
endforeach()
if(MSVC)
  set_target_properties(pantheon_rooms_fixed_viewer PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(pantheon_rooms_fixed_viewer PRIVATE /ENTRY:mainCRTStartup)
endif()
