add_library(vrlib STATIC
    src/vr/SkyParticles.cpp
    src/vr/Billboard.cpp
    src/vr/BounceAnimation.cpp
    src/glad.c
    src/vr/SpinCallback.cpp
    src/vr/SkeletalAnimation.cpp
    src/vr/OrbitCallback.cpp
    src/vr/LodDemoCallback.cpp
    src/vr/BoundingBox.cpp
    src/vr/CullVisitor.cpp
    src/vr/DebugLines.cpp
    src/vr/Frustum.cpp
    src/vr/Flashlight.cpp
    src/vr/GBuffer.cpp
    src/vr/ColorTarget.cpp
    src/vr/ScreenQuad.cpp
    src/vr/DeferredRenderer.cpp
    src/vr/LightGrid.cpp
    src/vr/DepthOfField.cpp
    src/vr/AmbientOcclusion.cpp
    src/vr/Antialiasing.cpp
    src/vr/EnvironmentMap.cpp
    src/vr/PlanarReflection.cpp
    src/vr/ReflectionProbes.cpp
    src/vr/FrameProfiler.cpp
    src/vr/PerformanceOverlay.cpp
    src/vr/Node.cpp
    src/vr/PortalVisibility.cpp
    src/vr/Application.cpp
    src/vr/Camera.cpp
    src/vr/DotVisitor.cpp
    src/vr/FramebufferState.cpp
    src/vr/GLState.cpp
    src/vr/FramebufferImage.cpp
    src/vr/Geometry.cpp
    src/vr/GeometryTerrain.cpp
    src/vr/GraphVisitor.cpp
    src/vr/CallbackVisitor.cpp
    src/vr/StreamBuffer.cpp
    src/vr/GpuSkinning.cpp
    src/vr/GeometrySkin.cpp
    src/vr/GeometryTangents.cpp
    src/vr/GeometryRay.cpp
    src/vr/Subdivision.cpp
    src/vr/ModelInstance.cpp
    src/vr/ModelPicker.cpp
    src/vr/GeometryFactory.cpp
    src/vr/Group.cpp
    src/vr/LOD.cpp
    src/vr/LightCollector.cpp
    src/vr/RenderToTexture.cpp
    src/vr/ShadowCameras.cpp
    src/vr/ShadowCollector.cpp
    src/vr/ShadowRenderer.cpp
    src/vr/Light.cpp
    src/vr/Material.cpp
    src/vr/ModelReader.cpp
    src/vr/ModelAnimationReader.cpp
    src/vr/NodeVisitor.cpp
    src/vr/PathVisitor.cpp
    src/vr/Program.cpp
    src/vr/Reader.cpp
    src/vr/RenderVisitor.cpp
    src/vr/RenderingValidator.cpp
    src/vr/ResourceLocator.cpp
    src/vr/Scene.cpp
    src/vr/SceneOverlay.cpp
    src/vr/SceneReader.cpp
    src/vr/State.cpp
    src/vr/Text.cpp
    src/vr/Texture.cpp
    src/vr/Transform.cpp
    src/vr/UpdateVisitor.cpp
    src/vr/Viewer.cpp
)
target_include_directories(vrlib PUBLIC include)
target_include_directories(vrlib SYSTEM PUBLIC "${VRLIB_SOURCE_DIR}/dependencies/glm"
  PRIVATE "${VRLIB_SOURCE_DIR}/dependencies"
  "${VRLIB_SOURCE_DIR}/dependencies/stb"
  "${VRLIB_SOURCE_DIR}/dependencies/assimp-5.3.1/contrib/zlib"
  "${PROJECT_BINARY_DIR}/dependencies/assimp-5.3.1/contrib/zlib"
  "${VRLIB_SOURCE_DIR}/dependencies/freetype-2.3.11/include")
target_link_libraries(vrlib PUBLIC glfw assimp OpenGL::GL PRIVATE freetype zlibstatic ${CMAKE_DL_LIBS})
target_compile_definitions(vrlib PUBLIC GLFW_INCLUDE_NONE GLM_FORCE_RADIANS)
target_compile_definitions(vrlib PRIVATE VR_SOURCE_ROOT="${VRLIB_SOURCE_DIR}")
if(MSVC)
  target_compile_definitions(vrlib PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
if(MSVC)
  target_compile_options(vrlib PRIVATE /W4 /utf-8 /permissive-)
  # RelWithDebInfo defaults to /Ob1. The measured per-vertex GLM kernel benefits
  # from the same automatic inlining as Release, while retaining debug symbols.
  set_property(SOURCE src/vr/GeometrySkin.cpp APPEND PROPERTY COMPILE_OPTIONS
    "$<$<CONFIG:RelWithDebInfo>:/Ob2>")
else()
  target_compile_options(vrlib PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wall;-Wextra;-Wpedantic>)
endif()
add_executable(viewer main.cpp)
target_link_libraries(viewer PRIVATE vrlib)
if(MSVC)
  set_target_properties(viewer PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(viewer PRIVATE /ENTRY:mainCRTStartup)
endif()
set_target_properties(viewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${config} CONFIG_UPPER)
  set_target_properties(viewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${PROJECT_BINARY_DIR}/bin")
endforeach()
# Keep the default scene runnable beside the executable and in installed packages.
include("${VRLIB_SOURCE_DIR}/cmake/runtime-resources.cmake")

set(VIEWER_RESOURCE_OUTPUTS)
foreach(resource IN LISTS VIEWER_RESOURCES)
    get_filename_component(resource_directory "${resource}" DIRECTORY)
    if(resource_directory STREQUAL "")
        set(resource_directory ".")
    endif()
    set(resource_output "${PROJECT_BINARY_DIR}/bin/${resource}")
    add_custom_command(OUTPUT "${resource_output}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${PROJECT_BINARY_DIR}/bin/${resource_directory}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${VRLIB_SOURCE_DIR}/${resource}" "${resource_output}"
        DEPENDS "${VRLIB_SOURCE_DIR}/${resource}"
        VERBATIM)
    list(APPEND VIEWER_RESOURCE_OUTPUTS "${resource_output}")
    install(FILES "${VRLIB_SOURCE_DIR}/${resource}" DESTINATION "${resource_directory}")
endforeach()
add_custom_target(viewer_resources DEPENDS ${VIEWER_RESOURCE_OUTPUTS})
add_dependencies(viewer viewer_resources)
install(TARGETS viewer RUNTIME DESTINATION .)
install(FILES "${VRLIB_SOURCE_DIR}/LICENSE" DESTINATION .)

# Both applications use the same library and carry the complete offline resource set.
add_executable(pantheon "${VRLIB_SOURCE_DIR}/experiments/pantheon/tools/gyro_main.cpp")
target_link_libraries(pantheon PRIVATE vrlib)
set_target_properties(pantheon PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${PROJECT_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER ${config} CONFIG_UPPER)
  set_target_properties(pantheon PROPERTIES RUNTIME_OUTPUT_DIRECTORY_${CONFIG_UPPER} "${PROJECT_BINARY_DIR}/bin")
endforeach()
if(MSVC)
  set_target_properties(pantheon PROPERTIES WIN32_EXECUTABLE TRUE)
  target_link_options(pantheon PRIVATE /ENTRY:mainCRTStartup)
endif()
add_dependencies(pantheon viewer_resources)
install(TARGETS pantheon RUNTIME DESTINATION .)
