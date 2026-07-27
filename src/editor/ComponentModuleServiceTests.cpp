#include "ComponentModuleService.h"
#include "MetadataManifestService.h"

#include <dragonpixel/metadata/registry.h>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTemporaryDir>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <system_error>

namespace
{
constexpr auto declared_root = "Authoring Components \xCE\xA9";

std::filesystem::path filesystem_path(const QString& path)
{
#if defined(Q_OS_WIN)
    return std::filesystem::path{path.toStdWString()};
#else
    return std::filesystem::path{path.toUtf8().constData()};
#endif
}

bool require(bool condition, const QString& message)
{
    if (!condition) std::cerr << message.toStdString() << '\n';
    return condition;
}

bool write_bytes(const QString& path, const QByteArray& bytes)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath())) return false;
    QFile file{path};
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(bytes) == bytes.size() && file.flush();
}

QByteArray read_bytes(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

struct TreeSnapshot final
{
    QMap<QString, QByteArray> files;
    QStringList directories;

    bool operator==(const TreeSnapshot&) const = default;
};

bool snapshot_tree(const QString& root_path, TreeSnapshot& snapshot, QString& error)
{
    snapshot = {};
    const QDir root{root_path};
    QDirIterator iterator{root_path,
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDirIterator::Subdirectories};
    while (iterator.hasNext())
    {
        const QFileInfo info{iterator.next()};
        const auto relative = QDir::fromNativeSeparators(
            root.relativeFilePath(info.absoluteFilePath()));
        if (info.isDir())
        {
            snapshot.directories.push_back(relative);
            continue;
        }
        QFile file{info.absoluteFilePath()};
        if (!file.open(QIODevice::ReadOnly))
        {
            error = QStringLiteral("Could not snapshot %1: %2")
                        .arg(info.absoluteFilePath(), file.errorString());
            return false;
        }
        snapshot.files.insert(relative, file.readAll());
    }
    std::sort(snapshot.directories.begin(), snapshot.directories.end());
    return true;
}

bool create_project_root(QTemporaryDir& project, const QString& root_name = QString::fromUtf8(declared_root))
{
    return require(project.isValid(), QStringLiteral("Temporary project directory is unavailable."))
        && require(QDir{project.path()}.mkpath(root_name),
            QStringLiteral("Could not create declared component root %1.").arg(root_name));
}

ComponentCreationRequest creation_request(
    const QString& project,
    const QString& name,
    ProjectComponentLanguage language,
    ComponentCreationFault fault = ComponentCreationFault::none,
    QStringList roots = {QString::fromUtf8(declared_root)})
{
    return {project, roots, name, QStringLiteral("Tests/Unicode"), language, fault};
}

bool empty_and_ambiguous_roots_are_rejected()
{
    QTemporaryDir project;
    if (!create_project_root(project)) return false;
    if (!QDir{project.path()}.mkpath(QStringLiteral("Second Components"))) return false;
    TreeSnapshot before;
    QString error;
    if (!snapshot_tree(project.path(), before, error)) return require(false, error);
    const auto empty = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("No Root"),
            ProjectComponentLanguage::csharp, ComponentCreationFault::none, {}));
    const auto ambiguous = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Ambiguous"),
            ProjectComponentLanguage::csharp, ComponentCreationFault::none,
            {QString::fromUtf8(declared_root), QStringLiteral("Second Components")}));
    const auto empty_build = ComponentModuleService::build(
        project.path(), {}, QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY));
    const auto empty_active = ComponentModuleService::active_runtime_manifest(
        project.path(), {}, QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY));
    TreeSnapshot after;
    if (!snapshot_tree(project.path(), after, error)) return require(false, error);
    if (!require(!empty.succeeded && !ambiguous.succeeded && !empty_build.succeeded
            && empty_active.isEmpty() && before == after,
            QStringLiteral("Empty/ambiguous component-root creation changed the project or succeeded.")))
        return false;

    auto selected = creation_request(
        project.path(),
        QStringLiteral("Selected Root"),
        ProjectComponentLanguage::csharp,
        ComponentCreationFault::none,
        {QString::fromUtf8(declared_root), QStringLiteral("Second Components")});
    selected.selected_component_root = QStringLiteral("Second Components");
    const auto created = ComponentModuleService::create(selected);
    return require(created.succeeded
            && QDir::fromNativeSeparators(created.source_path).contains(
                QStringLiteral("Second Components/CSharp/Selected_Root.cs"))
            && QDir::fromNativeSeparators(created.manifest_path).contains(
                QStringLiteral("Second Components/ProjectComponents.dpecomponents")),
        QStringLiteral("Explicit multi-root component creation did not use the selected root: %1")
            .arg(created.error));
}

bool generation_failure_is_transactional(
    ProjectComponentLanguage language,
    ComponentCreationFault fault,
    const QString& fault_name)
{
    QTemporaryDir project;
    if (!create_project_root(project)) return false;
    const auto seed_language = language == ProjectComponentLanguage::csharp
        ? ProjectComponentLanguage::cpp : ProjectComponentLanguage::csharp;
    const auto seed = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Existing Sentinel"), seed_language));
    if (!require(seed.succeeded,
            QStringLiteral("Could not seed %1: %2").arg(fault_name, seed.error)))
        return false;

    TreeSnapshot before;
    QString snapshot_error;
    if (!snapshot_tree(project.path(), before, snapshot_error)) return require(false, snapshot_error);
    const auto generated = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Rejected %1").arg(fault_name), language, fault));
    if (!require(!generated.succeeded && !generated.error.isEmpty(),
            QStringLiteral("%1 did not fail with a diagnostic.").arg(fault_name)))
        return false;
    TreeSnapshot after;
    if (!snapshot_tree(project.path(), after, snapshot_error)) return require(false, snapshot_error);
    return require(after == before,
        QStringLiteral("%1 changed the project tree despite transaction failure.").arg(fault_name));
}

bool generation_failures_are_transactional()
{
    struct FailureCase final
    {
        ProjectComponentLanguage language;
        ComponentCreationFault fault;
        const char* name;
    };
    const FailureCase cases[]{
        {ProjectComponentLanguage::csharp, ComponentCreationFault::stage_source_write, "stage-source-write"},
        {ProjectComponentLanguage::cpp, ComponentCreationFault::stage_native_header_write, "stage-native-header-write"},
        {ProjectComponentLanguage::cpp, ComponentCreationFault::stage_manifest_write, "stage-manifest-write"},
        {ProjectComponentLanguage::csharp, ComponentCreationFault::stage_build_definition_write, "isolated-stage-build-definition"},
        {ProjectComponentLanguage::csharp, ComponentCreationFault::staged_validation, "staged-validation"},
        {ProjectComponentLanguage::csharp, ComponentCreationFault::commit_source, "commit-source"},
        {ProjectComponentLanguage::cpp, ComponentCreationFault::commit_native_header, "commit-native-header"},
        {ProjectComponentLanguage::cpp, ComponentCreationFault::commit_manifest, "commit-manifest"},
        {ProjectComponentLanguage::csharp, ComponentCreationFault::commit_build_definition, "isolated-commit-build-definition"},
    };
    for (const auto& test : cases)
    {
        if (!generation_failure_is_transactional(
                test.language, test.fault, QString::fromLatin1(test.name)))
            return false;
    }
    return true;
}

bool malformed_manifest_is_rejected_transactionally()
{
    QTemporaryDir project;
    if (!create_project_root(project)) return false;
    const auto manifest = QDir{project.path()}.filePath(
        QStringLiteral("%1/ProjectComponents.dpecomponents").arg(QString::fromUtf8(declared_root)));
    if (!write_bytes(manifest, QByteArrayLiteral("{ malformed"))) return false;
    TreeSnapshot before;
    QString error;
    if (!snapshot_tree(project.path(), before, error)) return require(false, error);
    const auto created = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Rejected Metadata"),
            ProjectComponentLanguage::csharp));
    const auto built = ComponentModuleService::build(
        project.path(), {QString::fromUtf8(declared_root)},
        QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY));
    TreeSnapshot after;
    if (!snapshot_tree(project.path(), after, error)) return require(false, error);
    return require(!created.succeeded && !built.succeeded && before == after,
        QStringLiteral("Malformed metadata was accepted or changed during rejected creation."));
}

bool component_generation_rejects_link_escape()
{
    QTemporaryDir project;
    QTemporaryDir outside;
    if (!create_project_root(project)
        || !require(outside.isValid(), QStringLiteral("Link-escape target is unavailable.")))
        return false;
    const auto sentinel_path = QDir{outside.path()}.filePath(QStringLiteral("sentinel.txt"));
    if (!write_bytes(sentinel_path, QByteArrayLiteral("outside-before\n"))) return false;
    const auto link_path = QDir{project.path()}.filePath(
        QStringLiteral("%1/CSharp").arg(QString::fromUtf8(declared_root)));
    std::error_code link_error;
    std::filesystem::create_directory_symlink(
        filesystem_path(outside.path()), filesystem_path(link_path), link_error);
    if (link_error)
    {
        std::cout << "Link-escape test skipped: symlink creation is unavailable on this host.\n";
        return true;
    }
    const auto generated = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Escaped Component"),
            ProjectComponentLanguage::csharp));
    const auto unchanged = read_bytes(sentinel_path) == QByteArrayLiteral("outside-before\n");
    std::filesystem::remove(filesystem_path(link_path), link_error);
    return require(!generated.succeeded && unchanged
            && !QFileInfo::exists(QDir{outside.path()}.filePath(QStringLiteral("Escaped_Component.cs"))),
        QStringLiteral("Component generation followed a linked source directory outside the project."));
}

bool copy_contracts(const QString& destination, QByteArray& original)
{
    const auto source = QString::fromUtf8(DPE_CONTRACTS_ASSEMBLY);
    original = read_bytes(source);
    return require(!original.isEmpty(), QStringLiteral("Could not read DragonPixel.Contracts test input."))
        && require(write_bytes(destination, original),
            QStringLiteral("Could not copy DragonPixel.Contracts into a spaced test path."));
}

bool runtime_artifacts(const QString& manifest_path, QString& managed, QString& native)
{
    const auto document = QJsonDocument::fromJson(read_bytes(manifest_path));
    const auto root = document.object();
    const auto managed_modules = root.value(QStringLiteral("managedModules")).toArray();
    const auto native_modules = root.value(QStringLiteral("nativeModules")).toArray();
    if (managed_modules.size() != 1 || native_modules.size() != 1) return false;
    managed = managed_modules.at(0).toObject().value(QStringLiteral("path")).toString();
    native = native_modules.at(0).toObject().value(QStringLiteral("path")).toString();
    return !managed.isEmpty() && !native.isEmpty();
}

bool custom_roots_isolation_and_cache_integrity()
{
    QTemporaryDir project;
    if (!create_project_root(project)) return false;
    const auto declared = QString::fromUtf8(declared_root);
    const auto declared_absolute = QDir{project.path()}.filePath(declared);
    const auto custom_csproj = QDir{declared_absolute}.filePath(
        QStringLiteral("CSharp/DragonPixel.ProjectComponents.csproj"));
    const auto custom_cmake = QDir{declared_absolute}.filePath(QStringLiteral("Cpp/CMakeLists.txt"));
    const QByteArray csproj_sentinel{"<Project><!-- user-owned sentinel --></Project>\n"};
    const QByteArray cmake_sentinel{"# user-owned sentinel\n"};
    if (!write_bytes(custom_csproj, csproj_sentinel)
        || !write_bytes(custom_cmake, cmake_sentinel)) return false;

    const auto undeclared = QDir{project.path()}.filePath(QStringLiteral("Components"));
    if (!write_bytes(QDir{undeclared}.filePath(QStringLiteral("ProjectComponents.dpecomponents")),
            QByteArrayLiteral("{ intentionally malformed and undeclared"))
        || !write_bytes(QDir{undeclared}.filePath(QStringLiteral("CSharp/ShouldNeverCompile.cs")),
            QByteArrayLiteral("this is not valid C#")))
        return false;

    const auto managed = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Managed \u03A9 Input Driver"),
            ProjectComponentLanguage::csharp));
    const auto native = ComponentModuleService::create(
        creation_request(project.path(), QStringLiteral("Native Pulse With Spaces"),
            ProjectComponentLanguage::cpp));
    if (!require(managed.succeeded && native.succeeded,
            QStringLiteral("Custom-root generation failed: %1 | %2").arg(managed.error, native.error)))
        return false;
    if (!require(managed.source_path.contains(declared) && native.source_path.contains(declared),
            QStringLiteral("Generated source did not use the declared custom root."))) return false;
    const auto managed_source = read_bytes(managed.source_path);
    const auto native_source = read_bytes(native.source_path);
    const auto native_header_path = QDir{QFileInfo{native.source_path}.absolutePath()}.filePath(
        QStringLiteral("dpe_component_plugin.h"));
    const auto generated_native_header = read_bytes(native_header_path);
    if (!require(managed_source.contains(": GameObjectController")
            && managed_source.contains("override void Enabled()")
            && managed_source.contains("override void Update()")
            && managed_source.contains("override void Disabled()")
            && managed_source.contains("override void FixedUpdate()")
            && managed_source.contains("HorizontalActionPropertyId")
            && managed_source.contains("VerticalActionPropertyId")
            && managed_source.contains("Input.GetAction(HorizontalAction)")
            && managed_source.contains("Input.GetAction(VerticalAction)")
            && managed_source.contains("Transform.Translate(direction * Speed * DeltaTime)")
            && managed_source.contains("override void PropertiesChanged(")
            && native_source.contains("dpe_component_plugin_get_v2")
            && native_source.contains("dispatch_lifecycle")
            && native_source.contains("void Start(")
            && native_source.contains("void OnEnable()")
            && native_source.contains("void OnUpdate(")
            && native_source.contains("void OnDisable()")
            && generated_native_header.contains("uint32_t struct_size;")
            && generated_native_header.contains("DPE_COMPONENT_LIFECYCLE_LATE_UPDATE_V2")
            && generated_native_header.contains("dpe_component_plugin_v2"),
            QStringLiteral("Generated managed/native sources omitted the GameObject controller or v2 lifecycle contract.")))
        return false;

    auto metadata = dragonpixel::metadata::registry::slice_one_defaults();
    const auto metadata_result = MetadataManifestService{}.load_project(
        project.path(), {declared}, metadata);
    if (!require(metadata_result.succeeded && metadata_result.component_count == 2,
            metadata_result.diagnostics.join(QLatin1Char{'\n'}))) return false;
    const auto* managed_descriptor = metadata.find(managed.type_id.toStdString());
    if (!require(managed_descriptor != nullptr && managed_descriptor->properties.size() == 3,
            QStringLiteral("Generated C# mover metadata did not expose the Input Motion 2D field set.")))
        return false;
    const auto has_property = [managed_descriptor](const std::string& suffix, const std::string& display_name) {
        return std::any_of(managed_descriptor->properties.begin(), managed_descriptor->properties.end(),
            [&](const auto& property) {
                return property.property_id.ends_with(suffix)
                    && property.display_name == display_name
                    && property.category == "Input";
            });
    };
    if (!require(has_property(".horizontal_action", "Horizontal Action")
            && has_property(".vertical_action", "Vertical Action")
            && has_property(".speed", "Speed"),
            QStringLiteral("Generated C# mover action/speed metadata did not match Input Motion 2D.")))
        return false;

    const auto contracts_copy = QDir{project.path()}.filePath(
        QStringLiteral("Tool Inputs With Spaces/DragonPixel.Contracts.dll"));
    QByteArray contracts_original;
    if (!copy_contracts(contracts_copy, contracts_original)) return false;
    const auto built = ComponentModuleService::build(project.path(), {declared}, contracts_copy);
    if (!require(built.succeeded,
            built.error + QLatin1Char{'\n'} + built.diagnostics.join(QLatin1Char{'\n'}))) return false;
    if (!require(!built.reused_cache && built.managed_component_count == 1
            && built.native_component_count == 1,
            QStringLiteral("Initial isolated build returned unexpected counts/cache state."))) return false;
    if (!require(read_bytes(custom_csproj) == csproj_sentinel
            && read_bytes(custom_cmake) == cmake_sentinel,
            QStringLiteral("Isolated component build overwrote user-owned build definitions."))) return false;
    if (!require(!QFileInfo::exists(QDir{undeclared}.filePath(QStringLiteral("bin")))
            && !QFileInfo::exists(QDir{undeclared}.filePath(QStringLiteral("obj"))),
            QStringLiteral("Build scanned or compiled the undeclared Components directory."))) return false;

    const auto active = ComponentModuleService::active_runtime_manifest(
        project.path(), {declared}, contracts_copy);
    if (!require(!active.isEmpty() && QFileInfo::exists(active),
            QStringLiteral("Validated active runtime manifest was not available."))) return false;
    QString managed_artifact;
    QString native_artifact;
    if (!require(runtime_artifacts(built.runtime_manifest_path, managed_artifact, native_artifact)
            && QFileInfo::exists(managed_artifact) && QFileInfo::exists(native_artifact),
            QStringLiteral("Runtime manifest references missing build artifacts."))) return false;
    const auto runtime_root = QJsonDocument::fromJson(read_bytes(built.runtime_manifest_path)).object();
    if (!require(runtime_root.value(QStringLiteral("generatorIdentity")).toString()
                == QStringLiteral("dpe-component-generator-v2")
            && runtime_root.value(QStringLiteral("contractsSha256")).toString().size() == 64
            && runtime_root.value(QStringLiteral("managedModules")).toArray().at(0)
                   .toObject().value(QStringLiteral("sha256")).toString().size() == 64,
            QStringLiteral("Runtime manifest omitted generator/contracts/artifact identities."))) return false;

    const auto reused = ComponentModuleService::build(project.path(), {declared}, contracts_copy);
    if (!require(reused.succeeded && reused.reused_cache && reused.build_hash == built.build_hash,
            QStringLiteral("Validated identical inputs did not reuse the cache."))) return false;

    if (!write_bytes(built.runtime_manifest_path, QByteArrayLiteral("{ malformed cache manifest"))) return false;
    const auto repaired_manifest = ComponentModuleService::build(project.path(), {declared}, contracts_copy);
    if (!require(repaired_manifest.succeeded && !repaired_manifest.reused_cache,
            QStringLiteral("Malformed cache manifest was trusted instead of rebuilt."))) return false;
    if (!runtime_artifacts(repaired_manifest.runtime_manifest_path, managed_artifact, native_artifact)) return false;

    if (!write_bytes(managed_artifact, QByteArrayLiteral("corrupt managed artifact"))) return false;
    const auto repaired_hash = ComponentModuleService::build(project.path(), {declared}, contracts_copy);
    if (!require(repaired_hash.succeeded && !repaired_hash.reused_cache,
            QStringLiteral("Hash-corrupt artifact was trusted instead of rebuilt."))) return false;
    if (!runtime_artifacts(repaired_hash.runtime_manifest_path, managed_artifact, native_artifact)) return false;
    if (!QFile::remove(native_artifact)) return false;
    const auto repaired_missing = ComponentModuleService::build(project.path(), {declared}, contracts_copy);
    if (!require(repaired_missing.succeeded && !repaired_missing.reused_cache,
            QStringLiteral("Deleted artifact was trusted instead of rebuilt."))) return false;

    QFile contracts{contracts_copy};
    if (!contracts.open(QIODevice::Append) || contracts.write("\0", 1) != 1 || !contracts.flush()) return false;
    contracts.close();
    if (!require(ComponentModuleService::active_runtime_manifest(
            project.path(), {declared}, contracts_copy).isEmpty(),
            QStringLiteral("Contracts change did not invalidate the active cache identity."))) return false;
    if (!write_bytes(contracts_copy, contracts_original)) return false;
    return require(!ComponentModuleService::active_runtime_manifest(
            project.path(), {declared}, contracts_copy).isEmpty(),
        QStringLiteral("Restored contracts did not restore the matching active cache identity."));
}
}

int main(int argc, char** argv)
{
    QCoreApplication application{argc, argv};
    if (!empty_and_ambiguous_roots_are_rejected()) return 1;
    if (!generation_failures_are_transactional()) return 1;
    if (!malformed_manifest_is_rejected_transactionally()) return 1;
    if (!component_generation_rejects_link_escape()) return 1;
    if (!custom_roots_isolation_and_cache_integrity()) return 1;
    std::cout << "Declared-root component creation, isolated build, and cache-integrity proof passed.\n";
    return 0;
}
