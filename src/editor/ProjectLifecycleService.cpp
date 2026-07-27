#include "ProjectLifecycleService.h"

#include "ProjectIndexService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QSettings>
#include <QUuid>

#include <algorithm>
#include <optional>
#include <tuple>
#include <utility>

namespace
{
constexpr auto project_manifest_name = "DragonPixelProject.json";
constexpr auto recovery_manifest_name = ".dpe-project-creation-recovery.json";

QString normalized_absolute_path(const QString& path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(QFileInfo{path}.absoluteFilePath()));
}

bool is_portable_relative_path(const QString& path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains(QLatin1Char{'\\'}))
    {
        return false;
    }
    const auto clean = QDir::cleanPath(path);
    return clean != QStringLiteral(".") && clean != QStringLiteral("..")
        && !clean.startsWith(QStringLiteral("../")) && clean == path;
}

void add_diagnostic(
    ProjectLifecycleResult& result,
    QString code,
    QString message,
    QString path = {})
{
    result.diagnostics.push_back(ProjectLifecycleDiagnostic{
        std::move(code), std::move(message), std::move(path)});
}

void add_diagnostic(
    QVector<ProjectLifecycleDiagnostic>* diagnostics,
    QString code,
    QString message,
    QString path = {})
{
    if (diagnostics)
    {
        diagnostics->push_back(ProjectLifecycleDiagnostic{
            std::move(code), std::move(message), std::move(path)});
    }
}

QByteArray json_bytes(const QJsonObject& object)
{
    return QJsonDocument{object}.toJson(QJsonDocument::Indented);
}

bool write_atomic(const QString& path, const QByteArray& bytes, QString& error)
{
    QSaveFile output{path};
    output.setDirectWriteFallback(false);
    if (!output.open(QIODevice::WriteOnly)
        || output.write(bytes) != bytes.size()
        || !output.commit())
    {
        error = output.errorString();
        return false;
    }
    return true;
}

std::optional<QJsonObject> read_json_object(
    const QString& path,
    ProjectLifecycleResult& result,
    const QString& code)
{
    QFile input{path};
    if (!input.open(QIODevice::ReadOnly))
    {
        add_diagnostic(result, code, input.errorString(), path);
        return std::nullopt;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        add_diagnostic(result, code, parse_error.errorString(), path);
        return std::nullopt;
    }
    return document.object();
}

std::optional<ProjectTemplateDescriptor> parse_template(
    const QString& manifest_path,
    QVector<ProjectLifecycleDiagnostic>* diagnostics)
{
    QFile input{manifest_path};
    if (!input.open(QIODevice::ReadOnly))
    {
        add_diagnostic(diagnostics, QStringLiteral("DPE-PROJECT-TEMPLATE-READ"),
            input.errorString(), manifest_path);
        return std::nullopt;
    }
    QJsonParseError parse_error;
    const auto document = QJsonDocument::fromJson(input.readAll(), &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !document.isObject())
    {
        add_diagnostic(diagnostics, QStringLiteral("DPE-PROJECT-TEMPLATE-JSON"),
            parse_error.errorString(), manifest_path);
        return std::nullopt;
    }
    const auto object = document.object();
    const auto kind = object.value(QStringLiteral("kind")).toString();
    if (object.value(QStringLiteral("$schema")).toString()
            != QStringLiteral("https://dragonpixel.dev/schemas/v1/project-template.schema.json")
        || object.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.project-template")
        || object.value(QStringLiteral("formatVersion")).toInt() != 1
        || object.value(QStringLiteral("templateVersion")).toInt() != 1
        || QUuid{object.value(QStringLiteral("templateId")).toString()}.isNull()
        || object.value(QStringLiteral("name")).toString().trimmed().isEmpty()
        || object.value(QStringLiteral("engineRange")).toString().trimmed().isEmpty()
        || (kind != QStringLiteral("2d") && kind != QStringLiteral("3d"))
        || !object.value(QStringLiteral("entries")).isArray())
    {
        add_diagnostic(diagnostics, QStringLiteral("DPE-PROJECT-TEMPLATE-CONTRACT"),
            QStringLiteral("Template does not satisfy dpe.project-template v1."), manifest_path);
        return std::nullopt;
    }
    for (const auto& entry_value : object.value(QStringLiteral("entries")).toArray())
    {
        if (!entry_value.isObject())
        {
            add_diagnostic(diagnostics, QStringLiteral("DPE-PROJECT-TEMPLATE-ENTRY"),
                QStringLiteral("Template entries must be objects."), manifest_path);
            return std::nullopt;
        }
        const auto entry = entry_value.toObject();
        if (!is_portable_relative_path(entry.value(QStringLiteral("path")).toString())
            || entry.value(QStringLiteral("kind")).toString().isEmpty())
        {
            add_diagnostic(diagnostics, QStringLiteral("DPE-PROJECT-TEMPLATE-PATH"),
                QStringLiteral("Template contains an unsafe or incomplete entry."), manifest_path);
            return std::nullopt;
        }
    }
    return ProjectTemplateDescriptor{
        object.value(QStringLiteral("templateId")).toString(),
        1,
        object.value(QStringLiteral("name")).toString(),
        kind == QStringLiteral("2d") ? ProjectTemplateKind::two_d : ProjectTemplateKind::three_d,
        object.value(QStringLiteral("engineRange")).toString(),
        normalized_absolute_path(manifest_path),
        object,
    };
}

QJsonObject transform_component(double x, double y, double z)
{
    return QJsonObject{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("owner"), QStringLiteral("native")},
        {QStringLiteral("properties"), QJsonObject{
            {QStringLiteral("dpe.transform.position"), QJsonObject{{QStringLiteral("x"), x}, {QStringLiteral("y"), y}, {QStringLiteral("z"), z}}},
            {QStringLiteral("dpe.transform.rotation"), QJsonObject{{QStringLiteral("w"), 1.0}, {QStringLiteral("x"), 0.0}, {QStringLiteral("y"), 0.0}, {QStringLiteral("z"), 0.0}}},
            {QStringLiteral("dpe.transform.scale"), QJsonObject{{QStringLiteral("x"), 1.0}, {QStringLiteral("y"), 1.0}, {QStringLiteral("z"), 1.0}}},
        }},
        {QStringLiteral("qualifiedName"), QStringLiteral("DragonPixel.Native.TransformComponent")},
        {QStringLiteral("schemaVersion"), 2},
        {QStringLiteral("typeId"), QStringLiteral("52e52fbd-ea15-40c5-bd9a-7dd320f7cd1e")},
    };
}

QJsonObject camera_entity(const QString& id, ProjectTemplateKind kind)
{
    const bool is_2d = kind == ProjectTemplateKind::two_d;
    return QJsonObject{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("components"), QJsonArray{
            transform_component(0.0, is_2d ? 0.0 : 2.0, is_2d ? 10.0 : 7.0),
            QJsonObject{
                {QStringLiteral("enabled"), true},
                {QStringLiteral("owner"), QStringLiteral("native")},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("dpe.camera.far"), 1000.0},
                    {QStringLiteral("dpe.camera.field_of_view"), 60.0},
                    {QStringLiteral("dpe.camera.near"), 0.1},
                    {QStringLiteral("dpe.camera.primary"), true},
                    {QStringLiteral("dpe.camera.projection"), is_2d ? QStringLiteral("orthographic") : QStringLiteral("perspective")},
                }},
                {QStringLiteral("qualifiedName"), QStringLiteral("DragonPixel.Native.CameraComponent")},
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("typeId"), QStringLiteral("4d054cdc-20e0-4f4f-80d9-a38923c36d46")},
            },
        }},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QStringLiteral("Main Camera")},
        {QStringLiteral("parentId"), QJsonValue::Null},
        {QStringLiteral("siblingOrder"), 0},
    };
}

QJsonObject light_entity(const QString& id)
{
    return QJsonObject{
        {QStringLiteral("enabled"), true},
        {QStringLiteral("components"), QJsonArray{
            transform_component(0.0, 3.0, 0.0),
            QJsonObject{
                {QStringLiteral("enabled"), true},
                {QStringLiteral("owner"), QStringLiteral("native")},
                {QStringLiteral("properties"), QJsonObject{
                    {QStringLiteral("dpe.light.color"), QJsonObject{{QStringLiteral("a"), 1.0}, {QStringLiteral("b"), 1.0}, {QStringLiteral("g"), 1.0}, {QStringLiteral("r"), 1.0}}},
                    {QStringLiteral("dpe.light.intensity"), 1.0},
                    {QStringLiteral("dpe.light.kind"), QStringLiteral("directional")},
                }},
                {QStringLiteral("qualifiedName"), QStringLiteral("DragonPixel.Native.LightComponent")},
                {QStringLiteral("schemaVersion"), 1},
                {QStringLiteral("typeId"), QStringLiteral("1859c426-7cfe-42fc-a815-f5fc1bf1dad6")},
            },
        }},
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), QStringLiteral("Directional Light")},
        {QStringLiteral("parentId"), QJsonValue::Null},
        {QStringLiteral("siblingOrder"), 1},
    };
}

QJsonObject scene_document(
    const QString& scene_id,
    const QString& scene_name,
    ProjectTemplateKind kind,
    const QString& camera_id,
    const QString& light_id = {})
{
    QJsonArray entities{camera_entity(camera_id, kind)};
    if (kind == ProjectTemplateKind::three_d)
    {
        entities.push_back(light_entity(light_id));
    }
    return QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v3/scene.schema.json")},
        {QStringLiteral("entities"), entities},
        {QStringLiteral("format"), QStringLiteral("dpe.scene")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("engineVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("name"), scene_name},
        {QStringLiteral("physicsSettings"), QJsonObject{
            {QStringLiteral("fixedTimeStepSeconds"), 1.0 / 60.0},
            {QStringLiteral("maxCatchUpTicks"), 4},
            {QStringLiteral("box2DSolverSubsteps"), 4},
            {QStringLiteral("joltCollisionSteps"), 1},
            {QStringLiteral("gravity2D"), QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), -9.81}}},
            {QStringLiteral("gravity3D"), QJsonObject{{QStringLiteral("x"), 0.0}, {QStringLiteral("y"), -9.81}, {QStringLiteral("z"), 0.0}}},
        }},
        {QStringLiteral("prefabInstances"), QJsonArray{}},
        {QStringLiteral("sceneId"), scene_id},
    };
}

QJsonObject axis_action(
    const QString& action_id,
    const QString& name,
    const QVector<std::tuple<QString, QString, double, double>>& bindings)
{
    QJsonArray result_bindings;
    for (const auto& [id, path, scale, dead_zone] : bindings)
    {
        QJsonObject binding{
            {QStringLiteral("bindingId"), id},
            {QStringLiteral("path"), path},
            {QStringLiteral("scale"), scale},
        };
        if (dead_zone > 0.0)
        {
            binding.insert(QStringLiteral("deadZone"), dead_zone);
        }
        result_bindings.push_back(binding);
    }
    return QJsonObject{
        {QStringLiteral("actionId"), action_id},
        {QStringLiteral("name"), name},
        {QStringLiteral("kind"), QStringLiteral("axis1d")},
        {QStringLiteral("bindings"), result_bindings},
    };
}

bool cancelled(const std::shared_ptr<ProjectLifecycleCancellation>& cancellation)
{
    return cancellation && cancellation->is_cancelled();
}

bool remove_tree(const QString& path)
{
    return !QFileInfo::exists(path) || QDir{path}.removeRecursively();
}

QString default_recent_store()
{
    return QDir{QDir::homePath()}.filePath(QStringLiteral(".dragonpixel/recent-projects.ini"));
}
} // namespace

ProjectLifecycleService::ProjectLifecycleService(
    QString recent_projects_store,
    IdProvider id_provider)
    : recent_projects_store_(recent_projects_store.isEmpty()
          ? default_recent_store()
          : normalized_absolute_path(recent_projects_store))
    , id_provider_(std::move(id_provider))
{
}

QString ProjectLifecycleService::next_id() const
{
    if (id_provider_)
    {
        return id_provider_();
    }
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toLower();
}

QVector<ProjectTemplateDescriptor> ProjectLifecycleService::discover_templates(
    const QString& templates_root,
    QVector<ProjectLifecycleDiagnostic>* diagnostics) const
{
    QVector<ProjectTemplateDescriptor> result;
    const auto root = normalized_absolute_path(templates_root);
    if (!QFileInfo{root}.isDir())
    {
        add_diagnostic(diagnostics, QStringLiteral("DPE-PROJECT-TEMPLATE-ROOT"),
            QStringLiteral("Template root does not exist."), root);
        return result;
    }
    QDirIterator iterator{root, QStringList{QStringLiteral("DragonPixelTemplate.json")},
        QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories};
    while (iterator.hasNext())
    {
        if (const auto descriptor = parse_template(iterator.next(), diagnostics))
        {
            result.push_back(*descriptor);
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.name.compare(right.name, Qt::CaseInsensitive) < 0;
    });
    return result;
}

ProjectLifecycleResult ProjectLifecycleService::dry_run(
    const ProjectCreationRequest& request) const
{
    ProjectLifecycleResult result;
    const auto template_descriptor = parse_template(request.template_manifest_path, &result.diagnostics);
    if (!template_descriptor)
    {
        return result;
    }
    if (request.project_name.trimmed().isEmpty()
        || request.project_name.contains(QLatin1Char{'/'})
        || request.project_name.contains(QLatin1Char{'\\'}))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-NAME"),
            QStringLiteral("Project name must be non-empty and cannot contain path separators."));
    }
    const auto destination = normalized_absolute_path(request.destination_path);
    const QFileInfo destination_info{destination};
    if (destination_info.exists())
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-DESTINATION-EXISTS"),
            QStringLiteral("The project destination already exists."), destination);
    }
    const auto parent = destination_info.absoluteDir();
    if (!parent.exists() || !QFileInfo{parent.absolutePath()}.isDir())
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-PARENT"),
            QStringLiteral("The selected destination parent does not exist."), parent.absolutePath());
    }
    if (QFileInfo{parent.absolutePath()}.isSymLink())
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-LINK-PARENT"),
            QStringLiteral("Project creation does not accept a linked destination parent."), parent.absolutePath());
    }
    result.project_manifest_path = QDir{destination}.filePath(QString::fromLatin1(project_manifest_name));
    result.succeeded = result.diagnostics.isEmpty();
    return result;
}

ProjectLifecycleResult ProjectLifecycleService::create_project(
    const ProjectCreationRequest& request,
    const std::shared_ptr<ProjectLifecycleCancellation>& cancellation,
    ProjectLifecycleFault fault) const
{
    auto result = dry_run(request);
    if (!result.succeeded)
    {
        return result;
    }
    result.succeeded = false;
    if (cancelled(cancellation))
    {
        result.cancelled = true;
        return result;
    }
    const auto descriptor = *parse_template(request.template_manifest_path, nullptr);
    result.operation_id = next_id();
    const auto destination = normalized_absolute_path(request.destination_path);
    const QFileInfo destination_info{destination};
    const auto staging_name = QStringLiteral(".%1.dpe-create-%2")
                                  .arg(destination_info.fileName(), result.operation_id);
    result.staging_path = destination_info.absoluteDir().filePath(staging_name);
    if (QFileInfo::exists(result.staging_path)
        || !QDir{}.mkdir(result.staging_path))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-STAGING"),
            QStringLiteral("Could not create the isolated sibling staging directory."), result.staging_path);
        return result;
    }

    QJsonObject recovery{
        {QStringLiteral("format"), QStringLiteral("dpe.project-creation-recovery")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("operationId"), result.operation_id},
        {QStringLiteral("destination"), destination},
        {QStringLiteral("templateId"), descriptor.template_id},
        {QStringLiteral("state"), QStringLiteral("staging")},
    };
    QString error;
    const auto recovery_path = QDir{result.staging_path}.filePath(QString::fromLatin1(recovery_manifest_name));
    if (!write_atomic(recovery_path, json_bytes(recovery), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-RECOVERY-WRITE"), error, recovery_path);
        remove_tree(result.staging_path);
        return result;
    }
    if (fault == ProjectLifecycleFault::after_staging)
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-INJECTED-STAGING"),
            QStringLiteral("Injected failure after staging creation."), result.staging_path);
        return result;
    }
    if (cancelled(cancellation))
    {
        result.cancelled = true;
        remove_tree(result.staging_path);
        return result;
    }

    const auto project_id = next_id();
    const auto scene_id = next_id();
    const auto camera_id = next_id();
    const auto light_id = descriptor.kind == ProjectTemplateKind::three_d ? next_id() : QString{};
    const auto input_asset_id = next_id();
    const auto input_map_id = next_id();
    const auto control_map_id = next_id();
    const auto move_x_id = next_id();
    const auto move_y_id = next_id();
    auto binding = [&]() { return next_id(); };

    QJsonArray entries = descriptor.manifest.value(QStringLiteral("entries")).toArray();
    for (const auto& entry_value : entries)
    {
        const auto entry = entry_value.toObject();
        if (entry.value(QStringLiteral("kind")).toString() == QStringLiteral("directory"))
        {
            const auto path = QDir{result.staging_path}.filePath(entry.value(QStringLiteral("path")).toString());
            if (!QDir{}.mkpath(path))
            {
                add_diagnostic(result, QStringLiteral("DPE-PROJECT-DIRECTORY"),
                    QStringLiteral("Could not create a declared project directory."), path);
                return result;
            }
            result.created_paths.push_back(entry.value(QStringLiteral("path")).toString());
        }
    }

    const auto scene = scene_document(scene_id, QStringLiteral("Main"), descriptor.kind, camera_id, light_id);
    const auto scene_relative = QStringLiteral("Scenes/Main.dpescene");
    const auto scene_path = QDir{result.staging_path}.filePath(scene_relative);
    if (!write_atomic(scene_path, json_bytes(scene), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-SCENE-WRITE"), error, scene_path);
        return result;
    }
    result.created_paths.push_back(scene_relative);

    const auto input_map = QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v1/input-map.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.inputmap")},
        {QStringLiteral("formatVersion"), 1},
        {QStringLiteral("inputMapId"), input_map_id},
        {QStringLiteral("name"), QStringLiteral("Default Input")},
        {QStringLiteral("activeControlMapId"), control_map_id},
        {QStringLiteral("controlMaps"), QJsonArray{QJsonObject{
            {QStringLiteral("mapId"), control_map_id},
            {QStringLiteral("name"), QStringLiteral("Gameplay")},
            {QStringLiteral("enabled"), true},
            {QStringLiteral("actions"), QJsonArray{
                axis_action(move_x_id, QStringLiteral("move.x"), {
                    {binding(), QStringLiteral("keyboard/d"), 1.0, 0.0},
                    {binding(), QStringLiteral("keyboard/right"), 1.0, 0.0},
                    {binding(), QStringLiteral("keyboard/a"), -1.0, 0.0},
                    {binding(), QStringLiteral("keyboard/left"), -1.0, 0.0},
                    {binding(), QStringLiteral("gamepad/left-x"), 1.0, 0.18},
                }),
                axis_action(move_y_id, QStringLiteral("move.y"), {
                    {binding(), QStringLiteral("keyboard/w"), 1.0, 0.0},
                    {binding(), QStringLiteral("keyboard/up"), 1.0, 0.0},
                    {binding(), QStringLiteral("keyboard/s"), -1.0, 0.0},
                    {binding(), QStringLiteral("keyboard/down"), -1.0, 0.0},
                    {binding(), QStringLiteral("gamepad/left-y"), -1.0, 0.18},
                }),
            }},
        }}},
    };
    const auto input_bytes = json_bytes(input_map);
    const auto input_relative = QStringLiteral("Assets/Input/DefaultGameplay.dpeinputmap");
    const auto input_path = QDir{result.staging_path}.filePath(input_relative);
    if (!write_atomic(input_path, input_bytes, error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-INPUT-WRITE"), error, input_path);
        return result;
    }
    result.created_paths.push_back(input_relative);

    const auto source_hash = QString::fromLatin1(
        QCryptographicHash::hash(input_bytes, QCryptographicHash::Sha256).toHex());
    const auto input_asset = QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v3/asset-metadata.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.asset")},
        {QStringLiteral("formatVersion"), 3},
        {QStringLiteral("engineVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("assetId"), input_asset_id},
        {QStringLiteral("assetType"), QStringLiteral("input-map")},
        {QStringLiteral("source"), QStringLiteral("Input/DefaultGameplay.dpeinputmap")},
        {QStringLiteral("sourceOwnership"), QStringLiteral("generated")},
        {QStringLiteral("sourceHash"), source_hash},
        {QStringLiteral("importHash"), source_hash},
        {QStringLiteral("importer"), QJsonObject{{QStringLiteral("id"), QStringLiteral("dragonpixel.input-map")}, {QStringLiteral("version"), 1}}},
        {QStringLiteral("cacheKey"), QStringLiteral("sha256:") + source_hash},
        {QStringLiteral("dependencies"), QJsonArray{}},
        {QStringLiteral("dependencyRevisions"), QJsonArray{}},
        {QStringLiteral("importSettings"), QJsonObject{}},
        {QStringLiteral("recoveryState"), QJsonObject{{QStringLiteral("state"), QStringLiteral("ready")}}},
        {QStringLiteral("importerDiagnostics"), QJsonArray{}},
        {QStringLiteral("previewDiagnostics"), QJsonArray{}},
    };
    const auto asset_relative = QStringLiteral("Assets/DefaultInput.dpeasset");
    const auto asset_path = QDir{result.staging_path}.filePath(asset_relative);
    if (!write_atomic(asset_path, json_bytes(input_asset), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-ASSET-WRITE"), error, asset_path);
        return result;
    }
    result.created_paths.push_back(asset_relative);

    const QByteArray component_project =
        "<Project Sdk=\"Microsoft.NET.Sdk\">\n"
        "  <PropertyGroup>\n"
        "    <TargetFramework>net10.0</TargetFramework>\n"
        "    <Nullable>enable</Nullable>\n"
        "    <ImplicitUsings>enable</ImplicitUsings>\n"
        "    <EnableDefaultCompileItems>false</EnableDefaultCompileItems>\n"
        "    <AssemblyName>DragonPixel.ProjectComponents</AssemblyName>\n"
        "  </PropertyGroup>\n"
        "  <ItemGroup>\n"
        "    <Compile Include=\"*.cs\" />\n"
        "    <Reference Include=\"DragonPixel.Contracts\">\n"
        "      <HintPath>$(DPEContractsPath)</HintPath>\n"
        "      <Private>true</Private>\n"
        "    </Reference>\n"
        "  </ItemGroup>\n"
        "</Project>\n";
    const auto component_relative = QStringLiteral("Components/CSharp/DragonPixel.ProjectComponents.csproj");
    const auto component_path = QDir{result.staging_path}.filePath(component_relative);
    if (!write_atomic(component_path, component_project, error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-COMPONENT-WRITE"), error, component_path);
        return result;
    }
    result.created_paths.push_back(component_relative);

    const auto project = QJsonObject{
        {QStringLiteral("$schema"), QStringLiteral("https://dragonpixel.dev/schemas/v4/project.schema.json")},
        {QStringLiteral("format"), QStringLiteral("dpe.project")},
        {QStringLiteral("formatVersion"), 4},
        {QStringLiteral("engineVersion"), QStringLiteral("1.0.0")},
        {QStringLiteral("engineRange"), descriptor.engine_range},
        {QStringLiteral("projectId"), project_id},
        {QStringLiteral("name"), request.project_name.trimmed()},
        {QStringLiteral("startupScene"), scene_relative},
        {QStringLiteral("sceneRoots"), QJsonArray{QStringLiteral("Scenes")}},
        {QStringLiteral("assetRoots"), QJsonArray{QStringLiteral("Assets"), QStringLiteral("Prefabs")}},
        {QStringLiteral("componentRoots"), QJsonArray{QStringLiteral("Components")}},
        {QStringLiteral("requiredSdks"), QJsonArray{}},
        {QStringLiteral("buildTargets"), QJsonArray{}},
        {QStringLiteral("packageTargets"), QJsonArray{}},
        {QStringLiteral("pluginRequirements"), QJsonArray{}},
        {QStringLiteral("templateProvenance"), QJsonObject{
            {QStringLiteral("templateId"), descriptor.template_id},
            {QStringLiteral("templateVersion"), descriptor.template_version},
        }},
    };
    const auto staged_manifest = QDir{result.staging_path}.filePath(QString::fromLatin1(project_manifest_name));
    if (!write_atomic(staged_manifest, json_bytes(project), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-MANIFEST-WRITE"), error, staged_manifest);
        return result;
    }
    result.created_paths.push_back(QString::fromLatin1(project_manifest_name));

    recovery.insert(QStringLiteral("state"), QStringLiteral("generated"));
    recovery.insert(QStringLiteral("createdPaths"), QJsonArray::fromStringList(result.created_paths));
    if (!write_atomic(recovery_path, json_bytes(recovery), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-RECOVERY-WRITE"), error, recovery_path);
        return result;
    }
    if (fault == ProjectLifecycleFault::after_generation)
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-INJECTED-GENERATION"),
            QStringLiteral("Injected failure after generation."), result.staging_path);
        return result;
    }
    if (cancelled(cancellation))
    {
        result.cancelled = true;
        remove_tree(result.staging_path);
        return result;
    }

    ProjectIndexService index_service;
    const auto validation = index_service.build_candidate(staged_manifest);
    if (!validation.succeeded())
    {
        for (const auto& diagnostic : validation.diagnostics)
        {
            add_diagnostic(result, QStringLiteral("DPE-PROJECT-VALIDATION"),
                diagnostic.message, diagnostic.document_path);
        }
        return result;
    }
    recovery.insert(QStringLiteral("state"), QStringLiteral("validated"));
    if (!write_atomic(recovery_path, json_bytes(recovery), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-RECOVERY-WRITE"), error, recovery_path);
        return result;
    }
    if (fault == ProjectLifecycleFault::before_commit)
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-INJECTED-COMMIT"),
            QStringLiteral("Injected failure before the atomic directory commit."), result.staging_path);
        return result;
    }
    if (QFileInfo::exists(destination)
        || !QDir{destination_info.absolutePath()}.rename(staging_name, destination_info.fileName()))
    {
        add_diagnostic(result, QStringLiteral("DPE-PROJECT-COMMIT"),
            QStringLiteral("Could not atomically publish the staged project directory."), destination);
        return result;
    }

    const auto committed_recovery = QDir{destination}.filePath(QString::fromLatin1(recovery_manifest_name));
    QFile::remove(committed_recovery);
    result.staging_path.clear();
    result.project_manifest_path = QDir{destination}.filePath(QString::fromLatin1(project_manifest_name));
    result.scene_path = QDir{destination}.filePath(scene_relative);
    result.succeeded = true;
    record_recent_project(result.project_manifest_path);
    return result;
}

ProjectLifecycleResult ProjectLifecycleService::create_clean_scene(
    const CleanSceneRequest& request,
    const std::shared_ptr<ProjectLifecycleCancellation>& cancellation,
    ProjectLifecycleFault fault) const
{
    ProjectLifecycleResult result;
    result.operation_id = next_id();
    if (cancelled(cancellation))
    {
        result.cancelled = true;
        return result;
    }
    ProjectIndexService index_service;
    const auto candidate = index_service.build_candidate(request.project_manifest_path);
    if (!candidate.succeeded())
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-PROJECT"),
            QStringLiteral("The active project is not valid."), request.project_manifest_path);
        return result;
    }
    if (request.scene_name.trimmed().isEmpty()
        || !is_portable_relative_path(request.relative_scene_path)
        || !request.relative_scene_path.endsWith(QStringLiteral(".dpescene"), Qt::CaseInsensitive))
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-REQUEST"),
            QStringLiteral("Scene name and contained .dpescene path are required."));
        return result;
    }
    const auto& project = *candidate.candidate;
    const auto destination = normalized_absolute_path(
        QDir{project.project_root}.filePath(request.relative_scene_path));
    bool under_scene_root = false;
    for (const auto& root : project.roots)
    {
        if (root.kind != ProjectIndexRootKind::scenes)
        {
            continue;
        }
        const auto prefix = QDir::fromNativeSeparators(root.absolute_path) + QLatin1Char{'/'};
        if (QDir::fromNativeSeparators(destination).startsWith(prefix, Qt::CaseInsensitive))
        {
            under_scene_root = true;
            break;
        }
    }
    if (!under_scene_root || QFileInfo::exists(destination))
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-DESTINATION"),
            QStringLiteral("Scene destination must be new and beneath a declared scene root."), destination);
        return result;
    }
    if (!QDir{}.mkpath(QFileInfo{destination}.absolutePath()))
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-DIRECTORY"),
            QStringLiteral("Could not create the scene directory."), QFileInfo{destination}.absolutePath());
        return result;
    }
    const auto staging = destination + QStringLiteral(".dpe-stage-") + result.operation_id;
    result.staging_path = staging;
    const auto scene_id = next_id();
    const auto camera_id = next_id();
    const auto light_id = request.kind == ProjectTemplateKind::three_d ? next_id() : QString{};
    QString error;
    if (!write_atomic(staging, json_bytes(scene_document(
            scene_id, request.scene_name.trimmed(), request.kind, camera_id, light_id)), error))
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-STAGING"), error, staging);
        return result;
    }
    if (fault == ProjectLifecycleFault::before_scene_commit)
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-INJECTED-COMMIT"),
            QStringLiteral("Injected failure before scene publication."), staging);
        return result;
    }
    if (cancelled(cancellation))
    {
        result.cancelled = true;
        QFile::remove(staging);
        return result;
    }
    if (!QFile::rename(staging, destination))
    {
        add_diagnostic(result, QStringLiteral("DPE-SCENE-COMMIT"),
            QStringLiteral("Could not atomically publish the clean scene."), destination);
        return result;
    }
    result.succeeded = true;
    result.staging_path.clear();
    result.project_manifest_path = project.manifest_path;
    result.scene_path = destination;
    result.created_paths.push_back(request.relative_scene_path);
    return result;
}

QStringList ProjectLifecycleService::recoverable_staging_paths(
    const QString& destination_parent) const
{
    QStringList result;
    QDir directory{normalized_absolute_path(destination_parent)};
    for (const auto& name : directory.entryList(
             QStringList{QStringLiteral(".*.dpe-create-*")}, QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot))
    {
        const auto path = directory.filePath(name);
        if (QFileInfo{QDir{path}.filePath(QString::fromLatin1(recovery_manifest_name))}.isFile())
        {
            result.push_back(path);
        }
    }
    std::sort(result.begin(), result.end());
    return result;
}

bool ProjectLifecycleService::discard_recoverable_staging(
    const QString& staging_path,
    ProjectLifecycleDiagnostic* diagnostic) const
{
    const auto path = normalized_absolute_path(staging_path);
    if (!QFileInfo{QDir{path}.filePath(QString::fromLatin1(recovery_manifest_name))}.isFile())
    {
        if (diagnostic)
        {
            *diagnostic = {QStringLiteral("DPE-PROJECT-RECOVERY-MARKER"),
                QStringLiteral("The directory is not a recognized project-creation staging area."), path};
        }
        return false;
    }
    if (!QDir{path}.removeRecursively())
    {
        if (diagnostic)
        {
            *diagnostic = {QStringLiteral("DPE-PROJECT-RECOVERY-DISCARD"),
                QStringLiteral("Could not remove the recoverable staging directory."), path};
        }
        return false;
    }
    return true;
}

QStringList ProjectLifecycleService::recent_projects() const
{
    QSettings settings{recent_projects_store_, QSettings::IniFormat};
    auto paths = settings.value(QStringLiteral("projects")).toStringList();
    paths.erase(std::remove_if(paths.begin(), paths.end(), [](const auto& path) {
        return !QFileInfo{path}.isFile();
    }), paths.end());
    paths.removeDuplicates();
    return paths;
}

void ProjectLifecycleService::record_recent_project(const QString& manifest_path) const
{
    const auto path = normalized_absolute_path(manifest_path);
    if (!QFileInfo{path}.isFile())
    {
        return;
    }
    QDir{}.mkpath(QFileInfo{recent_projects_store_}.absolutePath());
    QSettings settings{recent_projects_store_, QSettings::IniFormat};
    auto paths = settings.value(QStringLiteral("projects")).toStringList();
    paths.removeAll(path);
    paths.prepend(path);
    while (paths.size() > 12)
    {
        paths.removeLast();
    }
    settings.setValue(QStringLiteral("projects"), paths);
    settings.sync();
}
