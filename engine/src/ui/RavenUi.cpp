#include "RavenUi.hpp"

#include "../render/opengl/GlRavenBackend.hpp"
#include "RavenHost.hpp"

#include "engine/core/Log.hpp"

#include <Raven.h>

#include <atomic>
#include <string>
#include <type_traits>
#include <utility>

namespace engine::ui {

static_assert(std::is_same_v<RavenId, Rid>, "engine::ui::RavenId must be Raven's Rid");
static_assert(kInvalidRavenId == R_INVALID_ID, "kInvalidRavenId must be Raven's R_INVALID_ID");

namespace {

// Raven's state is per process (one Rinit at a time, callbacks without user
// data), so at most one RavenUi exists, and the count of Raven's errors is
// one per process too.
std::atomic<bool> g_exists{false};
std::atomic<std::uint32_t> g_ravenErrors{0};

// Raven's error callback: every error and warning goes to the engine's log.
// Raven may call it from its worker threads; the log is thread-safe.
void logRavenError(RError code, RSeverity severity, const char* detail)
{
    g_ravenErrors.fetch_add(1, std::memory_order_relaxed);
    const char* text = detail != nullptr ? detail : "";
    const char* name = Rerror_to_string(code);
    if (severity == R_SEVERITY_ERROR || severity == R_SEVERITY_FATAL) {
        core::logError("raven", "{}: {}", name != nullptr ? name : "error", text);
    } else {
        core::logWarning("raven", "{}: {}", name != nullptr ? name : "warning", text);
    }
}

// Raven's last error, for a startup message.
std::string lastRavenError()
{
    const char* detail = nullptr;
    const RError code = Rerror_get_last(&detail, nullptr);
    if (code == R_NO_ERROR || detail == nullptr || detail[0] == '\0') {
        return "no detail from Raven";
    }
    return detail;
}

std::size_t ravenAllocations() noexcept
{
    RMemoryStats memory{};
    Rmemory_get_stats(&memory);
    return memory.allocation_count;
}

// Undoes a partial start: Raven (if it started), the host platform
// registration, the error callback and the one-instance flag.
void abandonStart(bool ravenStarted) noexcept
{
    if (ravenStarted) {
        Rshutdown();
    }
    Rhost_platform_register(nullptr);
    render::opengl::setRavenPresentedWindow(R_INVALID_ID);
    Rerror_set_callback(nullptr);
    g_exists.store(false);
}

} // namespace

core::Result<std::unique_ptr<RavenUi>> RavenUi::create(platform::Window& window, const UiDesc& desc,
                                                       RavenBackendKind backend)
{
    if (g_exists.exchange(true)) {
        return core::Error{"UI: Raven is already running in this process; only one UI can exist"};
    }
    if (Ris_initialized() != R_FALSE) {
        g_exists.store(false);
        return core::Error{"UI: Raven was started outside the engine; only one Raven can run"};
    }
    g_ravenErrors.store(0);
    Rerror_set_callback(&logRavenError);

    auto host = createRavenHost(window);
    if (!host) {
        abandonStart(false);
        return core::Error{"UI: the window has no native handle for Raven's host platform"};
    }
    host->registerPlatform();

    RInitDesc init{};
    init.app_name = "LowPolyEngine";
    // Raven draws on the calling (main) thread, which owns the OpenGL context.
    // (The engine swaps the buffers; the backend never presents.)
    init.disable_render_thread = R_TRUE;
    if (backend == RavenBackendKind::OpenGl) {
        Rbackend_register(&render::opengl::ravenBackend());
        init.backend = R_BACKEND_CUSTOM;
    } else {
        init.backend = R_BACKEND_NULL;
    }
    if (Rinit_ex(&init) == R_FALSE) {
        std::string reason = lastRavenError();
        if (backend == RavenBackendKind::OpenGl &&
            !render::opengl::ravenBackendStats().lastError.empty()) {
            reason += " (" + render::opengl::ravenBackendStats().lastError + ")";
        }
        abandonStart(false);
        return core::Error{"UI: Raven did not start: " + reason};
    }

    // Before any document (ADR 0026): files are never looked at again, and
    // nothing is taken from the current folder, the base path or PATH.
    Rxml_set_live_reload(R_FALSE);
    Rxml_set_auto_load(R_FALSE);
    if (Rxml_get_live_reload() != R_FALSE || Rxml_get_auto_load() != R_FALSE) {
        abandonStart(true);
        return core::Error{"UI: Raven's live reload and auto-loading could not be turned off"};
    }

    std::unique_ptr<RavenUi> ui(new RavenUi(std::move(host), backend));
    ui->running_ = true;
    const Rid main = Rwindow_create_from_xml(desc.layoutXml.data(), desc.layoutXml.size());
    if (main == R_INVALID_ID) {
        const std::string reason = lastRavenError();
        ui->host_->detach();
        ui->running_ = false;
        abandonStart(true);
        return core::Error{"UI: " + desc.layoutName +
                           ": Raven could not build the window: " + reason};
    }
    ui->mainWindow_ = main;
    // The engine draws every frame from scratch (it clears and swaps), so
    // Raven draws its window every frame too, not only when something changed.
    Rwindow_set_continuous_redraw(main, R_TRUE);
    Rwindow_show(main);
    if (backend == RavenBackendKind::OpenGl) {
        render::opengl::setRavenPresentedWindow(main);
    }
    ui->host_->attach(main);
    core::logInfo("ui", "Raven {} started: {} ({}), scale {:.2f}", Rget_version(), desc.layoutName,
                  backend == RavenBackendKind::OpenGl ? "OpenGL backend" : "null backend",
                  static_cast<double>(ui->host_->scale()));
    return ui;
}

RavenUi::RavenUi(std::unique_ptr<RavenHost> host, RavenBackendKind backend)
    : host_(std::move(host)), backend_(backend)
{
}

RavenUi::~RavenUi()
{
    (void)shutdown();
}

RavenId RavenUi::mainWindow() const noexcept
{
    return isOpen() ? mainWindow_ : kInvalidRavenId;
}

float RavenUi::scale() const noexcept
{
    return host_ ? host_->scale() : 1.0F;
}

bool RavenUi::isOpen() const noexcept
{
    // The host detaches as soon as Raven destroyed the window (an accepted
    // close request, during the events); Raven's handle check covers the rest.
    return running_ && !ended_ && host_ && host_->attachedWindow() == mainWindow_ &&
           Rwindow_is_valid(mainWindow_) != R_FALSE;
}

void RavenUi::frame()
{
    if (!isOpen()) {
        return;
    }
    bool drawn = true;
    if (backend_ == RavenBackendKind::OpenGl) {
        // Everything Raven did this frame, its texture uploads included, must
        // leave the renderer's state as it was (the backend checks its own
        // frame too).
        const bool same =
            render::opengl::sameGlStateAround([&drawn] { drawn = Rdraw_frame() != R_FALSE; },
                                              render::opengl::GlStateGroups::Restored);
        if (!same) {
            ++frameStateMismatches_;
        }
    } else {
        drawn = Rdraw_frame() != R_FALSE;
    }
    ++frames_;
    if (!drawn) {
        // Rquit, or Raven's last window closed during the frame.
        ended_ = true;
        return;
    }
    host_->syncTextInput();
}

UiRunStats RavenUi::stats() const noexcept
{
    if (!running_) {
        return final_;
    }
    UiRunStats stats;
    stats.enabled = true;
    if (backend_ == RavenBackendKind::OpenGl) {
        const auto& backend = render::opengl::ravenBackendStats();
        stats.frames = backend.frames;
        stats.glErrors = backend.glErrors;
        stats.stateMismatches = backend.stateMismatches;
    } else {
        stats.frames = frames_;
    }
    stats.stateMismatches += frameStateMismatches_;
    stats.ravenErrors = g_ravenErrors.load(std::memory_order_relaxed);
    return stats;
}

UiRunStats RavenUi::shutdown() noexcept
{
    if (!running_) {
        return final_;
    }
    final_ = stats();
    if (host_) {
        host_->detach();
    }
    // Destroys every window and widget, stops Raven's threads and calls the
    // backend's Shutdown, which releases its OpenGL objects.
    Rshutdown();
    running_ = false;
    mainWindow_ = kInvalidRavenId;
    if (backend_ == RavenBackendKind::OpenGl) {
        // The backend's last counts (Shutdown checks for OpenGL errors too).
        const auto& backend = render::opengl::ravenBackendStats();
        final_.glErrors = backend.glErrors;
        final_.stateMismatches = backend.stateMismatches + frameStateMismatches_;
    }
    final_.ravenErrors = g_ravenErrors.load(std::memory_order_relaxed);
    final_.blocksAfterShutdown = ravenAllocations();
    if (final_.blocksAfterShutdown != 0) {
        core::logError("ui", "Raven holds {} allocation(s) after Rshutdown",
                       final_.blocksAfterShutdown);
    }
    host_.reset();
    abandonStart(false);
    return final_;
}

bool RavenUi::exists() noexcept
{
    return g_exists.load();
}

} // namespace engine::ui
