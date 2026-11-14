# Dear ImGui's SDL3 platform and OpenGL 3 renderer backends (ADR 0018). The
# Conan imgui package ships them as sources in res/bindings next to the
# library; they are compiled here once, without project warning flags, like the
# other third-party code. The engine links this privately: only
# engine/src/platform/sdl3/ includes imgui_impl_sdl3.h and only
# engine/src/render/opengl/ includes imgui_impl_opengl3.h, so no SDL or OpenGL
# type reaches another part of the engine.
find_package(imgui REQUIRED CONFIG)
find_package(SDL3 REQUIRED CONFIG)

# Each build folder has the Conan package of one configuration.
set(imgui_bindings "")
foreach(config RELEASE DEBUG RELWITHDEBINFO MINSIZEREL)
    if(NOT imgui_bindings AND DEFINED imgui_PACKAGE_FOLDER_${config})
        set(imgui_bindings "${imgui_PACKAGE_FOLDER_${config}}/res/bindings")
    endif()
endforeach()
if(NOT EXISTS "${imgui_bindings}/imgui_impl_sdl3.cpp" OR
   NOT EXISTS "${imgui_bindings}/imgui_impl_opengl3.cpp")
    message(FATAL_ERROR "Dear ImGui backends not found in the Conan package: '${imgui_bindings}'")
endif()

add_library(imgui_backends STATIC
    "${imgui_bindings}/imgui_impl_sdl3.cpp"
    "${imgui_bindings}/imgui_impl_opengl3.cpp"
)
# SYSTEM: third-party headers stay out of the project warnings (-Werror).
target_include_directories(imgui_backends SYSTEM PUBLIC "${imgui_bindings}")
target_compile_features(imgui_backends PUBLIC cxx_std_20)
# The OpenGL 3 backend loads its few OpenGL functions itself (its bundled
# loader opens the system OpenGL library at run time).
target_link_libraries(imgui_backends
    PUBLIC imgui::imgui
    PRIVATE SDL3::SDL3 ${CMAKE_DL_LIBS}
)
