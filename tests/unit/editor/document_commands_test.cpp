// The editor's commands (M5.5, ADR 0029), headless: each applies, undoes and
// redoes with the scene document and the world settings byte for byte as
// before and after; a deleted parent comes back with its children's links;
// IDs never change through undo; a refused command changes nothing.

#include "DocumentTestSupport.hpp"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

using editor::AddComponent;
using editor::ComponentType;
using editor::CreateEntity;
using editor::DeleteEntity;
using editor::EditorDocument;
using editor::RemoveComponent;
using editor::RenameEntity;
using editor::ReplaceComponent;
using editor::SetParent;
using editor::SetWorldSettings;
using engine::scene::EntityId;
using engine::scene::ParentComponent;
using lpe_test::at;
using lpe_test::create;
using lpe_test::DocumentTexts;
using lpe_test::memoryProject;
using lpe_test::name;
using lpe_test::textsOf;

namespace {

// Executes the command, then checks: the content changed; undo gives the
// texts before byte for byte, redo those after; undo and redo again too.
void checkUndoRedo(EditorDocument& document, std::unique_ptr<editor::Command> command,
                   const std::string& expectedName)
{
    const DocumentTexts before = textsOf(document);
    const std::size_t steps = document.undoCount();
    const auto error = document.execute(std::move(command));
    INFO((error ? error->message : std::string()));
    REQUIRE_FALSE(error.has_value());
    const DocumentTexts after = textsOf(document);
    CHECK(after != before);
    CHECK(document.undoCount() == steps + 1);
    CHECK(document.undoName() == expectedName);
    for (int round = 0; round < 2; ++round) {
        REQUIRE_FALSE(document.undo().has_value());
        CHECK(textsOf(document) == before);
        CHECK(document.redoName() == expectedName);
        REQUIRE_FALSE(document.redo().has_value());
        CHECK(textsOf(document) == after);
    }
}

// The command must fail and change nothing: content, history, redo steps.
std::string checkRefused(EditorDocument& document, std::unique_ptr<editor::Command> command)
{
    const DocumentTexts before = textsOf(document);
    const std::size_t undo = document.undoCount();
    const std::size_t redo = document.redoCount();
    const bool dirty = document.isDirty();
    const auto error = document.execute(std::move(command));
    REQUIRE(error.has_value());
    CHECK(textsOf(document) == before);
    CHECK(document.undoCount() == undo);
    CHECK(document.redoCount() == redo);
    CHECK(document.isDirty() == dirty);
    return error->message;
}

bool contains(const std::string& text, std::string_view part)
{
    return text.find(part) != std::string::npos;
}

engine::scene::StaticMeshComponent mesh(engine::assets::AssetId meshAsset,
                                        engine::assets::AssetId material = {})
{
    return engine::scene::StaticMeshComponent{meshAsset, material};
}

} // namespace

TEST_CASE("Creating an entity, with and without components, undoes and redoes exactly",
          "[editor][document]")
{
    EditorDocument document(memoryProject());
    const EntityId parent = create(document, {name("Parent")});

    checkUndoRedo(document, std::make_unique<CreateEntity>(), "Create entity");

    engine::scene::DirectionalLightComponent light;
    light.direction = engine::math::Vec3{0.0F, -2.0F, 1.0F};
    auto full = std::make_unique<CreateEntity>(std::vector<editor::ComponentValue>{
        name("Tower"), at(1.0F, 2.0F, 3.0F), mesh(lpe_test::kMesh, lpe_test::kMaterial),
        engine::scene::CameraComponent{}, light,
        engine::scene::ScriptComponent{lpe_test::kScript, true}, ParentComponent{parent},
        engine::scene::PrefabInstanceComponent{lpe_test::kPrefab,
                                               engine::scene::PrefabInstanceId{engine::core::Uuid{
                                                   0x9000000000004000ULL, 0x8000000000000009ULL}},
                                               1}});
    const CreateEntity* raw = full.get();
    checkUndoRedo(document, std::move(full), "Create entity");
    const EntityId tower = raw->entity();
    REQUIRE(document.scene().contains(tower));
    CHECK(editor::componentsOf(document.scene(), tower).size() == 8U);
    CHECK(document.scene().component<ParentComponent>(tower).value().entity == parent);
}

TEST_CASE("Deleting an entity undoes with its ID, place and components", "[editor][document]")
{
    EditorDocument document(memoryProject());
    const EntityId first = create(document, {name("First")});
    const EntityId middle =
        create(document, {name("Middle"), at(5.0F), mesh(lpe_test::kMesh), ParentComponent{first}});
    const EntityId last = create(document, {name("Last")});

    checkUndoRedo(document, std::make_unique<DeleteEntity>(middle), "Delete entity");
    REQUIRE_FALSE(document.scene().contains(middle));
    REQUIRE_FALSE(document.undo().has_value());
    // Its place: between the entities created before and after it.
    CHECK(document.scene().entityIds() == std::vector<EntityId>{first, middle, last});
    CHECK(document.scene().component<ParentComponent>(middle).value().entity == first);
}

TEST_CASE("Deleting a parent and undoing it restores its children's links", "[editor][document]")
{
    EditorDocument document(memoryProject());
    const EntityId grand = create(document, {name("Grand")});
    const EntityId parent = create(document, {name("Parent"), ParentComponent{grand}});
    const EntityId child1 = create(document, {name("Child 1"), ParentComponent{parent}});
    const EntityId other = create(document, {name("Other")});
    const EntityId child2 = create(document, {name("Child 2"), ParentComponent{parent}});
    const EntityId grandchild = create(document, {name("Grandchild"), ParentComponent{child1}});
    const DocumentTexts before = textsOf(document);

    REQUIRE_FALSE(document.execute(std::make_unique<DeleteEntity>(parent)).has_value());
    // The children stay, without their link; the grandchild keeps its own.
    CHECK(document.scene().contains(child1));
    CHECK(document.scene().contains(child2));
    CHECK_FALSE(document.scene().hasComponent<ParentComponent>(child1));
    CHECK_FALSE(document.scene().hasComponent<ParentComponent>(child2));
    CHECK(document.scene().component<ParentComponent>(grandchild).value().entity == child1);
    const DocumentTexts after = textsOf(document);

    REQUIRE_FALSE(document.undo().has_value());
    CHECK(textsOf(document) == before);
    CHECK(document.scene().component<ParentComponent>(child1).value().entity == parent);
    CHECK(document.scene().component<ParentComponent>(child2).value().entity == parent);
    CHECK(document.scene().component<ParentComponent>(parent).value().entity == grand);
    CHECK(document.scene().entityIds() ==
          std::vector<EntityId>{grand, parent, child1, other, child2, grandchild});
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(textsOf(document) == after);
}

TEST_CASE("Renaming, components and parents undo and redo exactly", "[editor][document]")
{
    EditorDocument document(memoryProject());
    const EntityId a = create(document);
    const EntityId b = create(document, {name("B"), at(1.0F)});

    SECTION("Rename")
    {
        checkUndoRedo(document, std::make_unique<RenameEntity>(a, "First name"), "Rename entity");
        checkUndoRedo(document, std::make_unique<RenameEntity>(a, "Второе имя"), "Rename entity");
    }
    SECTION("Add, replace and remove a component")
    {
        checkUndoRedo(document, std::make_unique<AddComponent>(a, mesh(lpe_test::kMesh)),
                      "Add static mesh");
        checkUndoRedo(
            document,
            std::make_unique<ReplaceComponent>(a, mesh(lpe_test::kOtherMesh, lpe_test::kMaterial)),
            "Change static mesh");
        checkUndoRedo(document, std::make_unique<ReplaceComponent>(b, at(-4.0F, 2.5F)),
                      "Change transform");
        checkUndoRedo(document, std::make_unique<RemoveComponent>(b, ComponentType::Transform),
                      "Remove transform");
        checkUndoRedo(document, std::make_unique<RemoveComponent>(b, ComponentType::Name),
                      "Remove name");
    }
    SECTION("Set, change and clear a parent")
    {
        const EntityId c = create(document);
        checkUndoRedo(document, std::make_unique<SetParent>(a, b), "Set parent");
        checkUndoRedo(document, std::make_unique<SetParent>(a, c), "Set parent");
        checkUndoRedo(document, std::make_unique<SetParent>(a, std::nullopt), "Clear parent");
    }
    SECTION("World settings")
    {
        engine::worldgen::WorldGenSettings settings = document.worldSettings();
        settings.seed = 42;
        settings.islands.count = 7;
        checkUndoRedo(document, std::make_unique<SetWorldSettings>(settings),
                      "Change world settings");
        CHECK(document.worldSettings().seed == 42U);
    }
}

TEST_CASE("Entity IDs never change through undo and redo", "[editor][document]")
{
    EditorDocument document(memoryProject());
    const EntityId kept = create(document, {name("Kept")});
    const EntityId made = create(document, {name("Made")});
    REQUIRE_FALSE(document.execute(std::make_unique<DeleteEntity>(kept)).has_value());

    for (int round = 0; round < 3; ++round) {
        // Undo the delete, then the create; redo both.
        REQUIRE_FALSE(document.undo().has_value());
        REQUIRE_FALSE(document.undo().has_value());
        CHECK(document.scene().entityIds() == std::vector<EntityId>{kept});
        REQUIRE_FALSE(document.redo().has_value());
        CHECK(document.scene().entityIds() == std::vector<EntityId>{kept, made});
        REQUIRE_FALSE(document.redo().has_value());
        CHECK(document.scene().entityIds() == std::vector<EntityId>{made});
    }
    // A new entity after undoing gets a new ID, not one of the old ones.
    REQUIRE_FALSE(document.undo().has_value());
    REQUIRE_FALSE(document.undo().has_value());
    const EntityId fresh = create(document);
    CHECK(fresh != made);
    CHECK(fresh != kept);
}

TEST_CASE("A refused command changes nothing and says why", "[editor][document]")
{
    EditorDocument document(memoryProject());
    const EntityId a = create(document, {name("A")});
    const EntityId b = create(document, {name("B"), ParentComponent{a}});
    const EntityId missing{engine::core::Uuid{0x7777777777774777ULL, 0x8777777777777777ULL}};
    // A redo step that a refused command must not drop.
    const EntityId undone = create(document);
    REQUIRE_FALSE(document.undo().has_value());
    REQUIRE(document.redoCount() == 1U);
    CHECK_FALSE(document.scene().contains(undone));

    CHECK(contains(
        checkRefused(document, std::make_unique<CreateEntity>(
                                   std::vector<editor::ComponentValue>{name("x"), name("y")})),
        "two name components"));
    CHECK(contains(
        checkRefused(document, std::make_unique<CreateEntity>(std::vector<editor::ComponentValue>{
                                   mesh(lpe_test::kUnknownAsset)})),
        "static mesh: mesh"));
    CHECK(contains(
        checkRefused(document, std::make_unique<CreateEntity>(
                                   std::vector<editor::ComponentValue>{mesh(lpe_test::kScript)})),
        "static mesh: mesh"));
    CHECK(contains(
        checkRefused(document, std::make_unique<CreateEntity>(
                                   std::vector<editor::ComponentValue>{ParentComponent{missing}})),
        "parent"));
    CHECK(contains(checkRefused(document, std::make_unique<CreateEntity>(
                                              std::vector<editor::ComponentValue>{name("")})),
                   "name"));
    (void)checkRefused(document, std::make_unique<DeleteEntity>(missing));
    (void)checkRefused(document, std::make_unique<RenameEntity>(missing, "x"));
    (void)checkRefused(document, std::make_unique<RenameEntity>(a, std::string(300, 'x')));
    // A cycle, an unknown parent, clearing without a parent.
    (void)checkRefused(document, std::make_unique<SetParent>(a, b));
    (void)checkRefused(document, std::make_unique<SetParent>(a, a));
    (void)checkRefused(document, std::make_unique<SetParent>(a, missing));
    (void)checkRefused(document, std::make_unique<SetParent>(a, std::nullopt));
    (void)checkRefused(document, std::make_unique<AddComponent>(a, name("Again")));
    (void)checkRefused(document, std::make_unique<AddComponent>(
                                     a, engine::scene::ScriptComponent{lpe_test::kMesh, true}));
    (void)checkRefused(document, std::make_unique<ReplaceComponent>(a, at(1.0F)));
    (void)checkRefused(document, std::make_unique<ReplaceComponent>(b, ParentComponent{b}));
    (void)checkRefused(document, std::make_unique<RemoveComponent>(a, ComponentType::Camera));
    engine::worldgen::WorldGenSettings invalid;
    invalid.islands.count = -1;
    CHECK(contains(checkRefused(document, std::make_unique<SetWorldSettings>(invalid)),
                   "world settings: "));
    CHECK(contains(checkRefused(document, nullptr), "no command"));

    // The redo step survived every refusal.
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(document.scene().contains(undone));
}

TEST_CASE("Undo and redo with nothing to take report it", "[editor][document]")
{
    EditorDocument document(memoryProject());
    CHECK_FALSE(document.canUndo());
    CHECK_FALSE(document.canRedo());
    CHECK_FALSE(document.undoName().has_value());
    CHECK_FALSE(document.redoName().has_value());
    REQUIRE(document.undo().has_value());
    CHECK(document.undo()->message == "nothing to undo");
    CHECK(document.redo()->message == "nothing to redo");
    CHECK_FALSE(document.isDirty());
}
