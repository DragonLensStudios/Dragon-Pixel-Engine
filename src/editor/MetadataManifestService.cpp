#include "MetadataManifestService.h"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <optional>
#include <functional>
#include <unordered_map>
#include <unordered_set>

namespace
{
using namespace dragonpixel::metadata;

std::optional<value_type> parse_value_type(const QString& value)
{
    if (value == QStringLiteral("boolean")) return value_type::boolean;
    if (value == QStringLiteral("integer")) return value_type::integer;
    if (value == QStringLiteral("number")) return value_type::number;
    if (value == QStringLiteral("string")) return value_type::string;
    if (value == QStringLiteral("vector2")) return value_type::vector2;
    if (value == QStringLiteral("vector3")) return value_type::vector3;
    if (value == QStringLiteral("quaternion")) return value_type::quaternion;
    if (value == QStringLiteral("color")) return value_type::color;
    if (value == QStringLiteral("entity-reference")) return value_type::entity_reference;
    if (value == QStringLiteral("asset-reference")) return value_type::asset_reference;
    if (value == QStringLiteral("component-reference")) return value_type::component_reference;
    if (value == QStringLiteral("object")) return value_type::object;
    if (value == QStringLiteral("list")) return value_type::list;
    if (value == QStringLiteral("dictionary")) return value_type::dictionary;
    if (value == QStringLiteral("polymorphic-object")) return value_type::polymorphic_object;
    return std::nullopt;
}

std::string compact_json(const QJsonValue& value)
{
    QJsonArray wrapper{value};
    auto encoded = QJsonDocument{wrapper}.toJson(QJsonDocument::Compact);
    if (encoded.size() >= 2)
    {
        encoded = encoded.mid(1, encoded.size() - 2);
    }
    return encoded.toStdString();
}

std::optional<value_shape> parse_shape(const QJsonObject& object)
{
    const auto type = parse_value_type(object.value(QStringLiteral("kind")).toString());
    if (!type)
    {
        return std::nullopt;
    }
    value_shape result;
    result.type = *type;
    result.nullable = object.value(QStringLiteral("nullable")).toBool();
    result.object_type_id = object.value(QStringLiteral("objectTypeId")).toString().toStdString();
    result.contract_id = object.value(QStringLiteral("contractId")).toString().toStdString();
    result.reference_filter = object.value(QStringLiteral("referenceFilter")).toString().toStdString();
    if (object.value(QStringLiteral("element")).isObject())
    {
        const auto element = parse_shape(object.value(QStringLiteral("element")).toObject());
        if (!element)
        {
            return std::nullopt;
        }
        result.arguments.push_back(*element);
    }
    return result;
}

std::optional<property_descriptor> parse_property(const QJsonObject& object)
{
    const auto shape = parse_shape(object.value(QStringLiteral("shape")).toObject());
    if (!shape)
    {
        return std::nullopt;
    }
    property_descriptor result;
    result.property_id = object.value(QStringLiteral("propertyId")).toString().toStdString();
    result.display_name = object.value(QStringLiteral("displayName")).toString().toStdString();
    result.type = shape->type;
    result.order = static_cast<std::uint32_t>(object.value(QStringLiteral("order")).toInt());
    result.read_only = object.value(QStringLiteral("readOnly")).toBool();
    result.default_json = object.contains(QStringLiteral("default"))
        ? compact_json(object.value(QStringLiteral("default"))) : std::string{};
    if (object.value(QStringLiteral("minimum")).isDouble()) result.minimum = object.value(QStringLiteral("minimum")).toDouble();
    if (object.value(QStringLiteral("maximum")).isDouble()) result.maximum = object.value(QStringLiteral("maximum")).toDouble();
    if (object.value(QStringLiteral("step")).isDouble()) result.step = object.value(QStringLiteral("step")).toDouble();
    result.units = object.value(QStringLiteral("units")).toString().toStdString();
    for (const auto& choice : object.value(QStringLiteral("enumChoices")).toArray())
        result.enum_choices.push_back(choice.toString().toStdString());
    result.nullable = shape->nullable;
    result.reference_filter = shape->reference_filter;
    result.category = object.value(QStringLiteral("category")).toString().toStdString();
    result.tooltip = object.value(QStringLiteral("tooltip")).toString().toStdString();
    result.drawer_key = object.value(QStringLiteral("drawerKey")).toString().toStdString();
    result.shape = *shape;
    if (result.property_id.empty() || result.display_name.empty()) return std::nullopt;
    return result;
}

std::vector<property_descriptor> parse_properties(const QJsonArray& values, bool& valid)
{
    std::vector<property_descriptor> result;
    for (const auto& value : values)
    {
        const auto property = parse_property(value.toObject());
        if (!property)
        {
            valid = false;
            return {};
        }
        result.push_back(*property);
    }
    return result;
}

bool contained(const QString& root, const QString& path)
{
    const auto relative = QDir{root}.relativeFilePath(path);
    return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../")) && !relative.startsWith(QStringLiteral("..\\"));
}

bool collect_shape_edges(
    const value_shape& shape,
    const registry& descriptors,
    std::unordered_set<std::string>& edges,
    QStringList& diagnostics,
    const QString& context)
{
    bool valid = true;
    if (!shape.object_type_id.empty())
    {
        if (descriptors.find_object_type(shape.object_type_id) == nullptr)
        {
            diagnostics.push_back(QStringLiteral("%1 references missing object type %2.")
                .arg(context, QString::fromStdString(shape.object_type_id)));
            valid = false;
        }
        else
        {
            edges.insert(shape.object_type_id);
        }
    }
    if (!shape.contract_id.empty())
    {
        if (descriptors.find_contract(shape.contract_id) == nullptr)
        {
            diagnostics.push_back(QStringLiteral("%1 references missing contract %2.")
                .arg(context, QString::fromStdString(shape.contract_id)));
            valid = false;
        }
        for (const auto& implementation : descriptors.implementations(shape.contract_id))
        {
            edges.insert(implementation.get().type_id);
        }
    }
    for (const auto& argument : shape.arguments)
    {
        valid = collect_shape_edges(argument, descriptors, edges, diagnostics, context) && valid;
    }
    return valid;
}
}

MetadataManifestLoadResult MetadataManifestService::load_project(
    const QString& project_root,
    const QStringList& component_roots,
    dragonpixel::metadata::registry& registry) const
{
    MetadataManifestLoadResult result;
    for (const auto& declared : component_roots)
    {
        const auto absolute_root = QDir{project_root}.absoluteFilePath(declared);
        if (!contained(project_root, absolute_root) || !QFileInfo{absolute_root}.isDir())
        {
            result.succeeded = false;
            result.diagnostics.push_back(QStringLiteral("Component root is missing or unsafe: %1").arg(declared));
            continue;
        }
        QDirIterator iterator{absolute_root, {QStringLiteral("*.dpecomponents")}, QDir::Files, QDirIterator::Subdirectories};
        while (iterator.hasNext())
        {
            const auto path = iterator.next();
            QFile file{path};
            if (!file.open(QIODevice::ReadOnly))
            {
                result.succeeded = false;
                result.diagnostics.push_back(QStringLiteral("Could not read component manifest: %1").arg(path));
                continue;
            }
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(file.readAll(), &error);
            const auto root = document.object();
            if (error.error != QJsonParseError::NoError || root.value(QStringLiteral("format")).toString() != QStringLiteral("dpe.component-metadata")
                || root.value(QStringLiteral("formatVersion")).toInt() != 4)
            {
                result.succeeded = false;
                result.diagnostics.push_back(QStringLiteral("Invalid metadata-v4 manifest: %1").arg(path));
                continue;
            }
            bool manifest_valid = true;
            for (const auto& value : root.value(QStringLiteral("contracts")).toArray())
            {
                const auto object = value.toObject();
                manifest_valid = registry.add_contract({
                    object.value(QStringLiteral("contractId")).toString().toStdString(),
                    object.value(QStringLiteral("qualifiedName")).toString().toStdString(),
                    object.value(QStringLiteral("displayName")).toString().toStdString(),
                    object.value(QStringLiteral("tooltip")).toString().toStdString(),
                }) && manifest_valid;
            }
            for (const auto& value : root.value(QStringLiteral("objectTypes")).toArray())
            {
                const auto object = value.toObject();
                object_type_descriptor descriptor;
                descriptor.type_id = object.value(QStringLiteral("typeId")).toString().toStdString();
                descriptor.qualified_name = object.value(QStringLiteral("qualifiedName")).toString().toStdString();
                descriptor.display_name = object.value(QStringLiteral("displayName")).toString().toStdString();
                descriptor.schema_version = static_cast<std::uint32_t>(object.value(QStringLiteral("schemaVersion")).toInt());
                for (const auto& contract : object.value(QStringLiteral("implements")).toArray())
                    descriptor.contracts.push_back(contract.toString().toStdString());
                descriptor.properties = parse_properties(object.value(QStringLiteral("properties")).toArray(), manifest_valid);
                manifest_valid = registry.add_object_type(std::move(descriptor)) && manifest_valid;
                ++result.object_type_count;
            }
            for (const auto& value : root.value(QStringLiteral("components")).toArray())
            {
                const auto object = value.toObject();
                component_descriptor descriptor;
                descriptor.type_id = object.value(QStringLiteral("typeId")).toString().toStdString();
                descriptor.qualified_name = object.value(QStringLiteral("qualifiedName")).toString().toStdString();
                descriptor.display_name = object.value(QStringLiteral("displayName")).toString().toStdString();
                descriptor.schema_version = static_cast<std::uint32_t>(object.value(QStringLiteral("schemaVersion")).toInt());
                const auto owner = object.value(QStringLiteral("owner")).toString();
                descriptor.owner = owner == QStringLiteral("managed") ? runtime_owner::managed
                    : owner == QStringLiteral("data-only") ? runtime_owner::data_only : runtime_owner::native;
                descriptor.properties = parse_properties(object.value(QStringLiteral("properties")).toArray(), manifest_valid);
                descriptor.category = object.value(QStringLiteral("category")).toString().toStdString();
                descriptor.tooltip = object.value(QStringLiteral("tooltip")).toString().toStdString();
                descriptor.addable = object.value(QStringLiteral("addable")).toBool(true);
                descriptor.removable = object.value(QStringLiteral("removable")).toBool(true);
                descriptor.resettable = object.value(QStringLiteral("resettable")).toBool(true);
                const auto language = object.value(QStringLiteral("implementationLanguage")).toString();
                descriptor.language = language == QStringLiteral("csharp") ? implementation_language::csharp
                    : language == QStringLiteral("data-only") ? implementation_language::data_only : implementation_language::cpp;
                descriptor.source_path = object.value(QStringLiteral("sourcePath")).toString().toStdString();
                descriptor.runtime_module_id = object.value(QStringLiteral("runtimeModuleId")).toString().toStdString();
                manifest_valid = registry.add(std::move(descriptor)) && manifest_valid;
                ++result.component_count;
            }
            ++result.manifest_count;
            if (!manifest_valid)
            {
                result.succeeded = false;
                result.diagnostics.push_back(QStringLiteral("Metadata manifest contains invalid or duplicate stable IDs: %1").arg(path));
            }
        }
    }
    std::unordered_map<std::string, std::unordered_set<std::string>> edges;
    for (const auto& object_reference : registry.object_types())
    {
        const auto& object = object_reference.get();
        for (const auto& contract : object.contracts)
        {
            if (registry.find_contract(contract) == nullptr)
            {
                result.succeeded = false;
                result.diagnostics.push_back(QStringLiteral("Object type %1 implements missing contract %2.")
                    .arg(QString::fromStdString(object.display_name), QString::fromStdString(contract)));
            }
        }
        for (const auto& property : object.properties)
        {
            if (property.shape)
            {
                result.succeeded = collect_shape_edges(*property.shape, registry, edges[object.type_id],
                    result.diagnostics, QString::fromStdString(object.display_name + "." + property.property_id))
                    && result.succeeded;
            }
        }
    }
    for (const auto& component_reference : registry.descriptors())
    {
        const auto& component = component_reference.get();
        for (const auto& property : component.properties)
        {
            if (property.shape)
            {
                std::unordered_set<std::string> ignored;
                result.succeeded = collect_shape_edges(*property.shape, registry, ignored, result.diagnostics,
                    QString::fromStdString(component.display_name + "." + property.property_id))
                    && result.succeeded;
            }
        }
    }
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;
    std::function<bool(const std::string&)> visit = [&](const std::string& id) {
        if (visiting.contains(id)) return false;
        if (visited.contains(id)) return true;
        visiting.insert(id);
        for (const auto& target : edges[id])
        {
            if (!visit(target)) return false;
        }
        visiting.erase(id);
        visited.insert(id);
        return true;
    };
    for (const auto& [id, targets] : edges)
    {
        static_cast<void>(targets);
        if (!visit(id))
        {
            result.succeeded = false;
            result.diagnostics.push_back(QStringLiteral("Inline object metadata contains a cycle involving %1; use entity/component references instead.")
                .arg(QString::fromStdString(id)));
            break;
        }
    }
    return result;
}
