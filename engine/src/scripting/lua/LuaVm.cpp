#include "LuaVm.hpp"

#include "engine/core/Log.hpp"

#include <lauxlib.h>
#include <lualib.h>

#include <array>
#include <cstdlib>
#include <format>
#include <utility>

namespace engine::scripting::lua {

namespace {

// A fixed string-hash seed: the same script iterates `pairs` in the same
// order in every run (scripts are project content; ADR 0024).
constexpr unsigned kHashSeed = 0x4C504531U; // "LPE1"

// The VM's allocator (lua_Alloc): realloc within the memory limit.
void* allocate(void* userData, void* block, std::size_t oldSize, std::size_t newSize)
{
    auto* accounting = static_cast<VmAccounting*>(userData);
    // For a new block Lua passes the kind of object in oldSize, not a size.
    const std::size_t held = block == nullptr ? 0 : oldSize;
    if (newSize == 0) {
        std::free(block);
        accounting->inUse -= held;
        return nullptr;
    }
    if (newSize > held && newSize - held > accounting->limit - accounting->inUse) {
        // Lua turns this into a memory error ("not enough memory").
        return nullptr;
    }
    void* resized = std::realloc(block, newSize);
    if (resized == nullptr) {
        if (newSize <= held) {
            // A failed shrink keeps the larger block, which still holds the data.
            accounting->inUse -= held - newSize;
            return block;
        }
        return nullptr;
    }
    accounting->inUse = accounting->inUse - held + newSize;
    return resized;
}

// Called every kBudgetCheckInterval instructions while Lua code runs.
void budgetHook(lua_State* state, lua_Debug* /*event*/)
{
    void* userData = nullptr;
    (void)lua_getallocf(state, &userData);
    auto* accounting = static_cast<VmAccounting*>(userData);
    if (++accounting->checksUsed <= accounting->checksAllowed) {
        return;
    }
    // From now on every instruction raises the error, so a script that
    // catches it with pcall cannot go on running.
    lua_sethook(state, budgetHook, LUA_MASKCOUNT, 1);
    (void)LuaVm::raiseError(state,
                            std::format("instruction budget exceeded: more than {} instructions "
                                        "in one call",
                                        accounting->budget));
}

// Adds a traceback to the error message (as lua.c does).
int messageHandler(lua_State* state)
{
    const char* message = lua_tostring(state, 1);
    if (message == nullptr) {
        if (luaL_callmeta(state, 1, "__tostring") != 0 && lua_type(state, -1) == LUA_TSTRING) {
            return 1;
        }
        message = lua_pushfstring(state, "(error object is a %s value)", luaL_typename(state, 1));
    }
    luaL_traceback(state, state, message, 1);
    return 1;
}

// Never reached: every Lua call runs in protected mode.
int panic(lua_State* state)
{
    const char* message = lua_tostring(state, -1);
    core::logError("script", "unprotected Lua error: {}", message != nullptr ? message : "?");
    return 0;
}

constexpr std::array<const char*, 17> kBaseFunctions{
    "assert",       "error",    "getmetatable", "ipairs", "next",   "pairs",
    "pcall",        "rawequal", "rawget",       "rawlen", "rawset", "select",
    "setmetatable", "tonumber", "tostring",     "type",   "xpcall",
};
constexpr std::array<const char*, 4> kLibraries{"string", "table", "math", "utf8"};

} // namespace

LuaVm::LuaVm(std::unique_ptr<VmAccounting> accounting, lua_State* state) noexcept
    : accounting_(std::move(accounting)), state_(state)
{
}

LuaVm::LuaVm(LuaVm&& other) noexcept
    : accounting_(std::move(other.accounting_)), state_(std::exchange(other.state_, nullptr))
{
}

LuaVm& LuaVm::operator=(LuaVm&& other) noexcept
{
    if (this != &other) {
        close();
        accounting_ = std::move(other.accounting_);
        state_ = std::exchange(other.state_, nullptr);
    }
    return *this;
}

LuaVm::~LuaVm()
{
    close();
}

void LuaVm::close() noexcept
{
    if (state_ != nullptr) {
        lua_close(state_);
        state_ = nullptr;
    }
}

core::Result<LuaVm> LuaVm::create(const VmLimits& limits)
{
    auto accounting = std::make_unique<VmAccounting>();
    accounting->limit = limits.memoryLimitBytes;
    accounting->budget = limits.instructionBudget;
    accounting->checksAllowed =
        (limits.instructionBudget + kBudgetCheckInterval - 1) / kBudgetCheckInterval;
    lua_State* state = lua_newstate(allocate, accounting.get(), kHashSeed);
    if (state == nullptr) {
        return core::Error{std::format("cannot create a Lua VM within the memory limit of {} bytes",
                                       limits.memoryLimitBytes)};
    }
    (void)lua_atpanic(state, panic);
    LuaVm vm(std::move(accounting), state);

    if (auto error = vm.protect([](lua_State* L) {
            luaL_openselectedlibs(
                L, LUA_GLIBK | LUA_STRLIBK | LUA_TABLIBK | LUA_MATHLIBK | LUA_UTF8LIBK, 0);
            // No bytecode: string.dump makes binary chunks, which can crash
            // the VM when loaded.
            lua_getglobal(L, "string");
            lua_pushnil(L);
            lua_setfield(L, -2, "dump");
            lua_pop(L, 1);
            // The shared string metatable (its __index is the VM's string
            // table) is out of reach: getmetatable("") gives false and
            // setmetatable on it fails.
            lua_pushliteral(L, "");
            if (lua_getmetatable(L, -1) != 0) {
                lua_pushboolean(L, 0);
                lua_setfield(L, -2, "__metatable");
                lua_pop(L, 1);
            }
            lua_pop(L, 1);
        })) {
        return core::Error{"cannot open the Lua libraries: " + error->message};
    }
    return vm;
}

int LuaVm::raiseError(lua_State* state, std::string_view message)
{
    lua_pushlstring(state, message.data(), message.size());
    return lua_error(state);
}

std::optional<core::Error> LuaVm::runProtected(lua_CFunction trampoline, void* call)
{
    if (state_ == nullptr) {
        return core::Error{"the Lua VM is closed"};
    }
    lua_State* state = state_;
    const int base = lua_gettop(state);
    accounting_->checksUsed = 0;
    lua_sethook(state, budgetHook, LUA_MASKCOUNT, kBudgetCheckInterval);
    // Light C functions and a light userdata: pushing them allocates nothing.
    lua_pushcfunction(state, messageHandler);
    lua_pushcfunction(state, trampoline);
    lua_pushlightuserdata(state, call);
    const int status = lua_pcall(state, 1, 0, base + 1);
    std::optional<core::Error> error;
    if (status != LUA_OK) {
        const char* message = lua_tostring(state, -1);
        if (message != nullptr) {
            error = core::Error{message};
        } else {
            error = core::Error{status == LUA_ERRMEM ? "not enough memory" : "unknown Lua error"};
        }
    }
    lua_settop(state, base);
    lua_sethook(state, nullptr, 0, 0);
    return error;
}

void LuaVm::pushSandbox(lua_State* state)
{
    lua_newtable(state);
    const int environment = lua_gettop(state);
    for (const char* name : kBaseFunctions) {
        lua_getglobal(state, name);
        lua_setfield(state, environment, name);
    }
    lua_getglobal(state, "_VERSION");
    lua_setfield(state, environment, "_VERSION");
    lua_pushvalue(state, environment);
    lua_setfield(state, environment, "_G");
    // Copies, so that one script changing string.format does not change it
    // for the others.
    for (const char* library : kLibraries) {
        lua_newtable(state);
        const int copy = lua_gettop(state);
        lua_getglobal(state, library);
        const int source = lua_gettop(state);
        lua_pushnil(state);
        while (lua_next(state, source) != 0) {
            lua_pushvalue(state, -2);
            lua_insert(state, -2);
            lua_settable(state, copy);
        }
        lua_pop(state, 1);
        lua_setfield(state, environment, library);
    }
}

void LuaVm::loadText(lua_State* state, std::string_view source, std::string_view chunkName)
{
    const std::string name = "@" + std::string(chunkName);
    if (luaL_loadbufferx(state, source.data(), source.size(), name.c_str(), "t") != LUA_OK) {
        // The message is on the stack.
        (void)lua_error(state);
    }
}

} // namespace engine::scripting::lua
