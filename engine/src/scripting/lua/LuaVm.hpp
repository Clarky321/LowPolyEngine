#pragma once

// Internal to engine/src/scripting/lua/: one Lua 5.5 VM with a memory limit
// and an instruction budget (M4.5, ADR 0024).
//
// Lua is compiled as C++ (conanfile.py), so a Lua error is a C++ exception of
// Lua's own and unwinds the frames of the bindings like any exception; its
// declarations have C++ linkage, which is why lua.h is included directly
// (lua.hpp, the extern "C" wrapper, does not exist in this build).
//
// Every entry from C++ into Lua goes through protect(): the work runs inside
// lua_pcall with a message handler that adds a traceback, so no Lua error -
// a script's, a memory error or the budget's - ever reaches lua_atpanic.

#include "engine/core/Result.hpp"

#include <lua.h>

#include <cstddef>
#include <cstdint>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>

namespace engine::scripting::lua {

struct VmLimits {
    std::size_t memoryLimitBytes = 0;
    std::uint64_t instructionBudget = 0;
};

// The allocator's user data: what the VM holds, and the budget of the
// current call.
struct VmAccounting {
    std::size_t limit = 0;
    std::size_t inUse = 0;
    std::uint64_t budget = 0;
    std::uint64_t checksAllowed = 0;
    std::uint64_t checksUsed = 0;
};

// Instructions between two budget checks.
inline constexpr int kBudgetCheckInterval = 1000;

class LuaVm {
public:
    // Opens a VM with the libraries base, string, table, math and utf8 and
    // removes what the sandbox does not allow from them (see sandbox()).
    // Fails if the memory limit does not even hold a fresh VM.
    [[nodiscard]] static core::Result<LuaVm> create(const VmLimits& limits);

    LuaVm(LuaVm&& other) noexcept;
    LuaVm& operator=(LuaVm&& other) noexcept;
    LuaVm(const LuaVm&) = delete;
    LuaVm& operator=(const LuaVm&) = delete;
    ~LuaVm();

    // Closes the VM; afterwards memoryInUse() shows what it did not return
    // (0 unless Lua leaks).
    void close() noexcept;
    [[nodiscard]] bool isOpen() const noexcept { return state_ != nullptr; }

    [[nodiscard]] lua_State* state() const noexcept { return state_; }
    [[nodiscard]] std::size_t memoryInUse() const noexcept
    {
        return accounting_ ? accounting_->inUse : 0;
    }
    [[nodiscard]] std::size_t memoryLimit() const noexcept
    {
        return accounting_ ? accounting_->limit : 0;
    }

    // Runs work(L) in protected mode within a fresh instruction budget. A
    // Lua error inside - from a script the work calls, from the budget, or
    // from memory - comes back as the error with its message and traceback;
    // the stack is restored to its height before the call. The work returns
    // nothing; it passes results out through its captures. A C++ exception
    // from the work becomes an error too.
    template <typename Work>
    [[nodiscard]] std::optional<core::Error> protect(Work&& work)
    {
        struct Call {
            std::remove_reference_t<Work>* work;
            std::string failure;
        };
        Call call{&work, {}};
        return runProtected(
            [](lua_State* state) -> int {
                auto* current = static_cast<Call*>(lua_touserdata(state, 1));
                lua_settop(state, 0);
                bool failed = false;
                try {
                    (*current->work)(state);
                } catch (const std::exception& exception) {
                    // Lua's own errors are not std::exceptions and pass on.
                    current->failure = exception.what();
                    failed = true;
                }
                if (failed) {
                    return raiseError(state, "internal error: " + current->failure);
                }
                return 0;
            },
            &call);
    }

    // Raises a Lua error with this message; does not return (the int is
    // for `return raiseError(...)` in C functions, the Lua idiom).
    static int raiseError(lua_State* state, std::string_view message);

    // Pushes a new environment table with the sandboxed library (ADR 0024):
    // assert, error, getmetatable, ipairs, next, pairs, pcall, rawequal,
    // rawget, rawlen, rawset, select, setmetatable, tonumber, tostring, type,
    // xpcall, _VERSION, _G (the table itself), and copies of string, table,
    // math and utf8. No print (the caller adds its own), collectgarbage,
    // dofile, load, loadfile, require, warn, coroutine, io, os, package or
    // debug; no string.dump. Call inside protect().
    static void pushSandbox(lua_State* state);

    // Compiles text (never a binary chunk) and pushes the function; chunkName
    // is how messages name it ("scripts/door.lua:3: ..."). Raises the syntax
    // error. Call inside protect().
    static void loadText(lua_State* state, std::string_view source, std::string_view chunkName);

private:
    LuaVm(std::unique_ptr<VmAccounting> accounting, lua_State* state) noexcept;

    [[nodiscard]] std::optional<core::Error> runProtected(lua_CFunction trampoline, void* call);

    std::unique_ptr<VmAccounting> accounting_;
    lua_State* state_ = nullptr;
};

} // namespace engine::scripting::lua
