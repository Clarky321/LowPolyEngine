// The renderer's handle storage (internal header, no GPU needed).
#include "render/HandlePool.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>

using engine::render::detail::HandlePool;
using engine::render::detail::PoolHandle;

namespace {

// Copy of the value behind a handle, or empty if the handle is invalid. Never
// dereferences a null result (GCC's -Wnull-dereference checks that at -O3).
std::optional<std::string> valueOf(const HandlePool<std::string>& pool, PoolHandle handle)
{
    const std::string* value = pool.find(handle);
    return value != nullptr ? std::optional<std::string>(*value) : std::nullopt;
}

} // namespace

TEST_CASE("Inserted values are found by their handle", "[render][handles]")
{
    HandlePool<std::string> pool;
    const PoolHandle a = pool.insert("a");
    const PoolHandle b = pool.insert("b");
    CHECK(valueOf(pool, a) == "a");
    CHECK(valueOf(pool, b) == "b");
    CHECK(pool.size() == 2);
}

TEST_CASE("A default handle is never valid", "[render][handles]")
{
    HandlePool<int> pool;
    (void)pool.insert(1);
    CHECK(pool.find(PoolHandle{}) == nullptr);
    CHECK_FALSE(pool.erase(PoolHandle{}));
}

TEST_CASE("Erased handles stay invalid when the slot is reused", "[render][handles]")
{
    HandlePool<std::string> pool;
    const PoolHandle old = pool.insert("old");
    CHECK(pool.erase(old));
    CHECK(pool.find(old) == nullptr);
    CHECK_FALSE(pool.erase(old));
    CHECK(pool.size() == 0);

    const PoolHandle reused = pool.insert("new");
    CHECK(reused.index == old.index);
    CHECK(reused.generation != old.generation);
    CHECK(pool.find(old) == nullptr);
    CHECK(valueOf(pool, reused) == "new");
}

TEST_CASE("Out-of-range handles are rejected", "[render][handles]")
{
    HandlePool<int> pool;
    (void)pool.insert(1);
    CHECK(pool.find(PoolHandle{42, 1}) == nullptr);
    CHECK_FALSE(pool.erase(PoolHandle{42, 1}));
}

TEST_CASE("Erasing destroys the value", "[render][handles]")
{
    auto shared = std::make_shared<int>(5);
    HandlePool<std::shared_ptr<int>> pool;
    const PoolHandle handle = pool.insert(shared);
    CHECK(shared.use_count() == 2);
    CHECK(pool.erase(handle));
    CHECK(shared.use_count() == 1);
}
