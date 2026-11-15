// Raven UI evaluation prototype (docs/experiments/raven-ui-evaluation.md).
//
// One SDL3 window with the engine's OpenGL 4.5 context and renderer. Raven
// runs on its host platform (no window or event queue of its own) with the
// prototype's OpenGL backend, its render thread disabled, so every OpenGL
// call happens on this thread. The editor shell comes from editor.xml; its
// viewport shows island-preview's generated archipelago, drawn by the
// engine's renderer from Raven's viewport draw callback (or, to compare,
// read back to the CPU and shown as a Raven texture).
//
// Modes: interactive (default), --self-test (scripted input through SDL's
// event queue and checks), --stress N (Raven started and stopped N times),
// --benchmark (frame times of both viewport modes), --frames N with
// --screenshot FILE (a picture of frame N), --input-check (real OS input
// from input_driver.py, docs/experiments/raven-final-validation.md), and
// --with-imgui (Dear ImGui's debug UI drawn next to Raven).

#include "GlRavenBackend.hpp"
#include "SdlRavenHost.hpp"
#include "Sky.hpp"
#include "WorldScene.hpp"

#include "engine/camera/Camera.hpp"
#include "engine/camera/FreeFlyController.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/core/Image.hpp"
#include "engine/core/Log.hpp"
#include "engine/render/Renderer.hpp"
#include "engine/worldgen/Archipelago.hpp"
#include "engine/worldgen/WorldGenSettings.hpp"

#include <Raven.h>
#include <SDL3/SDL.h>
#include <glad/gl.h>
#include <glm/geometric.hpp>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl3.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <format>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start)
{
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

// Raven reports errors through one callback without user data.
struct RavenErrors {
    std::vector<std::string> messages;
    std::uint32_t errors = 0;
    std::uint32_t warnings = 0;
};

RavenErrors& ravenErrors()
{
    static RavenErrors errors;
    return errors;
}

void onRavenError(RError code, RSeverity severity, const char* detail)
{
    RavenErrors& errors = ravenErrors();
    const bool error = severity == R_SEVERITY_ERROR || severity == R_SEVERITY_FATAL;
    if (error) {
        ++errors.errors;
    } else {
        ++errors.warnings;
    }
    std::string text = std::format("raven {} 0x{:04X}: {}", error ? "error" : "warning",
                                   static_cast<unsigned>(code), detail != nullptr ? detail : "");
    if (error) {
        engine::core::logError("raven", "{}", text);
    } else {
        engine::core::logWarning("raven", "{}", text);
    }
    if (errors.messages.size() < 200) {
        errors.messages.push_back(std::move(text));
    }
}

struct Options {
    std::uint64_t frames = 0; // 0: until the window closes
    std::optional<std::filesystem::path> screenshot;
    bool selfTest = false;
    std::uint32_t stressCycles = 0;
    bool benchmark = false;
    // Phases driven by real OS input from input_driver.py.
    bool inputCheck = false;
    // Dear ImGui's debug UI next to Raven, on the same window and context.
    bool withImGui = false;
    bool textureMode = false;
    bool vsync = true;
    // Raven's look: dark (default), light, glass (needs backdrop copies), windows.
    RLook look = R_LOOK_RAVEN_DARK;
    // RInitDesc::ui_scale, on top of the display's scale (0: 1).
    float uiScale = 0.0F;
    int width = 1280;
    int height = 760;
    std::uint64_t seed = 1;
};

template <typename T>
bool parseNumber(std::string_view text, T& value)
{
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size();
}

std::optional<Options> parseOptions(int argc, char** argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--frames" && hasValue && parseNumber(argv[i + 1], options.frames)) {
            ++i;
        } else if (arg == "--screenshot" && hasValue) {
            options.screenshot = std::filesystem::path(argv[++i]);
        } else if (arg == "--self-test") {
            options.selfTest = true;
        } else if (arg == "--input-check") {
            options.inputCheck = true;
        } else if (arg == "--with-imgui") {
            options.withImGui = true;
        } else if (arg == "--stress" && hasValue &&
                   parseNumber(argv[i + 1], options.stressCycles)) {
            ++i;
        } else if (arg == "--benchmark") {
            options.benchmark = true;
            options.vsync = false;
        } else if (arg == "--texture-mode") {
            options.textureMode = true;
        } else if (arg == "--no-vsync") {
            options.vsync = false;
        } else if (arg == "--ui-scale" && hasValue && parseNumber(argv[i + 1], options.uiScale) &&
                   options.uiScale >= 0.25F && options.uiScale <= 4.0F) {
            ++i;
        } else if (arg == "--look" && hasValue) {
            const std::string_view look = argv[++i];
            if (look == "dark") {
                options.look = R_LOOK_RAVEN_DARK;
            } else if (look == "light") {
                options.look = R_LOOK_RAVEN_LIGHT;
            } else if (look == "glass") {
                options.look = R_LOOK_RAVEN_GLASS_DARK;
            } else if (look == "windows") {
                options.look = R_LOOK_WINDOWS_DARK;
            } else {
                return std::nullopt;
            }
        } else if (arg == "--seed" && hasValue && parseNumber(argv[i + 1], options.seed)) {
            ++i;
        } else if (arg == "--size" && hasValue) {
            const std::string_view size = argv[++i];
            const std::size_t x = size.find('x');
            if (x == std::string_view::npos || !parseNumber(size.substr(0, x), options.width) ||
                !parseNumber(size.substr(x + 1), options.height) || options.width < 320 ||
                options.height < 240) {
                return std::nullopt;
            }
        } else {
            return std::nullopt;
        }
    }
    return options;
}

engine::render::LightDesc sunlight()
{
    // island-preview's sun for the archipelago (M3.8).
    return engine::render::LightDesc{.direction = {-0.45F, -0.78F, -0.43F},
                                     .color = engine::math::Vec3{0.5F, 0.465F, 0.41F},
                                     .ambient = engine::math::Vec3{0.5F, 0.53F, 0.62F}};
}

engine::math::Mat4 rotationOnly(const engine::math::Mat4& view)
{
    engine::math::Mat4 rotation = view;
    rotation[3] = engine::math::Vec4{0.0F, 0.0F, 0.0F, 1.0F};
    return rotation;
}

engine::camera::Camera lookingAt(const engine::math::Vec3& position,
                                 const engine::math::Vec3& target)
{
    const engine::math::Vec3 d = glm::normalize(target - position);
    engine::camera::Camera camera;
    camera.position = position;
    camera.pitchRadians = std::asin(std::clamp(d.y, -1.0F, 1.0F));
    camera.yawRadians = std::atan2(-d.x, -d.z);
    return camera;
}

struct IslandInfo {
    std::string name;
    engine::math::Vec3 anchor{0.0F};
    float radius = 0.0F;
    float topHeight = 0.0F;
    float undersideDepth = 0.0F;
    std::size_t lakes = 0;
    std::size_t streams = 0;
    std::size_t waterfalls = 0;
};

enum class ViewportMode { Callback, Texture };

struct StepResult {
    std::string name;
    bool passed = false;
    std::string detail;
};

class Prototype {
public:
    explicit Prototype(Options options) : options_(std::move(options)) {}
    Prototype(const Prototype&) = delete;
    Prototype& operator=(const Prototype&) = delete;
    ~Prototype() { shutdownAll(); }

    int run()
    {
        if (!startPlatform() || !startWorld()) {
            return 1;
        }
        if (options_.stressCycles > 0) {
            return runStress();
        }
        if (!startRaven()) {
            return 1;
        }
        if (options_.withImGui && !startImGui()) {
            return 1;
        }
        if (options_.benchmark) {
            return runBenchmark();
        }
        if (options_.selfTest) {
            buildSelfTest();
        }
        if (options_.inputCheck) {
            buildInputCheck();
        }
        std::uint64_t frame = 0;
        while (running_) {
            ++frame;
            const bool last = options_.frames != 0 && frame == options_.frames;
            if (last && options_.screenshot) {
                screenshotPath_ = options_.screenshot;
            }
            runFrame();
            if (options_.selfTest) {
                advanceSelfTest();
            }
            if (options_.inputCheck) {
                advanceInputCheck();
            }
            if (last) {
                break;
            }
        }
        const bool closedByRaven = !running_;
        const auto memory = stopRaven();
        return report(closedByRaven, memory);
    }

private:
    // ---- platform, renderer and world ----------------------------------

    bool startPlatform()
    {
        if (!SDL_Init(SDL_INIT_VIDEO)) {
            engine::core::logError("app", "SDL_Init: {}", SDL_GetError());
            return false;
        }
        sdlStarted_ = true;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 5);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
        SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
        window_ = SDL_CreateWindow(
            "LowPolyEngine - Raven UI evaluation", options_.width, options_.height,
            SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (window_ == nullptr) {
            engine::core::logError("app", "SDL_CreateWindow: {}", SDL_GetError());
            return false;
        }
        context_ = SDL_GL_CreateContext(window_);
        if (context_ == nullptr || !SDL_GL_MakeCurrent(window_, context_)) {
            engine::core::logError("app", "OpenGL 4.5 context: {}", SDL_GetError());
            return false;
        }
        SDL_GL_SetSwapInterval(options_.vsync ? 1 : 0);
        if (gladLoadGL(reinterpret_cast<GLADloadfunc>(SDL_GL_GetProcAddress)) == 0) {
            engine::core::logError("app", "OpenGL functions could not be loaded");
            return false;
        }
        auto renderer = engine::render::Renderer::create({
            .glProcLoader = SDL_GL_GetProcAddress,
            .shaderDirectory = std::filesystem::path(LPE_RAVEN_ASSET_DIR) / "shaders",
        });
        if (!renderer) {
            engine::core::logError("app", "renderer: {}", renderer.error().message);
            return false;
        }
        renderer_.emplace(std::move(renderer).value());
        const auto& info = renderer_->info();
        log(std::format("OpenGL: {} / {} / {}", info.device, info.driverVersion, info.vendor));
        SDL_StartTextInput(window_);
        host_ = std::make_unique<raven_eval::SdlRavenHost>(window_);
        return true;
    }

    bool startWorld()
    {
        settings_.seed = options_.seed;
        if (!generateWorld()) {
            return false;
        }
        auto sky =
            renderer_->createMesh(island_preview::buildSkyDome(island_preview::floatingWorldSky()));
        if (!sky) {
            engine::core::logError("app", "sky: {}", sky.error().message);
            return false;
        }
        sky_ = sky.value();
        camera_ = overviewCamera();
        controller_ = engine::camera::FreeFlyController{{.moveSpeed = 30.0F}};
        return true;
    }

    bool generateWorld()
    {
        const auto started = Clock::now();
        auto generated = engine::worldgen::generateArchipelago(settings_);
        if (!generated) {
            log("world generation failed: " + generated.error().message);
            return false;
        }
        const engine::worldgen::Archipelago& archipelago = generated.value();
        if (auto error = world_.upload(*renderer_, archipelago, settings_)) {
            log("world upload failed: " + error->message);
            return false;
        }
        islands_.clear();
        for (const auto& island : archipelago.islands) {
            const auto& placed = island.shape.island;
            islands_.push_back(IslandInfo{
                .name = std::format("Island {}", placed.index),
                .anchor = placed.anchor,
                .radius = placed.radius,
                .topHeight = placed.topHeight,
                .undersideDepth = placed.undersideDepth,
                .lakes = island.water.lakes.size(),
                .streams = island.water.streams.size(),
                .waterfalls = island.water.waterfalls.size(),
            });
        }
        triangles_ = archipelago.triangleCount();
        log(std::format("world: seed {}, {} islands, {} triangles, generated in {:.1f} ms",
                        settings_.seed, islands_.size(), triangles_, millisecondsSince(started)));
        return true;
    }

    [[nodiscard]] engine::camera::Camera overviewCamera() const
    {
        engine::math::Vec3 centre{0.0F};
        for (const auto& island : islands_) {
            centre += island.anchor;
        }
        centre /= static_cast<float>(std::max<std::size_t>(islands_.size(), 1U));
        float reach = 0.0F;
        for (const auto& island : islands_) {
            const engine::math::Vec3 offset = island.anchor - centre;
            reach = std::max(reach,
                             std::sqrt(offset.x * offset.x + offset.z * offset.z) + island.radius);
        }
        const engine::math::Vec3 direction =
            glm::normalize(engine::math::Vec3{0.55F, 0.45F, 0.72F});
        return lookingAt(centre + direction * (1.3F * reach + 60.0F),
                         centre - engine::math::Vec3{0.0F, 0.25F * reach, 0.0F});
    }

    [[nodiscard]] engine::camera::Camera islandCamera(const IslandInfo& island) const
    {
        const engine::math::Vec3 direction =
            glm::normalize(engine::math::Vec3{0.55F, 0.35F, 0.72F});
        const float distance = 2.6F * island.radius + 20.0F;
        return lookingAt(island.anchor + direction * distance,
                         island.anchor - engine::math::Vec3{0.0F, 0.2F * island.radius, 0.0F});
    }

    void drawWorld(int width, int height)
    {
        engine::render::Renderer& renderer = *renderer_;
        const engine::math::Mat4 view = engine::camera::viewMatrix(camera_);
        const engine::math::Mat4 projection =
            engine::camera::projectionMatrix(camera_, engine::camera::aspectRatio(width, height));
        renderer.setView({.view = rotationOnly(view),
                          .projection = projection,
                          .depth = engine::render::DepthMode::Off});
        renderer.drawMesh(sky_);
        renderer.setView({.view = view, .projection = projection});
        renderer.setLight(sunlight());
        const double seconds = std::chrono::duration<double>(Clock::now() - startTime_).count();
        world_.drawOpaque(renderer, seconds);
        world_.drawWater(renderer, view, projection, camera_.position);
    }

    // ---- Raven ---------------------------------------------------------

    bool startRaven()
    {
        raven_eval::setShaderDirectory(LPE_RAVEN_SHADER_DIR);
        Rerror_set_callback(&onRavenError);
        host_->registerPlatform();
        Rbackend_register(&raven_eval::openGlBackend());
        RInitDesc desc{};
        desc.backend = R_BACKEND_CUSTOM;
        desc.app_name = "LowPolyEngine Raven evaluation";
        desc.disable_render_thread = R_TRUE;
        desc.ui_scale = options_.uiScale;
        if (Rinit_ex(&desc) == R_FALSE) {
            const char* detail = nullptr;
            (void)Rerror_get_last(&detail, nullptr);
            engine::core::logError("app", "Rinit_ex failed: {} ({})", detail ? detail : "?",
                                   raven_eval::backendStats().lastError);
            return false;
        }
        ravenStarted_ = true;
        Rlook_set(options_.look);
        if (!Rhost_platform_is_active()) {
            engine::core::logError("app", "Raven does not use the host platform");
            return false;
        }
        Raction_bind("world.regenerate", &Prototype::onAction, this);
        Raction_bind("viewport.mode", &Prototype::onAction, this);
        Raction_bind("app.screenshot", &Prototype::onAction, this);
        Raction_bind("island.focus", &Prototype::onAction, this);
        const std::string layout = std::string(LPE_RAVEN_EVALUATION_DIR) + "/editor.xml";
        const auto created = Clock::now();
        window_ui_ = Rwindow_create_from_xml_file(layout.c_str());
        if (window_ui_ == R_INVALID_ID) {
            engine::core::logError("app", "Raven could not create the window from {}", layout);
            return false;
        }
        host_->attach(window_ui_);
        raven_eval::setPresentedWindow(window_ui_);
        Rwindow_set_close_callback(window_ui_, &Prototype::onClose, this);
        if (!findWidgets()) {
            return false;
        }
        Rviewport_set_realtime(viewport_, R_TRUE);
        Rviewport_set_resize_callback(viewport_, &Prototype::onViewportResize, this);
        Rwidget_set_event_callback(viewport_, &Prototype::onViewportEvent, this);
        Rtree_view_set_selection_changed_callback(hierarchy_, &Prototype::onTreeSelection, this);
        Rtext_input_set_changed_callback(inspectorName_, &Prototype::onNameChanged, this);
        Rdock_area_set_changed_callback(dock_, &Prototype::onDockChanged, this);
        setViewportMode(options_.textureMode ? ViewportMode::Texture : ViewportMode::Callback);
        fillHierarchy();
        Rtext_input_set_text(inspectorSeed_, std::to_string(settings_.seed).c_str());
        refreshConsole();
        log(std::format("Raven {} (commit {}), window from XML in {:.1f} ms, scale {:.2f}",
                        Rget_version(), LPE_RAVEN_COMMIT, millisecondsSince(created),
                        host_->scale()));
        running_ = true;
        return true;
    }

    bool findWidgets()
    {
        struct Named {
            const char* name;
            Rid* id;
        };
        const Named widgets[] = {
            {"toolbar.regenerate", &regenerateButton_},
            {"toolbar.mode", &modeButton_},
            {"toolbar.screenshot", &screenshotButton_},
            {"toolbar.stats", &statsLabel_},
            {"dock", &dock_},
            {"inspector.panel", &inspectorPanel_},
            {"viewport", &viewport_},
            {"hierarchy", &hierarchy_},
            {"inspector.name", &inspectorName_},
            {"inspector.seed", &inspectorSeed_},
            {"inspector.details", &inspectorDetails_},
            {"inspector.focus", &focusButton_},
            {"inspector.notes", &inspectorNotes_},
            {"console.tabs", &consoleTabs_},
            {"console", &console_},
            {"console.frame", &frameLabel_},
        };
        bool ok = true;
        for (const Named& widget : widgets) {
            *widget.id = Rwidget_find(window_ui_, widget.name);
            if (*widget.id == R_INVALID_ID) {
                engine::core::logError("app", "editor.xml has no widget '{}'", widget.name);
                ok = false;
            }
        }
        return ok;
    }

    RMemoryStats stopRaven()
    {
        RMemoryStats memory{};
        if (ravenStarted_) {
            if (viewportTexture_ != R_INVALID_ID) {
                Rtexture_destroy(viewportTexture_);
                viewportTexture_ = R_INVALID_ID;
            }
            Rshutdown();
            ravenStarted_ = false;
            host_->detach();
            raven_eval::setPresentedWindow(R_INVALID_ID);
            Rmemory_get_stats(&memory);
        }
        window_ui_ = R_INVALID_ID;
        treeItems_.clear();
        return memory;
    }

    void shutdownAll()
    {
        stopImGui();
        (void)stopRaven();
        if (renderer_) {
            world_.release(*renderer_);
            renderer_->destroyMesh(sky_);
            renderer_.reset();
        }
        host_.reset();
        if (context_ != nullptr) {
            SDL_GL_DestroyContext(context_);
            context_ = nullptr;
        }
        if (window_ != nullptr) {
            SDL_DestroyWindow(window_);
            window_ = nullptr;
        }
        if (sdlStarted_) {
            SDL_Quit();
            sdlStarted_ = false;
        }
    }

    void fillHierarchy()
    {
        Rtree_view_clear_items(hierarchy_);
        treeItems_.clear();
        const Rid root = Rtree_view_add_item(
            hierarchy_, R_INVALID_ID, std::format("Archipelago (seed {})", settings_.seed).c_str());
        for (const auto& island : islands_) {
            treeItems_.push_back(Rtree_view_add_item(hierarchy_, root, island.name.c_str()));
        }
        Rtree_item_set_expanded(root, R_TRUE);
        selectIsland(islands_.empty() ? -1 : 0);
    }

    void selectIsland(int index)
    {
        selected_ = index;
        if (index < 0 || static_cast<std::size_t>(index) >= islands_.size()) {
            Rtext_input_set_text(inspectorName_, "");
            Rlabel_set_text(inspectorDetails_, "No island selected.");
            return;
        }
        const IslandInfo& island = islands_[static_cast<std::size_t>(index)];
        updatingInspector_ = true;
        Rtext_input_set_text(inspectorName_, island.name.c_str());
        updatingInspector_ = false;
        Rlabel_set_text(inspectorDetails_,
                        std::format("Anchor ({:.1f}, {:.1f}, {:.1f}) m\nRadius {:.1f} m\n"
                                    "Top {:.1f} m, underside {:.1f} m\n"
                                    "{} lakes, {} streams, {} waterfalls",
                                    island.anchor.x, island.anchor.y, island.anchor.z,
                                    island.radius, island.topHeight, island.undersideDepth,
                                    island.lakes, island.streams, island.waterfalls)
                            .c_str());
    }

    void regenerate()
    {
        ++regenerations_;
        std::uint64_t seed = settings_.seed;
        const char* typed = Rtext_input_get_text(inspectorSeed_);
        std::uint64_t parsed = 0;
        if (typed != nullptr && parseNumber(std::string_view(typed), parsed) &&
            parsed != settings_.seed) {
            seed = parsed;
        } else {
            seed = settings_.seed + 1;
        }
        settings_.seed = seed;
        if (generateWorld()) {
            fillHierarchy();
            Rtext_input_set_text(inspectorSeed_, std::to_string(settings_.seed).c_str());
            const auto counts = renderer_->resourceCounts();
            log(std::format("renderer holds {} meshes, {} materials (world needs {} + sky, {})",
                            counts.meshes, counts.materials, world_.meshCount(),
                            world_.materialCount()));
        }
    }

    void setViewportMode(ViewportMode mode)
    {
        viewportMode_ = mode;
        if (mode == ViewportMode::Callback) {
            Rviewport_set_texture(viewport_, R_INVALID_ID);
            Rviewport_set_draw_callback(viewport_, &Prototype::onViewportDraw, this);
            Rbutton_set_text(modeButton_, "Viewport: draw callback");
        } else {
            Rviewport_set_draw_callback(viewport_, nullptr, nullptr);
            if (viewportTexture_ != R_INVALID_ID) {
                Rviewport_set_texture(viewport_, viewportTexture_);
            }
            Rbutton_set_text(modeButton_, "Viewport: CPU read-back");
        }
        log(mode == ViewportMode::Callback ? "viewport: draw callback"
                                           : "viewport: texture with CPU read-back");
    }

    void log(std::string line)
    {
        engine::core::logInfo("app", "{}", line);
        consoleLines_.push_back(std::move(line));
        if (consoleLines_.size() > 2000) {
            consoleLines_.erase(consoleLines_.begin(), consoleLines_.begin() + 500);
        }
        refreshConsole();
    }

    void refreshConsole()
    {
        if (!ravenStarted_ || console_ == R_INVALID_ID) {
            return;
        }
        Rlist_view_set_virtual_items(console_, static_cast<int>(consoleLines_.size()),
                                     &Prototype::consoleLine, this);
        Rlist_view_scroll_to_item(console_, static_cast<int>(consoleLines_.size()) - 1);
    }

    // ---- the frame -----------------------------------------------------

    void runFrame()
    {
        const auto frameStart = Clock::now();
        const double delta =
            std::min(0.1, std::chrono::duration<double>(frameStart - lastFrame_).count());
        lastFrame_ = frameStart;

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (options_.inputCheck &&
                (event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP) &&
                event.key.key == SDLK_F9) {
                // The input driver's "done" key: for the check, not the UI.
                if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat) {
                    driverDone_ = true;
                }
                continue;
            }
            noteInput(event);
            if (imguiStarted_ && !routeThroughImGui(event)) {
                continue;
            }
            // Keys and buttons held when the window loses the focus are
            // released by the host (SdlRavenHost), which gives Raven their
            // key-ups; the camera then hears them through the viewport.
            (void)host_->handleEvent(event);
        }
        updateCamera(static_cast<float>(delta));
        if (focusLossFrames_ > 0 && --focusLossFrames_ == 0) {
            focusLossPosition_ = camera_.position;
        }

        const int width = host_->pixelWidth();
        const int height = host_->pixelHeight();
        const engine::math::Vec3 horizon =
            island_preview::skyColor(island_preview::floatingWorldSky(), 0.0F);
        renderer_->beginFrame(width, height, {horizon.r, horizon.g, horizon.b, 1.0F});
        if (viewportMode_ == ViewportMode::Texture) {
            renderViewportTexture(width, height);
            renderer_->beginFrame(width, height, {horizon.r, horizon.g, horizon.b, 1.0F});
        }
        updateStats();
        const std::uint64_t uiFramesBefore = raven_eval::backendStats().frames;
        const auto uiStart = Clock::now();
        bool drawn = true;
        if (options_.benchmark) {
            drawn = Rdraw_frame() != R_FALSE;
        } else if (!raven_eval::sameGlStateAround([&drawn] { drawn = Rdraw_frame() != R_FALSE; },
                                                  raven_eval::GlStateGroups::Restored)) {
            // Everything Raven did this frame, its texture uploads included.
            ++frameStateMismatches_;
        }
        if (!drawn) {
            running_ = false;
        }
        uiMilliseconds_ += millisecondsSince(uiStart);
        if (raven_eval::backendStats().frames == uiFramesBefore) {
            ++framesWithoutUi_;
        }
        if (imguiStarted_ && running_) {
            drawImGui();
        }
        if (screenshotPath_) {
            saveScreenshot(*screenshotPath_);
            screenshotPath_.reset();
        }
        renderer_->endFrame();
        SDL_GL_SwapWindow(window_);
        if (options_.benchmark) {
            // Count the GPU's work too: wait for the frame to finish.
            glFinish();
        }
        ++frames_;
        const double frameMilliseconds = millisecondsSince(frameStart);
        frameMilliseconds_ += frameMilliseconds;
        benchmarkFrameMilliseconds_ += frameMilliseconds;
    }

    // A frame of the world alone over the whole window, without Raven: the
    // baseline the UI's cost is compared with.
    void runWorldOnlyFrame()
    {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
        }
        const int width = host_->pixelWidth();
        const int height = host_->pixelHeight();
        renderer_->beginFrame(width, height, {0.0F, 0.0F, 0.0F, 1.0F});
        drawWorld(width, height);
        renderer_->endFrame();
        SDL_GL_SwapWindow(window_);
        glFinish();
    }

    void updateCamera(float delta)
    {
        engine::camera::FlyInput fly;
        const auto axis = [](bool positive, bool negative) {
            return (positive ? 1.0F : 0.0F) - (negative ? 1.0F : 0.0F);
        };
        fly.move.x = axis(keys_.d, keys_.a);
        fly.move.y = axis(keys_.e, keys_.q);
        fly.move.z = axis(keys_.w, keys_.s);
        fly.fast = keys_.shift;
        fly.look = look_;
        fly.speedSteps = wheel_;
        look_ = engine::math::Vec2{0.0F};
        wheel_ = 0.0F;
        controller_.update(camera_, fly, delta);
    }

    void renderViewportTexture(int framebufferWidth, int framebufferHeight)
    {
        int width = 0;
        int height = 0;
        Rviewport_get_pixel_size(viewport_, &width, &height);
        width = std::min(width, framebufferWidth);
        height = std::min(height, framebufferHeight);
        if (width <= 0 || height <= 0) {
            return;
        }
        const auto started = Clock::now();
        glViewport(0, 0, width, height);
        drawWorld(width, height);
        const auto rowBytes = static_cast<std::size_t>(width) * 4;
        readback_.resize(rowBytes * static_cast<std::size_t>(height));
        flipped_.resize(readback_.size());
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, readback_.data());
        for (int y = 0; y < height; ++y) {
            std::copy_n(readback_.data() + static_cast<std::size_t>(height - 1 - y) * rowBytes,
                        rowBytes, flipped_.data() + static_cast<std::size_t>(y) * rowBytes);
        }
        const double readMilliseconds = millisecondsSince(started);
        const auto uploadStart = Clock::now();
        if (viewportTexture_ == R_INVALID_ID) {
            viewportTexture_ = Rtexture_create_from_pixels(flipped_.data(), width, height);
            Rviewport_set_texture(viewport_, viewportTexture_);
        } else {
            Rtexture_update(viewportTexture_, flipped_.data(), width, height);
        }
        readbackMilliseconds_ += readMilliseconds;
        uploadMilliseconds_ += millisecondsSince(uploadStart);
        ++readbacks_;
    }

    void saveScreenshot(const std::filesystem::path& path)
    {
        auto image = renderer_->captureFrame();
        if (!image) {
            log("screenshot failed: " + image.error().message);
            return;
        }
        if (auto error = engine::core::writePng(path, image.value())) {
            log("screenshot failed: " + error->message);
            return;
        }
        ++screenshots_;
        log("screenshot saved to " + engine::core::toUtf8(path));
    }

    void updateStats()
    {
        const double now = std::chrono::duration<double>(Clock::now() - startTime_).count();
        if (now - lastStatsTime_ < 0.5 || window_ui_ == R_INVALID_ID) {
            return;
        }
        const std::uint64_t frames = frames_ - statsFrames_;
        const double frameMs = frames > 0 ? frameMilliseconds_ / static_cast<double>(frames) : 0.0;
        const double fps = frames > 0 ? static_cast<double>(frames) / (now - lastStatsTime_) : 0.0;
        RFrameStats ui{};
        Rwindow_get_frame_stats(window_ui_, &ui);
        const auto& backend = raven_eval::backendStats();
        Rlabel_set_text(statsLabel_,
                        std::format("{:.0f} fps   frame {:.2f} ms   UI: layout {:.2f} / paint "
                                    "{:.2f} / render {:.2f} ms   {} instances",
                                    fps, frameMs, ui.layout_ms, ui.paint_ms,
                                    backend.lastFrameMilliseconds, ui.instance_count)
                            .c_str());
        const auto host = host_->stats();
        Rlabel_set_text(
            frameLabel_,
            std::format("Frames {}   UI frames {}   frames without UI {}\n"
                        "Viewport callbacks {}   read-backs {}   backdrops {}\n"
                        "Raven widgets {}   draw calls {}   paint jobs {}\n"
                        "OpenGL errors {}   state mismatches {}   wrong-thread calls {}\n"
                        "Input: {} moves, {} buttons, {} wheels, {} keys, {} code points, "
                        "{} focus, {} leaves, {} resizes, {} cursor changes\n"
                        "Scene: {} triangles, camera speed {:.1f} m/s",
                        frames_, backend.frames, framesWithoutUi_, backend.viewportCallbacks,
                        readbacks_, backend.backdrops, ui.widget_count, ui.draw_call_count,
                        ui.paint_job_count, backend.glErrors, backend.stateMismatches,
                        backend.wrongThreadCalls, host.mouseMoves, host.mouseButtons, host.wheels,
                        host.keys, host.codepoints, host.focusChanges, host.mouseLeaves,
                        host.resizes, host.cursorChanges, triangles_, controller_.moveSpeed())
                .c_str());
        frameMilliseconds_ = 0.0;
        statsFrames_ = frames_;
        lastStatsTime_ = now;
    }

    // ---- Raven callbacks -----------------------------------------------

    static void onAction(Rid /*sender*/, const char* action, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        const std::string_view name = action != nullptr ? action : "";
        ++self->actions_;
        if (name == "world.regenerate") {
            self->regenerate();
        } else if (name == "viewport.mode") {
            self->setViewportMode(self->viewportMode_ == ViewportMode::Callback
                                      ? ViewportMode::Texture
                                      : ViewportMode::Callback);
        } else if (name == "app.screenshot") {
            self->screenshotPath_ = self->options_.screenshot.value_or(
                std::filesystem::path("screenshots") / "raven-evaluation.png");
        } else if (name == "island.focus") {
            if (self->selected_ >= 0) {
                self->camera_ =
                    self->islandCamera(self->islands_[static_cast<std::size_t>(self->selected_)]);
            }
        }
    }

    static int onClose(Rid /*window*/, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        ++self->closeCallbacks_;
        self->log("window close requested");
        return R_TRUE;
    }

    static void onViewportDraw(const RViewportDrawInfo* info, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        self->drawWorld(info->width, info->height);
    }

    static void onViewportResize(Rid /*viewport*/, int width, int height, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        ++self->viewportResizes_;
        self->viewportSize_ = {width, height};
    }

    struct Keys {
        bool w = false;
        bool a = false;
        bool s = false;
        bool d = false;
        bool q = false;
        bool e = false;
        bool shift = false;
    };

    static int onViewportEvent(Rid /*widget*/, const REvent* event, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        ++self->viewportEvents_;
        switch (event->type) {
        case R_EVENT_MOUSE_DOWN:
            if (event->button == R_MOUSE_RIGHT) {
                self->looking_ = true;
                return R_TRUE;
            }
            return R_FALSE; // a left click focuses the viewport
        case R_EVENT_MOUSE_UP:
            if (event->button == R_MOUSE_RIGHT) {
                self->looking_ = false;
                return R_TRUE;
            }
            return R_FALSE;
        case R_EVENT_MOUSE_MOVE:
            if (self->looking_) {
                // Logical units to pixels, the unit of the fly controller.
                self->look_ +=
                    engine::math::Vec2{event->delta_x, event->delta_y} * self->host_->scale();
                return R_TRUE;
            }
            return R_FALSE;
        case R_EVENT_MOUSE_WHEEL:
            self->wheel_ += event->wheel_y;
            return R_TRUE;
        case R_EVENT_KEY_DOWN:
        case R_EVENT_KEY_UP:
            return self->setKey(event->key, event->type == R_EVENT_KEY_DOWN) ? R_TRUE : R_FALSE;
        case R_EVENT_FOCUS_LOST:
            self->keys_ = Keys{};
            self->looking_ = false;
            ++self->viewportFocusLosses_;
            return R_FALSE;
        default:
            return R_FALSE;
        }
    }

    bool setKey(RKey key, bool down)
    {
        switch (key) {
        case R_KEY_W:
        case R_KEY_UP:
            keys_.w = down;
            return true;
        case R_KEY_A:
        case R_KEY_LEFT:
            keys_.a = down;
            return true;
        case R_KEY_S:
        case R_KEY_DOWN:
            keys_.s = down;
            return true;
        case R_KEY_D:
        case R_KEY_RIGHT:
            keys_.d = down;
            return true;
        case R_KEY_Q:
        case R_KEY_LEFT_CTRL:
            keys_.q = down;
            return true;
        case R_KEY_E:
        case R_KEY_SPACE:
            keys_.e = down;
            return true;
        case R_KEY_LEFT_SHIFT:
        case R_KEY_RIGHT_SHIFT:
            keys_.shift = down;
            return true;
        default:
            return false;
        }
    }

    static void onTreeSelection(Rid /*tree*/, Rid item, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        ++self->treeSelections_;
        const auto found = std::find(self->treeItems_.begin(), self->treeItems_.end(), item);
        self->selectIsland(found == self->treeItems_.end()
                               ? -1
                               : static_cast<int>(found - self->treeItems_.begin()));
    }

    static void onNameChanged(Rid /*input*/, const char* text, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        if (self->updatingInspector_ || self->selected_ < 0 || text == nullptr) {
            return;
        }
        const auto index = static_cast<std::size_t>(self->selected_);
        self->islands_[index].name = text;
        Rtree_item_set_text(self->treeItems_[index], text);
    }

    static void onDockChanged(Rid /*area*/, void* user)
    {
        ++static_cast<Prototype*>(user)->dockChanges_;
    }

    static const char* consoleLine(Rid /*list*/, int index, void* user)
    {
        auto* self = static_cast<Prototype*>(user);
        if (index < 0 || static_cast<std::size_t>(index) >= self->consoleLines_.size()) {
            return "";
        }
        return self->consoleLines_[static_cast<std::size_t>(index)].c_str();
    }

    // ---- Dear ImGui next to Raven (--with-imgui) ------------------------
    //
    // Raven is the editor's interface; Dear ImGui stays the engine's debug UI
    // (ADR 0018, ADR 0019). Both on one window, one context and one loop: each
    // SDL event goes to Dear ImGui first, and to Raven unless Dear ImGui wants
    // the mouse or the keyboard for it; Dear ImGui draws after Raven's frame.

    bool startImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();
        // The display's scale, as Raven gets it.
        ImGui::GetStyle().ScaleAllSizes(host_->scale());
        ImGui::GetStyle().FontScaleDpi = host_->scale();
        if (!ImGui_ImplSDL3_InitForOpenGL(window_, context_)) {
            engine::core::logError("app", "Dear ImGui SDL3 backend failed");
            ImGui::DestroyContext();
            return false;
        }
        if (!ImGui_ImplOpenGL3_Init("#version 410 core")) {
            engine::core::logError("app", "Dear ImGui OpenGL backend failed");
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext();
            return false;
        }
        imguiStarted_ = true;
        log("Dear ImGui " + std::string(ImGui::GetVersion()) + " next to Raven");
        return true;
    }

    void stopImGui()
    {
        if (!imguiStarted_) {
            return;
        }
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        imguiStarted_ = false;
    }

    // Gives the event to Dear ImGui; true if Raven should get it as well.
    bool routeThroughImGui(const SDL_Event& event)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
        const ImGuiIO& io = ImGui::GetIO();
        switch (event.type) {
        case SDL_EVENT_MOUSE_MOTION:
        case SDL_EVENT_MOUSE_WHEEL:
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
            if (io.WantCaptureMouse && host_->heldButtons() == 0) {
                ++imguiMouseEvents_;
                return false;
            }
            return true;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            // Raven still gets the release of a press it got.
            if (io.WantCaptureMouse && host_->heldButtons() == 0) {
                ++imguiMouseEvents_;
                return false;
            }
            return true;
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_TEXT_INPUT:
            if (io.WantCaptureKeyboard) {
                ++imguiKeyboardEvents_;
                return false;
            }
            return true;
        case SDL_EVENT_KEY_UP:
            if (io.WantCaptureKeyboard && host_->heldKeys() == 0) {
                ++imguiKeyboardEvents_;
                return false;
            }
            return true;
        default:
            return true;
        }
    }

    void drawImGui()
    {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        const ImGuiIO& io = ImGui::GetIO();
        // Inside the viewport's top-left corner, away from the widgets the
        // checks click.
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
        float h = 0.0F;
        Rwidget_get_rect(viewport_, &x, &y, &w, &h);
        const float toPoints = host_->scale() / std::max(SDL_GetWindowPixelDensity(window_), 0.01F);
        ImGui::SetNextWindowPos(ImVec2((x + 12.0F) * toPoints, (y + 12.0F) * toPoints),
                                ImGuiCond_Always);
        ImGui::SetNextWindowSize(ImVec2(250.0F * host_->scale(), 0.0F), ImGuiCond_Always);
        ImGui::Begin("Dear ImGui (debug UI)", nullptr,
                     ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoCollapse |
                         ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
        ImGui::TextUnformatted("Drawn after Raven's frame");
        if (ImGui::Button("ImGui button")) {
            ++imguiClicks_;
        }
        const float density = io.DisplayFramebufferScale.x;
        const auto pixelRect = [density] {
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            return std::array<float, 4>{min.x * density, min.y * density, (max.x - min.x) * density,
                                        (max.y - min.y) * density};
        };
        imguiButtonRect_ = pixelRect();
        ImGui::InputText("##text", imguiText_.data(), imguiText_.size());
        imguiInputRect_ = pixelRect();
        ImGui::Text("clicks %llu", static_cast<unsigned long long>(imguiClicks_));
        ImGui::End();
        ImGui::Render();
        if (!raven_eval::sameGlStateAround(
                [] { ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData()); },
                raven_eval::GlStateGroups::All)) {
            ++imguiStateMismatches_;
        }
    }

    // ---- input check: real OS input from input_driver.py ---------------
    //
    // The driver sends real keyboard and mouse input through the operating
    // system (SendInput on Windows, XTEST through xdotool on X11) and works
    // the system clipboard. The two talk through this program's standard
    // output: for each phase the program prepares the widgets and prints
    // "@@READY <phase> ...", the driver acts and then presses F9, and the
    // program checks the result and prints "@@CHECK <phase>.<what> PASS|FAIL
    // <detail>" and "@@DONE <phase>". F9 is the driver's own key and goes
    // neither to Raven nor to Dear ImGui.

    struct InputPhase {
        std::string name;
        std::function<std::string()> prepare;
        std::function<void()> check;
    };

    void emit(const std::string& line)
    {
        std::printf("%s\n", line.c_str());
        std::fflush(stdout);
    }

    void expect(const std::string& what, bool passed, const std::string& detail)
    {
        inputResults_.push_back(StepResult{what, passed, detail});
        emit(std::format("@@CHECK {} {} {}", what, passed ? "PASS" : "FAIL", detail));
    }

    // A widget's rectangle in the window's pixels: "name=x,y,w,h".
    [[nodiscard]] std::string rectOf(std::string_view name, Rid widget) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
        float h = 0.0F;
        Rwidget_get_rect(widget, &x, &y, &w, &h);
        const float s = host_->scale();
        return std::format("{}={:.1f},{:.1f},{:.1f},{:.1f}", name, x * s, y * s, w * s, h * s);
    }

    [[nodiscard]] std::array<float, 2> centreOf(Rid widget) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
        float h = 0.0F;
        Rwidget_get_rect(widget, &x, &y, &w, &h);
        const float s = host_->scale();
        return {(x + w * 0.5F) * s, (y + h * 0.5F) * s};
    }

    // Raven got the last press within a pixel of the widget's centre, where
    // the driver clicked.
    void expectPressAtCentre(const std::string& what, Rid widget)
    {
        const auto centre = centreOf(widget);
        const float dx = static_cast<float>(host_->lastPressX()) - centre[0];
        const float dy = static_cast<float>(host_->lastPressY()) - centre[1];
        expect(what, std::abs(dx) <= 1.5F && std::abs(dy) <= 1.5F,
               std::format("press at ({}, {}) px, centre ({:.1f}, {:.1f}) px", host_->lastPressX(),
                           host_->lastPressY(), centre[0], centre[1]));
    }

    [[nodiscard]] std::string inputText() const
    {
        const char* text = Rtext_input_get_text(inspectorName_);
        return text != nullptr ? text : "";
    }

    void focusName(const char* text)
    {
        updatingInspector_ = true;
        Rtext_input_set_text(inspectorName_, text);
        updatingInspector_ = false;
        Rwidget_focus(inspectorName_);
    }

    void expectText(const std::string& what, const std::string& expected)
    {
        const std::string text = inputText();
        expect(what, text == expected, std::format("text '{}', expected '{}'", text, expected));
    }

    [[nodiscard]] static std::string clipboardText()
    {
        char* text = SDL_GetClipboardText();
        std::string result = text != nullptr ? text : "";
        SDL_free(text);
        return result;
    }

    void addPhase(std::string name, std::function<std::string()> prepare,
                  std::function<void()> check)
    {
        phases_.push_back({std::move(name), std::move(prepare), std::move(check)});
    }

    void buildInputCheck()
    {
        addPhase(
            "focus", [] { return std::string(); },
            [this] {
                expect("focus.keyboard", SDL_GetKeyboardFocus() == window_,
                       "SDL keyboard focus on the window");
                expect("focus.text-input", SDL_TextInputActive(window_), "SDL text input active");
                if (options_.screenshot) {
                    screenshotPath_ = options_.screenshot;
                }
            });
        addPhase(
            "click-input",
            [this] {
                Rwindow_clear_focus(window_ui_);
                focusName("");
                Rwindow_clear_focus(window_ui_);
                before_.buttons = host_->stats().mouseButtons;
                return rectOf("target", inspectorName_);
            },
            [this] {
                expectPressAtCentre("click-input.position", inspectorName_);
                expect("click-input.focus", Rwidget_is_focused(inspectorName_) != 0,
                       "the TextInput has the keyboard after the click");
                const auto buttons = host_->stats().mouseButtons - before_.buttons;
                expect("click-input.events", buttons == 2,
                       std::format("{} button events (down and up expected)", buttons));
            });
        const auto clickRegenerate = [this](const std::string& phase) {
            addPhase(
                phase,
                [this] {
                    before_.regenerations = regenerations_;
                    before_.imguiClicks = imguiClicks_;
                    return rectOf("target", regenerateButton_);
                },
                [this, phase] {
                    expectPressAtCentre(phase + ".position", regenerateButton_);
                    expect(phase + ".clicked", regenerations_ == before_.regenerations + 1,
                           std::format("regenerations {} -> {}", before_.regenerations,
                                       regenerations_));
                    if (imguiStarted_) {
                        expect(phase + ".imgui-untouched", imguiClicks_ == before_.imguiClicks,
                               std::format("ImGui clicks {} -> {}", before_.imguiClicks,
                                           imguiClicks_));
                    }
                });
        };
        clickRegenerate("click-button");
        addPhase(
            "type-ascii",
            [this] {
                focusName("");
                before_.position = camera_.position;
                before_.codepoints = host_->stats().codepoints;
                return std::string();
            },
            [this] {
                const std::string expected = "wasd Hello, World 123.!?;:-_()";
                expectText("type-ascii.text", expected);
                const auto codepoints = host_->stats().codepoints - before_.codepoints;
                expect(
                    "type-ascii.no-duplicates", codepoints == expected.size(),
                    std::format("{} code points for {} characters", codepoints, expected.size()));
                const float moved = glm::length(camera_.position - before_.position);
                expect("type-ascii.camera-still", moved == 0.0F,
                       std::format("camera moved {:.3f} m while WASD went into the text", moved));
            });
        addPhase(
            "type-layout",
            [this] {
                focusName("");
                return std::string();
            },
            [this] { expectText("type-layout.text", "Привет, мир!"); });
        addPhase(
            "type-unicode",
            [this] {
                focusName("");
                return std::string();
            },
            [this] { expectText("type-unicode.text", "Тестовый текст: 123."); });
        addPhase(
            "editing",
            [this] {
                focusName("Тестовый текст: 123.");
                return std::string();
            },
            [this] {
                expectText("editing.text", "текстX: 123.Тестов");
                const std::string clip = clipboardText();
                expect("editing.cut-to-clipboard", clip == "Тестовый",
                       std::format("clipboard '{}'", clip));
            });
        addPhase(
            "select-all-copy",
            [this] {
                focusName("Привет, мир!");
                before_.clipboardWrites = host_->stats().clipboardWrites;
                return std::string();
            },
            [this] {
                const std::string clip = clipboardText();
                expect("select-all-copy.clipboard", clip == "Привет, мир!",
                       std::format("clipboard '{}'", clip));
                const auto writes = host_->stats().clipboardWrites - before_.clipboardWrites;
                expect("select-all-copy.once", writes == 1,
                       std::format("{} clipboard writes for one Ctrl+C", writes));
                expectText("select-all-copy.text-kept", "Привет, мир!");
            });
        addPhase(
            "paste-external",
            [this] {
                focusName("");
                before_.clipboardReads = host_->stats().clipboardReads;
                return std::string();
            },
            [this] {
                expectText("paste-external.text", "Вставка ✓ αβγ 🙂");
                const auto reads = host_->stats().clipboardReads - before_.clipboardReads;
                expect("paste-external.once", reads == 1,
                       std::format("{} clipboard reads for one Ctrl+V", reads));
            });
        addPhase(
            "paste-multiline-input",
            [this] {
                focusName("");
                return std::string();
            },
            [this] {
                // A single-line editor keeps the first line (Raven's textedit.c).
                expectText("paste-multiline-input.first-line", "Первая строка");
            });
        addPhase(
            "paste-multiline-editor",
            [this] {
                Rtext_editor_set_text(inspectorNotes_, "");
                Rwidget_focus(inspectorNotes_);
                return std::string();
            },
            [this] {
                const char* text = Rtext_editor_get_text(inspectorNotes_);
                const std::string got = text != nullptr ? text : "";
                const int lines = Rtext_editor_get_line_count(inspectorNotes_);
                std::string shown;
                for (const char c : got) {
                    if (c == '\r') {
                        shown += "\\r";
                    } else if (c == '\n') {
                        shown += "\\n";
                    } else {
                        shown += c;
                    }
                }
                expect("paste-multiline-editor.lines",
                       lines == 3 && got == "Первая строка\nВторая строка\nТретья",
                       std::format("{} lines, text '{}'", lines, shown));
            });
        addPhase(
            "paste-empty",
            [this] {
                focusName("abc");
                before_.ravenErrors = ravenErrors().errors;
                return std::string();
            },
            [this] {
                expectText("paste-empty.text-kept", "abc");
                expect("paste-empty.no-error", ravenErrors().errors == before_.ravenErrors,
                       std::format("{} Raven errors", ravenErrors().errors));
            });
        addPhase(
            "switch-and-paste",
            [this] {
                focusName("");
                before_.focusChanges = host_->stats().focusChanges;
                return std::string();
            },
            [this] {
                expectText("switch-and-paste.text", "После переключения");
                expect("switch-and-paste.input-focus", Rwidget_is_focused(inspectorName_) != 0,
                       "the TextInput still has Raven's keyboard after the window came back");
                const auto changes = host_->stats().focusChanges - before_.focusChanges;
                expect("switch-and-paste.focus-events", changes >= 2,
                       std::format("{} window focus changes (lost and gained)", changes));
            });
        addPhase(
            "focus-loss",
            [this] {
                Rwidget_focus(viewport_);
                before_.position = camera_.position;
                before_.focusLosses = windowFocusLosses_;
                before_.releasedKeys = host_->stats().releasedKeys;
                before_.releasedButtons = host_->stats().releasedButtons;
                sdlKeyUps_ = {};
                return rectOf("target", viewport_);
            },
            [this] {
                const float movedBefore = glm::length(focusLossPosition_ - before_.position);
                expect("focus-loss.keys-worked", movedBefore > 0.1F,
                       std::format("the held keys flew the camera {:.2f} m before the focus "
                                   "loss",
                                   movedBefore));
                expect("focus-loss.lost", windowFocusLosses_ > before_.focusLosses,
                       std::format("{} window focus losses",
                                   windowFocusLosses_ - before_.focusLosses));
                const float movedAfter = glm::length(camera_.position - focusLossPosition_);
                expect("focus-loss.camera-still", movedAfter == 0.0F,
                       std::format("camera moved {:.3f} m after the focus loss", movedAfter));
                const bool appKeys =
                    keys_.w || keys_.a || keys_.s || keys_.d || keys_.q || keys_.e || keys_.shift;
                expect("focus-loss.app-keys", !appKeys && !looking_,
                       std::format("viewport keys {}, looking {}", appKeys ? "held" : "free",
                                   looking_));
                expect("focus-loss.raven-held", host_->heldKeys() == 0 && host_->heldButtons() == 0,
                       std::format("{} keys and {} buttons held for Raven", host_->heldKeys(),
                                   host_->heldButtons()));
                const bool* state = SDL_GetKeyboardState(nullptr);
                const bool sdlHeld = state[SDL_SCANCODE_W] || state[SDL_SCANCODE_LSHIFT] ||
                                     state[SDL_SCANCODE_LCTRL] || state[SDL_SCANCODE_LALT];
                const SDL_MouseButtonFlags buttons = SDL_GetMouseState(nullptr, nullptr);
                expect("focus-loss.sdl-state", !sdlHeld && buttons == 0,
                       std::format("SDL: W/Shift/Ctrl/Alt {}, mouse buttons 0x{:X}",
                                   sdlHeld ? "held" : "up", static_cast<unsigned>(buttons)));
                expect("focus-loss.viewport-focus",
                       Rwindow_get_focused_widget(window_ui_) == viewport_,
                       "the viewport has Raven's keyboard again after the window came back");
                const auto host = host_->stats();
                emit(std::format(
                    "@@INFO focus-loss: SDL sent key-ups W {} Shift {} Ctrl {} Alt {} and "
                    "button-ups {}; the host released {} keys and {} buttons itself",
                    sdlKeyUps_[0], sdlKeyUps_[1], sdlKeyUps_[2], sdlKeyUps_[3], sdlButtonUps_,
                    host.releasedKeys - before_.releasedKeys,
                    host.releasedButtons - before_.releasedButtons));
            });
        addPhase(
            "resize",
            [this] {
                before_.size = {host_->pixelWidth(), host_->pixelHeight()};
                before_.resizes = viewportResizes_;
                return std::format("pixels={}x{}", host_->pixelWidth(), host_->pixelHeight());
            },
            [this] {
                int w = 0;
                int h = 0;
                Rviewport_get_pixel_size(viewport_, &w, &h);
                expect("resize.window",
                       host_->pixelWidth() > before_.size[0] &&
                           host_->pixelHeight() > before_.size[1],
                       std::format("window {}x{} -> {}x{} px", before_.size[0], before_.size[1],
                                   host_->pixelWidth(), host_->pixelHeight()));
                expect("resize.viewport",
                       viewportResizes_ > before_.resizes && w == viewportSize_[0] &&
                           h == viewportSize_[1],
                       std::format("viewport {}x{} px, {} resize callbacks", w, h,
                                   viewportResizes_ - before_.resizes));
            });
        clickRegenerate("click-after-resize");
        if (imguiStarted_) {
            addPhase(
                "imgui-click",
                [this] {
                    before_.imguiClicks = imguiClicks_;
                    before_.buttons = host_->stats().mouseButtons;
                    const auto& r = imguiButtonRect_;
                    return std::format("target={:.1f},{:.1f},{:.1f},{:.1f}", r[0], r[1], r[2],
                                       r[3]);
                },
                [this] {
                    expect("imgui-click.clicked", imguiClicks_ == before_.imguiClicks + 1,
                           std::format("ImGui clicks {} -> {}", before_.imguiClicks, imguiClicks_));
                    const auto buttons = host_->stats().mouseButtons - before_.buttons;
                    expect("imgui-click.raven-untouched", buttons == 0,
                           std::format("{} button events reached Raven", buttons));
                });
            addPhase(
                "imgui-type",
                [this] {
                    focusName("keep");
                    imguiText_.fill('\0');
                    before_.codepoints = host_->stats().codepoints;
                    const auto& r = imguiInputRect_;
                    return std::format("target={:.1f},{:.1f},{:.1f},{:.1f}", r[0], r[1], r[2],
                                       r[3]);
                },
                [this] {
                    const std::string typed = imguiText_.data();
                    expect("imgui-type.text", typed == "imgui",
                           std::format("ImGui text '{}'", typed));
                    expectText("imgui-type.raven-untouched", "keep");
                    const auto codepoints = host_->stats().codepoints - before_.codepoints;
                    expect("imgui-type.raven-codepoints", codepoints == 0,
                           std::format("{} code points reached Raven", codepoints));
                });
            clickRegenerate("raven-click-with-imgui");
        }
    }

    void startInputCheck()
    {
        SDL_PropertiesID properties = SDL_GetWindowProperties(window_);
        const void* hwnd =
            SDL_GetPointerProperty(properties, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
        const Sint64 x11 = SDL_GetNumberProperty(properties, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        int pointsW = 0;
        int pointsH = 0;
        SDL_GetWindowSize(window_, &pointsW, &pointsH);
        emit(std::format("@@WINDOW hwnd={} x11={} video={}",
                         static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(hwnd)),
                         static_cast<long long>(x11), SDL_GetCurrentVideoDriver()));
        emit(std::format("@@SCALE display={:.3f} content={:.3f} density={:.3f} points={}x{} "
                         "pixels={}x{} raven={:.3f} imgui={}",
                         SDL_GetWindowDisplayScale(window_),
                         SDL_GetDisplayContentScale(SDL_GetDisplayForWindow(window_)),
                         SDL_GetWindowPixelDensity(window_), pointsW, pointsH, host_->pixelWidth(),
                         host_->pixelHeight(), host_->scale(), imguiStarted_ ? "on" : "off"));
        inputStarted_ = true;
    }

    void advanceInputCheck()
    {
        if (!running_) {
            return;
        }
        if (!inputStarted_) {
            if (frames_ >= 30) {
                startInputCheck();
            }
            return;
        }
        if (phase_ >= phases_.size()) {
            if (!closeAnnounced_) {
                emit("@@READY close");
                closeAnnounced_ = true;
            }
            return;
        }
        InputPhase& phase = phases_[phase_];
        if (!phasePrepared_) {
            const std::string payload = phase.prepare();
            emit("@@READY " + phase.name + (payload.empty() ? "" : " " + payload));
            phasePrepared_ = true;
            driverDone_ = false;
            settle_ = 0;
            phaseStart_ = Clock::now();
            return;
        }
        if (!driverDone_) {
            if (millisecondsSince(phaseStart_) > 60000.0) {
                expect(phase.name + ".driver", false, "no F9 from the driver within 60 s");
                running_ = false;
            }
            return;
        }
        // Events before F9 reached Raven in the same poll; let it handle them.
        if (++settle_ < 3) {
            return;
        }
        phase.check();
        emit("@@DONE " + phase.name);
        ++phase_;
        phasePrepared_ = false;
    }

    // What the checks need to know about input before it reaches the UI.
    void noteInput(const SDL_Event& event)
    {
        switch (event.type) {
        case SDL_EVENT_WINDOW_FOCUS_LOST:
            ++windowFocusLosses_;
            // Where the camera is once the releases have been handled.
            focusLossFrames_ = 2;
            break;
        case SDL_EVENT_KEY_UP:
            switch (event.key.scancode) {
            case SDL_SCANCODE_W:
                ++sdlKeyUps_[0];
                break;
            case SDL_SCANCODE_LSHIFT:
                ++sdlKeyUps_[1];
                break;
            case SDL_SCANCODE_LCTRL:
                ++sdlKeyUps_[2];
                break;
            case SDL_SCANCODE_LALT:
                ++sdlKeyUps_[3];
                break;
            default:
                break;
            }
            break;
        case SDL_EVENT_MOUSE_BUTTON_UP:
            ++sdlButtonUps_;
            break;
        default:
            break;
        }
    }

    // ---- self-test: input through SDL's own queue ----------------------

    struct Step {
        std::string name;
        int waitFrames = 5;
        std::function<void()> act;
        std::function<bool(std::string&)> check;
    };

    [[nodiscard]] SDL_FPoint widgetPoint(Rid widget, float fx = 0.5F, float fy = 0.5F) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
        float h = 0.0F;
        Rwidget_get_rect(widget, &x, &y, &w, &h);
        // Logical units -> pixels -> SDL window points.
        const float density = std::max(SDL_GetWindowPixelDensity(window_), 0.01F);
        const float scale = host_->scale();
        return SDL_FPoint{(x + w * fx) * scale / density, (y + h * fy) * scale / density};
    }

    // A point dx, dy logical units from the widget's top-left corner.
    [[nodiscard]] SDL_FPoint widgetOffset(Rid widget, float dx, float dy) const
    {
        float x = 0.0F;
        float y = 0.0F;
        float w = 0.0F;
        float h = 0.0F;
        Rwidget_get_rect(widget, &x, &y, &w, &h);
        const float density = std::max(SDL_GetWindowPixelDensity(window_), 0.01F);
        const float scale = host_->scale();
        return SDL_FPoint{(x + dx) * scale / density, (y + dy) * scale / density};
    }

    void pushDrag(SDL_FPoint from, SDL_FPoint to, int steps)
    {
        pushMove(from);
        pushButton(from, SDL_BUTTON_LEFT, true);
        for (int i = 1; i <= steps; ++i) {
            const float t = static_cast<float>(i) / static_cast<float>(steps);
            pushMove(SDL_FPoint{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t});
        }
        pushButton(to, SDL_BUTTON_LEFT, false);
    }

    void push(SDL_Event event)
    {
        event.common.timestamp = SDL_GetTicksNS();
        SDL_PushEvent(&event);
    }

    void pushMove(SDL_FPoint p)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_MOTION;
        e.motion.windowID = SDL_GetWindowID(window_);
        e.motion.x = p.x;
        e.motion.y = p.y;
        push(e);
    }

    void pushButton(SDL_FPoint p, Uint8 button, bool down)
    {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        e.button.windowID = SDL_GetWindowID(window_);
        e.button.button = button;
        e.button.down = down;
        e.button.clicks = 1;
        e.button.x = p.x;
        e.button.y = p.y;
        push(e);
    }

    void pushClick(SDL_FPoint p)
    {
        pushMove(p);
        pushButton(p, SDL_BUTTON_LEFT, true);
        pushButton(p, SDL_BUTTON_LEFT, false);
    }

    void pushWheel(SDL_FPoint p, float amount)
    {
        pushMove(p);
        SDL_Event e{};
        e.type = SDL_EVENT_MOUSE_WHEEL;
        e.wheel.windowID = SDL_GetWindowID(window_);
        e.wheel.y = amount;
        e.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
        e.wheel.mouse_x = p.x;
        e.wheel.mouse_y = p.y;
        push(e);
    }

    void pushKey(SDL_Keycode key, SDL_Scancode scancode, SDL_Keymod mod, bool down)
    {
        SDL_Event e{};
        e.type = down ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        e.key.windowID = SDL_GetWindowID(window_);
        e.key.key = key;
        e.key.scancode = scancode;
        e.key.mod = mod;
        e.key.down = down;
        push(e);
    }

    // The text must live as long as the program (SDL keeps the pointer).
    void pushText(const char* text)
    {
        SDL_Event e{};
        e.type = SDL_EVENT_TEXT_INPUT;
        e.text.windowID = SDL_GetWindowID(window_);
        e.text.text = text;
        push(e);
    }

    void pushWindowEvent(Uint32 type)
    {
        SDL_Event e{};
        e.type = type;
        e.window.windowID = SDL_GetWindowID(window_);
        push(e);
    }

    void buildSelfTest()
    {
        steps_.push_back({"layout and first frames", 20, [] {},
                          [this](std::string& detail) {
                              const auto& b = raven_eval::backendStats();
                              int w = 0;
                              int h = 0;
                              Rviewport_get_pixel_size(viewport_, &w, &h);
                              detail = std::format("{} UI frames, {} viewport callbacks, {} tree "
                                                   "items, viewport {}x{} px ({} resize callbacks "
                                                   "for the first size)",
                                                   b.frames, b.viewportCallbacks, treeItems_.size(),
                                                   w, h, viewportResizes_);
                              return b.frames > 0 && b.viewportCallbacks > 0 &&
                                     treeItems_.size() == islands_.size() && w > 0 && h > 0;
                          }});
        steps_.push_back({"click Regenerate (mouse through SDL)", 8,
                          [this] {
                              before_.seed = settings_.seed;
                              before_.regenerations = regenerations_;
                              pushClick(widgetPoint(regenerateButton_));
                          },
                          [this](std::string& detail) {
                              const auto counts = renderer_->resourceCounts();
                              detail = std::format("seed {} -> {}, renderer {} meshes for {} + sky",
                                                   before_.seed, settings_.seed, counts.meshes,
                                                   world_.meshCount());
                              return regenerations_ == before_.regenerations + 1 &&
                                     settings_.seed == before_.seed + 1 &&
                                     counts.meshes == world_.meshCount() + 1;
                          }});
        steps_.push_back({"programmatic tree selection (observation)", 3,
                          [this] {
                              before_.selections = treeSelections_;
                              Rtree_view_set_selected_item(hierarchy_, treeItems_.at(5));
                          },
                          [this](std::string& detail) {
                              detail = std::format("Rtree_view_set_selected_item called the "
                                                   "selection callback {} times",
                                                   treeSelections_ - before_.selections);
                              return true;
                          }});
        steps_.push_back({"click and arrow keys in the hierarchy fill the inspector", 6,
                          [this] {
                              // The first row (the archipelago), then down to Island 2.
                              float x = 0.0F;
                              float y = 0.0F;
                              float w = 0.0F;
                              float h = 0.0F;
                              Rwidget_get_rect(hierarchy_, &x, &y, &w, &h);
                              pushClick(widgetPoint(hierarchy_, 0.3F, 12.0F / std::max(h, 1.0F)));
                              for (int i = 0; i < 3; ++i) {
                                  pushKey(SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_NONE, true);
                                  pushKey(SDLK_DOWN, SDL_SCANCODE_DOWN, SDL_KMOD_NONE, false);
                              }
                          },
                          [this](std::string& detail) {
                              const char* text = Rtext_input_get_text(inspectorName_);
                              detail = std::format("selected island {}, inspector name '{}'",
                                                   selected_, text != nullptr ? text : "");
                              return selected_ == 2 && text != nullptr && islands_[2].name == text;
                          }});
        steps_.push_back({"typing UTF-8 into a TextInput; the camera does not move", 8,
                          [this] {
                              before_.position = camera_.position;
                              Rtext_input_set_text(inspectorName_, "");
                              pushClick(widgetPoint(inspectorName_));
                              pushText("Остров α");
                              pushKey(SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE, true);
                              pushText("w");
                              pushKey(SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE, false);
                          },
                          [this](std::string& detail) {
                              const char* text = Rtext_input_get_text(inspectorName_);
                              const std::string typed = text != nullptr ? text : "";
                              const char* item = Rtree_item_get_text(treeItems_.at(2));
                              detail =
                                  std::format("text '{}', tree item '{}', camera moved {:.3f} m",
                                              typed, item != nullptr ? item : "",
                                              glm::length(camera_.position - before_.position));
                              return typed == "Остров αw" && item != nullptr && typed == item &&
                                     camera_.position == before_.position;
                          }});
        steps_.push_back(
            {"Ctrl+R shortcut with a Cyrillic layout (keycode 'к', scancode R)", 8,
             [this] {
                 before_.regenerations = regenerations_;
                 pushClick(widgetPoint(viewport_));
                 pushKey(SDLK_LCTRL, SDL_SCANCODE_LCTRL, SDL_KMOD_LCTRL, true);
                 pushKey(static_cast<SDL_Keycode>(0x043A), SDL_SCANCODE_R, SDL_KMOD_LCTRL, true);
                 pushKey(static_cast<SDL_Keycode>(0x043A), SDL_SCANCODE_R, SDL_KMOD_LCTRL, false);
                 pushKey(SDLK_LCTRL, SDL_SCANCODE_LCTRL, SDL_KMOD_NONE, false);
             },
             [this](std::string& detail) {
                 detail =
                     std::format("regenerations {} -> {}", before_.regenerations, regenerations_);
                 return regenerations_ == before_.regenerations + 1;
             }});
        steps_.push_back({"right-drag in the viewport turns the camera", 6,
                          [this] {
                              before_.yaw = camera_.yawRadians;
                              const SDL_FPoint p = widgetPoint(viewport_);
                              pushMove(p);
                              pushButton(p, SDL_BUTTON_RIGHT, true);
                              pushMove(SDL_FPoint{p.x + 40.0F, p.y});
                              pushMove(SDL_FPoint{p.x + 80.0F, p.y});
                              pushButton(SDL_FPoint{p.x + 80.0F, p.y}, SDL_BUTTON_RIGHT, false);
                          },
                          [this](std::string& detail) {
                              detail = std::format("yaw {:.3f} -> {:.3f} rad", before_.yaw,
                                                   camera_.yawRadians);
                              return camera_.yawRadians != before_.yaw;
                          }});
        steps_.push_back({"wheel over the viewport changes the speed, over the hierarchy not", 6,
                          [this] {
                              before_.speed = controller_.moveSpeed();
                              pushWheel(widgetPoint(viewport_), 2.0F);
                          },
                          [this](std::string& detail) {
                              detail = std::format("speed {:.2f} -> {:.2f}", before_.speed,
                                                   controller_.moveSpeed());
                              return controller_.moveSpeed() > before_.speed;
                          }});
        steps_.push_back({"wheel over the hierarchy leaves the camera alone", 6,
                          [this] {
                              before_.speed = controller_.moveSpeed();
                              pushWheel(widgetPoint(hierarchy_), -2.0F);
                          },
                          [this](std::string& detail) {
                              detail = std::format("speed {:.2f} -> {:.2f}", before_.speed,
                                                   controller_.moveSpeed());
                              return controller_.moveSpeed() == before_.speed;
                          }});
        steps_.push_back({"W held in the focused viewport flies forward", 10,
                          [this] {
                              before_.position = camera_.position;
                              pushClick(widgetPoint(viewport_));
                              pushKey(SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE, true);
                          },
                          [this](std::string& detail) {
                              const float moved = glm::length(camera_.position - before_.position);
                              detail = std::format("moved {:.2f} m", moved);
                              return moved > 0.5F;
                          }});
        steps_.push_back({"losing window focus: the host releases the held W", 3,
                          [this] {
                              before_.focusLosses = viewportFocusLosses_;
                              before_.releasedKeys = host_->stats().releasedKeys;
                              // Only the focus loss, without the key-up SDL
                              // sends on a real one: the host must release W.
                              pushWindowEvent(SDL_EVENT_WINDOW_FOCUS_LOST);
                          },
                          [this](std::string& detail) {
                              before_.position = camera_.position;
                              const auto released =
                                  host_->stats().releasedKeys - before_.releasedKeys;
                              detail = std::format("the host released {} held keys, the "
                                                   "viewport's W is {}, R_EVENT_FOCUS_LOST "
                                                   "reached it {} times, {} keys still held",
                                                   released, keys_.w ? "down" : "up",
                                                   viewportFocusLosses_ - before_.focusLosses,
                                                   host_->heldKeys());
                              return released == 1 && !keys_.w && host_->heldKeys() == 0;
                          }});
        steps_.push_back({"... and the camera then stays still", 6, [] {},
                          [this](std::string& detail) {
                              const float moved = glm::length(camera_.position - before_.position);
                              detail = std::format("moved {:.3f} m after the focus loss", moved);
                              return moved == 0.0F;
                          }});
        steps_.push_back({"focus regained, key released, mouse leaves the window", 4,
                          [this] {
                              pushWindowEvent(SDL_EVENT_WINDOW_FOCUS_GAINED);
                              pushKey(SDLK_W, SDL_SCANCODE_W, SDL_KMOD_NONE, false);
                              pushWindowEvent(SDL_EVENT_WINDOW_MOUSE_LEAVE);
                          },
                          [this](std::string& detail) {
                              const auto host = host_->stats();
                              detail = std::format("{} focus changes, {} leaves", host.focusChanges,
                                                   host.mouseLeaves);
                              return host.focusChanges >= 2 && host.mouseLeaves >= 1;
                          }});
        steps_.push_back(
            {"window resize reaches the layout and the viewport", 12,
             [this] {
                 before_.resizes = viewportResizes_;
                 Rviewport_get_pixel_size(viewport_, &before_.size[0], &before_.size[1]);
                 SDL_SetWindowSize(window_, options_.width - 200, options_.height - 80);
             },
             [this](std::string& detail) {
                 int w = 0;
                 int h = 0;
                 Rviewport_get_pixel_size(viewport_, &w, &h);
                 detail = std::format("window {}x{} px, viewport {}x{} -> {}x{} "
                                      "({} resize callbacks)",
                                      host_->pixelWidth(), host_->pixelHeight(), before_.size[0],
                                      before_.size[1], w, h, viewportResizes_ - before_.resizes);
                 return viewportResizes_ > before_.resizes && w < before_.size[0] &&
                        w == viewportSize_[0] && h == viewportSize_[1];
             }});
        steps_.push_back({"TabView: a click on the Frame tab", 5,
                          [this] { pushClick(widgetOffset(consoleTabs_, 77.0F, 14.0F)); },
                          [this](std::string& detail) {
                              detail =
                                  std::format("active tab {}", Rtab_view_get_active(consoleTabs_));
                              return Rtab_view_get_active(consoleTabs_) == 1;
                          }});
        steps_.push_back({"TabView: back to the Log tab", 5,
                          [this] { pushClick(widgetOffset(consoleTabs_, 23.0F, 14.0F)); },
                          [this](std::string& detail) {
                              detail =
                                  std::format("active tab {}", Rtab_view_get_active(consoleTabs_));
                              return Rtab_view_get_active(consoleTabs_) == 0;
                          }});
        steps_.push_back(
            {"DockArea: drag the Inspector tab to the left edge", 40,
             [this] {
                 const char* layout = Rdock_area_save_layout(dock_);
                 dockLayoutBefore_ = layout != nullptr ? layout : "";
                 before_.dockChanges = dockChanges_;
                 float x = 0.0F;
                 float y = 0.0F;
                 float w = 0.0F;
                 float h = 0.0F;
                 Rwidget_get_rect(dock_, &x, &y, &w, &h);
                 pushDrag(widgetOffset(inspectorPanel_, 30.0F, -14.0F),
                          widgetOffset(dock_, 12.0F, h * 0.5F), 24);
             },
             [this](std::string& detail) {
                 const char* layout = Rdock_area_save_layout(dock_);
                 const std::string after = layout != nullptr ? layout : "";
                 detail = std::format("{} dock change callbacks, layout {} ({} -> {} "
                                      "bytes)",
                                      dockChanges_ - before_.dockChanges,
                                      after == dockLayoutBefore_ ? "unchanged" : "changed",
                                      dockLayoutBefore_.size(), after.size());
                 return after != dockLayoutBefore_ && dockChanges_ > before_.dockChanges;
             }});
        steps_.push_back(
            {"DockArea: floating a panel as a native window (observation)", 5,
             [this] { floatWindow_ = Rdock_area_float_panel(dock_, inspectorPanel_, 200, 200); },
             [this](std::string& detail) {
                 detail = std::format(
                     "Rdock_area_float_panel returned window {}, "
                     "valid {}, backend surfaces {}, frames of "
                     "other windows {}, Raven errors {}",
                     floatWindow_ == R_INVALID_ID ? -1 : static_cast<long long>(floatWindow_),
                     floatWindow_ != R_INVALID_ID && Rwindow_is_valid(floatWindow_) != 0,
                     raven_eval::backendStats().surfaces,
                     raven_eval::backendStats().otherWindowFrames, ravenErrors().errors);
                 Rdock_area_dock_panel(dock_, inspectorPanel_);
                 return true;
             }});
        steps_.push_back(
            {"DockArea: floating a panel inside the window", 10,
             [this] {
                 Rdock_area_set_float_mode(dock_, R_DOCK_FLOAT_IN_WINDOW);
                 floatWindow_ = Rdock_area_float_panel(dock_, inspectorPanel_, 200, 120);
             },
             [this](std::string& detail) {
                 const bool shown = Rdock_area_is_panel_shown(dock_, inspectorPanel_) != 0;
                 detail = std::format(
                     "containing window {} (the main window is {}), "
                     "panel shown {}",
                     floatWindow_ == R_INVALID_ID ? -1 : static_cast<long long>(floatWindow_),
                     static_cast<long long>(window_ui_), shown);
                 Rdock_area_dock_panel(dock_, inspectorPanel_);
                 return floatWindow_ == window_ui_ && shown;
             }});
        steps_.push_back(
            {"viewport as a CPU read-back texture", 20,
             [this] { pushClick(widgetPoint(modeButton_)); },
             [this](std::string& detail) {
                 detail = std::format(
                     "{} read-backs, {:.2f} ms read + {:.2f} ms "
                     "upload on average",
                     readbacks_,
                     readbacks_ ? readbackMilliseconds_ / static_cast<double>(readbacks_) : 0.0,
                     readbacks_ ? uploadMilliseconds_ / static_cast<double>(readbacks_) : 0.0);
                 return viewportMode_ == ViewportMode::Texture && readbacks_ > 10;
             }});
        steps_.push_back({"back to the draw callback", 6,
                          [this] {
                              pushClick(widgetPoint(modeButton_));
                              before_.callbacks = raven_eval::backendStats().viewportCallbacks;
                          },
                          [this](std::string& detail) {
                              const auto now = raven_eval::backendStats().viewportCallbacks;
                              detail = std::format("{} callbacks since", now - before_.callbacks);
                              return viewportMode_ == ViewportMode::Callback &&
                                     now > before_.callbacks;
                          }});
        steps_.push_back({"F12 shortcut saves a screenshot", 4,
                          [this] {
                              before_.screenshots = screenshots_;
                              if (!options_.screenshot) {
                                  options_.screenshot =
                                      std::filesystem::path("screenshots") / "raven-self-test.png";
                              }
                              pushKey(SDLK_F12, SDL_SCANCODE_F12, SDL_KMOD_NONE, true);
                              pushKey(SDLK_F12, SDL_SCANCODE_F12, SDL_KMOD_NONE, false);
                          },
                          [this](std::string& detail) {
                              detail = engine::core::toUtf8(*options_.screenshot);
                              return screenshots_ == before_.screenshots + 1 &&
                                     std::filesystem::exists(*options_.screenshot);
                          }});
        steps_.push_back(
            {"close request (and SDL's quit after it) goes through Raven's close callback once",
             1000,
             [this] {
                 // As SDL does for the last window: the request,
                 // then SDL_EVENT_QUIT.
                 pushWindowEvent(SDL_EVENT_WINDOW_CLOSE_REQUESTED);
                 SDL_Event quit{};
                 quit.type = SDL_EVENT_QUIT;
                 push(quit);
             },
             [this](std::string& detail) {
                 detail = std::format("{} close callbacks, loop running {}, {} Raven errors",
                                      closeCallbacks_, running_, ravenErrors().errors);
                 return closeCallbacks_ == 1 && !running_ && ravenErrors().errors == 0;
             }});
    }

    void advanceSelfTest()
    {
        if (step_ >= steps_.size()) {
            return;
        }
        Step& step = steps_[step_];
        if (!stepStarted_) {
            step.act();
            stepStarted_ = true;
            waited_ = 0;
            return;
        }
        // The close step ends the loop; check it as soon as that happens.
        if (++waited_ < step.waitFrames && running_) {
            return;
        }
        std::string detail;
        bool passed = false;
        try {
            passed = step.check(detail);
        } catch (const std::exception& error) {
            detail = error.what();
        }
        results_.push_back({step.name, passed, detail});
        std::printf("[self-test] %s: %s (%s)\n", passed ? "PASS" : "FAIL", step.name.c_str(),
                    detail.c_str());
        ++step_;
        stepStarted_ = false;
        if (step_ == steps_.size()) {
            running_ = false;
        }
    }

    // ---- stress and benchmark ------------------------------------------

    int runStress()
    {
        bool ok = true;
        for (std::uint32_t cycle = 1; cycle <= options_.stressCycles && ok; ++cycle) {
            if (!startRaven()) {
                return 1;
            }
            for (int frame = 0; frame < 60 && running_; ++frame) {
                if (frame == 10 || frame == 40) {
                    regenerate();
                }
                if (frame == 20) {
                    SDL_SetWindowSize(window_, options_.width - 100 * static_cast<int>(cycle % 3),
                                      options_.height - 40 * static_cast<int>(cycle % 2));
                }
                if (frame == 30) {
                    setViewportMode(cycle % 2 == 0 ? ViewportMode::Texture
                                                   : ViewportMode::Callback);
                }
                runFrame();
            }
            const auto counts = renderer_->resourceCounts();
            const RMemoryStats memory = stopRaven();
            const auto& backend = raven_eval::backendStats();
            const bool levelCounts = counts.meshes == world_.meshCount() + 1 &&
                                     counts.materials == world_.materialCount();
            const bool cycleOk = memory.allocation_count == 0 && backend.liveTextures == 0 &&
                                 levelCounts && backend.glErrors == 0 &&
                                 backend.stateMismatches == 0 && renderer_->errorCount() == 0 &&
                                 ravenErrors().errors == 0;
            std::printf("[stress] cycle %u: %s (Raven blocks after Rshutdown %zu, peak %zu KiB, "
                        "backend textures %u, renderer %zu meshes for %zu + sky, OpenGL errors "
                        "%u, state mismatches %u, Raven errors %u)\n",
                        cycle, cycleOk ? "PASS" : "FAIL", memory.allocation_count,
                        memory.peak_bytes / 1024, backend.liveTextures, counts.meshes,
                        world_.meshCount(), backend.glErrors, backend.stateMismatches,
                        ravenErrors().errors);
            ok = ok && cycleOk;
        }
        const auto& backend = raven_eval::backendStats();
        std::printf("[stress] %s: %u Init / %u Shutdown of the backend, %llu UI frames\n",
                    ok ? "PASS" : "FAIL", backend.initCount, backend.shutdownCount,
                    static_cast<unsigned long long>(backend.frames));
        return ok ? 0 : 1;
    }

    int runBenchmark()
    {
        const auto measure = [this](ViewportMode mode, RLook look, const char* name, int frames) {
            setViewportMode(mode);
            Rlook_set(look);
            for (int i = 0; i < 30; ++i) {
                runFrame(); // warm-up
            }
            benchmarkFrameMilliseconds_ = 0.0;
            uiMilliseconds_ = 0.0;
            readbackMilliseconds_ = 0.0;
            uploadMilliseconds_ = 0.0;
            readbacks_ = 0;
            const std::uint64_t start = frames_;
            const std::uint64_t backdrops = raven_eval::backendStats().backdrops;
            double backendMs = 0.0;
            for (int i = 0; i < frames; ++i) {
                runFrame();
                backendMs += raven_eval::backendStats().lastFrameMilliseconds;
            }
            const auto backdropCopies = raven_eval::backendStats().backdrops - backdrops;
            const auto count = static_cast<double>(frames_ - start);
            RFrameStats ui{};
            Rwindow_get_frame_stats(window_ui_, &ui);
            int w = 0;
            int h = 0;
            Rviewport_get_pixel_size(viewport_, &w, &h);
            std::printf("[benchmark] %s: frame %.3f ms with glFinish (%.0f fps), Rdraw_frame "
                        "%.3f ms (CPU, includes the viewport callback), backend RenderFrame "
                        "%.3f ms, read-back %.3f ms + upload %.3f ms per frame, %.1f backdrop "
                        "copies per frame; window %dx%d px, viewport %dx%d px, %u instances, %u "
                        "draw calls, %u widgets, layout %.3f ms, paint %.3f ms (last frame)\n",
                        name, benchmarkFrameMilliseconds_ / count,
                        1000.0 * count / benchmarkFrameMilliseconds_, uiMilliseconds_ / count,
                        backendMs / count, readbackMilliseconds_ / count,
                        uploadMilliseconds_ / count, static_cast<double>(backdropCopies) / count,
                        host_->pixelWidth(), host_->pixelHeight(), w, h, ui.instance_count,
                        ui.draw_call_count, ui.widget_count, ui.layout_ms, ui.paint_ms);
        };
        // The world alone, as island-preview draws it, for comparison.
        {
            for (int i = 0; i < 30; ++i) {
                runWorldOnlyFrame();
            }
            const auto started = Clock::now();
            for (int i = 0; i < 600; ++i) {
                runWorldOnlyFrame();
            }
            std::printf("[benchmark] world only, full window, no UI: frame %.3f ms with glFinish\n",
                        millisecondsSince(started) / 600.0);
        }
        measure(ViewportMode::Callback, R_LOOK_RAVEN_DARK, "draw callback, dark look", 600);
        measure(ViewportMode::Texture, R_LOOK_RAVEN_DARK, "CPU read-back, dark look", 600);
        measure(ViewportMode::Callback, R_LOOK_RAVEN_GLASS_DARK, "draw callback, glass look", 600);
        const RMemoryStats memory = stopRaven();
        return report(false, memory);
    }

    int report(bool closedByRaven, const RMemoryStats& memory)
    {
        const auto& backend = raven_eval::backendStats();
        const auto host = host_->stats();
        std::printf("[summary] frames %llu, UI frames %llu, frames without a UI frame %llu, "
                    "viewport callbacks %llu, backdrop copies %llu, closed by Raven %s\n",
                    static_cast<unsigned long long>(frames_),
                    static_cast<unsigned long long>(backend.frames),
                    static_cast<unsigned long long>(framesWithoutUi_),
                    static_cast<unsigned long long>(backend.viewportCallbacks),
                    static_cast<unsigned long long>(backend.backdrops),
                    closedByRaven ? "yes" : "no");
        std::printf("[summary] OpenGL errors %u, state mismatches %u, wrong-thread backend calls "
                    "%u, renderer errors %u, Raven errors %u, Raven warnings %u\n",
                    backend.glErrors, backend.stateMismatches, backend.wrongThreadCalls,
                    renderer_->errorCount(), ravenErrors().errors, ravenErrors().warnings);
        std::printf("[summary] after Rshutdown: %zu Raven blocks, backend textures %u; peak Raven "
                    "memory %zu KiB, %llu allocations\n",
                    memory.allocation_count, backend.liveTextures, memory.peak_bytes / 1024,
                    static_cast<unsigned long long>(memory.total_allocations));
        std::printf("[summary] input: %llu moves, %llu buttons, %llu wheels, %llu keys, %llu code "
                    "points, %llu focus, %llu leaves, %llu resizes, %llu closes, %llu cursor "
                    "changes, %llu wake-ups\n",
                    static_cast<unsigned long long>(host.mouseMoves),
                    static_cast<unsigned long long>(host.mouseButtons),
                    static_cast<unsigned long long>(host.wheels),
                    static_cast<unsigned long long>(host.keys),
                    static_cast<unsigned long long>(host.codepoints),
                    static_cast<unsigned long long>(host.focusChanges),
                    static_cast<unsigned long long>(host.mouseLeaves),
                    static_cast<unsigned long long>(host.resizes),
                    static_cast<unsigned long long>(host.closeRequests),
                    static_cast<unsigned long long>(host.cursorChanges),
                    static_cast<unsigned long long>(host.wakeUps));
        std::printf("[summary] OpenGL state around Raven's frames: %u mismatches; Dear ImGui "
                    "%s, %u state mismatches around its renderer, %llu mouse and %llu keyboard "
                    "events kept from Raven\n",
                    frameStateMismatches_, imguiStarted_ || options_.withImGui ? "on" : "off",
                    imguiStateMismatches_, static_cast<unsigned long long>(imguiMouseEvents_),
                    static_cast<unsigned long long>(imguiKeyboardEvents_));
        std::printf("[summary] focus: %llu keys and %llu buttons released by the host\n",
                    static_cast<unsigned long long>(host.releasedKeys),
                    static_cast<unsigned long long>(host.releasedButtons));
        for (const auto& message : ravenErrors().messages) {
            std::printf("[raven] %s\n", message.c_str());
        }
        bool ok = backend.glErrors == 0 && backend.stateMismatches == 0 &&
                  frameStateMismatches_ == 0 && imguiStateMismatches_ == 0 &&
                  backend.wrongThreadCalls == 0 && renderer_->errorCount() == 0 &&
                  ravenErrors().errors == 0 && memory.allocation_count == 0 &&
                  backend.liveTextures == 0;
        if (options_.inputCheck) {
            std::size_t passed = 0;
            for (const auto& result : inputResults_) {
                passed += result.passed ? 1 : 0;
            }
            const bool allPhases = phase_ == phases_.size();
            emit(std::format("@@CHECK close.raven-callback {} {} close callbacks",
                             closeCallbacks_ == 1 && closedByRaven ? "PASS" : "FAIL",
                             closeCallbacks_));
            emit(std::format("@@CHECK close.clean-shutdown {} OpenGL errors {}, state mismatches "
                             "{} + {} + {}, Raven errors {}, Raven blocks after Rshutdown {}, "
                             "backend textures {}",
                             ok ? "PASS" : "FAIL", backend.glErrors, backend.stateMismatches,
                             frameStateMismatches_, imguiStateMismatches_, ravenErrors().errors,
                             memory.allocation_count, backend.liveTextures));
            std::printf("[input-check] %zu of %zu checks passed, %zu of %zu phases run\n", passed,
                        inputResults_.size(), phase_, phases_.size());
            ok = ok && allPhases && passed == inputResults_.size() && closeCallbacks_ == 1;
        }
        if (options_.selfTest) {
            std::size_t passed = 0;
            for (const auto& result : results_) {
                passed += result.passed ? 1 : 0;
            }
            std::printf("[self-test] %zu of %zu steps passed\n", passed, steps_.size());
            ok = ok && passed == steps_.size();
        }
        std::printf("[summary] %s\n", ok ? "PASS" : "FAIL");
        return ok ? 0 : 1;
    }

    Options options_;
    bool sdlStarted_ = false;
    SDL_Window* window_ = nullptr;
    SDL_GLContext context_ = nullptr;
    std::optional<engine::render::Renderer> renderer_;
    std::unique_ptr<raven_eval::SdlRavenHost> host_;
    island_preview::WorldScene world_;
    engine::render::MeshHandle sky_;
    engine::worldgen::WorldGenSettings settings_;
    std::vector<IslandInfo> islands_;
    std::uint64_t triangles_ = 0;
    engine::camera::Camera camera_;
    engine::camera::FreeFlyController controller_;
    Keys keys_;
    bool looking_ = false;
    engine::math::Vec2 look_{0.0F};
    float wheel_ = 0.0F;

    bool ravenStarted_ = false;
    bool running_ = false;
    Rid window_ui_ = R_INVALID_ID;
    Rid regenerateButton_ = R_INVALID_ID;
    Rid modeButton_ = R_INVALID_ID;
    Rid screenshotButton_ = R_INVALID_ID;
    Rid statsLabel_ = R_INVALID_ID;
    Rid dock_ = R_INVALID_ID;
    Rid viewport_ = R_INVALID_ID;
    Rid hierarchy_ = R_INVALID_ID;
    Rid inspectorName_ = R_INVALID_ID;
    Rid inspectorSeed_ = R_INVALID_ID;
    Rid inspectorDetails_ = R_INVALID_ID;
    Rid focusButton_ = R_INVALID_ID;
    Rid consoleTabs_ = R_INVALID_ID;
    Rid console_ = R_INVALID_ID;
    Rid frameLabel_ = R_INVALID_ID;
    Rid viewportTexture_ = R_INVALID_ID;
    std::vector<Rid> treeItems_;
    int selected_ = -1;
    bool updatingInspector_ = false;
    std::vector<std::string> consoleLines_;
    ViewportMode viewportMode_ = ViewportMode::Callback;
    std::optional<std::filesystem::path> screenshotPath_;

    Clock::time_point startTime_ = Clock::now();
    Clock::time_point lastFrame_ = Clock::now();
    std::uint64_t frames_ = 0;
    std::uint64_t statsFrames_ = 0;
    std::uint64_t framesWithoutUi_ = 0;
    double lastStatsTime_ = 0.0;
    double frameMilliseconds_ = 0.0;
    double benchmarkFrameMilliseconds_ = 0.0;
    double uiMilliseconds_ = 0.0;
    double readbackMilliseconds_ = 0.0;
    double uploadMilliseconds_ = 0.0;
    std::uint64_t readbacks_ = 0;
    std::vector<std::uint8_t> readback_;
    std::vector<std::uint8_t> flipped_;
    std::uint64_t regenerations_ = 0;
    std::uint64_t actions_ = 0;
    std::uint64_t screenshots_ = 0;
    std::uint64_t closeCallbacks_ = 0;
    std::uint64_t viewportEvents_ = 0;
    std::uint64_t viewportResizes_ = 0;
    std::uint64_t viewportFocusLosses_ = 0;
    std::uint64_t windowFocusLosses_ = 0;
    int focusLossFrames_ = 0;
    engine::math::Vec3 focusLossPosition_{0.0F};
    std::array<int, 4> sdlKeyUps_{};
    int sdlButtonUps_ = 0;
    // Frames after which the state the backend restores differed (checked
    // around Rdraw_frame, from outside the backend).
    std::uint32_t frameStateMismatches_ = 0;
    Rid inspectorNotes_ = R_INVALID_ID;

    bool imguiStarted_ = false;
    std::uint64_t imguiClicks_ = 0;
    std::array<char, 64> imguiText_{};
    std::array<float, 4> imguiButtonRect_{};
    std::array<float, 4> imguiInputRect_{};
    std::uint32_t imguiStateMismatches_ = 0;
    std::uint64_t imguiMouseEvents_ = 0;
    std::uint64_t imguiKeyboardEvents_ = 0;

    std::vector<InputPhase> phases_;
    std::vector<StepResult> inputResults_;
    std::size_t phase_ = 0;
    bool inputStarted_ = false;
    bool phasePrepared_ = false;
    bool driverDone_ = false;
    bool closeAnnounced_ = false;
    int settle_ = 0;
    Clock::time_point phaseStart_ = Clock::now();
    std::uint64_t treeSelections_ = 0;
    std::uint64_t dockChanges_ = 0;
    std::string dockLayoutBefore_;
    Rid floatWindow_ = R_INVALID_ID;
    Rid inspectorPanel_ = R_INVALID_ID;
    std::array<int, 2> viewportSize_{0, 0};

    struct Before {
        std::uint64_t seed = 0;
        std::uint64_t selections = 0;
        std::uint64_t focusLosses = 0;
        std::uint64_t releasedKeys = 0;
        std::uint64_t releasedButtons = 0;
        std::uint64_t buttons = 0;
        std::uint64_t imguiClicks = 0;
        std::uint64_t codepoints = 0;
        std::uint64_t clipboardWrites = 0;
        std::uint64_t clipboardReads = 0;
        std::uint64_t focusChanges = 0;
        std::uint32_t ravenErrors = 0;
        std::uint64_t dockChanges = 0;
        std::uint64_t regenerations = 0;
        std::uint64_t resizes = 0;
        std::uint64_t callbacks = 0;
        std::uint64_t screenshots = 0;
        engine::math::Vec3 position{0.0F};
        float yaw = 0.0F;
        float speed = 0.0F;
        std::array<int, 2> size{0, 0};
    } before_;
    std::vector<Step> steps_;
    std::vector<StepResult> results_;
    std::size_t step_ = 0;
    bool stepStarted_ = false;
    int waited_ = 0;
};

} // namespace

int main(int argc, char** argv)
{
    const auto options = parseOptions(argc, argv);
    if (!options) {
        std::fprintf(stderr,
                     "usage: raven-evaluation [--frames N] [--screenshot FILE.png] [--self-test] "
                     "[--stress N] [--benchmark] [--texture-mode] [--no-vsync] [--seed N] "
                     "[--size WxH] [--look dark|light|glass|windows] [--ui-scale F] "
                     "[--input-check] [--with-imgui]\n");
        return 2;
    }
    try {
        Prototype prototype(*options);
        return prototype.run();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "raven-evaluation: %s\n", error.what());
        return 1;
    }
}
