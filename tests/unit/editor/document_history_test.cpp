// The editor document's history (M5.5, ADR 0029), headless: merged continuous
// edits, the size limit, the dirty state against the last save (with a real
// save), the selection across deletes and undos, and 10 000 commands undone
// and redone byte for byte.

#include "DocumentTestSupport.hpp"

#include "engine/project/Project.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
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
using engine::scene::TransformComponent;
using lpe_test::at;
using lpe_test::create;
using lpe_test::DocumentTexts;
using lpe_test::memoryProject;
using lpe_test::name;
using lpe_test::textsOf;

namespace {

void drag(EditorDocument& document, EntityId entity, float x, editor::MergeKey key)
{
    REQUIRE_FALSE(
        document.execute(std::make_unique<ReplaceComponent>(entity, at(x)), key).has_value());
}

float xOf(const EditorDocument& document, EntityId entity)
{
    return document.scene().component<TransformComponent>(entity).value().position.x;
}

} // namespace

TEST_CASE("A continuous edit is one undo step", "[editor][document][history]")
{
    EditorDocument document(memoryProject());
    const EntityId a = create(document, {name("A"), at(0.0F)});
    const EntityId b = create(document, {name("B"), at(0.0F)});
    const std::size_t base = document.undoCount();
    const DocumentTexts before = textsOf(document);

    // A drag of A's transform: five commands, one step.
    for (int i = 1; i <= 5; ++i) {
        drag(document, a, static_cast<float>(i), 7);
    }
    CHECK(document.undoCount() == base + 1);
    const DocumentTexts dragged = textsOf(document);
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(textsOf(document) == before);
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(textsOf(document) == dragged);
    CHECK(xOf(document, a) == 5.0F);

    SECTION("Another key, no key, another entity or another command: new steps")
    {
        drag(document, a, 6.0F, 8);
        drag(document, a, 7.0F, editor::kNoMerge);
        drag(document, a, 8.0F, editor::kNoMerge);
        drag(document, b, 1.0F, 8);
        REQUIRE_FALSE(document.execute(std::make_unique<RenameEntity>(b, "B2"), 8).has_value());
        CHECK(document.undoCount() == base + 6);
    }
    SECTION("Typing a name and dragging a world value merge too")
    {
        for (const char* text : {"N", "Ne", "New"}) {
            REQUIRE_FALSE(document.execute(std::make_unique<RenameEntity>(a, text), 9).has_value());
        }
        engine::worldgen::WorldGenSettings settings = document.worldSettings();
        for (std::uint64_t seed = 2; seed < 6; ++seed) {
            settings.seed = seed;
            REQUIRE_FALSE(
                document.execute(std::make_unique<SetWorldSettings>(settings), 10).has_value());
        }
        CHECK(document.undoCount() == base + 3);
        REQUIRE_FALSE(document.undo().has_value());
        CHECK(document.worldSettings().seed == 1U);
        REQUIRE_FALSE(document.undo().has_value());
        CHECK(document.scene().component<engine::scene::NameComponent>(a).value().value == "A");
    }
    SECTION("Commands that add or remove do not merge")
    {
        REQUIRE_FALSE(
            document
                .execute(std::make_unique<AddComponent>(a, engine::scene::CameraComponent{}), 11)
                .has_value());
        REQUIRE_FALSE(
            document.execute(std::make_unique<RemoveComponent>(a, ComponentType::Camera), 11)
                .has_value());
        CHECK(document.undoCount() == base + 3);
    }
    SECTION("Not across a save, nor after an undo")
    {
        document.markSaved();
        drag(document, a, 6.0F, 7);
        CHECK(document.undoCount() == base + 2);
        // Undo the last step: the same key does not merge into the one before.
        REQUIRE_FALSE(document.undo().has_value());
        drag(document, a, 9.0F, 7);
        CHECK(document.undoCount() == base + 2);
        CHECK(document.redoCount() == 0U);
        REQUIRE_FALSE(document.undo().has_value());
        CHECK(xOf(document, a) == 5.0F);
        CHECK_FALSE(document.isDirty());
    }
}

TEST_CASE("The history keeps at most its limit of steps", "[editor][document][history]")
{
    EditorDocument document(memoryProject(), 3);
    CHECK(document.historyLimit() == 3U);
    std::vector<DocumentTexts> states{textsOf(document)};
    for (int i = 0; i < 5; ++i) {
        (void)create(document, {name("E" + std::to_string(i))});
        states.push_back(textsOf(document));
    }
    CHECK(document.undoCount() == 3U);
    for (int i = 0; i < 3; ++i) {
        REQUIRE_FALSE(document.undo().has_value());
    }
    CHECK_FALSE(document.canUndo());
    CHECK(textsOf(document) == states[2]);
    // The opened (saved) state was dropped with the oldest steps.
    CHECK(document.isDirty());
    for (int i = 0; i < 3; ++i) {
        REQUIRE_FALSE(document.redo().has_value());
    }
    CHECK(textsOf(document) == states[5]);

    EditorDocument tiny(memoryProject(), 0);
    CHECK(tiny.historyLimit() == 1U);
}

TEST_CASE("The dirty state follows the last save through undo and redo",
          "[editor][document][history]")
{
    EditorDocument document(memoryProject());
    CHECK_FALSE(document.isDirty());
    const EntityId a = create(document, {name("A")});
    CHECK(document.isDirty());
    REQUIRE_FALSE(document.undo().has_value());
    CHECK_FALSE(document.isDirty());
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(document.isDirty());

    document.markSaved();
    CHECK_FALSE(document.isDirty());
    REQUIRE_FALSE(document.execute(std::make_unique<RenameEntity>(a, "B")).has_value());
    CHECK(document.isDirty());
    REQUIRE_FALSE(document.undo().has_value());
    CHECK_FALSE(document.isDirty());
    // Undo past the save, then redo to it.
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.isDirty());
    REQUIRE_FALSE(document.redo().has_value());
    CHECK_FALSE(document.isDirty());

    // Undo past the save and change something else: the saved state is no
    // longer in the history, so no undo or redo makes the document clean.
    REQUIRE_FALSE(document.undo().has_value());
    (void)create(document, {name("Other")});
    CHECK(document.isDirty());
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.isDirty());
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(document.isDirty());
}

TEST_CASE("Saving the document writes its content and makes it clean",
          "[editor][document][history]")
{
    const lpe_test::TempProject folder("lpe_editor_document_save");
    auto created =
        engine::project::createProject(folder.root(), "Saved", engine::core::UuidGenerator{1});
    REQUIRE(created.hasValue());
    EditorDocument document(std::move(created).value());
    const EntityId tower = create(document, {name("Tower"), at(3.0F)});
    engine::worldgen::WorldGenSettings settings = document.worldSettings();
    settings.seed = 77;
    REQUIRE_FALSE(document.execute(std::make_unique<SetWorldSettings>(settings)).has_value());
    CHECK(document.isDirty());

    const auto saved = document.save();
    REQUIRE(saved.hasValue());
    CHECK_FALSE(document.isDirty());
    const auto opened = engine::project::openProject(folder.root(), engine::core::UuidGenerator{2});
    REQUIRE(opened.hasValue());
    CHECK(engine::scene::sceneToJson(opened.value().project.startScene) == textsOf(document).scene);
    CHECK(opened.value().project.worldSettings.seed == 77U);
    CHECK(opened.value().project.startScene.contains(tower));

    // Undo after the save: dirty; the files still hold the saved state.
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.isDirty());
    REQUIRE_FALSE(document.redo().has_value());
    CHECK_FALSE(document.isDirty());
}

TEST_CASE("The document's version changes with its content and with nothing else",
          "[editor][document][history]")
{
    const lpe_test::TempProject folder("lpe_editor_document_version");
    auto created =
        engine::project::createProject(folder.root(), "Versions", engine::core::UuidGenerator{1});
    REQUIRE(created.hasValue());
    EditorDocument document(std::move(created).value());
    CHECK(document.version() == 0);

    // Every applied command, undo and redo: a new number.
    std::vector<std::uint64_t> seen{document.version()};
    const auto changed = [&] {
        CHECK(std::find(seen.begin(), seen.end(), document.version()) == seen.end());
        seen.push_back(document.version());
    };
    const EntityId tower = create(document, {name("Tower"), at(0.0F)});
    changed();
    drag(document, tower, 1.0F, 7U);
    changed();
    // Merged into the last step: still a change of the content.
    drag(document, tower, 2.0F, 7U);
    changed();
    CHECK(document.undoCount() == 2);
    REQUIRE_FALSE(document.undo().has_value());
    changed();
    REQUIRE_FALSE(document.redo().has_value());
    changed();

    // Nothing that leaves the content as it was changes it.
    const std::uint64_t before = document.version();
    CHECK(document.execute(std::make_unique<RenameEntity>(EntityId{}, "Nobody")).has_value());
    CHECK(document.execute(nullptr).has_value());
    CHECK(document.redo().has_value());
    document.selection().select(tower);
    document.selection().clear();
    REQUIRE(document.save().hasValue());
    document.markSaved();
    CHECK(document.version() == before);
    REQUIRE_FALSE(document.undo().has_value());
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.undo().has_value());
    CHECK(document.version() == before + 2);
}

TEST_CASE("The selection stays valid across deletes, undo and redo",
          "[editor][document][selection]")
{
    EditorDocument document(memoryProject());
    const EntityId a = create(document, {name("A")});
    const EntityId b = create(document, {name("B")});
    const EntityId c = create(document, {name("C")});
    document.selection().select(a);
    document.selection().add(b);

    // A selected entity deleted leaves the selection; undo selects it again.
    REQUIRE_FALSE(document.execute(std::make_unique<DeleteEntity>(a)).has_value());
    CHECK(document.selection().ids() == std::vector<EntityId>{b});
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.selection().contains(a));
    CHECK(document.selection().contains(b));
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(document.selection().ids() == std::vector<EntityId>{b});

    // An entity not selected when deleted is not selected by the undo.
    REQUIRE_FALSE(document.execute(std::make_unique<DeleteEntity>(c)).has_value());
    REQUIRE_FALSE(document.undo().has_value());
    CHECK_FALSE(document.selection().contains(c));

    // Undoing a create removes the entity from the selection; redo brings it back.
    const EntityId d = create(document, {name("D")});
    document.selection().select(d);
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.selection().empty());
    REQUIRE_FALSE(document.redo().has_value());
    CHECK(document.selection().ids() == std::vector<EntityId>{d});

    // Changes that remove no entity leave the user's selection as it is.
    document.selection().select(b);
    REQUIRE_FALSE(document.execute(std::make_unique<RenameEntity>(d, "D2")).has_value());
    REQUIRE_FALSE(document.undo().has_value());
    CHECK(document.selection().ids() == std::vector<EntityId>{b});
    CHECK(document.selection().primary() == b);
}

TEST_CASE("10 000 commands undo and redo byte for byte", "[editor][document][history]")
{
    constexpr std::size_t kCommands = 10000;
    constexpr std::size_t kCheckpointEvery = 500;
    // The scene starts with entities of its own (some with parents), so that
    // deleting and restoring them is checked against a fixed order.
    engine::project::Project project = memoryProject();
    std::vector<EntityId> live;
    for (int i = 0; i < 40; ++i) {
        auto entity = project.startScene.createEntity();
        REQUIRE(entity.hasValue());
        REQUIRE_FALSE(
            project.startScene.addComponent(entity.value(), name("Start " + std::to_string(i)))
                .has_value());
        if (i % 3 == 2) {
            REQUIRE_FALSE(project.startScene
                              .addComponent(entity.value(),
                                            engine::scene::ParentComponent{live[live.size() - 2]})
                              .has_value());
        }
        live.push_back(entity.value());
    }
    EditorDocument document(std::move(project), kCommands);
    const DocumentTexts start = textsOf(document);
    // The texts after every kCheckpointEvery commands, checked on the way back.
    std::vector<DocumentTexts> checkpoints{start};

    // A fixed pseudo-random mix of every command.
    std::uint64_t state = 0x9E3779B97F4A7C15ULL;
    const auto next = [&state](std::uint64_t range) {
        state = state * 6364136223846793005ULL + 1442695040888963407ULL;
        return (state >> 33U) % range;
    };
    std::array<std::size_t, 9> executedOfKind{};
    std::size_t executed = 0;
    while (executed < kCommands) {
        std::unique_ptr<editor::Command> command;
        const std::uint64_t kind = live.size() < 4 ? 0 : next(9);
        const EntityId target = live.empty() ? EntityId{} : live[next(live.size())];
        switch (kind) {
        case 0:
            command = std::make_unique<CreateEntity>(std::vector<editor::ComponentValue>{
                name("E" + std::to_string(executed)), at(static_cast<float>(next(100)))});
            break;
        case 1:
            command = std::make_unique<DeleteEntity>(target);
            break;
        case 2:
            command = std::make_unique<RenameEntity>(target, "R" + std::to_string(executed));
            break;
        case 3:
            command =
                std::make_unique<ReplaceComponent>(target, at(static_cast<float>(next(1000))));
            break;
        case 4:
            command = std::make_unique<SetParent>(target, live[next(live.size())]);
            break;
        case 5:
            command = std::make_unique<SetParent>(target, std::nullopt);
            break;
        case 6:
            command = std::make_unique<AddComponent>(
                target, engine::scene::StaticMeshComponent{lpe_test::kMesh, {}});
            break;
        case 7:
            command = std::make_unique<RemoveComponent>(target, ComponentType::StaticMesh);
            break;
        default: {
            engine::worldgen::WorldGenSettings settings = document.worldSettings();
            settings.seed = next(1000000);
            command = std::make_unique<SetWorldSettings>(settings);
            break;
        }
        }
        const auto* created = dynamic_cast<const CreateEntity*>(command.get());
        if (document.execute(std::move(command))) {
            // Refused (a cycle, nothing to clear or remove, a mesh it has):
            // changes nothing and does not count.
            continue;
        }
        ++executed;
        ++executedOfKind[kind];
        if (kind == 0) {
            live.push_back(created->entity());
        } else if (kind == 1) {
            std::erase(live, target);
        }
        if (executed % kCheckpointEvery == 0) {
            checkpoints.push_back(textsOf(document));
        }
    }
    CHECK(document.undoCount() == kCommands);
    // Every kind of command took part.
    for (std::size_t kind = 0; kind < executedOfKind.size(); ++kind) {
        INFO("kind " << kind);
        CHECK(executedOfKind[kind] > 100U);
    }
    const DocumentTexts end = textsOf(document);
    CHECK(document.scene().entityCount() == live.size());

    std::size_t undone = 0;
    while (document.canUndo()) {
        const auto error = document.undo();
        INFO((error ? error->message : std::string()));
        REQUIRE_FALSE(error.has_value());
        ++undone;
        if (document.undoCount() % kCheckpointEvery == 0) {
            INFO("after undoing to step " << document.undoCount());
            CHECK(textsOf(document) == checkpoints[document.undoCount() / kCheckpointEvery]);
        }
    }
    CHECK(undone == kCommands);
    CHECK(textsOf(document) == start);
    CHECK_FALSE(document.isDirty());

    std::size_t redone = 0;
    while (document.canRedo()) {
        REQUIRE_FALSE(document.redo().has_value());
        ++redone;
        if (document.undoCount() % kCheckpointEvery == 0) {
            INFO("after redoing to step " << document.undoCount());
            CHECK(textsOf(document) == checkpoints[document.undoCount() / kCheckpointEvery]);
        }
    }
    CHECK(redone == kCommands);
    CHECK(textsOf(document) == end);
}
