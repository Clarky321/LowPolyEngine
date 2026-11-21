#include "engine/project/Project.hpp"

#include "ProjectFiles.hpp"
#include "SaveJournal.hpp"

#include "engine/assets/AssetManifest.hpp"
#include "engine/core/FileSystem.hpp"
#include "engine/scene/AssetReferences.hpp"
#include "engine/scene/SceneDocument.hpp"
#include "engine/worldgen/SettingsFile.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <initializer_list>
#include <map>
#include <utility>

namespace engine::project {

namespace detail {

namespace {

// A world settings file is a few kilobytes; this leaves ample room.
constexpr std::uintmax_t kMaxWorldSettingsBytes = 1024U * 1024U;

assets::AssetPath fixedPath(std::string_view text)
{
    auto path = assets::AssetPath::parse(text);
    // The fixed names of ProjectDocument.hpp are valid asset paths.
    return std::move(path).value();
}

// The folder part of a project-relative path ("" for the project's folder)
// and its file name.
std::pair<std::string, std::string> splitPath(const assets::AssetPath& path)
{
    const std::string& text = path.text();
    const std::size_t slash = text.rfind('/');
    if (slash == std::string::npos) {
        return {std::string(), text};
    }
    return {text.substr(0, slash), text.substr(slash + 1)};
}

// The files a project is read from (ADR 0028, point 5): each part's own file,
// or, after a committed save that did not finish, the temporary file the
// journal names for it while that file is still there.
class Sources {
public:
    static core::Result<Sources> read(ProjectFiles& files, const std::filesystem::path& root)
    {
        Sources sources;
        sources.root_ = root;
        const std::filesystem::path journal = root / kSaveJournalFileName;
        if (!files.isFile(journal)) {
            return sources;
        }
        auto text = files.read(journal, kMaxJournalBytes);
        if (!text) {
            return core::Error{std::format("{}: {}", kSaveJournalFileName, text.error().message)};
        }
        auto entries = journalFromJson(text.value());
        if (!entries) {
            return core::Error{
                std::format("{}: {}", kSaveJournalFileName, entries.error().message)};
        }
        sources.journal_ = std::move(entries).value();
        return sources;
    }

    [[nodiscard]] const std::vector<JournalEntry>& journal() const noexcept { return journal_; }
    [[nodiscard]] bool readFromTemporaryFiles() const noexcept { return temporary_; }

    // The file holding the part's content.
    std::filesystem::path fileFor(ProjectFiles& files, const assets::AssetPath& target)
    {
        for (const JournalEntry& entry : journal_) {
            if (entry.target == target) {
                std::filesystem::path temporary = entry.temporary.resolve(root_);
                if (files.isFile(temporary)) {
                    temporary_ = true;
                    return temporary;
                }
                break;
            }
        }
        return target.resolve(root_);
    }

    // A journal may name only the parts of the document its save wrote.
    [[nodiscard]] std::optional<core::Error> checkTargets(const ProjectDocument& document) const
    {
        const assets::AssetPath documentFile = fixedPath(kProjectFileName);
        for (const JournalEntry& entry : journal_) {
            if (entry.target != documentFile && entry.target != document.assetManifest &&
                entry.target != document.startScene && entry.target != document.worldSettings) {
                return core::Error{std::format("{}: '{}' is not a part of the project",
                                               kSaveJournalFileName, entry.target.text())};
            }
        }
        return std::nullopt;
    }

private:
    std::filesystem::path root_;
    std::vector<JournalEntry> journal_;
    bool temporary_ = false;
};

// A part's text; the error starts with its project-relative path.
core::Result<std::string> readPart(ProjectFiles& files, Sources& sources,
                                   const assets::AssetPath& target, std::uintmax_t maxBytes)
{
    auto text = files.read(sources.fileFor(files, target), maxBytes);
    if (!text) {
        return core::Error{std::format("{}: {}", target.text(), text.error().message)};
    }
    return text;
}

core::Result<LoadedProjectDocument> readDocument(ProjectFiles& files, Sources& sources,
                                                 const std::filesystem::path& root)
{
    const assets::AssetPath documentFile = fixedPath(kProjectFileName);
    if (!files.isFile(sources.fileFor(files, documentFile))) {
        return core::Error{std::format("'{}' holds no project: '{}' is missing", core::toUtf8(root),
                                       kProjectFileName)};
    }
    auto text = readPart(files, sources, documentFile, kMaxProjectDocumentBytes);
    if (!text) {
        return text.error();
    }
    auto loaded = projectDocumentFromJson(text.value());
    if (!loaded) {
        return core::Error{std::format("{}: {}", kProjectFileName, loaded.error().message)};
    }
    if (auto error = sources.checkTargets(loaded.value().document)) {
        return *error;
    }
    return loaded;
}

// No asset of the manifest may be one of the project's own files.
std::optional<core::Error> checkAssetsAvoidParts(const assets::AssetRegistry& registry,
                                                 const ProjectDocument& document)
{
    const std::array<std::pair<std::string_view, std::string>, 5> parts{{
        {"project document", assets::foldCase(kProjectFileName)},
        {"save journal", assets::foldCase(kSaveJournalFileName)},
        {"asset manifest", document.assetManifest.folded()},
        {"start scene", document.startScene.folded()},
        {"world settings", document.worldSettings.folded()},
    }};
    for (const assets::AssetMetadata& asset : registry.assets()) {
        const std::string folded = asset.path.folded();
        for (const auto& [what, path] : parts) {
            if (folded == path) {
                return core::Error{std::format("{}: the asset '{}' has the path of the {}",
                                               document.assetManifest.text(), asset.path.text(),
                                               what)};
            }
        }
    }
    return std::nullopt;
}

struct PartText {
    assets::AssetPath target;
    std::string text;
};

// Step 2 of a save: finish a committed save that did not finish, then remove
// the temporary files of saves that failed before their commit, next to the
// parts about to be written.
std::optional<core::Error> finishEarlierSaves(ProjectFiles& files,
                                              const std::filesystem::path& root,
                                              const std::vector<PartText>& parts,
                                              std::vector<std::string>& warnings)
{
    const std::filesystem::path journal = root / kSaveJournalFileName;
    if (files.isFile(journal)) {
        auto sources = Sources::read(files, root);
        if (!sources) {
            return sources.error();
        }
        // The journal is checked against the document its save wrote.
        auto committed = readDocument(files, sources.value(), root);
        if (!committed) {
            return committed.error();
        }
        for (const JournalEntry& entry : sources.value().journal()) {
            const std::filesystem::path temporary = entry.temporary.resolve(root);
            if (files.isFile(temporary)) {
                if (auto error = files.rename(temporary, entry.target.resolve(root))) {
                    return error;
                }
            }
        }
        if (auto error = files.remove(journal)) {
            return error;
        }
        warnings.emplace_back("finished an earlier save of the project that was interrupted");
    }

    // Folder (project-relative, "" for the project's folder) -> the names of
    // the files whose temporary files are leftovers.
    std::map<std::string, std::vector<std::string>> known;
    known[std::string()] = {std::string(kProjectFileName), std::string(kSaveJournalFileName)};
    for (const PartText& part : parts) {
        auto [folder, name] = splitPath(part.target);
        known[folder].push_back(std::move(name));
    }
    std::size_t removed = 0;
    for (const auto& [folder, names] : known) {
        const std::filesystem::path path =
            folder.empty() ? root : root / std::filesystem::path(folder);
        auto list = files.fileNames(path);
        if (!list) {
            warnings.push_back(list.error().message);
            continue;
        }
        for (const std::string& file : list.value()) {
            bool leftover = false;
            for (const std::string& name : names) {
                leftover = leftover || isTemporaryOf(file, name);
            }
            if (!leftover) {
                continue;
            }
            if (auto error = files.remove(path / std::filesystem::path(file))) {
                warnings.push_back(error->message);
            } else {
                ++removed;
            }
        }
    }
    if (removed > 0) {
        warnings.push_back(std::format(
            "removed {} leftover file(s) of an earlier save that failed before its commit",
            removed));
    }
    return std::nullopt;
}

void removeFiles(ProjectFiles& files, const std::vector<std::filesystem::path>& paths)
{
    for (const std::filesystem::path& path : paths) {
        (void)files.remove(path);
    }
}

} // namespace

core::Result<OpenedProject> openProject(ProjectFiles& files, const std::filesystem::path& root,
                                        core::UuidGenerator generator)
{
    if (!files.isFolder(root)) {
        return core::Error{std::format("'{}' is not a folder", core::toUtf8(root))};
    }
    auto sources = Sources::read(files, root);
    if (!sources) {
        return sources.error();
    }
    auto loadedDocument = readDocument(files, sources.value(), root);
    if (!loadedDocument) {
        return loadedDocument.error();
    }
    ProjectDocument& document = loadedDocument.value().document;
    std::vector<std::string> warnings;
    for (const std::string& warning : loadedDocument.value().warnings) {
        warnings.push_back(std::format("{}: {}", kProjectFileName, warning));
    }
    const auto addWarnings = [&warnings](const assets::AssetPath& file,
                                         const std::vector<std::string>& found) {
        for (const std::string& warning : found) {
            warnings.push_back(std::format("{}: {}", file.text(), warning));
        }
    };
    const auto prefixed = [](const assets::AssetPath& file, const core::Error& error) {
        return core::Error{std::format("{}: {}", file.text(), error.message)};
    };

    auto manifestText =
        readPart(files, sources.value(), document.assetManifest, assets::kMaxManifestBytes);
    if (!manifestText) {
        return manifestText.error();
    }
    auto manifest = assets::manifestFromJson(manifestText.value(), root);
    if (!manifest) {
        return prefixed(document.assetManifest, manifest.error());
    }
    addWarnings(document.assetManifest, manifest.value().warnings);
    if (auto error = checkAssetsAvoidParts(manifest.value().registry, document)) {
        return *error;
    }

    auto sceneText =
        readPart(files, sources.value(), document.startScene, scene::kMaxSceneDocumentBytes);
    if (!sceneText) {
        return sceneText.error();
    }
    auto scene =
        scene::sceneFromJson(sceneText.value(), std::move(generator), &manifest.value().registry);
    if (!scene) {
        return prefixed(document.startScene, scene.error());
    }
    addWarnings(document.startScene, scene.value().warnings);

    auto worldText =
        readPart(files, sources.value(), document.worldSettings, kMaxWorldSettingsBytes);
    if (!worldText) {
        return worldText.error();
    }
    auto world = worldgen::settingsFromJson(worldText.value());
    if (!world) {
        return prefixed(document.worldSettings, world.error());
    }
    addWarnings(document.worldSettings, world.value().warnings);

    if (sources.value().readFromTemporaryFiles()) {
        warnings.push_back(std::format("{}: a save was interrupted after its commit; the project "
                                       "opens as that save, and the next save finishes it",
                                       kSaveJournalFileName));
    }
    return OpenedProject{
        Project{root, std::move(document), std::move(manifest.value().registry),
                std::move(scene.value().scene), world.value().settings},
        std::move(warnings),
    };
}

core::Result<SaveReport> saveProject(ProjectFiles& files, const Project& project)
{
    const std::filesystem::path& root = project.root;
    const ProjectDocument& document = project.document;
    const auto refused = [](const std::string& reason) {
        return core::Error{"cannot save the project: " + reason + "; nothing was written"};
    };

    // 1. Check, and every part as text.
    if (auto error = validate(document)) {
        return refused(std::format("{}: {}", kProjectFileName, error->message));
    }
    if (auto error = worldgen::validate(project.worldSettings)) {
        return refused(std::format("{}: {}", document.worldSettings.text(), error->message));
    }
    if (project.startScene.id().isNull()) {
        return refused(std::format("{}: the scene has no ID", document.startScene.text()));
    }
    const auto problems = scene::findAssetReferenceProblems(project.startScene, project.assets);
    if (!problems.empty()) {
        return refused(std::format("{}: {}", document.startScene.text(), problems.front().message));
    }
    if (auto error = checkAssetsAvoidParts(project.assets, document)) {
        return refused(error->message);
    }
    const std::vector<PartText> parts{
        {document.assetManifest, assets::manifestToJson(project.assets)},
        {document.worldSettings, worldgen::settingsToJson(project.worldSettings)},
        {document.startScene, scene::sceneToJson(project.startScene)},
        {fixedPath(kProjectFileName), projectDocumentToJson(document)},
    };

    // 2. What earlier saves left.
    SaveReport report;
    if (auto error = finishEarlierSaves(files, root, parts, report.warnings)) {
        return refused(error->message);
    }

    // 3. Folders.
    for (const PartText& part : parts) {
        if (auto error = files.createFolders(part.target.resolve(root).parent_path())) {
            return refused(error->message);
        }
    }

    // 4. Temporary files, one save id for all.
    const std::string saveId =
        core::toString(core::UuidGenerator::fromEntropy().next()).substr(0, kSaveIdDigits);
    std::vector<JournalEntry> entries;
    for (const PartText& part : parts) {
        auto temporary = temporaryPath(part.target, saveId);
        if (!temporary) {
            return refused(std::format("{}: {}", part.target.text(), temporary.error().message));
        }
        entries.push_back({part.target, std::move(temporary).value()});
    }
    std::vector<std::filesystem::path> written;
    const auto unchanged = [&files, &written](const std::string& reason) {
        removeFiles(files, written);
        return core::Error{"cannot save the project: " + reason + "; the project is unchanged"};
    };
    for (std::size_t i = 0; i < parts.size(); ++i) {
        const std::filesystem::path file = entries[i].temporary.resolve(root);
        // Listed before writing: a failed write may leave part of the file.
        written.push_back(file);
        if (auto error = files.write(file, parts[i].text)) {
            return unchanged(error->message);
        }
    }

    // 5. The commit: the journal, put in place by a rename.
    const std::filesystem::path journal = root / kSaveJournalFileName;
    const std::filesystem::path journalTemporary =
        root / (std::string(kSaveJournalFileName) + std::string(kTemporaryMarker) + saveId);
    written.push_back(journalTemporary);
    if (auto error = files.write(journalTemporary, journalToJson(entries))) {
        return unchanged(error->message);
    }
    if (auto error = files.rename(journalTemporary, journal)) {
        return unchanged(error->message);
    }

    // 6. Every part in place.
    for (const JournalEntry& entry : entries) {
        if (auto error = files.rename(entry.temporary.resolve(root), entry.target.resolve(root))) {
            return core::Error{
                "the save of the project was interrupted after its commit: " + error->message +
                "; the project opens as this save, and the next save finishes it"};
        }
    }

    // 7. Done.
    if (auto error = files.remove(journal)) {
        report.warnings.push_back(std::format(
            "{}; the project is saved, and the next save removes the journal", error->message));
    }
    return report;
}

core::Result<Project> createProject(ProjectFiles& files, const std::filesystem::path& root,
                                    std::string_view name, core::UuidGenerator generator)
{
    const std::string where = core::toUtf8(root);
    auto document = defaultProjectDocument(name);
    if (!document) {
        return core::Error{
            std::format("cannot create a project in '{}': {}", where, document.error().message)};
    }
    if (files.isFile(root)) {
        return core::Error{std::format("cannot create a project in '{}': it is a file", where)};
    }
    if (files.isFile(root / kProjectFileName) || files.isFile(root / kSaveJournalFileName)) {
        return core::Error{
            std::format("cannot create a project in '{}': it already holds a project", where)};
    }
    for (const assets::AssetPath* path :
         {&document.value().assetManifest, &document.value().startScene,
          &document.value().worldSettings}) {
        if (files.isFile(path->resolve(root))) {
            return core::Error{std::format("cannot create a project in '{}': '{}' already "
                                           "exists, and a new project does not overwrite it",
                                           where, path->text())};
        }
    }
    auto scene = scene::Scene::create(kDefaultSceneName, std::move(generator));
    if (!scene) {
        return core::Error{
            std::format("cannot create a project in '{}': {}", where, scene.error().message)};
    }
    if (!files.isFolder(root)) {
        if (auto error = files.createFolders(root)) {
            return core::Error{
                std::format("cannot create a project in '{}': {}", where, error->message)};
        }
    }
    Project project{root, std::move(document).value(), assets::AssetRegistry(root),
                    std::move(scene).value(), worldgen::WorldGenSettings{}};
    if (auto saved = saveProject(files, project); !saved) {
        return core::Error{
            std::format("cannot create a project in '{}': {}", where, saved.error().message)};
    }
    return project;
}

} // namespace detail

core::Result<Project> createProject(const std::filesystem::path& root, std::string_view name,
                                    core::UuidGenerator generator)
{
    return detail::createProject(detail::diskFiles(), root, name, std::move(generator));
}

core::Result<OpenedProject> openProject(const std::filesystem::path& root,
                                        core::UuidGenerator generator)
{
    return detail::openProject(detail::diskFiles(), root, std::move(generator));
}

core::Result<SaveReport> saveProject(const Project& project)
{
    return detail::saveProject(detail::diskFiles(), project);
}

} // namespace engine::project
