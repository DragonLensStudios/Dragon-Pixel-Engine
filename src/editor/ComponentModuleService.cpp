#include "ComponentModuleService.h"
#include "MetadataManifestService.h"

#include <dragonpixel/core/uuid.h>

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <QSysInfo>
#include <QTemporaryDir>
#include <QTemporaryFile>

#include <algorithm>
#include <cstdint>
#include <optional>
#include <utility>

namespace
{
QString safe_identifier(QString value)
{
    value = value.trimmed();
    value.replace(QRegularExpression{QStringLiteral("[^A-Za-z0-9_]+")}, QStringLiteral("_"));
    while (value.startsWith(QLatin1Char{'_'})) value.remove(0, 1);
    if (!value.isEmpty() && value.front().isDigit()) value.prepend(QStringLiteral("Component_"));
    return value;
}

QString escaped_source_string(QString value)
{
    value.replace(QLatin1Char{'\\'}, QStringLiteral("\\\\"));
    value.replace(QLatin1Char{'\"'}, QStringLiteral("\\\""));
    value.replace(QLatin1Char{'\r'}, QStringLiteral("\\r"));
    value.replace(QLatin1Char{'\n'}, QStringLiteral("\\n"));
    value.replace(QLatin1Char{'\t'}, QStringLiteral("\\t"));
    return value;
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile file{path};
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        error = QStringLiteral("Could not atomically write %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

constexpr auto component_generator_identity = "dpe-component-generator-v2";
constexpr auto component_build_configuration = "Release";

struct DeclaredComponentRoot final
{
    QString declared;
    QString absolute;
};

bool is_link_or_junction(const QFileInfo& info)
{
    return info.isSymbolicLink()
#if defined(Q_OS_WIN)
        || info.isJunction()
#endif
        ;
}

QString normalized_path(const QString& value)
{
    return QDir::fromNativeSeparators(QDir::cleanPath(QFileInfo{value}.absoluteFilePath()));
}

QString comparison_path(QString value)
{
    value = QDir::fromNativeSeparators(QDir::cleanPath(value));
#if defined(Q_OS_WIN)
    value = value.toCaseFolded();
#endif
    return value;
}

bool lexically_contained(const QString& canonical_root, const QString& candidate)
{
    const auto relative = QDir{canonical_root}.relativeFilePath(candidate);
    return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"));
}

bool canonical_project_root(const QString& requested, QString& canonical, QString& error)
{
    const QFileInfo requested_info{requested};
    if (!requested_info.exists() || !requested_info.isDir() || is_link_or_junction(requested_info))
    {
        error = QStringLiteral("The project root must be an existing real directory, not a link or junction.");
        return false;
    }
    canonical = QDir::fromNativeSeparators(requested_info.canonicalFilePath());
    if (canonical.isEmpty())
    {
        error = QStringLiteral("The project root could not be canonicalized.");
        return false;
    }
    return true;
}

bool validate_no_follow_path(
    const QString& canonical_root,
    const QString& candidate,
    bool require_exists,
    bool require_directory,
    QString& error)
{
    const auto absolute = normalized_path(candidate);
    if (!lexically_contained(canonical_root, absolute))
    {
        error = QStringLiteral("Path escapes the project root: %1").arg(candidate);
        return false;
    }

    const auto relative = QDir::fromNativeSeparators(
        QDir{canonical_root}.relativeFilePath(absolute));
    auto current = canonical_root;
    const auto parts = relative == QStringLiteral(".")
        ? QStringList{} : relative.split(QLatin1Char{'/'}, Qt::SkipEmptyParts);
    bool missing_seen = false;
    for (const auto& part : parts)
    {
        current = QDir{current}.filePath(part);
        const QFileInfo info{current};
        if (!info.exists())
        {
            missing_seen = true;
            continue;
        }
        if (missing_seen)
        {
            error = QStringLiteral("A path component appeared below a missing ancestor: %1").arg(current);
            return false;
        }
        if (is_link_or_junction(info))
        {
            error = QStringLiteral("Symbolic links and junctions are not permitted in component operation paths: %1")
                        .arg(current);
            return false;
        }
        const auto resolved = QDir::fromNativeSeparators(info.canonicalFilePath());
        if (resolved.isEmpty() || !lexically_contained(canonical_root, resolved))
        {
            error = QStringLiteral("A component operation path resolved outside the project: %1").arg(current);
            return false;
        }
    }

    const QFileInfo final_info{absolute};
    if (require_exists && !final_info.exists())
    {
        error = QStringLiteral("Required component path does not exist: %1").arg(absolute);
        return false;
    }
    if (final_info.exists())
    {
        if (is_link_or_junction(final_info))
        {
            error = QStringLiteral("Component operation targets may not be links or junctions: %1").arg(absolute);
            return false;
        }
        if (require_directory && !final_info.isDir())
        {
            error = QStringLiteral("Required component root is not a directory: %1").arg(absolute);
            return false;
        }
        if (!require_directory && !final_info.isFile())
        {
            error = QStringLiteral("Component operation target is not a regular file: %1").arg(absolute);
            return false;
        }
    }
    return true;
}

bool validate_declared_roots(
    const QString& canonical_root,
    const QStringList& declarations,
    bool require_single,
    QList<DeclaredComponentRoot>& roots,
    QString& error)
{
    if (declarations.isEmpty() || (require_single && declarations.size() != 1))
    {
        error = require_single
            ? QStringLiteral("Component creation requires exactly one declared component root.")
            : QStringLiteral("At least one declared component root is required.");
        return false;
    }

    QSet<QString> seen;
    for (const auto& declared : declarations)
    {
        const auto normalized_declaration = QDir::fromNativeSeparators(QDir::cleanPath(declared));
        if (declared.trimmed().isEmpty() || QDir::isAbsolutePath(declared)
            || normalized_declaration == QStringLiteral("..")
            || normalized_declaration.startsWith(QStringLiteral("../")))
        {
            error = QStringLiteral("Declared component roots must be non-empty project-relative paths.");
            return false;
        }
        const auto absolute = normalized_path(QDir{canonical_root}.filePath(normalized_declaration));
        if (!validate_no_follow_path(canonical_root, absolute, true, true, error))
        {
            return false;
        }
        const auto key = comparison_path(QFileInfo{absolute}.canonicalFilePath());
        if (seen.contains(key))
        {
            error = QStringLiteral("Declared component roots resolve to the same directory: %1").arg(declared);
            return false;
        }
        for (const auto& existing : roots)
        {
            if (lexically_contained(existing.absolute, absolute)
                || lexically_contained(absolute, existing.absolute))
            {
                error = QStringLiteral("Declared component roots overlap and are ambiguous: %1 and %2")
                            .arg(existing.declared, declared);
                return false;
            }
        }
        seen.insert(key);
        roots.push_back({normalized_declaration, absolute});
    }
    std::sort(roots.begin(), roots.end(), [](const auto& left, const auto& right) {
        return left.declared < right.declared;
    });
    return true;
}

bool ensure_safe_directory(
    const QString& canonical_root,
    const QString& requested,
    QStringList* created,
    QString& error)
{
    const auto target = normalized_path(requested);
    if (!lexically_contained(canonical_root, target))
    {
        error = QStringLiteral("Directory creation target escapes the project root: %1").arg(requested);
        return false;
    }
    const auto relative = QDir::fromNativeSeparators(QDir{canonical_root}.relativeFilePath(target));
    auto current = canonical_root;
    for (const auto& part : relative.split(QLatin1Char{'/'}, Qt::SkipEmptyParts))
    {
        current = normalized_path(QDir{current}.filePath(part));
        const QFileInfo before{current};
        if (before.exists())
        {
            if (!before.isDir() || is_link_or_junction(before)
                || !validate_no_follow_path(canonical_root, current, true, true, error))
            {
                if (error.isEmpty())
                    error = QStringLiteral("Component directory is unsafe: %1").arg(current);
                return false;
            }
            continue;
        }
        if (!QDir{}.mkdir(current))
        {
            error = QStringLiteral("Could not create component directory %1.").arg(current);
            return false;
        }
        if (!validate_no_follow_path(canonical_root, current, true, true, error))
        {
            return false;
        }
        if (created != nullptr) created->push_back(current);
    }
    return validate_no_follow_path(canonical_root, target, true, true, error);
}

bool read_all(const QString& path, QByteArray& bytes, QString& error)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("Could not read %1: %2").arg(path, file.errorString());
        return false;
    }
    bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
    {
        error = QStringLiteral("Could not completely read %1: %2").arg(path, file.errorString());
        return false;
    }
    return true;
}

bool write_atomic_contained(
    const QString& canonical_root,
    const QString& path,
    const QByteArray& bytes,
    QString& error)
{
    if (!validate_no_follow_path(canonical_root, path, false, false, error))
    {
        return false;
    }
    const auto parent = QFileInfo{path}.absolutePath();
    if (!validate_no_follow_path(canonical_root, parent, true, true, error))
    {
        return false;
    }
    return write_atomic(path, bytes, error);
}

QJsonObject empty_manifest()
{
    return {
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v4/component-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.component-metadata")},
        {QStringLiteral("formatVersion"), 4},
        {QStringLiteral("generatorVersion"), QStringLiteral("0.3.0")},
        {QStringLiteral("contracts"), QJsonArray{}},
        {QStringLiteral("objectTypes"), QJsonArray{}},
        {QStringLiteral("components"), QJsonArray{}},
    };
}

QByteArray csharp_source(
    const QString& class_name,
    const QString& type_id,
    const QString& display_name)
{
    return QStringLiteral(R"CS(using System.Numerics;
using System.Text.Json;
using DragonPixel.Contracts;

namespace DragonPixel.ProjectComponents;

[DpeComponent("%1", "DragonPixel.ProjectComponents.%2", "%3", 1, ComponentOwner.Managed)]
public sealed class %2 : GameObjectController
{
    private const string HorizontalActionPropertyId = "%1.horizontal_action";
    private const string VerticalActionPropertyId = "%1.vertical_action";
    private const string SpeedPropertyId = "%1.speed";

    public string HorizontalAction { get; private set; } = "move.x";
    public string VerticalAction { get; private set; } = "move.y";
    public float Speed { get; private set; } = 5.0f;

    public override void Enabled() { }

    public override void Update()
    {
        var direction = new Vector3(
            Input.GetAction(HorizontalAction),
            Input.GetAction(VerticalAction),
            0.0f);
        if (direction.LengthSquared() > 1.0f)
            direction = Vector3.Normalize(direction);
        Transform.Translate(direction * Speed * DeltaTime);
    }

    public override void FixedUpdate() { }

    public override void Disabled() { }

    public override void PropertiesChanged(string propertiesJson)
    {
        using var document = JsonDocument.Parse(propertiesJson);
        if (document.RootElement.TryGetProperty(HorizontalActionPropertyId, out var horizontalAction)
            && horizontalAction.ValueKind == JsonValueKind.String
            && !string.IsNullOrWhiteSpace(horizontalAction.GetString()))
        {
            HorizontalAction = horizontalAction.GetString()!;
        }
        if (document.RootElement.TryGetProperty(VerticalActionPropertyId, out var verticalAction)
            && verticalAction.ValueKind == JsonValueKind.String
            && !string.IsNullOrWhiteSpace(verticalAction.GetString()))
        {
            VerticalAction = verticalAction.GetString()!;
        }
        if (document.RootElement.TryGetProperty(SpeedPropertyId, out var speed)
            && speed.ValueKind == JsonValueKind.Number
            && speed.TryGetSingle(out var value))
        {
            Speed = Math.Clamp(value, 0.0f, 1000.0f);
        }
    }
}

public sealed class %2Factory : IProjectComponentFactory
{
    public string TypeId => "%1";
    public IProjectComponent Create() => new %2();
}
)CS").arg(type_id, class_name, escaped_source_string(display_name)).toUtf8();
}

QByteArray native_header()
{
    return QByteArrayLiteral(R"CPP(#pragma once
#include <stdint.h>
#include <stddef.h>

#if defined(_WIN32)
#define DPE_COMPONENT_EXPORT __declspec(dllexport)
#define DPE_COMPONENT_CALL __cdecl
#else
#define DPE_COMPONENT_EXPORT __attribute__((visibility("default")))
#define DPE_COMPONENT_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef void* dpe_component_handle;
typedef void (DPE_COMPONENT_CALL *dpe_component_diagnostic_fn)(void* user, int severity, const char* utf8_message, size_t length);

typedef struct dpe_component_update_v1 {
    double elapsed_seconds;
    double delta_seconds;
    const char* input_json;
    size_t input_json_length;
} dpe_component_update_v1;

typedef struct dpe_component_plugin_v1 {
    uint32_t abi_version;
    const char* component_type_id;
    dpe_component_handle (DPE_COMPONENT_CALL *create)(const char* entity_id, dpe_component_diagnostic_fn diagnostic, void* user);
    void (DPE_COMPONENT_CALL *destroy)(dpe_component_handle handle);
    int32_t (DPE_COMPONENT_CALL *set_properties)(dpe_component_handle handle, const char* json, size_t length);
    int32_t (DPE_COMPONENT_CALL *update)(dpe_component_handle handle, const dpe_component_update_v1* update);
    const char* (DPE_COMPONENT_CALL *last_error)(dpe_component_handle handle);
} dpe_component_plugin_v1;

typedef enum dpe_component_lifecycle_phase_v2 {
    DPE_COMPONENT_LIFECYCLE_ENABLE_V2 = 1,
    DPE_COMPONENT_LIFECYCLE_FIXED_UPDATE_V2 = 2,
    DPE_COMPONENT_LIFECYCLE_VARIABLE_UPDATE_V2 = 3,
    DPE_COMPONENT_LIFECYCLE_LATE_UPDATE_V2 = 4,
    DPE_COMPONENT_LIFECYCLE_RENDER_SUBMISSION_V2 = 5,
    DPE_COMPONENT_LIFECYCLE_DISABLE_V2 = 6
} dpe_component_lifecycle_phase_v2;

typedef struct dpe_component_plugin_v2 {
    uint32_t abi_version;
    uint32_t struct_size;
    const char* component_type_id;
    dpe_component_handle (DPE_COMPONENT_CALL *create)(const char* entity_id, dpe_component_diagnostic_fn diagnostic, void* user);
    void (DPE_COMPONENT_CALL *destroy)(dpe_component_handle handle);
    int32_t (DPE_COMPONENT_CALL *set_properties)(dpe_component_handle handle, const char* json, size_t length);
    int32_t (DPE_COMPONENT_CALL *dispatch_lifecycle)(dpe_component_handle handle, dpe_component_lifecycle_phase_v2 phase, const dpe_component_update_v1* update);
    const char* (DPE_COMPONENT_CALL *last_error)(dpe_component_handle handle);
} dpe_component_plugin_v2;

DPE_COMPONENT_EXPORT const dpe_component_plugin_v1* DPE_COMPONENT_CALL dpe_component_plugin_get_v1(void);
DPE_COMPONENT_EXPORT const dpe_component_plugin_v2* DPE_COMPONENT_CALL dpe_component_plugin_get_v2(void);

#ifdef __cplusplus
}
#endif
)CPP");
}

QByteArray cpp_source(const QString& class_name, const QString& type_id, const QString& display_name)
{
    return QStringLiteral(R"CPP(#include "dpe_component_plugin.h"

#include <new>
#include <string>

namespace {
struct %1State final {
    std::string entity_id;
    std::string properties{"{}"};
    std::string error;

    void Start(dpe_component_diagnostic_fn diagnostic, void* user)
    {
        if (diagnostic != nullptr) {
            constexpr char message[] = "%3 initialized inside the isolated worker.";
            diagnostic(user, 0, message, sizeof(message) - 1);
        }
    }

    void OnEnable() { }
    void FixedUpdate(const dpe_component_update_v1& update) { (void)update; }
    void OnUpdate(const dpe_component_update_v1& update) { (void)update; }
    void LateUpdate(const dpe_component_update_v1& update) { (void)update; }
    void SubmitRender(const dpe_component_update_v1& update) { (void)update; }
    void OnDisable() { }
};

dpe_component_handle DPE_COMPONENT_CALL create_component(
    const char* entity_id,
    dpe_component_diagnostic_fn diagnostic,
    void* user)
{
    auto* state = new (std::nothrow) %1State;
    if (state == nullptr) return nullptr;
    state->entity_id = entity_id == nullptr ? "" : entity_id;
    state->Start(diagnostic, user);
    return state;
}

void DPE_COMPONENT_CALL destroy_component(dpe_component_handle handle) { delete static_cast<%1State*>(handle); }

int32_t DPE_COMPONENT_CALL set_properties(dpe_component_handle handle, const char* json, size_t length)
{
    if (handle == nullptr || json == nullptr) return -1;
    static_cast<%1State*>(handle)->properties.assign(json, length);
    return 0;
}

int32_t DPE_COMPONENT_CALL dispatch_lifecycle(
    dpe_component_handle handle,
    dpe_component_lifecycle_phase_v2 phase,
    const dpe_component_update_v1* update)
{
    if (handle == nullptr) return -1;
    if (phase != DPE_COMPONENT_LIFECYCLE_ENABLE_V2
        && phase != DPE_COMPONENT_LIFECYCLE_DISABLE_V2
        && update == nullptr) return -1;
    auto& state = *static_cast<%1State*>(handle);
    switch (phase) {
    case DPE_COMPONENT_LIFECYCLE_ENABLE_V2: state.OnEnable(); return 0;
    case DPE_COMPONENT_LIFECYCLE_FIXED_UPDATE_V2: state.FixedUpdate(*update); return 0;
    case DPE_COMPONENT_LIFECYCLE_VARIABLE_UPDATE_V2: state.OnUpdate(*update); return 0;
    case DPE_COMPONENT_LIFECYCLE_LATE_UPDATE_V2: state.LateUpdate(*update); return 0;
    case DPE_COMPONENT_LIFECYCLE_RENDER_SUBMISSION_V2: state.SubmitRender(*update); return 0;
    case DPE_COMPONENT_LIFECYCLE_DISABLE_V2: state.OnDisable(); return 0;
    }
    return -1;
}

const char* DPE_COMPONENT_CALL last_error(dpe_component_handle handle)
{
    if (handle == nullptr) return "component handle is null";
    return static_cast<%1State*>(handle)->error.c_str();
}

const dpe_component_plugin_v2 api{2u, static_cast<uint32_t>(sizeof(dpe_component_plugin_v2)), "%2",
    &create_component, &destroy_component, &set_properties, &dispatch_lifecycle, &last_error};
}

extern "C" DPE_COMPONENT_EXPORT const dpe_component_plugin_v2* DPE_COMPONENT_CALL dpe_component_plugin_get_v2(void)
{
    return &api;
}
)CPP").arg(class_name, type_id, escaped_source_string(display_name)).toUtf8();
}

QString xml_escape(QString value)
{
    value.replace(QLatin1Char{'&'}, QStringLiteral("&amp;"));
    value.replace(QLatin1Char{'\"'}, QStringLiteral("&quot;"));
    value.replace(QLatin1Char{'<'}, QStringLiteral("&lt;"));
    value.replace(QLatin1Char{'>'}, QStringLiteral("&gt;"));
    return value;
}

QByteArray csharp_project(const QStringList& staged_sources)
{
    QString project = QStringLiteral(R"XML(<Project Sdk="Microsoft.NET.Sdk">
  <PropertyGroup>
    <TargetFramework>net10.0</TargetFramework>
    <Nullable>enable</Nullable>
    <ImplicitUsings>enable</ImplicitUsings>
    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>
    <AssemblyName>DragonPixel.ProjectComponents</AssemblyName>
    <Deterministic>true</Deterministic>
  </PropertyGroup>
  <ItemGroup>
)XML");
    for (const auto& source : staged_sources)
    {
        project += QStringLiteral("    <Compile Include=\"%1\" />\n").arg(xml_escape(source));
    }
    project += QStringLiteral(R"XML(    <Reference Include="DragonPixel.Contracts">
      <HintPath>$(DPEContractsPath)</HintPath>
      <Private>true</Private>
    </Reference>
  </ItemGroup>
</Project>
)XML");
    return project.toUtf8();
}

QString cmake_escape(QString value)
{
    value = QDir::fromNativeSeparators(value);
    value.replace(QLatin1Char{'\\'}, QStringLiteral("/"));
    value.replace(QLatin1Char{'\"'}, QStringLiteral("\\\""));
    value.replace(QLatin1Char{';'}, QStringLiteral("\\;"));
    return value;
}

enum class GeneratedFileKind
{
    source,
    native_header,
    manifest,
};

struct GeneratedFile final
{
    GeneratedFileKind kind{};
    QString target_path;
    QByteArray contents;
    QString staged_path;
    bool existed{};
    QByteArray before;
    QFileDevice::Permissions permissions{};
};

GeneratedFile generated_file(
    GeneratedFileKind kind,
    QString target_path,
    QByteArray contents)
{
    GeneratedFile file;
    file.kind = kind;
    file.target_path = std::move(target_path);
    file.contents = std::move(contents);
    return file;
}

bool matches_stage_fault(GeneratedFileKind kind, ComponentCreationFault fault)
{
    return (kind == GeneratedFileKind::source && fault == ComponentCreationFault::stage_source_write)
        || (kind == GeneratedFileKind::native_header
            && fault == ComponentCreationFault::stage_native_header_write)
        || (kind == GeneratedFileKind::manifest && fault == ComponentCreationFault::stage_manifest_write);
}

bool matches_commit_fault(GeneratedFileKind kind, ComponentCreationFault fault)
{
    return (kind == GeneratedFileKind::source && fault == ComponentCreationFault::commit_source)
        || (kind == GeneratedFileKind::native_header
            && fault == ComponentCreationFault::commit_native_header)
        || (kind == GeneratedFileKind::manifest && fault == ComponentCreationFault::commit_manifest);
}

QString generated_file_label(GeneratedFileKind kind)
{
    switch (kind)
    {
    case GeneratedFileKind::source: return QStringLiteral("component source");
    case GeneratedFileKind::native_header: return QStringLiteral("native ABI header");
    case GeneratedFileKind::manifest: return QStringLiteral("component manifest");
    }
    return QStringLiteral("generated file");
}

bool validate_staged_metadata(
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    const QString& target_manifest,
    const QByteArray& staged_manifest,
    const QString& expected_type_id,
    const QString& pending_source_relative,
    QTemporaryDir& staging,
    QString& error);

bool capture_before_images(
    const QString& canonical_root,
    QList<GeneratedFile>& files,
    QString& error)
{
    for (auto& file : files)
    {
        if (!validate_no_follow_path(canonical_root, file.target_path, false, false, error))
        {
            return false;
        }
        const QFileInfo info{file.target_path};
        file.existed = info.exists();
        if (!file.existed) continue;
        if (!info.isFile())
        {
            error = QStringLiteral("Generated-file target is not a regular file: %1").arg(file.target_path);
            return false;
        }
        if (!read_all(file.target_path, file.before, error)) return false;
        file.permissions = info.permissions();
    }
    return true;
}

bool stage_generated_files(
    QList<GeneratedFile>& files,
    QTemporaryDir& staging,
    ComponentCreationFault fault,
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    const QString& target_manifest,
    const QString& expected_type_id,
    const QString& pending_source_relative,
    QString& error)
{
    for (auto index = 0; index < files.size(); ++index)
    {
        auto& file = files[index];
        if (matches_stage_fault(file.kind, fault))
        {
            error = QStringLiteral("Injected failure while staging the %1.").arg(generated_file_label(file.kind));
            return false;
        }
        file.staged_path = QDir{staging.path()}.filePath(QStringLiteral("%1.pending").arg(index));
        if (!write_atomic(file.staged_path, file.contents, error)) return false;
    }

    if (fault == ComponentCreationFault::staged_validation)
    {
        error = QStringLiteral("Injected failure while validating staged component generation.");
        return false;
    }

    for (const auto& file : files)
    {
        QFile staged{file.staged_path};
        if (!staged.open(QIODevice::ReadOnly))
        {
            error = QStringLiteral("Could not validate staged %1: %2")
                        .arg(generated_file_label(file.kind), staged.errorString());
            return false;
        }
        if (staged.readAll() != file.contents)
        {
            error = QStringLiteral("Staged %1 did not match its generated bytes.")
                        .arg(generated_file_label(file.kind));
            return false;
        }
        if (file.kind == GeneratedFileKind::source && file.contents.isEmpty())
        {
            error = QStringLiteral("Generated component source was empty.");
            return false;
        }
        if (file.kind == GeneratedFileKind::manifest)
        {
            QJsonParseError parse_error;
            const auto document = QJsonDocument::fromJson(file.contents, &parse_error);
            if (parse_error.error != QJsonParseError::NoError || !document.isObject())
            {
                error = QStringLiteral("Generated component manifest failed staged validation: %1")
                            .arg(parse_error.errorString());
                return false;
            }
            bool found_expected_component = false;
            for (const auto& value : document.object().value(QStringLiteral("components")).toArray())
            {
                if (value.toObject().value(QStringLiteral("typeId")).toString() == expected_type_id)
                {
                    found_expected_component = true;
                    break;
                }
            }
            if (!found_expected_component)
            {
                error = QStringLiteral("Generated component manifest did not contain the new stable type ID.");
                return false;
            }
        }
    }
    const auto manifest = std::find_if(files.begin(), files.end(), [](const auto& file) {
        return file.kind == GeneratedFileKind::manifest;
    });
    if (manifest == files.end())
    {
        error = QStringLiteral("Component transaction did not stage a metadata manifest.");
        return false;
    }
    return validate_staged_metadata(canonical_root, roots, target_manifest, manifest->contents,
        expected_type_id, pending_source_relative, staging, error);
}

bool remove_created_directories(const QStringList& directories, QString& error)
{
    bool succeeded = true;
    for (auto index = directories.size() - 1; index >= 0; --index)
    {
        if (QDir{}.rmdir(directories.at(index))) continue;
        if (!QFileInfo::exists(directories.at(index))) continue;
        succeeded = false;
        if (error.isEmpty())
            error = QStringLiteral("Could not remove rolled-back component directory %1.").arg(directories.at(index));
    }
    return succeeded;
}

bool rollback_generated_files(
    const QString& canonical_root,
    const QList<GeneratedFile>& files,
    const QList<int>& committed,
    const QStringList& created_directories,
    QString& error)
{
    bool succeeded = true;
    for (auto committed_index = committed.size() - 1; committed_index >= 0; --committed_index)
    {
        const auto& file = files.at(committed.at(committed_index));
        if (file.existed)
        {
            QString restore_error;
            if (!write_atomic_contained(canonical_root, file.target_path, file.before, restore_error))
            {
                succeeded = false;
                if (error.isEmpty()) error = restore_error;
                continue;
            }
            if (!QFile::setPermissions(file.target_path, file.permissions))
            {
                succeeded = false;
                if (error.isEmpty())
                    error = QStringLiteral("Could not restore permissions for %1.").arg(file.target_path);
            }
        }
        else if (QFileInfo::exists(file.target_path))
        {
            QString validation_error;
            if (!validate_no_follow_path(canonical_root, file.target_path, true, false, validation_error)
                || !QFile::remove(file.target_path))
            {
                succeeded = false;
                if (error.isEmpty())
                    error = validation_error.isEmpty()
                        ? QStringLiteral("Could not remove rolled-back file %1.").arg(file.target_path)
                        : validation_error;
            }
        }
    }
    if (!remove_created_directories(created_directories, error)) succeeded = false;
    return succeeded;
}

bool commit_generated_files(
    const QList<GeneratedFile>& files,
    const QString& project_root,
    ComponentCreationFault fault,
    QString& error)
{
    QStringList actually_created;
    for (const auto& file : files)
    {
        if (ensure_safe_directory(project_root, QFileInfo{file.target_path}.absolutePath(),
                &actually_created, error)) continue;
        QString rollback_error;
        remove_created_directories(actually_created, rollback_error);
        if (!rollback_error.isEmpty()) error += QStringLiteral(" Rollback failed: %1").arg(rollback_error);
        return false;
    }

    QList<int> committed;
    for (auto index = 0; index < files.size(); ++index)
    {
        const auto& file = files.at(index);
        const auto inject = matches_commit_fault(file.kind, fault);
        if (inject)
        {
            error = QStringLiteral("Injected failure while committing the %1.").arg(generated_file_label(file.kind));
        }
        else
        {
            if (!validate_no_follow_path(project_root, file.target_path, false, false, error))
            {
                // A path was swapped after staging; fail without following it.
            }
            else
            {
                QByteArray current;
                const auto now_exists = QFileInfo::exists(file.target_path);
                if (now_exists != file.existed)
                {
                    error = QStringLiteral("Component target changed while the transaction was staged: %1")
                                .arg(file.target_path);
                }
                else if (now_exists && (!read_all(file.target_path, current, error) || current != file.before))
                {
                    if (error.isEmpty())
                        error = QStringLiteral("Component target changed while the transaction was staged: %1")
                                    .arg(file.target_path);
                }
            }
            if (!error.isEmpty())
            {
                // Fall through to rollback below.
            }
            else
            {
            QFile staged{file.staged_path};
            if (!staged.open(QIODevice::ReadOnly))
                error = QStringLiteral("Could not reopen staged %1: %2")
                            .arg(generated_file_label(file.kind), staged.errorString());
            else if (!write_atomic_contained(project_root, file.target_path, staged.readAll(), error))
            {
                // write_atomic_contained supplied the diagnostic.
            }
            else
            {
                committed.push_back(index);
                continue;
            }
            }
        }

        QString rollback_error;
        if (!rollback_generated_files(project_root, files, committed, actually_created, rollback_error))
            error += QStringLiteral(" Rollback failed: %1").arg(rollback_error);
        return false;
    }
    return true;
}

struct ProcessResult final
{
    bool succeeded{};
    QString output;
};

ProcessResult run(
    const QString& program,
    const QStringList& arguments,
    const QString& working_directory,
    const QProcessEnvironment* environment = nullptr)
{
    QProcess process;
    process.setWorkingDirectory(working_directory);
    process.setProcessChannelMode(QProcess::MergedChannels);
    if (environment != nullptr) process.setProcessEnvironment(*environment);
    process.start(program, arguments);
    if (!process.waitForStarted(10000)) return {false, process.errorString()};
    if (!process.waitForFinished(10 * 60 * 1000))
    {
        process.kill();
        process.waitForFinished();
        return {false, QStringLiteral("Build timed out after ten minutes.\n%1").arg(QString::fromUtf8(process.readAll()))};
    }
    const auto output = QString::fromUtf8(process.readAll());
    return {process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0, output};
}

#if defined(Q_OS_WIN)
bool visual_studio_environment(QProcessEnvironment& environment, QString& error)
{
    const auto program_files_x86 = qEnvironmentVariable("ProgramFiles(x86)");
    const auto vswhere = QDir{program_files_x86}.filePath(
        QStringLiteral("Microsoft Visual Studio/Installer/vswhere.exe"));
    if (!QFileInfo::exists(vswhere))
    {
        error = QStringLiteral("Visual Studio Installer's vswhere.exe was not found. Install Desktop development with C++.");
        return false;
    }
    const auto discovered = run(vswhere,
        {QStringLiteral("-latest"), QStringLiteral("-products"), QStringLiteral("*"),
         QStringLiteral("-requires"), QStringLiteral("Microsoft.VisualStudio.Component.VC.Tools.x86.x64"),
         QStringLiteral("-property"), QStringLiteral("installationPath")},
        QDir::tempPath());
    if (!discovered.succeeded || discovered.output.trimmed().isEmpty())
    {
        error = QStringLiteral("No Visual Studio installation with the C++ toolchain was found.\n%1")
                    .arg(discovered.output.trimmed());
        return false;
    }
    const auto dev_shell = QDir{discovered.output.trimmed()}.filePath(
        QStringLiteral("Common7/Tools/VsDevCmd.bat"));
    if (!QFileInfo::exists(dev_shell))
    {
        error = QStringLiteral("Visual Studio developer shell was not found at %1.").arg(dev_shell);
        return false;
    }

    QTemporaryFile environment_script{QDir{QDir::tempPath()}.filePath(QStringLiteral("dpe-vs-environment-XXXXXX.cmd"))};
    if (!environment_script.open())
    {
        error = QStringLiteral("Could not create the Visual Studio environment bootstrap script: %1")
                    .arg(environment_script.errorString());
        return false;
    }
    const auto script = QStringLiteral("@call \"%1\" -no_logo -arch=x64 -host_arch=x64 >nul\r\n@set\r\n")
                            .arg(QDir::toNativeSeparators(dev_shell)).toLocal8Bit();
    if (environment_script.write(script) != script.size() || !environment_script.flush())
    {
        error = QStringLiteral("Could not write the Visual Studio environment bootstrap script: %1")
                    .arg(environment_script.errorString());
        return false;
    }

    QProcess capture;
    capture.setProcessChannelMode(QProcess::MergedChannels);
    capture.start(QStringLiteral("cmd.exe"),
        {QStringLiteral("/d"), QStringLiteral("/s"), QStringLiteral("/c"),
         QDir::toNativeSeparators(environment_script.fileName())});
    if (!capture.waitForStarted(10000) || !capture.waitForFinished(60000)
        || capture.exitStatus() != QProcess::NormalExit || capture.exitCode() != 0)
    {
        error = QStringLiteral("Visual Studio developer environment initialization failed.\n%1")
                    .arg(QString::fromUtf8(capture.readAll()).trimmed());
        return false;
    }
    environment = QProcessEnvironment::systemEnvironment();
    const auto lines = QString::fromLocal8Bit(capture.readAll()).split(QRegularExpression{QStringLiteral("[\\r\\n]+")}, Qt::SkipEmptyParts);
    for (const auto& line : lines)
    {
        const auto separator = line.indexOf(QLatin1Char{'='});
        if (separator <= 0) continue;
        environment.insert(line.left(separator), line.mid(separator + 1));
    }
    return true;
}
#endif

QString platform_name()
{
#if defined(Q_OS_WIN)
    return QStringLiteral("windows");
#elif defined(Q_OS_MACOS)
    return QStringLiteral("macos");
#else
    return QStringLiteral("linux");
#endif
}

QString architecture_name()
{
    const auto architecture = QSysInfo::currentCpuArchitecture().toLower();
    if (architecture == QStringLiteral("x86_64") || architecture == QStringLiteral("amd64")) return QStringLiteral("x64");
    if (architecture == QStringLiteral("arm64") || architecture == QStringLiteral("aarch64")) return QStringLiteral("arm64");
    return architecture;
}

struct ComponentRecord final
{
    QString type_id;
    QString module_id;
    QString language;
    QString source_relative;
    QString source_absolute;
    int root_index{-1};
};

struct ComponentInputModel final
{
    QList<QFileInfo> files;
    QList<QFileInfo> manifests;
    QList<QFileInfo> csharp_sources;
    QList<QFileInfo> native_sources;
    QList<ComponentRecord> components;
};

bool ignored_source_directory(const QString& name)
{
    return name.compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral("obj"), Qt::CaseInsensitive) == 0
        || name.compare(QStringLiteral(".dragonpixel"), Qt::CaseInsensitive) == 0;
}

bool collect_component_tree(
    const QString& canonical_root,
    const QString& directory,
    ComponentInputModel& model,
    QString& error)
{
    if (!validate_no_follow_path(canonical_root, directory, true, true, error)) return false;
    const auto entries = QDir{directory}.entryInfoList(
        QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System,
        QDir::DirsFirst | QDir::Name);
    for (const auto& entry : entries)
    {
        if (is_link_or_junction(entry))
        {
            error = QStringLiteral("Component roots may not contain symbolic links or junctions: %1")
                        .arg(entry.absoluteFilePath());
            return false;
        }
        if (entry.isDir())
        {
            if (ignored_source_directory(entry.fileName())) continue;
            if (!collect_component_tree(canonical_root, entry.absoluteFilePath(), model, error)) return false;
            continue;
        }
        if (!entry.isFile())
        {
            error = QStringLiteral("Component roots may contain only regular files and directories: %1")
                        .arg(entry.absoluteFilePath());
            return false;
        }
        if (!validate_no_follow_path(canonical_root, entry.absoluteFilePath(), true, false, error)) return false;
        const auto suffix = entry.suffix().toLower();
        if (suffix != QStringLiteral("cs") && suffix != QStringLiteral("cpp")
            && suffix != QStringLiteral("h") && suffix != QStringLiteral("hpp")
            && suffix != QStringLiteral("dpecomponents"))
        {
            continue;
        }
        model.files.push_back(entry);
        if (suffix == QStringLiteral("dpecomponents")) model.manifests.push_back(entry);
        else if (suffix == QStringLiteral("cs")) model.csharp_sources.push_back(entry);
        else model.native_sources.push_back(entry);
    }
    return true;
}

void sort_file_infos(QList<QFileInfo>& files)
{
    std::sort(files.begin(), files.end(), [](const auto& left, const auto& right) {
        return comparison_path(left.absoluteFilePath()) < comparison_path(right.absoluteFilePath());
    });
}

bool collect_component_files(
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    ComponentInputModel& model,
    QString& error)
{
    for (const auto& root : roots)
    {
        if (!collect_component_tree(canonical_root, root.absolute, model, error)) return false;
    }
    sort_file_infos(model.files);
    sort_file_infos(model.manifests);
    sort_file_infos(model.csharp_sources);
    sort_file_infos(model.native_sources);
    return true;
}

int containing_root_index(const QString& path, const QList<DeclaredComponentRoot>& roots)
{
    for (auto index = 0; index < roots.size(); ++index)
    {
        if (lexically_contained(roots.at(index).absolute, path)) return index;
    }
    return -1;
}

bool valid_uuid(const QString& value)
{
    static const QRegularExpression canonical_v4{
        QStringLiteral("^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$")};
    return canonical_v4.match(value).hasMatch()
        && dragonpixel::core::uuid::parse(value.toStdString()).has_value();
}

bool validate_component_manifest_bytes(
    const QByteArray& bytes,
    const QString& manifest_path,
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    const QString& pending_source_relative,
    QSet<QString>& type_ids,
    QSet<QString>& module_ids,
    QList<ComponentRecord>& records,
    QString& error)
{
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        error = QStringLiteral("Invalid metadata-v4 JSON in %1: %2")
                    .arg(manifest_path, parse_error.errorString());
        return false;
    }
    const auto root = document.object();
    if (root.value(QStringLiteral("$schema")).toString()
            != QStringLiteral("https://dragonpixel.dev/schemas/v4/component-metadata.schema.json")
        || root.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.component-metadata")
        || root.value(QStringLiteral("formatVersion")).toInt(-1) != 4
        || !root.value(QStringLiteral("contracts")).isArray()
        || !root.value(QStringLiteral("objectTypes")).isArray()
        || !root.value(QStringLiteral("components")).isArray())
    {
        error = QStringLiteral("Metadata manifest does not satisfy the metadata-v4 root contract: %1")
                    .arg(manifest_path);
        return false;
    }

    for (const auto& value : root.value(QStringLiteral("components")).toArray())
    {
        if (!value.isObject())
        {
            error = QStringLiteral("Metadata component entries must be objects: %1").arg(manifest_path);
            return false;
        }
        const auto component = value.toObject();
        const auto type_id = component.value(QStringLiteral("typeId")).toString();
        const auto module_id = component.value(QStringLiteral("runtimeModuleId")).toString();
        const auto language = component.value(QStringLiteral("implementationLanguage")).toString();
        const auto owner = component.value(QStringLiteral("owner")).toString();
        const auto qualified_name = component.value(QStringLiteral("qualifiedName")).toString();
        const auto display_name = component.value(QStringLiteral("displayName")).toString();
        if (!valid_uuid(type_id) || type_ids.contains(type_id)
            || qualified_name.trimmed().isEmpty() || display_name.trimmed().isEmpty()
            || component.value(QStringLiteral("schemaVersion")).toInt(0) <= 0
            || !component.value(QStringLiteral("properties")).isArray())
        {
            error = QStringLiteral("Metadata component has an invalid/duplicate identity or descriptor: %1")
                        .arg(manifest_path);
            return false;
        }
        type_ids.insert(type_id);
        QSet<QString> property_ids;
        for (const auto& property_value : component.value(QStringLiteral("properties")).toArray())
        {
            const auto property = property_value.toObject();
            const auto property_id = property.value(QStringLiteral("propertyId")).toString();
            if (!property_value.isObject() || property_id.trimmed().isEmpty()
                || property_ids.contains(property_id)
                || property.value(QStringLiteral("displayName")).toString().trimmed().isEmpty()
                || !property.value(QStringLiteral("shape")).isObject()
                || property.value(QStringLiteral("shape")).toObject()
                       .value(QStringLiteral("kind")).toString().isEmpty())
            {
                error = QStringLiteral("Metadata component contains an invalid or duplicate property: %1")
                            .arg(manifest_path);
                return false;
            }
            property_ids.insert(property_id);
        }

        if (language == QStringLiteral("data-only"))
        {
            if (owner != QStringLiteral("data-only") || !module_id.isEmpty()
                || !component.value(QStringLiteral("sourcePath")).toString().isEmpty())
            {
                error = QStringLiteral("Data-only component ownership/source metadata is inconsistent: %1")
                            .arg(manifest_path);
                return false;
            }
            continue;
        }
        if ((language == QStringLiteral("csharp") && owner != QStringLiteral("managed"))
            || (language == QStringLiteral("cpp") && owner != QStringLiteral("native"))
            || (language != QStringLiteral("csharp") && language != QStringLiteral("cpp"))
            || !valid_uuid(module_id) || module_ids.contains(module_id))
        {
            error = QStringLiteral("Executable component ownership, language, or module identity is invalid: %1")
                        .arg(manifest_path);
            return false;
        }
        module_ids.insert(module_id);
        const auto declared_source = QDir::fromNativeSeparators(
            component.value(QStringLiteral("sourcePath")).toString());
        if (declared_source.trimmed().isEmpty() || QDir::isAbsolutePath(declared_source)
            || declared_source == QStringLiteral("..")
            || declared_source.startsWith(QStringLiteral("../")))
        {
            error = QStringLiteral("Executable component sourcePath must be project-relative: %1")
                        .arg(manifest_path);
            return false;
        }
        const auto source_relative = QDir::fromNativeSeparators(QDir::cleanPath(declared_source));
        const auto source_absolute = normalized_path(QDir{canonical_root}.filePath(source_relative));
        const auto root_index = containing_root_index(source_absolute, roots);
        if (root_index < 0)
        {
            error = QStringLiteral("Component source is outside every declared component root: %1")
                        .arg(declared_source);
            return false;
        }
        const auto is_pending = !pending_source_relative.isEmpty()
            && comparison_path(source_relative) == comparison_path(pending_source_relative);
        if (!is_pending
            && !validate_no_follow_path(canonical_root, source_absolute, true, false, error))
        {
            return false;
        }
        const auto suffix = QFileInfo{source_relative}.suffix();
        if ((language == QStringLiteral("csharp") && suffix.compare(QStringLiteral("cs"), Qt::CaseInsensitive) != 0)
            || (language == QStringLiteral("cpp") && suffix.compare(QStringLiteral("cpp"), Qt::CaseInsensitive) != 0))
        {
            error = QStringLiteral("Component source extension does not match implementationLanguage: %1")
                        .arg(declared_source);
            return false;
        }
        records.push_back({type_id, module_id, language, source_relative, source_absolute, root_index});
    }
    return true;
}

bool validate_metadata_registry(
    const QString& project_root,
    const QList<DeclaredComponentRoot>& roots,
    const QString& expected_type_id,
    QString& error)
{
    QStringList declarations;
    for (const auto& root : roots) declarations.push_back(root.declared);
    auto registry = dragonpixel::metadata::registry::slice_one_defaults();
    const auto loaded = MetadataManifestService{}.load_project(project_root, declarations, registry);
    if (!loaded.succeeded)
    {
        error = QStringLiteral("Metadata-v4 registry validation failed: %1")
                    .arg(loaded.diagnostics.join(QStringLiteral(" | ")));
        return false;
    }
    if (!expected_type_id.isEmpty() && registry.find(expected_type_id.toStdString()) == nullptr)
    {
        error = QStringLiteral("Metadata-v4 registry did not expose the newly generated component.");
        return false;
    }
    return true;
}

bool load_component_model(
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    ComponentInputModel& model,
    QString& error)
{
    if (!collect_component_files(canonical_root, roots, model, error)) return false;
    if (model.manifests.isEmpty())
    {
        error = QStringLiteral("No metadata-v4 manifests were found in the declared component roots.");
        return false;
    }
    QSet<QString> type_ids;
    QSet<QString> module_ids;
    for (const auto& manifest : model.manifests)
    {
        QByteArray bytes;
        if (!read_all(manifest.absoluteFilePath(), bytes, error)
            || !validate_component_manifest_bytes(bytes, manifest.absoluteFilePath(), canonical_root,
                roots, {}, type_ids, module_ids, model.components, error))
        {
            return false;
        }
    }
    QSet<QString> executable_sources;
    for (const auto& component : model.components)
    {
        const auto source_key = comparison_path(component.source_absolute);
        if (executable_sources.contains(source_key))
        {
            error = QStringLiteral("Multiple executable component records declare the same source file: %1")
                        .arg(component.source_relative);
            return false;
        }
        executable_sources.insert(source_key);
    }
    if (!validate_metadata_registry(canonical_root, roots, {}, error)) return false;
    if (model.components.isEmpty())
    {
        error = QStringLiteral("No executable project components were declared by metadata-v4.");
        return false;
    }
    std::sort(model.components.begin(), model.components.end(), [](const auto& left, const auto& right) {
        return left.type_id < right.type_id;
    });
    return true;
}

bool validate_staged_metadata(
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    const QString& target_manifest,
    const QByteArray& staged_manifest,
    const QString& expected_type_id,
    const QString& pending_source_relative,
    QTemporaryDir& staging,
    QString& error)
{
    ComponentInputModel existing;
    if (!collect_component_files(canonical_root, roots, existing, error)) return false;
    QSet<QString> type_ids;
    QSet<QString> module_ids;
    QList<ComponentRecord> records;
    bool target_seen = false;
    for (const auto& manifest : existing.manifests)
    {
        const auto is_target = comparison_path(manifest.absoluteFilePath())
            == comparison_path(target_manifest);
        QByteArray bytes;
        if (is_target)
        {
            bytes = staged_manifest;
            target_seen = true;
        }
        else if (!read_all(manifest.absoluteFilePath(), bytes, error))
        {
            return false;
        }
        if (!validate_component_manifest_bytes(bytes, manifest.absoluteFilePath(), canonical_root,
                roots, pending_source_relative, type_ids, module_ids, records, error))
        {
            return false;
        }
    }
    if (!target_seen
        && !validate_component_manifest_bytes(staged_manifest, target_manifest, canonical_root,
            roots, pending_source_relative, type_ids, module_ids, records, error))
    {
        return false;
    }
    QSet<QString> executable_sources;
    for (const auto& component : records)
    {
        const auto source_key = comparison_path(component.source_absolute);
        if (executable_sources.contains(source_key))
        {
            error = QStringLiteral("Staged metadata declares one executable source more than once: %1")
                        .arg(component.source_relative);
            return false;
        }
        executable_sources.insert(source_key);
    }

    const auto validation_root = QDir{staging.path()}.filePath(QStringLiteral("metadata-validation"));
    if (!QDir{}.mkpath(validation_root))
    {
        error = QStringLiteral("Could not create metadata-v4 validation staging.");
        return false;
    }
    for (const auto& root : roots)
    {
        if (!QDir{}.mkpath(QDir{validation_root}.filePath(root.declared)))
        {
            error = QStringLiteral("Could not mirror a declared root for staged metadata validation.");
            return false;
        }
    }
    for (const auto& manifest : existing.manifests)
    {
        const auto relative = QDir::fromNativeSeparators(
            QDir{canonical_root}.relativeFilePath(manifest.absoluteFilePath()));
        const auto destination = QDir{validation_root}.filePath(relative);
        if (!QDir{}.mkpath(QFileInfo{destination}.absolutePath()))
        {
            error = QStringLiteral("Could not create staged metadata parent storage.");
            return false;
        }
        QByteArray bytes;
        if (comparison_path(manifest.absoluteFilePath()) == comparison_path(target_manifest))
            bytes = staged_manifest;
        else if (!read_all(manifest.absoluteFilePath(), bytes, error))
            return false;
        if (!write_atomic(destination, bytes, error)) return false;
    }
    if (!target_seen)
    {
        const auto relative = QDir::fromNativeSeparators(
            QDir{canonical_root}.relativeFilePath(target_manifest));
        const auto destination = QDir{validation_root}.filePath(relative);
        if (!QDir{}.mkpath(QFileInfo{destination}.absolutePath())
            || !write_atomic(destination, staged_manifest, error))
        {
            if (error.isEmpty()) error = QStringLiteral("Could not stage the generated metadata manifest.");
            return false;
        }
    }
    return validate_metadata_registry(validation_root, roots, expected_type_id, error);
}

QString staged_source_path(
    const QFileInfo& source,
    const QList<DeclaredComponentRoot>& roots,
    const QString& prefix)
{
    const auto absolute = normalized_path(source.absoluteFilePath());
    const auto root_index = containing_root_index(absolute, roots);
    const auto within_root = QDir::fromNativeSeparators(
        QDir{roots.at(root_index).absolute}.relativeFilePath(absolute));
    return QStringLiteral("%1/root%2/%3").arg(prefix).arg(root_index).arg(within_root);
}

QString staged_record_path(
    const ComponentRecord& component,
    const QList<DeclaredComponentRoot>& roots,
    const QString& prefix)
{
    const auto within_root = QDir::fromNativeSeparators(
        QDir{roots.at(component.root_index).absolute}.relativeFilePath(component.source_absolute));
    return QStringLiteral("%1/root%2/%3").arg(prefix).arg(component.root_index).arg(within_root);
}

QByteArray native_project(
    const ComponentInputModel& model,
    const QList<DeclaredComponentRoot>& roots)
{
    QString cmake = QStringLiteral(
        "cmake_minimum_required(VERSION 3.25)\n"
        "project(DragonPixelProjectComponents LANGUAGES CXX)\n"
        "set(CMAKE_CXX_STANDARD 20)\n"
        "set(CMAKE_CXX_STANDARD_REQUIRED ON)\n"
        "set(CMAKE_POSITION_INDEPENDENT_CODE ON)\n");
    for (const auto& component : model.components)
    {
        if (component.language != QStringLiteral("cpp")) continue;
        auto identity = component.type_id;
        identity.remove(QLatin1Char{'-'});
        const auto target = QStringLiteral("dpe_component_%1").arg(identity);
        const auto source = staged_record_path(component, roots, QStringLiteral("native-sources"));
        const auto source_directory = QDir::fromNativeSeparators(QFileInfo{source}.path());
        const auto root_directory = QStringLiteral("native-sources/root%1").arg(component.root_index);
        cmake += QStringLiteral("add_library(%1 SHARED \"%2\")\n")
                     .arg(target, cmake_escape(source));
        cmake += QStringLiteral("target_include_directories(%1 PRIVATE \"${CMAKE_CURRENT_SOURCE_DIR}/%2\" \"${CMAKE_CURRENT_SOURCE_DIR}/%3\")\n")
                     .arg(target, cmake_escape(source_directory), cmake_escape(root_directory));
        cmake += QStringLiteral("set_target_properties(%1 PROPERTIES OUTPUT_NAME \"%1\" PREFIX \"\")\n")
                     .arg(target);
    }
    return cmake.toUtf8();
}

QString native_artifact_filename(QString type_id)
{
    type_id.remove(QLatin1Char{'-'});
#if defined(Q_OS_WIN)
    return QStringLiteral("dpe_component_%1.dll").arg(type_id);
#elif defined(Q_OS_MACOS)
    return QStringLiteral("dpe_component_%1.dylib").arg(type_id);
#else
    return QStringLiteral("dpe_component_%1.so").arg(type_id);
#endif
}

void add_hash_frame(QCryptographicHash& hasher, const QByteArray& label, const QByteArray& value)
{
    hasher.addData(QByteArray::number(label.size()));
    hasher.addData(QByteArrayLiteral(":"));
    hasher.addData(label);
    hasher.addData(QByteArray::number(value.size()));
    hasher.addData(QByteArrayLiteral(":"));
    hasher.addData(value);
}

bool sha256_file(const QString& path, QString& digest, QString& error)
{
    QByteArray bytes;
    if (!read_all(path, bytes, error)) return false;
    digest = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
    return true;
}

bool file_identity(const QString& path, QByteArray& identity, QString& error)
{
    const QFileInfo info{path};
    const auto canonical = info.canonicalFilePath();
    if (canonical.isEmpty() || !info.isFile())
    {
        error = QStringLiteral("Required component-build input was not found: %1").arg(path);
        return false;
    }
    QString digest;
    if (!sha256_file(canonical, digest, error)) return false;
    identity = QStringLiteral("%1|%2|%3|%4")
                   .arg(QDir::fromNativeSeparators(canonical))
                   .arg(info.size())
                   .arg(info.lastModified().toUTC().toMSecsSinceEpoch())
                   .arg(digest)
                   .toUtf8();
    return true;
}

struct BuildIdentity final
{
    QString build_hash;
    QString contracts_sha256;
    QByteArray dotnet_identity;
    QByteArray cmake_identity;
    QByteArray cxx_identity;
    QByteArray managed_project;
    QByteArray native_project_file;
};

bool compute_build_identity(
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    const ComponentInputModel& model,
    const QString& contracts_assembly,
    BuildIdentity& identity,
    QString& error)
{
    const QFileInfo contracts_info{contracts_assembly};
    const auto contracts_path = contracts_info.canonicalFilePath();
    if (contracts_path.isEmpty() || !contracts_info.isFile()
        || !sha256_file(contracts_path, identity.contracts_sha256, error))
    {
        if (error.isEmpty())
            error = QStringLiteral("DragonPixel.Contracts was not found at %1.").arg(contracts_assembly);
        return false;
    }
    if (!file_identity(QString::fromUtf8(DPE_DOTNET_EXECUTABLE), identity.dotnet_identity, error)
        || !file_identity(QString::fromUtf8(DPE_CMAKE_EXECUTABLE), identity.cmake_identity, error)
        || !file_identity(QString::fromUtf8(DPE_CXX_COMPILER), identity.cxx_identity, error))
    {
        return false;
    }
    QByteArray make_program_identity;
    if (!file_identity(QString::fromUtf8(DPE_COMPONENT_MAKE_PROGRAM), make_program_identity, error))
    {
        return false;
    }
    const auto dotnet_info = run(QString::fromUtf8(DPE_DOTNET_EXECUTABLE),
        {QStringLiteral("--info")}, canonical_root);
    const auto cmake_version = run(QString::fromUtf8(DPE_CMAKE_EXECUTABLE),
        {QStringLiteral("--version")}, canonical_root);
    if (!dotnet_info.succeeded || !cmake_version.succeeded)
    {
        error = QStringLiteral("Could not determine the component-build toolchain identity.\n%1\n%2")
                    .arg(dotnet_info.output, cmake_version.output);
        return false;
    }
    identity.dotnet_identity.append("\n--dotnet-info--\n").append(dotnet_info.output.toUtf8());
    identity.cmake_identity.append("\n--cmake-version--\n").append(cmake_version.output.toUtf8());
    identity.cxx_identity.append("\n--compiler--\n")
        .append(QByteArrayLiteral(DPE_CXX_COMPILER_ID)).append('\n')
        .append(QByteArrayLiteral(DPE_CXX_COMPILER_VERSION)).append('\n')
        .append(QByteArrayLiteral(DPE_COMPONENT_CMAKE_GENERATOR))
        .append("\n--build-program--\n")
        .append(make_program_identity);
    QStringList managed_sources;
    for (const auto& source : model.csharp_sources)
        managed_sources.push_back(staged_source_path(source, roots, QStringLiteral("managed-sources")));
    identity.managed_project = csharp_project(managed_sources);
    identity.native_project_file = native_project(model, roots);

    QCryptographicHash hasher{QCryptographicHash::Sha256};
    add_hash_frame(hasher, QByteArrayLiteral("generator"), QByteArrayLiteral(component_generator_identity));
    add_hash_frame(hasher, QByteArrayLiteral("configuration"), QByteArrayLiteral(component_build_configuration));
    add_hash_frame(hasher, QByteArrayLiteral("platform"), platform_name().toUtf8());
    add_hash_frame(hasher, QByteArrayLiteral("architecture"), architecture_name().toUtf8());
    for (const auto& root : roots)
        add_hash_frame(hasher, QByteArrayLiteral("component-root"), root.declared.toUtf8());
    for (const auto& input : model.files)
    {
        if (!validate_no_follow_path(canonical_root, input.absoluteFilePath(), true, false, error)) return false;
        QByteArray bytes;
        if (!read_all(input.absoluteFilePath(), bytes, error)) return false;
        add_hash_frame(hasher, QByteArrayLiteral("input-path"),
            QDir::fromNativeSeparators(QDir{canonical_root}.relativeFilePath(input.absoluteFilePath())).toUtf8());
        add_hash_frame(hasher, QByteArrayLiteral("input-bytes"), bytes);
    }
    add_hash_frame(hasher, QByteArrayLiteral("contracts-path"),
        QDir::fromNativeSeparators(contracts_path).toUtf8());
    add_hash_frame(hasher, QByteArrayLiteral("contracts-sha256"), identity.contracts_sha256.toLatin1());
    add_hash_frame(hasher, QByteArrayLiteral("dotnet-identity"), identity.dotnet_identity);
    add_hash_frame(hasher, QByteArrayLiteral("cmake-identity"), identity.cmake_identity);
    add_hash_frame(hasher, QByteArrayLiteral("cxx-identity"), identity.cxx_identity);
    add_hash_frame(hasher, QByteArrayLiteral("managed-template"), identity.managed_project);
    add_hash_frame(hasher, QByteArrayLiteral("native-template"), identity.native_project_file);
    identity.build_hash = QString::fromLatin1(hasher.result().toHex());
    return true;
}

QJsonObject runtime_component(const ComponentRecord& component)
{
    return {
        {QStringLiteral("typeId"), component.type_id},
        {QStringLiteral("moduleId"), component.module_id},
        {QStringLiteral("sourcePath"), component.source_relative},
    };
}

bool validate_artifact(
    const QString& canonical_root,
    const QString& expected_directory,
    const QJsonObject& module,
    QString& path,
    QString& error)
{
    path = normalized_path(module.value(QStringLiteral("path")).toString());
    const auto expected_hash = module.value(QStringLiteral("sha256")).toString();
    if (!QDir::isAbsolutePath(module.value(QStringLiteral("path")).toString())
        || !lexically_contained(expected_directory, path)
        || expected_hash.size() != 64
        || !validate_no_follow_path(canonical_root, path, true, false, error))
    {
        if (error.isEmpty()) error = QStringLiteral("Runtime module path/hash metadata is missing or unsafe.");
        return false;
    }
    QString actual_hash;
    if (!sha256_file(path, actual_hash, error)) return false;
    if (actual_hash.compare(expected_hash, Qt::CaseInsensitive) != 0)
    {
        error = QStringLiteral("Runtime module artifact hash mismatch: %1").arg(path);
        return false;
    }
    return true;
}

bool validate_runtime_manifest(
    const QString& canonical_root,
    const QList<DeclaredComponentRoot>& roots,
    const ComponentInputModel& model,
    const BuildIdentity& identity,
    const QString& cache_directory,
    const QString& manifest_path,
    int& managed_count,
    int& native_count,
    QString& error)
{
    if (!validate_no_follow_path(canonical_root, manifest_path, true, false, error)) return false;
    QByteArray bytes;
    if (!read_all(manifest_path, bytes, error)) return false;
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        error = QStringLiteral("Runtime-module manifest is malformed: %1").arg(parse_error.errorString());
        return false;
    }
    const auto manifest = document.object();
    QStringList declared_roots;
    for (const auto& root : roots) declared_roots.push_back(root.declared);
    QStringList recorded_roots;
    for (const auto& value : manifest.value(QStringLiteral("componentRoots")).toArray())
        recorded_roots.push_back(value.toString());
    const auto tool_identities = manifest.value(QStringLiteral("toolIdentities")).toObject();
    const auto expected_dotnet_identity = QString::fromLatin1(QCryptographicHash::hash(
        identity.dotnet_identity, QCryptographicHash::Sha256).toHex());
    const auto expected_cmake_identity = QString::fromLatin1(QCryptographicHash::hash(
        identity.cmake_identity, QCryptographicHash::Sha256).toHex());
    const auto expected_cxx_identity = QString::fromLatin1(QCryptographicHash::hash(
        identity.cxx_identity, QCryptographicHash::Sha256).toHex());
    if (manifest.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.runtime-modules")
        || manifest.value(QStringLiteral("formatVersion")).toInt(-1) != 1
        || manifest.value(QStringLiteral("buildHash")).toString() != identity.build_hash
        || manifest.value(QStringLiteral("platform")).toString() != platform_name()
        || manifest.value(QStringLiteral("architecture")).toString() != architecture_name()
        || manifest.value(QStringLiteral("configuration")).toString()
            != QString::fromLatin1(component_build_configuration)
        || manifest.value(QStringLiteral("generatorIdentity")).toString()
            != QString::fromLatin1(component_generator_identity)
        || manifest.value(QStringLiteral("contractsSha256")).toString() != identity.contracts_sha256
        || declared_roots != recorded_roots
        || !manifest.value(QStringLiteral("toolIdentities")).isObject()
        || tool_identities.value(QStringLiteral("dotnet")).toString() != expected_dotnet_identity
        || tool_identities.value(QStringLiteral("cmake")).toString() != expected_cmake_identity
        || tool_identities.value(QStringLiteral("cxx")).toString() != expected_cxx_identity
        || !manifest.value(QStringLiteral("managedModules")).isArray()
        || !manifest.value(QStringLiteral("nativeModules")).isArray())
    {
        error = QStringLiteral("Runtime-module manifest identity does not match current declared inputs.");
        return false;
    }

    QSet<QString> expected_managed;
    QSet<QString> expected_native;
    QHash<QString, QString> expected_managed_modules;
    QHash<QString, QString> expected_native_modules;
    QHash<QString, QString> expected_sources;
    for (const auto& component : model.components)
    {
        if (component.language == QStringLiteral("csharp"))
        {
            expected_managed.insert(component.type_id);
            expected_managed_modules.insert(component.type_id, component.module_id);
        }
        else if (component.language == QStringLiteral("cpp"))
        {
            expected_native.insert(component.type_id);
            expected_native_modules.insert(component.type_id, component.module_id);
        }
        expected_sources.insert(component.type_id, component.source_relative);
    }
    QSet<QString> actual_managed;
    const auto managed_modules = manifest.value(QStringLiteral("managedModules")).toArray();
    if (managed_modules.size() != (expected_managed.isEmpty() ? 0 : 1))
    {
        error = QStringLiteral("Runtime manifest has an unexpected managed module count.");
        return false;
    }
    for (const auto& value : managed_modules)
    {
        if (!value.isObject()) { error = QStringLiteral("Managed runtime module is malformed."); return false; }
        const auto module = value.toObject();
        QString artifact;
        if (!validate_artifact(canonical_root, QDir{cache_directory}.filePath(QStringLiteral("managed")),
                module, artifact, error)
            || comparison_path(artifact) != comparison_path(QDir{cache_directory}.filePath(
                QStringLiteral("managed/DragonPixel.ProjectComponents.dll"))))
        {
            return false;
        }
        for (const auto& component_value : module.value(QStringLiteral("components")).toArray())
        {
            const auto component = component_value.toObject();
            const auto type_id = component.value(QStringLiteral("typeId")).toString();
            if (!component_value.isObject() || !expected_managed.contains(type_id)
                || actual_managed.contains(type_id)
                || component.value(QStringLiteral("moduleId")).toString()
                    != expected_managed_modules.value(type_id)
                || component.value(QStringLiteral("sourcePath")).toString()
                    != expected_sources.value(type_id))
            {
                error = QStringLiteral("Managed runtime component metadata is invalid or stale.");
                return false;
            }
            actual_managed.insert(type_id);
        }
    }
    if (actual_managed != expected_managed)
    {
        error = QStringLiteral("Managed runtime component set does not match metadata-v4.");
        return false;
    }

    QSet<QString> actual_native;
    const auto native_modules = manifest.value(QStringLiteral("nativeModules")).toArray();
    for (const auto& value : native_modules)
    {
        if (!value.isObject()) { error = QStringLiteral("Native runtime module is malformed."); return false; }
        const auto module = value.toObject();
        const auto component = module.value(QStringLiteral("component")).toObject();
        const auto type_id = component.value(QStringLiteral("typeId")).toString();
        QString artifact;
        if (!module.value(QStringLiteral("component")).isObject()
            || !expected_native.contains(type_id) || actual_native.contains(type_id)
            || component.value(QStringLiteral("moduleId")).toString() != expected_native_modules.value(type_id)
            || component.value(QStringLiteral("sourcePath")).toString() != expected_sources.value(type_id)
            || !validate_artifact(canonical_root, QDir{cache_directory}.filePath(QStringLiteral("native")),
                module, artifact, error)
            || comparison_path(artifact) != comparison_path(QDir{cache_directory}.filePath(
                QStringLiteral("native/%1").arg(native_artifact_filename(type_id)))))
        {
            if (error.isEmpty()) error = QStringLiteral("Native runtime component metadata is invalid or stale.");
            return false;
        }
        actual_native.insert(type_id);
    }
    if (actual_native != expected_native)
    {
        error = QStringLiteral("Native runtime component set does not match metadata-v4.");
        return false;
    }
    managed_count = actual_managed.size();
    native_count = actual_native.size();
    return true;
}

bool copy_to_staging(const QString& source, const QString& staging_root, const QString& relative, QString& error)
{
    QByteArray bytes;
    if (!read_all(source, bytes, error)) return false;
    const auto destination = QDir{staging_root}.filePath(relative);
    if (!QDir{}.mkpath(QFileInfo{destination}.absolutePath()))
    {
        error = QStringLiteral("Could not create isolated component-build staging directories.");
        return false;
    }
    return write_atomic(destination, bytes, error);
}
}

ComponentCreationResult ComponentModuleService::create(const ComponentCreationRequest& request)
{
    ComponentCreationResult result;
    QString root;
    QString validation_error;
    if (!canonical_project_root(request.project_root, root, validation_error))
    {
        result.error = validation_error;
        return result;
    }
    QList<DeclaredComponentRoot> component_roots;
    if (!validate_declared_roots(root, request.component_roots, false,
            component_roots, validation_error))
    {
        result.error = validation_error;
        return result;
    }
    const auto selected_declaration = request.selected_component_root.trimmed().isEmpty()
        ? (component_roots.size() == 1 ? component_roots.front().declared : QString{})
        : QDir::fromNativeSeparators(QDir::cleanPath(request.selected_component_root));
    const auto selected_iterator = std::find_if(
        component_roots.cbegin(), component_roots.cend(), [&](const auto& candidate) {
            return candidate.declared == selected_declaration;
        });
    if (selected_iterator == component_roots.cend())
    {
        result.error = component_roots.size() > 1
            ? QStringLiteral("Component creation requires an explicit declared component-root selection.")
            : QStringLiteral("The selected component root is not declared by the project.");
        return result;
    }
    const auto& selected_root = *selected_iterator;
    const auto class_name = safe_identifier(request.display_name);
    if (class_name.isEmpty())
    {
        result.error = QStringLiteral("A non-empty component name is required.");
        return result;
    }
    if (request.injected_fault == ComponentCreationFault::stage_build_definition_write
        || request.injected_fault == ComponentCreationFault::commit_build_definition)
    {
        result.error = QStringLiteral("Injected failure for a source-tree build definition; build definitions are isolated now.");
        return result;
    }
    const auto language_folder = request.language == ProjectComponentLanguage::csharp
        ? QStringLiteral("CSharp") : QStringLiteral("Cpp");
    QDir project{root};
    const auto relative_source = QDir::fromNativeSeparators(
        QDir{selected_root.declared}.filePath(
            QStringLiteral("%1/%2.%3").arg(language_folder, class_name,
                request.language == ProjectComponentLanguage::csharp
                    ? QStringLiteral("cs") : QStringLiteral("cpp"))));
    result.source_path = normalized_path(project.filePath(relative_source));
    result.manifest_path = normalized_path(
        QDir{selected_root.absolute}.filePath(QStringLiteral("ProjectComponents.dpecomponents")));
    if (!validate_no_follow_path(root, result.source_path, false, false, validation_error)
        || !validate_no_follow_path(root, result.manifest_path, false, false, validation_error))
    {
        result.error = validation_error;
        return result;
    }
    if (QFileInfo::exists(result.source_path))
    {
        result.error = QStringLiteral("A component source already exists at %1.").arg(result.source_path);
        return result;
    }
    QJsonObject manifest = empty_manifest();
    if (QFileInfo::exists(result.manifest_path))
    {
        QByteArray bytes;
        if (!read_all(result.manifest_path, bytes, result.error)) return result;
        QJsonParseError error;
        const auto document = QJsonDocument::fromJson(bytes, &error);
        if (error.error != QJsonParseError::NoError || !document.isObject())
        {
            result.error = QStringLiteral("Project component manifest is invalid: %1").arg(error.errorString());
            return result;
        }
        manifest = document.object();
    }
    result.type_id = QString::fromStdString(dragonpixel::core::uuid::random_v4().to_string());
    result.module_id = QString::fromStdString(dragonpixel::core::uuid::random_v4().to_string());
    auto components = manifest.value(QStringLiteral("components")).toArray();
    QJsonArray properties;
    if (request.language == ProjectComponentLanguage::csharp)
    {
        properties.push_back(QJsonObject{
            {QStringLiteral("propertyId"), QStringLiteral("%1.horizontal_action").arg(result.type_id)},
            {QStringLiteral("displayName"), QStringLiteral("Horizontal Action")},
            {QStringLiteral("order"), 0},
            {QStringLiteral("readOnly"), false},
            {QStringLiteral("shape"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("string")}, {QStringLiteral("nullable"), false}}},
            {QStringLiteral("default"), QStringLiteral("move.x")},
            {QStringLiteral("category"), QStringLiteral("Input")},
        });
        properties.push_back(QJsonObject{
            {QStringLiteral("propertyId"), QStringLiteral("%1.vertical_action").arg(result.type_id)},
            {QStringLiteral("displayName"), QStringLiteral("Vertical Action")},
            {QStringLiteral("order"), 1},
            {QStringLiteral("readOnly"), false},
            {QStringLiteral("shape"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("string")}, {QStringLiteral("nullable"), false}}},
            {QStringLiteral("default"), QStringLiteral("move.y")},
            {QStringLiteral("category"), QStringLiteral("Input")},
        });
    }
    properties.push_back(QJsonObject{
        {QStringLiteral("propertyId"), QStringLiteral("%1.speed").arg(result.type_id)},
        {QStringLiteral("displayName"), QStringLiteral("Speed")},
        {QStringLiteral("order"), request.language == ProjectComponentLanguage::csharp ? 2 : 0},
        {QStringLiteral("readOnly"), false},
        {QStringLiteral("shape"), QJsonObject{{QStringLiteral("kind"), QStringLiteral("number")}, {QStringLiteral("nullable"), false}}},
        {QStringLiteral("default"), request.language == ProjectComponentLanguage::csharp ? 5.0 : 1.0},
        {QStringLiteral("minimum"), 0.0},
        {QStringLiteral("maximum"), 1000.0},
        {QStringLiteral("step"), 0.1},
        {QStringLiteral("category"), QStringLiteral("Input")},
    });
    components.push_back(QJsonObject{
        {QStringLiteral("typeId"), result.type_id},
        {QStringLiteral("qualifiedName"), QStringLiteral("DragonPixel.ProjectComponents.%1").arg(class_name)},
        {QStringLiteral("displayName"), request.display_name.trimmed()},
        {QStringLiteral("schemaVersion"), 1},
        {QStringLiteral("owner"), request.language == ProjectComponentLanguage::csharp ? QStringLiteral("managed") : QStringLiteral("native")},
        {QStringLiteral("implementationLanguage"), request.language == ProjectComponentLanguage::csharp ? QStringLiteral("csharp") : QStringLiteral("cpp")},
        {QStringLiteral("category"), request.category.trimmed().isEmpty() ? QStringLiteral("Scripts") : request.category.trimmed()},
        {QStringLiteral("tooltip"), QStringLiteral("Project-defined component executed only inside Preview and Play workers.")},
        {QStringLiteral("addable"), true},
        {QStringLiteral("removable"), true},
        {QStringLiteral("resettable"), true},
        {QStringLiteral("sourcePath"), relative_source},
        {QStringLiteral("runtimeModuleId"), result.module_id},
        {QStringLiteral("properties"), properties},
    });
    manifest.insert(QStringLiteral("components"), components);
    const auto source = request.language == ProjectComponentLanguage::csharp
        ? csharp_source(class_name, result.type_id, request.display_name.trimmed())
        : cpp_source(class_name, result.type_id, request.display_name.trimmed());

    QList<GeneratedFile> files{
        generated_file(GeneratedFileKind::source, result.source_path, source),
    };
    if (request.language == ProjectComponentLanguage::cpp)
    {
        const auto header_path = normalized_path(QDir{selected_root.absolute}.filePath(
            QStringLiteral("Cpp/dpe_component_plugin.h")));
        if (!validate_no_follow_path(root, header_path, false, false, validation_error))
        {
            result.error = validation_error;
            return result;
        }
        if (!QFileInfo::exists(header_path))
            files.push_back(generated_file(
                GeneratedFileKind::native_header,
                header_path,
                native_header()));
        else
        {
            QByteArray existing_header;
            if (!read_all(header_path, existing_header, result.error)) return result;
            if (existing_header != native_header())
            {
                result.error = QStringLiteral(
                    "The declared component root contains an unrecognized dpe_component_plugin.h; it was not overwritten.");
                return result;
            }
        }
    }
    files.push_back(generated_file(
        GeneratedFileKind::manifest,
        result.manifest_path,
        QJsonDocument{manifest}.toJson(QJsonDocument::Indented)));

    for (const auto& file : files)
    {
        if (validate_no_follow_path(root, file.target_path, false, false, validation_error)) continue;
        result.error = validation_error;
        return result;
    }

    QString error;
    if (!capture_before_images(root, files, error))
    {
        result.error = error;
        return result;
    }
    QTemporaryDir staging{QDir{QDir::tempPath()}.filePath(QStringLiteral("dpe-component-create-XXXXXX"))};
    if (!staging.isValid())
    {
        result.error = QStringLiteral("Could not create component generation staging storage.");
        return result;
    }
    if (!stage_generated_files(files, staging, request.injected_fault, root, component_roots,
            result.manifest_path, result.type_id, relative_source, error)
        || !commit_generated_files(files, root, request.injected_fault, error))
    {
        result.error = error;
        return result;
    }
    result.succeeded = true;
    return result;
}

QString ComponentModuleService::active_runtime_manifest(
    const QString& project_root,
    const QStringList& component_roots,
    const QString& contracts_assembly)
{
    QString root;
    QString error;
    QList<DeclaredComponentRoot> roots;
    if (!canonical_project_root(project_root, root, error)
        || !validate_declared_roots(root, component_roots, false, roots, error))
    {
        return {};
    }
    ComponentInputModel model;
    BuildIdentity identity;
    if (!load_component_model(root, roots, model, error)
        || !compute_build_identity(root, roots, model, contracts_assembly, identity, error))
    {
        return {};
    }
    const auto cache_directory = normalized_path(QDir{root}.filePath(
        QStringLiteral(".dragonpixel/Cache/%1/%2-%3")
            .arg(identity.build_hash, platform_name(), architecture_name())));
    if (!validate_no_follow_path(root, cache_directory, true, true, error)) return {};
    const auto active_path = normalized_path(
        QDir{root}.filePath(QStringLiteral(".dragonpixel/Cache/active-runtime-modules.json")));
    int managed_count{};
    int native_count{};
    if (!validate_runtime_manifest(root, roots, model, identity, cache_directory,
            active_path, managed_count, native_count, error))
    {
        return {};
    }
    return active_path;
}

ComponentBuildResult ComponentModuleService::build(
    const QString& project_root,
    const QStringList& component_roots,
    const QString& contracts_assembly)
{
    ComponentBuildResult result;
    QString root;
    QString error;
    QList<DeclaredComponentRoot> roots;
    if (!canonical_project_root(project_root, root, error)
        || !validate_declared_roots(root, component_roots, false, roots, error))
    {
        result.error = error;
        return result;
    }
    ComponentInputModel model;
    BuildIdentity identity;
    if (!load_component_model(root, roots, model, error)
        || !compute_build_identity(root, roots, model, contracts_assembly, identity, error))
    {
        result.error = error;
        return result;
    }
    result.build_hash = identity.build_hash;
    result.cache_directory = normalized_path(QDir{root}.filePath(
        QStringLiteral(".dragonpixel/Cache/%1/%2-%3")
            .arg(result.build_hash, platform_name(), architecture_name())));
    if (!ensure_safe_directory(root, result.cache_directory, nullptr, error))
    {
        result.error = error;
        return result;
    }
    result.runtime_manifest_path = normalized_path(
        QDir{result.cache_directory}.filePath(QStringLiteral("runtime-modules.json")));
    if (QFileInfo::exists(result.runtime_manifest_path))
    {
        QString cache_error;
        if (validate_runtime_manifest(root, roots, model, identity, result.cache_directory,
                result.runtime_manifest_path, result.managed_component_count,
                result.native_component_count, cache_error))
        {
            result.reused_cache = true;
        }
        else
        {
            result.diagnostics.push_back(
                QStringLiteral("Rejected invalid component cache entry and rebuilt it: %1").arg(cache_error));
        }
    }

    if (!result.reused_cache)
    {
        QTemporaryDir staging{QDir{QDir::tempPath()}.filePath(
            QStringLiteral("dpe-component-build-%1-XXXXXX").arg(result.build_hash.left(12)))};
        if (!staging.isValid())
        {
            result.error = QStringLiteral("Could not allocate isolated component-build staging storage.");
            return result;
        }
        for (const auto& source : model.csharp_sources)
        {
            if (!validate_no_follow_path(root, source.absoluteFilePath(), true, false, error)
                || !copy_to_staging(source.absoluteFilePath(), staging.path(),
                    staged_source_path(source, roots, QStringLiteral("managed-sources")), error))
            {
                result.error = error;
                return result;
            }
        }
        for (const auto& source : model.native_sources)
        {
            if (!validate_no_follow_path(root, source.absoluteFilePath(), true, false, error)
                || !copy_to_staging(source.absoluteFilePath(), staging.path(),
                    staged_source_path(source, roots, QStringLiteral("native-sources")), error))
            {
                result.error = error;
                return result;
            }
        }

        bool has_managed{};
        bool has_native{};
        for (const auto& component : model.components)
        {
            has_managed = has_managed || component.language == QStringLiteral("csharp");
            has_native = has_native || component.language == QStringLiteral("cpp");
        }
        const auto staged_output = QDir{staging.path()}.filePath(QStringLiteral("build-output"));
        if (!QDir{}.mkpath(staged_output))
        {
            result.error = QStringLiteral("Could not create isolated component output staging.");
            return result;
        }
        if (has_managed)
        {
            const auto project_file = QDir{staging.path()}.filePath(
                QStringLiteral("DragonPixel.ProjectComponents.csproj"));
            const auto managed_output = QDir{staged_output}.filePath(QStringLiteral("managed"));
            if (!QDir{}.mkpath(managed_output)
                || !write_atomic(project_file, identity.managed_project, error))
            {
                if (error.isEmpty()) error = QStringLiteral("Could not create isolated managed build staging.");
                result.error = error;
                return result;
            }
            const auto built = run(QString::fromUtf8(DPE_DOTNET_EXECUTABLE),
                {QStringLiteral("build"), project_file,
                 QStringLiteral("--configuration"), QString::fromLatin1(component_build_configuration),
                 QStringLiteral("--nologo"), QStringLiteral("--output"), managed_output,
                 QStringLiteral("-p:DPEContractsPath=%1")
                     .arg(QFileInfo{contracts_assembly}.canonicalFilePath())},
                staging.path());
            result.diagnostics.push_back(built.output);
            if (!built.succeeded)
            {
                result.error = QStringLiteral("C# component build failed in isolated staging.");
                return result;
            }
        }
        if (has_native)
        {
            const auto cmake_file = QDir{staging.path()}.filePath(QStringLiteral("CMakeLists.txt"));
            const auto native_build = QDir{staging.path()}.filePath(QStringLiteral("native-build"));
            const auto native_output = QDir{staged_output}.filePath(QStringLiteral("native"));
            if (!QDir{}.mkpath(native_output)
                || !write_atomic(cmake_file, identity.native_project_file, error))
            {
                if (error.isEmpty()) error = QStringLiteral("Could not create isolated native build staging.");
                result.error = error;
                return result;
            }
            QStringList configure_arguments{
                QStringLiteral("-S"), staging.path(),
                 QStringLiteral("-B"), native_build,
                 QStringLiteral("-G"), QString::fromUtf8(DPE_COMPONENT_CMAKE_GENERATOR),
                 QStringLiteral("-DCMAKE_MAKE_PROGRAM=%1")
                     .arg(QString::fromUtf8(DPE_COMPONENT_MAKE_PROGRAM)),
                 QStringLiteral("-DCMAKE_CXX_COMPILER=%1").arg(QString::fromUtf8(DPE_CXX_COMPILER)),
                QStringLiteral("-DCMAKE_BUILD_TYPE=%1").arg(
                    QString::fromLatin1(component_build_configuration)),
                QStringLiteral("-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=%1").arg(native_output),
                QStringLiteral("-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=%1").arg(native_output),
                QStringLiteral("-DCMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE=%1").arg(native_output),
                QStringLiteral("-DCMAKE_LIBRARY_OUTPUT_DIRECTORY_RELEASE=%1").arg(native_output),
            };
#if defined(Q_OS_WIN)
            QProcessEnvironment build_environment;
            if (!visual_studio_environment(build_environment, error))
            {
                result.error = error;
                return result;
            }
#endif
            const auto configured = run(QString::fromUtf8(DPE_CMAKE_EXECUTABLE),
                configure_arguments, staging.path()
#if defined(Q_OS_WIN)
                , &build_environment
#endif
                );
            result.diagnostics.push_back(configured.output);
            if (!configured.succeeded)
            {
                result.error = QStringLiteral("C++ component configure failed in isolated staging.");
                return result;
            }
            const auto built = run(QString::fromUtf8(DPE_CMAKE_EXECUTABLE),
                {QStringLiteral("--build"), native_build,
                 QStringLiteral("--config"), QString::fromLatin1(component_build_configuration)},
                staging.path()
#if defined(Q_OS_WIN)
                , &build_environment
#endif
                );
            result.diagnostics.push_back(built.output);
            if (!built.succeeded)
            {
                result.error = QStringLiteral("C++ component build failed in isolated staging.");
                return result;
            }
        }

        ComponentInputModel post_build_model;
        BuildIdentity post_build_identity;
        if (!load_component_model(root, roots, post_build_model, error)
            || !compute_build_identity(
                root, roots, post_build_model, contracts_assembly, post_build_identity, error)
            || post_build_identity.build_hash != identity.build_hash)
        {
            result.error = error.isEmpty()
                ? QStringLiteral("Component inputs changed during the isolated build; no cache manifest was published.")
                : error;
            return result;
        }

        QJsonArray managed_components;
        QJsonArray native_modules;
        for (const auto& component : model.components)
        {
            if (component.language == QStringLiteral("csharp"))
                managed_components.push_back(runtime_component(component));
        }
        QJsonArray managed_modules;
        if (!managed_components.isEmpty())
        {
            const auto staged_assembly = QDir{staged_output}.filePath(
                QStringLiteral("managed/DragonPixel.ProjectComponents.dll"));
            if (!QFileInfo{staged_assembly}.isFile())
            {
                result.error = QStringLiteral("Managed build did not produce DragonPixel.ProjectComponents.dll.");
                return result;
            }
            const auto cache_managed = QDir{result.cache_directory}.filePath(QStringLiteral("managed"));
            if (!ensure_safe_directory(root, cache_managed, nullptr, error))
            {
                result.error = error;
                return result;
            }
            QByteArray assembly_bytes;
            if (!read_all(staged_assembly, assembly_bytes, error))
            {
                result.error = error;
                return result;
            }
            const auto artifact = normalized_path(
                QDir{cache_managed}.filePath(QStringLiteral("DragonPixel.ProjectComponents.dll")));
            if (!write_atomic_contained(root, artifact, assembly_bytes, error))
            {
                result.error = error;
                return result;
            }
            managed_modules.push_back(QJsonObject{
                {QStringLiteral("path"), artifact},
                {QStringLiteral("sha256"), QString::fromLatin1(
                    QCryptographicHash::hash(assembly_bytes, QCryptographicHash::Sha256).toHex())},
                {QStringLiteral("components"), managed_components},
            });
        }
        for (const auto& component : model.components)
        {
            if (component.language != QStringLiteral("cpp")) continue;
            const auto filename = native_artifact_filename(component.type_id);
            const auto staged_artifact = QDir{staged_output}.filePath(
                QStringLiteral("native/%1").arg(filename));
            if (!QFileInfo{staged_artifact}.isFile())
            {
                result.error = QStringLiteral("Native build did not produce the declared module %1.").arg(filename);
                return result;
            }
            const auto cache_native = QDir{result.cache_directory}.filePath(QStringLiteral("native"));
            if (!ensure_safe_directory(root, cache_native, nullptr, error))
            {
                result.error = error;
                return result;
            }
            QByteArray artifact_bytes;
            if (!read_all(staged_artifact, artifact_bytes, error))
            {
                result.error = error;
                return result;
            }
            const auto artifact = normalized_path(QDir{cache_native}.filePath(filename));
            if (!write_atomic_contained(root, artifact, artifact_bytes, error))
            {
                result.error = error;
                return result;
            }
            native_modules.push_back(QJsonObject{
                {QStringLiteral("path"), artifact},
                {QStringLiteral("sha256"), QString::fromLatin1(
                    QCryptographicHash::hash(artifact_bytes, QCryptographicHash::Sha256).toHex())},
                {QStringLiteral("component"), runtime_component(component)},
            });
        }

        QJsonArray declared_roots;
        for (const auto& declared : roots) declared_roots.push_back(declared.declared);
        const QJsonObject runtime_manifest{
            {QStringLiteral("format"), QStringLiteral("dpe.runtime-modules")},
            {QStringLiteral("formatVersion"), 1},
            {QStringLiteral("buildHash"), result.build_hash},
            {QStringLiteral("platform"), platform_name()},
            {QStringLiteral("architecture"), architecture_name()},
            {QStringLiteral("configuration"), QString::fromLatin1(component_build_configuration)},
            {QStringLiteral("generatorIdentity"), QString::fromLatin1(component_generator_identity)},
            {QStringLiteral("contractsSha256"), identity.contracts_sha256},
            {QStringLiteral("componentRoots"), declared_roots},
            {QStringLiteral("toolIdentities"), QJsonObject{
                {QStringLiteral("dotnet"), QString::fromLatin1(QCryptographicHash::hash(
                    identity.dotnet_identity, QCryptographicHash::Sha256).toHex())},
                {QStringLiteral("cmake"), QString::fromLatin1(QCryptographicHash::hash(
                    identity.cmake_identity, QCryptographicHash::Sha256).toHex())},
                {QStringLiteral("cxx"), QString::fromLatin1(QCryptographicHash::hash(
                    identity.cxx_identity, QCryptographicHash::Sha256).toHex())},
            }},
            {QStringLiteral("managedModules"), managed_modules},
            {QStringLiteral("nativeModules"), native_modules},
        };
        if (!write_atomic_contained(root, result.runtime_manifest_path,
                QJsonDocument{runtime_manifest}.toJson(QJsonDocument::Indented), error)
            || !validate_runtime_manifest(root, roots, model, identity, result.cache_directory,
                result.runtime_manifest_path, result.managed_component_count,
                result.native_component_count, error))
        {
            result.error = error;
            return result;
        }
    }

    const auto active_path = normalized_path(
        QDir{root}.filePath(QStringLiteral(".dragonpixel/Cache/active-runtime-modules.json")));
    QByteArray runtime_bytes;
    if (!read_all(result.runtime_manifest_path, runtime_bytes, error)
        || !write_atomic_contained(root, active_path, runtime_bytes, error))
    {
        result.error = error;
        return result;
    }
    int active_managed{};
    int active_native{};
    if (!validate_runtime_manifest(root, roots, model, identity, result.cache_directory,
            active_path, active_managed, active_native, error))
    {
        result.error = QStringLiteral("Published active runtime-module manifest failed validation: %1").arg(error);
        return result;
    }
    result.succeeded = true;
    return result;
}
