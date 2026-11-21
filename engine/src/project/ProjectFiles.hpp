#pragma once

// Internal to the engine (not a public header): every file operation a
// project's open and save make (M5.4, ADR 0028), behind one interface, so the
// tests can make any of them fail and check that the project stays whole.
// diskFiles() is the real file system; openProject, saveProject and
// createProject of engine/project/Project.hpp use it.

#include "engine/core/Result.hpp"
#include "engine/core/Uuid.hpp"
#include "engine/project/Project.hpp"

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace engine::project::detail {

class ProjectFiles {
public:
    ProjectFiles() = default;
    ProjectFiles(const ProjectFiles&) = delete;
    ProjectFiles& operator=(const ProjectFiles&) = delete;
    ProjectFiles(ProjectFiles&&) = delete;
    ProjectFiles& operator=(ProjectFiles&&) = delete;
    virtual ~ProjectFiles() = default;

    // Whether a regular file or a folder is there; false when it cannot be
    // told.
    [[nodiscard]] virtual bool isFile(const std::filesystem::path& path) = 0;
    [[nodiscard]] virtual bool isFolder(const std::filesystem::path& path) = 0;
    // The file's bytes. Fails for a missing file, a file larger than
    // `maxBytes`, and a read error. The error does not name the file; the
    // caller does.
    [[nodiscard]] virtual core::Result<std::string> read(const std::filesystem::path& path,
                                                         std::uintmax_t maxBytes) = 0;
    // Creates or replaces a file with the bytes; not atomic. The folder must
    // exist.
    [[nodiscard]] virtual std::optional<core::Error> write(const std::filesystem::path& path,
                                                           std::string_view bytes) = 0;
    // Renames a file, replacing `to` in one step if it exists.
    [[nodiscard]] virtual std::optional<core::Error> rename(const std::filesystem::path& from,
                                                            const std::filesystem::path& to) = 0;
    // Removes a file; a file that is not there is not an error.
    [[nodiscard]] virtual std::optional<core::Error> remove(const std::filesystem::path& path) = 0;
    // Creates a folder and its missing parents.
    [[nodiscard]] virtual std::optional<core::Error>
    createFolders(const std::filesystem::path& path) = 0;
    // The names of the regular files in a folder; empty for a missing folder.
    [[nodiscard]] virtual core::Result<std::vector<std::string>>
    fileNames(const std::filesystem::path& folder) = 0;
};

// The real file system (stateless; shared).
[[nodiscard]] ProjectFiles& diskFiles();

// The functions of engine/project/Project.hpp on given files.
[[nodiscard]] core::Result<Project> createProject(ProjectFiles& files,
                                                  const std::filesystem::path& root,
                                                  std::string_view name,
                                                  core::UuidGenerator generator);
[[nodiscard]] core::Result<OpenedProject>
openProject(ProjectFiles& files, const std::filesystem::path& root, core::UuidGenerator generator);
[[nodiscard]] core::Result<SaveReport> saveProject(ProjectFiles& files, const Project& project);

} // namespace engine::project::detail
