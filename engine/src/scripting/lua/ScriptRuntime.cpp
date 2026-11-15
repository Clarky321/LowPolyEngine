#include "engine/scripting/ScriptRuntime.hpp"

#include "LuaVm.hpp"

#include "engine/core/FileSystem.hpp"
#include "engine/core/Log.hpp"
#include "engine/scene/ScriptComponent.hpp"
#include "engine/scene/TransformComponent.hpp"

#include <lauxlib.h>

#include <cmath>
#include <format>
#include <limits>
#include <map>
#include <system_error>
#include <utility>

namespace engine::scripting {

namespace {

constexpr std::size_t kMaxSourceLimit = 16U * 1024U * 1024U;
constexpr std::size_t kMinMemoryLimit = 256U * 1024U;

struct Instance {
    std::string name;
    // The script's table in the Lua registry.
    int module = LUA_NOREF;
    ScriptStatus status = ScriptStatus::Loaded;
    std::string error;
};

std::string describe(const Instance& instance, scene::EntityId entity)
{
    return std::format("script '{}' (entity {})", instance.name, entity.toString());
}

void logPrint(scene::EntityId entity, std::string_view script, std::string_view text)
{
    core::logInfo("script", "{} (entity {}): {}", script, entity.toString(), text);
}

} // namespace

std::optional<core::Error> validate(const ScriptLimits& limits)
{
    if (limits.maxSourceBytes < 1 || limits.maxSourceBytes > kMaxSourceLimit) {
        return core::Error{std::format("script limits: maxSourceBytes is {}, must be in [1, {}]",
                                       limits.maxSourceBytes, kMaxSourceLimit)};
    }
    if (limits.instructionBudget < static_cast<std::uint64_t>(lua::kBudgetCheckInterval)) {
        return core::Error{
            std::format("script limits: instructionBudget is {}, must be at least {}",
                        limits.instructionBudget, lua::kBudgetCheckInterval)};
    }
    if (limits.memoryLimitBytes < kMinMemoryLimit) {
        return core::Error{std::format("script limits: memoryLimitBytes is {}, must be at least {}",
                                       limits.memoryLimitBytes, kMinMemoryLimit)};
    }
    return std::nullopt;
}

struct ScriptRuntime::Impl {
    ScriptLimits limits;
    lua::LuaVm vm;
    std::map<scene::EntityId, Instance> instances;
    PrintHandler print = logPrint;
    // Set while start or update runs: the engine API works on this scene.
    scene::Scene* scene = nullptr;
    float deltaSeconds = 0.0F;

    Impl(const ScriptLimits& scriptLimits, lua::LuaVm luaVm)
        : limits(scriptLimits), vm(std::move(luaVm))
    {
    }

    // Clears the call context when a start or update pass ends.
    struct SceneScope {
        Impl& impl;
        SceneScope(Impl& owner, scene::Scene& active, float delta) noexcept : impl(owner)
        {
            impl.scene = &active;
            impl.deltaSeconds = delta;
        }
        ~SceneScope()
        {
            impl.scene = nullptr;
            impl.deltaSeconds = 0.0F;
        }
        SceneScope(const SceneScope&) = delete;
        SceneScope& operator=(const SceneScope&) = delete;
    };

    void fail(Instance& instance, std::string message)
    {
        instance.status = ScriptStatus::Failed;
        instance.error = std::move(message);
        const int module = std::exchange(instance.module, LUA_NOREF);
        // Releases the script's table so its memory can be collected.
        (void)vm.protect([module](lua_State* state) {
            luaL_unref(state, LUA_REGISTRYINDEX, module);
            (void)lua_gc(state, LUA_GCCOLLECT);
        });
    }

    // Calls instance.module[function](entity[, dt]); a missing function is
    // no error.
    std::optional<core::Error> call(scene::EntityId entity, Instance& instance,
                                    std::string_view function, bool withDelta)
    {
        const std::string id = entity.toString();
        const std::string key(function);
        const int module = instance.module;
        const double delta = static_cast<double>(deltaSeconds);
        return vm.protect([&](lua_State* state) {
            lua_rawgeti(state, LUA_REGISTRYINDEX, module);
            lua_getfield(state, -1, key.c_str());
            if (lua_isnil(state, -1)) {
                return;
            }
            if (!lua_isfunction(state, -1)) {
                (void)lua::LuaVm::raiseError(state, std::format("'{}' is a {}, not a function", key,
                                                                luaL_typename(state, -1)));
            }
            lua_pushlstring(state, id.data(), id.size());
            if (withDelta) {
                lua_pushnumber(state, delta);
            }
            lua_call(state, withDelta ? 2 : 1, 0);
        });
    }

    // ---- The engine API --------------------------------------------------

    static Impl& self(lua_State* state)
    {
        return *static_cast<Impl*>(lua_touserdata(state, lua_upvalueindex(1)));
    }

    static scene::Scene& sceneFor(lua_State* state, std::string_view function)
    {
        Impl& impl = self(state);
        if (impl.scene == nullptr) {
            (void)lua::LuaVm::raiseError(
                state, std::format("engine.{}: only available in start and update", function));
        }
        return *impl.scene;
    }

    static void expectArguments(lua_State* state, int count, std::string_view function,
                                std::string_view parameters)
    {
        const int given = lua_gettop(state);
        if (given != count) {
            (void)lua::LuaVm::raiseError(
                state, std::format("engine.{} expects {} argument{} ({}), got {}", function, count,
                                   count == 1 ? "" : "s", parameters, given));
        }
    }

    static scene::EntityId entityArgument(lua_State* state, int index)
    {
        luaL_checktype(state, index, LUA_TSTRING);
        std::size_t length = 0;
        const char* text = lua_tolstring(state, index, &length);
        auto entity = scene::EntityId::parse(std::string_view(text, length));
        if (!entity) {
            (void)luaL_argerror(state, index, entity.error().message.c_str());
        }
        return entity.value();
    }

    static float floatArgument(lua_State* state, int index)
    {
        luaL_checktype(state, index, LUA_TNUMBER);
        const double value = lua_tonumber(state, index);
        // Converting a finite double outside the float range is undefined.
        if (std::isfinite(value) &&
            std::abs(value) > static_cast<double>(std::numeric_limits<float>::max())) {
            (void)luaL_argerror(state, index, "number outside the float range");
        }
        return static_cast<float>(value);
    }

    static scene::TransformComponent transformOf(lua_State* state, std::string_view function,
                                                 scene::EntityId entity)
    {
        auto transform = sceneFor(state, function).component<scene::TransformComponent>(entity);
        if (!transform) {
            (void)lua::LuaVm::raiseError(
                state, std::format("engine.{}: {}", function, transform.error().message));
        }
        return transform.value();
    }

    static void store(lua_State* state, std::string_view function, scene::EntityId entity,
                      const scene::TransformComponent& transform)
    {
        if (auto error = sceneFor(state, function).replaceComponent(entity, transform)) {
            (void)lua::LuaVm::raiseError(state,
                                         std::format("engine.{}: {}", function, error->message));
        }
    }

    static int exists(lua_State* state)
    {
        expectArguments(state, 1, "exists", "id");
        const scene::EntityId entity = entityArgument(state, 1);
        lua_pushboolean(state, sceneFor(state, "exists").contains(entity) ? 1 : 0);
        return 1;
    }

    static int name(lua_State* state)
    {
        expectArguments(state, 1, "name", "id");
        const scene::EntityId entity = entityArgument(state, 1);
        const scene::Scene& active = sceneFor(state, "name");
        if (auto error = active.findEntity(entity)) {
            return lua::LuaVm::raiseError(state, "engine.name: " + error->message);
        }
        auto component = active.component<scene::NameComponent>(entity);
        if (!component) {
            lua_pushnil(state);
            return 1;
        }
        const std::string& text = component.value().value;
        lua_pushlstring(state, text.data(), text.size());
        return 1;
    }

    static int position(lua_State* state)
    {
        expectArguments(state, 1, "position", "id");
        const auto transform = transformOf(state, "position", entityArgument(state, 1));
        lua_pushnumber(state, static_cast<double>(transform.position.x));
        lua_pushnumber(state, static_cast<double>(transform.position.y));
        lua_pushnumber(state, static_cast<double>(transform.position.z));
        return 3;
    }

    static int setPosition(lua_State* state)
    {
        expectArguments(state, 4, "setPosition", "id, x, y, z");
        const scene::EntityId entity = entityArgument(state, 1);
        auto transform = transformOf(state, "setPosition", entity);
        transform.position = {floatArgument(state, 2), floatArgument(state, 3),
                              floatArgument(state, 4)};
        store(state, "setPosition", entity, transform);
        return 0;
    }

    static int rotation(lua_State* state)
    {
        expectArguments(state, 1, "rotation", "id");
        const auto transform = transformOf(state, "rotation", entityArgument(state, 1));
        lua_pushnumber(state, static_cast<double>(transform.rotation.w));
        lua_pushnumber(state, static_cast<double>(transform.rotation.x));
        lua_pushnumber(state, static_cast<double>(transform.rotation.y));
        lua_pushnumber(state, static_cast<double>(transform.rotation.z));
        return 4;
    }

    static int setRotation(lua_State* state)
    {
        expectArguments(state, 5, "setRotation", "id, w, x, y, z");
        const scene::EntityId entity = entityArgument(state, 1);
        auto transform = transformOf(state, "setRotation", entity);
        transform.rotation = math::Quat{floatArgument(state, 2), floatArgument(state, 3),
                                        floatArgument(state, 4), floatArgument(state, 5)};
        store(state, "setRotation", entity, transform);
        return 0;
    }

    static int scale(lua_State* state)
    {
        expectArguments(state, 1, "scale", "id");
        const auto transform = transformOf(state, "scale", entityArgument(state, 1));
        lua_pushnumber(state, static_cast<double>(transform.scale.x));
        lua_pushnumber(state, static_cast<double>(transform.scale.y));
        lua_pushnumber(state, static_cast<double>(transform.scale.z));
        return 3;
    }

    static int setScale(lua_State* state)
    {
        expectArguments(state, 4, "setScale", "id, x, y, z");
        const scene::EntityId entity = entityArgument(state, 1);
        auto transform = transformOf(state, "setScale", entity);
        transform.scale = {floatArgument(state, 2), floatArgument(state, 3),
                           floatArgument(state, 4)};
        store(state, "setScale", entity, transform);
        return 0;
    }

    static int deltaTime(lua_State* state)
    {
        expectArguments(state, 0, "deltaTime", "");
        (void)sceneFor(state, "deltaTime");
        lua_pushnumber(state, static_cast<double>(self(state).deltaSeconds));
        return 1;
    }

    // print(...): the arguments as tostring gives them, separated by tabs.
    // Upvalues: the runtime, the entity ID, the script's name.
    static int luaPrint(lua_State* state)
    {
        const int count = lua_gettop(state);
        std::string text;
        for (int i = 1; i <= count; ++i) {
            std::size_t length = 0;
            const char* piece = luaL_tolstring(state, i, &length);
            if (i > 1) {
                text += '\t';
            }
            text.append(piece, length);
            lua_pop(state, 1);
        }
        Impl& impl = self(state);
        std::size_t idLength = 0;
        const char* idText = lua_tolstring(state, lua_upvalueindex(2), &idLength);
        std::size_t nameLength = 0;
        const char* nameText = lua_tolstring(state, lua_upvalueindex(3), &nameLength);
        const auto entity = scene::EntityId::parse(std::string_view(idText, idLength));
        if (impl.print && entity) {
            impl.print(entity.value(), std::string_view(nameText, nameLength), text);
        }
        return 0;
    }

    // env.engine and env.print for one instance; the environment is on top.
    void addEngineApi(lua_State* state, scene::EntityId entity, std::string_view scriptName)
    {
        const int environment = lua_gettop(state);
        struct Function {
            const char* name;
            lua_CFunction function;
        };
        static constexpr Function kFunctions[] = {
            {"exists", exists},       {"name", name},
            {"position", position},   {"setPosition", setPosition},
            {"rotation", rotation},   {"setRotation", setRotation},
            {"scale", scale},         {"setScale", setScale},
            {"deltaTime", deltaTime},
        };
        lua_newtable(state);
        for (const Function& function : kFunctions) {
            lua_pushlightuserdata(state, this);
            lua_pushcclosure(state, function.function, 1);
            lua_setfield(state, -2, function.name);
        }
        lua_setfield(state, environment, "engine");

        const std::string id = entity.toString();
        lua_pushlightuserdata(state, this);
        lua_pushlstring(state, id.data(), id.size());
        lua_pushlstring(state, scriptName.data(), scriptName.size());
        lua_pushcclosure(state, &Impl::luaPrint, 3);
        lua_setfield(state, environment, "print");
    }
};

ScriptRuntime::ScriptRuntime(std::unique_ptr<Impl> impl) noexcept : impl_(std::move(impl)) {}
ScriptRuntime::ScriptRuntime(ScriptRuntime&& other) noexcept = default;
ScriptRuntime& ScriptRuntime::operator=(ScriptRuntime&& other) noexcept = default;
ScriptRuntime::~ScriptRuntime() = default;

core::Result<ScriptRuntime> ScriptRuntime::create(const ScriptLimits& limits)
{
    if (auto error = validate(limits)) {
        return *error;
    }
    auto vm = lua::LuaVm::create({limits.memoryLimitBytes, limits.instructionBudget});
    if (!vm) {
        return vm.error();
    }
    return ScriptRuntime(std::make_unique<Impl>(limits, std::move(vm).value()));
}

std::optional<core::Error> ScriptRuntime::addScript(scene::EntityId entity, std::string_view name,
                                                    std::string_view source)
{
    Impl& impl = *impl_;
    const std::string scriptName(name);
    if (const auto existing = impl.instances.find(entity); existing != impl.instances.end()) {
        return core::Error{std::format("script '{}' (entity {}): the entity already has the script "
                                       "'{}'",
                                       scriptName, entity.toString(), existing->second.name)};
    }
    if (source.size() > impl.limits.maxSourceBytes) {
        return core::Error{std::format("script '{}' (entity {}): {} bytes, at most {} allowed",
                                       scriptName, entity.toString(), source.size(),
                                       impl.limits.maxSourceBytes)};
    }
    Instance instance;
    instance.name = scriptName;
    int module = LUA_NOREF;
    auto error = impl.vm.protect([&](lua_State* state) {
        lua::LuaVm::loadText(state, source, scriptName);
        lua::LuaVm::pushSandbox(state);
        impl.addEngineApi(state, entity, scriptName);
        // The environment becomes the chunk's _ENV, its first upvalue.
        (void)lua_setupvalue(state, -2, 1);
        lua_call(state, 0, 1);
        if (!lua_istable(state, -1)) {
            (void)lua::LuaVm::raiseError(
                state, std::format("the script returned a {}, not a table with start and update",
                                   luaL_typename(state, -1)));
        }
        module = luaL_ref(state, LUA_REGISTRYINDEX);
    });
    if (error) {
        instance.status = ScriptStatus::Failed;
        instance.error = std::format("{}: {}", describe(instance, entity), error->message);
        auto stored = impl.instances.emplace(entity, std::move(instance)).first;
        return core::Error{stored->second.error};
    }
    instance.module = module;
    impl.instances.emplace(entity, std::move(instance));
    return std::nullopt;
}

std::vector<core::Error> ScriptRuntime::loadScripts(const scene::Scene& scene,
                                                    const assets::AssetRegistry& registry)
{
    Impl& impl = *impl_;
    std::vector<core::Error> errors;
    for (const scene::EntityId entity : scene.entityIds()) {
        auto component = scene.component<scene::ScriptComponent>(entity);
        if (!component || !component.value().enabled || impl.instances.contains(entity)) {
            continue;
        }
        const assets::AssetId asset = component.value().asset;
        // Until the file is known, the asset ID names the script.
        Instance failed;
        failed.name = asset.toString();
        failed.status = ScriptStatus::Failed;
        const auto record = [&](const std::string& problem) {
            failed.error = std::format("{}: {}", describe(failed, entity), problem);
            errors.push_back(core::Error{failed.error});
            impl.instances.emplace(entity, std::move(failed));
        };

        auto file = registry.resolve(asset, assets::AssetType::Script);
        if (!file) {
            record(file.error().message);
            continue;
        }
        const auto metadata = registry.find(asset);
        failed.name = metadata ? metadata.value().path.text() : failed.name;
        std::error_code sizeError;
        const auto size = std::filesystem::file_size(file.value(), sizeError);
        if (!sizeError && size > impl.limits.maxSourceBytes) {
            record(std::format("{} bytes, at most {} allowed", size, impl.limits.maxSourceBytes));
            continue;
        }
        auto source = core::readTextFile(file.value());
        if (!source) {
            record(source.error().message);
            continue;
        }
        if (auto error = addScript(entity, failed.name, source.value())) {
            errors.push_back(*error);
        }
    }
    return errors;
}

std::vector<core::Error> ScriptRuntime::start(scene::Scene& scene)
{
    Impl& impl = *impl_;
    std::vector<core::Error> errors;
    const Impl::SceneScope scope(impl, scene, 0.0F);
    for (const scene::EntityId entity : scene.entityIds()) {
        auto found = impl.instances.find(entity);
        if (found == impl.instances.end() || found->second.status != ScriptStatus::Loaded) {
            continue;
        }
        Instance& instance = found->second;
        if (auto error = impl.call(entity, instance, "start", false)) {
            const std::string message =
                std::format("{}: start: {}", describe(instance, entity), error->message);
            impl.fail(instance, message);
            errors.push_back(core::Error{message});
            continue;
        }
        instance.status = ScriptStatus::Started;
    }
    for (auto& [entity, instance] : impl.instances) {
        if (instance.status != ScriptStatus::Failed && !scene.contains(entity)) {
            const std::string message = std::format("{}: the entity is not in scene '{}'",
                                                    describe(instance, entity), scene.name());
            impl.fail(instance, message);
            errors.push_back(core::Error{message});
        }
    }
    return errors;
}

std::vector<core::Error> ScriptRuntime::update(scene::Scene& scene, float deltaSeconds)
{
    Impl& impl = *impl_;
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return {core::Error{std::format("script update: deltaSeconds is {}, must be finite and "
                                        ">= 0",
                                        deltaSeconds)}};
    }
    std::vector<core::Error> errors;
    const Impl::SceneScope scope(impl, scene, deltaSeconds);
    for (const scene::EntityId entity : scene.entityIds()) {
        auto found = impl.instances.find(entity);
        if (found == impl.instances.end() || found->second.status != ScriptStatus::Started) {
            continue;
        }
        if (const auto component = scene.component<scene::ScriptComponent>(entity);
            component && !component.value().enabled) {
            continue;
        }
        Instance& instance = found->second;
        if (auto error = impl.call(entity, instance, "update", true)) {
            const std::string message =
                std::format("{}: update: {}", describe(instance, entity), error->message);
            impl.fail(instance, message);
            errors.push_back(core::Error{message});
        }
    }
    for (auto& [entity, instance] : impl.instances) {
        if (instance.status == ScriptStatus::Started && !scene.contains(entity)) {
            const std::string message = std::format("{}: the entity is not in scene '{}'",
                                                    describe(instance, entity), scene.name());
            impl.fail(instance, message);
            errors.push_back(core::Error{message});
        }
    }
    return errors;
}

std::optional<ScriptInstanceInfo> ScriptRuntime::instance(scene::EntityId entity) const
{
    const auto found = impl_->instances.find(entity);
    if (found == impl_->instances.end()) {
        return std::nullopt;
    }
    return ScriptInstanceInfo{entity, found->second.name, found->second.status,
                              found->second.error};
}

std::vector<ScriptInstanceInfo> ScriptRuntime::instances() const
{
    std::vector<ScriptInstanceInfo> result;
    result.reserve(impl_->instances.size());
    for (const auto& [entity, instance] : impl_->instances) {
        result.push_back(
            ScriptInstanceInfo{entity, instance.name, instance.status, instance.error});
    }
    return result;
}

std::optional<core::Error> ScriptRuntime::restart()
{
    Impl& impl = *impl_;
    impl.instances.clear();
    impl.vm.close();
    const std::size_t leaked = impl.vm.memoryInUse();
    auto vm = lua::LuaVm::create({impl.limits.memoryLimitBytes, impl.limits.instructionBudget});
    if (!vm) {
        return vm.error();
    }
    impl.vm = std::move(vm).value();
    if (leaked != 0) {
        return core::Error{
            std::format("the closed Lua VM kept {} bytes; a new VM was opened anyway", leaked)};
    }
    return std::nullopt;
}

std::size_t ScriptRuntime::memoryInUse() const noexcept
{
    return impl_->vm.memoryInUse();
}

const ScriptLimits& ScriptRuntime::limits() const noexcept
{
    return impl_->limits;
}

void ScriptRuntime::setPrintHandler(PrintHandler handler)
{
    impl_->print = handler ? std::move(handler) : PrintHandler{logPrint};
}

} // namespace engine::scripting
