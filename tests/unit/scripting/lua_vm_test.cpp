// The internal Lua VM of the script runtime (engine/src/scripting/lua/):
// memory accounting, the instruction budget, the sandbox and text-only
// loading. The tests link Lua directly for this; public headers never
// include it (tests/unit/scene/public_headers_test.cpp).

#include "scripting/lua/LuaVm.hpp"

#include <catch2/catch_test_macros.hpp>

#include <lauxlib.h>

#include <stdexcept>
#include <string>
#include <string_view>

using engine::scripting::lua::LuaVm;
using engine::scripting::lua::VmLimits;

namespace {

constexpr std::size_t kMiB = 1024U * 1024U;

LuaVm makeVm(std::size_t memory = 4 * kMiB, std::uint64_t budget = 1'000'000)
{
    auto vm = LuaVm::create(VmLimits{memory, budget});
    INFO((vm ? std::string{} : vm.error().message));
    REQUIRE(vm);
    return std::move(vm).value();
}

// Runs a chunk in a fresh sandbox environment; its single result as text.
std::optional<engine::core::Error> runSandboxed(LuaVm& vm, std::string_view source,
                                                std::string* result = nullptr)
{
    return vm.protect([&](lua_State* state) {
        LuaVm::loadText(state, source, "test.lua");
        LuaVm::pushSandbox(state);
        (void)lua_setupvalue(state, -2, 1);
        lua_call(state, 0, 1);
        if (result != nullptr) {
            std::size_t length = 0;
            const char* text = luaL_tolstring(state, -1, &length);
            result->assign(text, length);
        }
    });
}

std::string errorOf(const std::optional<engine::core::Error>& error)
{
    REQUIRE(error.has_value());
    return error->message;
}

} // namespace

TEST_CASE("A Lua VM returns all its memory when it is closed", "[scripting][vm]")
{
    LuaVm vm = makeVm();
    CHECK(vm.isOpen());
    const std::size_t fresh = vm.memoryInUse();
    CHECK(fresh > 0);
    CHECK(vm.memoryLimit() == 4 * kMiB);
    std::string result;
    REQUIRE_FALSE(runSandboxed(vm,
                               "local t = {} for i = 1, 10000 do t[i] = tostring(i) end "
                               "return #t",
                               &result)
                      .has_value());
    CHECK(result == "10000");
    vm.close();
    CHECK_FALSE(vm.isOpen());
    CHECK(vm.memoryInUse() == 0);
    CHECK(errorOf(runSandboxed(vm, "return 1")) == "the Lua VM is closed");
    vm.close(); // Twice is fine.
}

TEST_CASE("A Lua VM needs room for itself", "[scripting][vm]")
{
    const auto tooSmall = LuaVm::create(VmLimits{512, 1000});
    REQUIRE_FALSE(tooSmall);
    CHECK(tooSmall.error().message.find("memory") != std::string::npos);
}

TEST_CASE("The instruction budget stops endless loops", "[scripting][vm]")
{
    LuaVm vm = makeVm(4 * kMiB, 50'000);
    CHECK(errorOf(runSandboxed(vm, "while true do end"))
              .starts_with("instruction budget exceeded: more than 50000 instructions in one "
                           "call"));
    // Catching the error does not help: from then on every instruction raises it.
    const std::string caught =
        errorOf(runSandboxed(vm, "while true do pcall(function() while true do end end) end"));
    CHECK(caught.find("instruction budget exceeded") != std::string::npos);
    // Each call starts with a fresh budget.
    std::string result;
    REQUIRE_FALSE(runSandboxed(vm, "local n = 0 for i = 1, 1000 do n = n + i end return n", &result)
                      .has_value());
    CHECK(result == "500500");
}

TEST_CASE("The memory limit stops scripts that allocate without end", "[scripting][vm]")
{
    LuaVm vm = makeVm(2 * kMiB, 100'000'000);
    const std::size_t before = vm.memoryInUse();
    CHECK(errorOf(runSandboxed(vm, "local t = {} for i = 1, 1e9 do t[i] = {i} end")) ==
          "not enough memory");
    CHECK(errorOf(runSandboxed(vm, "return string.rep('x', 64 * 1024 * 1024)")) ==
          "not enough memory");
    CHECK(vm.memoryInUse() <= 2 * kMiB);
    // The VM goes on working, and the garbage is collected.
    std::string result;
    REQUIRE_FALSE(runSandboxed(vm, "return 6 * 7", &result).has_value());
    CHECK(result == "42");
    REQUIRE_FALSE(
        vm.protect([](lua_State* state) { (void)lua_gc(state, LUA_GCCOLLECT); }).has_value());
    CHECK(vm.memoryInUse() < before + 64U * 1024U);
}

TEST_CASE("The sandbox holds exactly the documented library", "[scripting][vm]")
{
    LuaVm vm = makeVm();
    std::string present;
    REQUIRE_FALSE(runSandboxed(vm, R"(
        local names = {}
        for name in pairs(_G) do names[#names + 1] = name end
        table.sort(names)
        return table.concat(names, " ")
    )",
                               &present)
                      .has_value());
    CHECK(present == "_G _VERSION assert error getmetatable ipairs math next pairs pcall rawequal "
                     "rawget rawlen rawset select setmetatable string table tonumber tostring "
                     "type utf8 xpcall");

    std::string checks;
    REQUIRE_FALSE(runSandboxed(vm, R"(
        local found = {}
        if string.dump ~= nil then found[#found + 1] = "string.dump" end
        if ("").dump ~= nil then found[#found + 1] = "(''):dump" end
        if getmetatable("") ~= false then found[#found + 1] = "string metatable" end
        if not pcall(setmetatable, {}, {}) then found[#found + 1] = "setmetatable" end
        if _VERSION ~= "Lua 5.5" then found[#found + 1] = _VERSION end
        return table.concat(found, ",")
    )",
                               &checks)
                      .has_value());
    CHECK(checks.empty());
    CHECK(errorOf(runSandboxed(vm, "setmetatable('', {})"))
              .find("bad argument #1 to 'setmetatable' (table expected, got string)") !=
          std::string::npos);
}

TEST_CASE("Sandbox environments do not share their tables", "[scripting][vm]")
{
    LuaVm vm = makeVm();
    REQUIRE_FALSE(
        runSandboxed(vm, "string.upper = nil math.pi = 3 shared = 1 return true").has_value());
    std::string result;
    REQUIRE_FALSE(runSandboxed(vm,
                               "return tostring(shared) .. ' ' .. string.upper('a') .. ' ' .. "
                               "tostring(math.pi > 3.14)",
                               &result)
                      .has_value());
    CHECK(result == "nil A true");
    // String methods use the VM's own string table, which no script reaches.
    REQUIRE_FALSE(runSandboxed(vm, "return ('b'):upper()", &result).has_value());
    CHECK(result == "B");
}

TEST_CASE("Only text chunks load", "[scripting][vm]")
{
    LuaVm vm = makeVm();
    const std::string bytecode = std::string("\x1bLua") + std::string(40, '\0');
    CHECK(
        errorOf(runSandboxed(vm, bytecode)).find("attempt to load a binary chunk (mode is 't')") !=
        std::string::npos);
    CHECK(errorOf(runSandboxed(vm, "return {")).starts_with("test.lua:1: "));
    CHECK(errorOf(runSandboxed(vm, "\n\nerror('broken')"))
              .starts_with("test.lua:3: broken\nstack traceback:"));
    CHECK(errorOf(runSandboxed(vm, "error({})")).starts_with("(error object is a table value)"));
}

TEST_CASE("A C++ exception in protected work becomes an error", "[scripting][vm]")
{
    LuaVm vm = makeVm();
    CHECK(errorOf(vm.protect([](lua_State*) {
              throw std::runtime_error("boom");
          })).starts_with("internal error: boom"));
    std::string result;
    REQUIRE_FALSE(runSandboxed(vm, "return 'still fine'", &result).has_value());
    CHECK(result == "still fine");
}
