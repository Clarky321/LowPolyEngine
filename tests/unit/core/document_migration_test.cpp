// The migration hook of versioned documents (ADR 0004 rule 5), tested with a
// test-only document type and versions: no real format has migrations yet.

#include "core/DocumentMigration.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

using engine::core::DocumentMigration;
using engine::core::Error;
using engine::core::migrateDocument;

namespace {

struct TestDocument {
    int version = 1;
    std::vector<std::string> steps;
};

std::optional<Error> oneToTwo(TestDocument& document)
{
    document.steps.emplace_back("1->2");
    document.version = 2;
    return std::nullopt;
}

std::optional<Error> twoToThree(TestDocument& document)
{
    document.steps.emplace_back("2->3");
    document.version = 3;
    return std::nullopt;
}

std::optional<Error> failing(TestDocument& /*document*/)
{
    return Error{"field 'colour' has no new name"};
}

constexpr std::array<DocumentMigration<TestDocument>, 2> kMigrations{oneToTwo, twoToThree};

std::optional<Error> migrate(TestDocument& document, int version, int current,
                             std::span<const DocumentMigration<TestDocument>> migrations)
{
    return migrateDocument(document, version, current, migrations);
}

} // namespace

TEST_CASE("Migrations run in order from the document's version", "[core][migration]")
{
    TestDocument fromOne;
    REQUIRE_FALSE(migrate(fromOne, 1, 3, kMigrations).has_value());
    CHECK(fromOne.steps == std::vector<std::string>{"1->2", "2->3"});
    CHECK(fromOne.version == 3);

    TestDocument fromTwo{2, {}};
    REQUIRE_FALSE(migrate(fromTwo, 2, 3, kMigrations).has_value());
    CHECK(fromTwo.steps == std::vector<std::string>{"2->3"});

    // The current version needs nothing, even with no migrations at all.
    TestDocument current{3, {}};
    REQUIRE_FALSE(migrate(current, 3, 3, kMigrations).has_value());
    CHECK(current.steps.empty());
    TestDocument first;
    REQUIRE_FALSE(migrate(first, 1, 1, {}).has_value());
}

TEST_CASE("Migrations fail for versions out of range and missing steps", "[core][migration]")
{
    TestDocument document;
    const auto zero = migrate(document, 0, 3, kMigrations);
    REQUIRE(zero.has_value());
    CHECK(zero->message == "format version 0 cannot be brought to version 3");
    const auto newer = migrate(document, 4, 3, kMigrations);
    REQUIRE(newer.has_value());
    CHECK(newer->message == "format version 4 cannot be brought to version 3");

    const auto missing = migrate(document, 1, 4, kMigrations);
    REQUIRE(missing.has_value());
    CHECK(missing->message == "no migration from format version 3 to 4");
    // The steps before the missing one ran on the caller's copy; loaders
    // discard a document whose migration failed.
    CHECK(document.steps == std::vector<std::string>{"1->2", "2->3"});

    const std::array<DocumentMigration<TestDocument>, 2> gap{oneToTwo, nullptr};
    TestDocument other;
    const auto hole = migrate(other, 1, 3, gap);
    REQUIRE(hole.has_value());
    CHECK(hole->message == "no migration from format version 2 to 3");
}

TEST_CASE("A failing migration names its versions", "[core][migration]")
{
    const std::array<DocumentMigration<TestDocument>, 2> migrations{oneToTwo, failing};
    TestDocument document;
    const auto error = migrate(document, 1, 3, migrations);
    REQUIRE(error.has_value());
    CHECK(error->message == "migration from format version 2 to 3: field 'colour' has no new name");
}
