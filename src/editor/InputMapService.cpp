#include "InputMapService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

#include <algorithm>
#include <cmath>

namespace
{
constexpr auto input_map_format = "dpe.inputmap";
constexpr int input_map_format_version = 1;

bool is_uuid(const QString& value)
{
    static const QRegularExpression expression{
        QStringLiteral("^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}$")};
    return expression.match(value).hasMatch();
}

bool is_action_name(const QString& value)
{
    static const QRegularExpression expression{
        QStringLiteral("^[a-z][a-z0-9]*(?:[._-][a-z0-9]+)*$")};
    return expression.match(value).hasMatch();
}

bool contained_existing_file(const QString& project_root, const QString& path)
{
    const QFileInfo root_info{project_root};
    const QFileInfo file_info{path};
    if (!root_info.isDir() || !file_info.isFile() || root_info.isSymbolicLink()
        || file_info.isSymbolicLink())
    {
        return false;
    }
    const auto canonical_root = root_info.canonicalFilePath();
    const auto canonical_file = file_info.canonicalFilePath();
    if (canonical_root.isEmpty() || canonical_file.isEmpty()) return false;
    const auto relative = QDir{canonical_root}.relativeFilePath(canonical_file);
    return !QDir::isAbsolutePath(relative) && relative != QStringLiteral("..")
        && !relative.startsWith(QStringLiteral("../"))
        && !relative.startsWith(QStringLiteral("..\\"));
}

QByteArray sha256(const QByteArray& bytes)
{
    return QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
}

void remove_known(QJsonObject& object, std::initializer_list<const char*> names)
{
    for (const auto* name : names) object.remove(QString::fromLatin1(name));
}

void add_diagnostic(
    QVector<InputMapDiagnostic>& diagnostics,
    QString code,
    QString message,
    QString pointer = {})
{
    diagnostics.push_back({std::move(code), std::move(message), std::move(pointer)});
}

std::optional<InputActionKind> parse_kind(const QString& value)
{
    if (value == QStringLiteral("button")) return InputActionKind::button;
    if (value == QStringLiteral("axis1d")) return InputActionKind::axis1d;
    return std::nullopt;
}

bool is_supported_path(const QString& path)
{
    static const auto supported = [] {
        QSet<QString> result;
        for (const auto& item : InputMapService::supported_control_paths()) result.insert(item);
        return result;
    }();
    return supported.contains(path);
}

QJsonObject serialize_binding(const InputBinding& binding)
{
    auto result = binding.extensions;
    result.insert(QStringLiteral("bindingId"), binding.id);
    result.insert(QStringLiteral("path"), binding.path);
    result.insert(QStringLiteral("scale"), binding.scale);
    if (binding.dead_zone > 0.0)
    {
        result.insert(QStringLiteral("deadZone"), binding.dead_zone);
    }
    else
    {
        result.remove(QStringLiteral("deadZone"));
    }
    return result;
}

QJsonObject serialize_action(const InputAction& action)
{
    auto result = action.extensions;
    result.insert(QStringLiteral("actionId"), action.id);
    result.insert(QStringLiteral("name"), action.name);
    result.insert(QStringLiteral("kind"), InputMapService::action_kind_name(action.kind));
    QJsonArray bindings;
    for (const auto& binding : action.bindings) bindings.push_back(serialize_binding(binding));
    result.insert(QStringLiteral("bindings"), bindings);
    return result;
}

QJsonObject serialize_control_map(const InputControlMap& map)
{
    auto result = map.extensions;
    result.insert(QStringLiteral("mapId"), map.id);
    result.insert(QStringLiteral("name"), map.name);
    result.insert(QStringLiteral("enabled"), map.enabled);
    QJsonArray actions;
    for (const auto& action : map.actions) actions.push_back(serialize_action(action));
    result.insert(QStringLiteral("actions"), actions);
    return result;
}
}

InputMapLoadResult InputMapService::load(
    const QString& source_path,
    const QString& project_root) const
{
    InputMapLoadResult result;
    if (!contained_existing_file(project_root, source_path))
    {
        add_diagnostic(
            result.diagnostics,
            QStringLiteral("unsafe_path"),
            QStringLiteral("Input map must be a regular contained project file."));
        return result;
    }
    QFile file{source_path};
    if (!file.open(QIODevice::ReadOnly))
    {
        add_diagnostic(
            result.diagnostics,
            QStringLiteral("unreadable"),
            QStringLiteral("Input map could not be opened for reading."));
        return result;
    }
    const auto bytes = file.readAll();
    result = parse(bytes);
    if (result.document) result.document->source_hash = sha256(bytes);
    return result;
}

InputMapLoadResult InputMapService::parse(const QByteArray& bytes) const
{
    InputMapLoadResult result;
    QJsonParseError parse_error;
    const auto parsed = QJsonDocument::fromJson(bytes, &parse_error);
    if (parse_error.error != QJsonParseError::NoError || !parsed.isObject())
    {
        add_diagnostic(
            result.diagnostics,
            QStringLiteral("invalid_json"),
            QStringLiteral("Input map is not a valid JSON object."));
        return result;
    }

    const auto root = parsed.object();
    if (root.value(QStringLiteral("format")).toString() != QString::fromLatin1(input_map_format))
    {
        add_diagnostic(result.diagnostics, QStringLiteral("invalid_format"),
            QStringLiteral("Expected format dpe.inputmap."), QStringLiteral("/format"));
    }
    if (root.value(QStringLiteral("formatVersion")).toInt(-1) != input_map_format_version)
    {
        add_diagnostic(result.diagnostics, QStringLiteral("unsupported_version"),
            QStringLiteral("Only dpe.inputmap format version 1 is supported."),
            QStringLiteral("/formatVersion"));
    }

    InputMapDocument document;
    document.id = root.value(QStringLiteral("inputMapId")).toString().toLower();
    document.name = root.value(QStringLiteral("name")).toString().trimmed();
    document.active_control_map_id =
        root.value(QStringLiteral("activeControlMapId")).toString().toLower();
    document.extensions = root;
    remove_known(document.extensions, {
        "$schema", "format", "formatVersion", "inputMapId", "name",
        "activeControlMapId", "controlMaps"});

    if (!is_uuid(document.id))
    {
        add_diagnostic(result.diagnostics, QStringLiteral("invalid_id"),
            QStringLiteral("inputMapId must be a UUID."), QStringLiteral("/inputMapId"));
    }
    if (document.name.isEmpty())
    {
        add_diagnostic(result.diagnostics, QStringLiteral("missing_name"),
            QStringLiteral("Input map name is required."), QStringLiteral("/name"));
    }
    if (!is_uuid(document.active_control_map_id))
    {
        add_diagnostic(result.diagnostics, QStringLiteral("invalid_active_map"),
            QStringLiteral("activeControlMapId must be a UUID."),
            QStringLiteral("/activeControlMapId"));
    }

    const auto maps = root.value(QStringLiteral("controlMaps"));
    if (!maps.isArray() || maps.toArray().isEmpty())
    {
        add_diagnostic(result.diagnostics, QStringLiteral("missing_maps"),
            QStringLiteral("At least one control map is required."),
            QStringLiteral("/controlMaps"));
    }

    QSet<QString> ids;
    QSet<QString> map_names;
    bool active_map_found = false;
    const auto map_array = maps.toArray();
    for (qsizetype map_index = 0; map_index < map_array.size(); ++map_index)
    {
        const auto pointer = QStringLiteral("/controlMaps/%1").arg(map_index);
        if (!map_array.at(map_index).isObject())
        {
            add_diagnostic(result.diagnostics, QStringLiteral("invalid_map"),
                QStringLiteral("Control map must be an object."), pointer);
            continue;
        }
        const auto object = map_array.at(map_index).toObject();
        InputControlMap map;
        map.id = object.value(QStringLiteral("mapId")).toString().toLower();
        map.name = object.value(QStringLiteral("name")).toString().trimmed();
        map.enabled = object.value(QStringLiteral("enabled")).toBool(true);
        map.extensions = object;
        remove_known(map.extensions, {"mapId", "name", "enabled", "actions"});
        if (!is_uuid(map.id) || ids.contains(map.id))
        {
            add_diagnostic(result.diagnostics, QStringLiteral("invalid_map_id"),
                QStringLiteral("mapId must be a unique UUID."), pointer + QStringLiteral("/mapId"));
        }
        else ids.insert(map.id);
        const auto folded_map_name = map.name.toCaseFolded();
        if (map.name.isEmpty() || map_names.contains(folded_map_name))
        {
            add_diagnostic(result.diagnostics, QStringLiteral("invalid_map_name"),
                QStringLiteral("Control map name must be non-empty and unique."),
                pointer + QStringLiteral("/name"));
        }
        else map_names.insert(folded_map_name);
        if (map.id == document.active_control_map_id) active_map_found = true;

        const auto actions = object.value(QStringLiteral("actions"));
        if (!actions.isArray())
        {
            add_diagnostic(result.diagnostics, QStringLiteral("invalid_actions"),
                QStringLiteral("actions must be an array."), pointer + QStringLiteral("/actions"));
        }
        QSet<QString> action_names;
        const auto action_array = actions.toArray();
        for (qsizetype action_index = 0; action_index < action_array.size(); ++action_index)
        {
            const auto action_pointer = pointer + QStringLiteral("/actions/%1").arg(action_index);
            if (!action_array.at(action_index).isObject())
            {
                add_diagnostic(result.diagnostics, QStringLiteral("invalid_action"),
                    QStringLiteral("Action must be an object."), action_pointer);
                continue;
            }
            const auto action_object = action_array.at(action_index).toObject();
            InputAction action;
            action.id = action_object.value(QStringLiteral("actionId")).toString().toLower();
            action.name = action_object.value(QStringLiteral("name")).toString().trimmed().toLower();
            const auto parsed_kind = parse_kind(
                action_object.value(QStringLiteral("kind")).toString().toLower());
            if (parsed_kind) action.kind = *parsed_kind;
            action.extensions = action_object;
            remove_known(action.extensions, {"actionId", "name", "kind", "bindings"});
            if (!is_uuid(action.id) || ids.contains(action.id))
            {
                add_diagnostic(result.diagnostics, QStringLiteral("invalid_action_id"),
                    QStringLiteral("actionId must be a unique UUID."),
                    action_pointer + QStringLiteral("/actionId"));
            }
            else ids.insert(action.id);
            if (!is_action_name(action.name) || action_names.contains(action.name))
            {
                add_diagnostic(result.diagnostics, QStringLiteral("invalid_action_name"),
                    QStringLiteral("Action name must be canonical and unique in its map."),
                    action_pointer + QStringLiteral("/name"));
            }
            else action_names.insert(action.name);
            if (!parsed_kind)
            {
                add_diagnostic(result.diagnostics, QStringLiteral("invalid_action_kind"),
                    QStringLiteral("Action kind must be button or axis1d."),
                    action_pointer + QStringLiteral("/kind"));
            }

            const auto bindings = action_object.value(QStringLiteral("bindings"));
            if (!bindings.isArray())
            {
                add_diagnostic(result.diagnostics, QStringLiteral("invalid_bindings"),
                    QStringLiteral("bindings must be an array."),
                    action_pointer + QStringLiteral("/bindings"));
            }
            const auto binding_array = bindings.toArray();
            for (qsizetype binding_index = 0; binding_index < binding_array.size(); ++binding_index)
            {
                const auto binding_pointer = action_pointer
                    + QStringLiteral("/bindings/%1").arg(binding_index);
                if (!binding_array.at(binding_index).isObject())
                {
                    add_diagnostic(result.diagnostics, QStringLiteral("invalid_binding"),
                        QStringLiteral("Binding must be an object."), binding_pointer);
                    continue;
                }
                const auto binding_object = binding_array.at(binding_index).toObject();
                InputBinding binding;
                binding.id = binding_object.value(QStringLiteral("bindingId")).toString().toLower();
                binding.path = binding_object.value(QStringLiteral("path")).toString().trimmed().toLower();
                binding.scale = binding_object.value(QStringLiteral("scale")).toDouble(1.0);
                binding.dead_zone = binding_object.value(QStringLiteral("deadZone")).toDouble(0.0);
                binding.extensions = binding_object;
                remove_known(binding.extensions, {"bindingId", "path", "scale", "deadZone"});
                if (!is_uuid(binding.id) || ids.contains(binding.id))
                {
                    add_diagnostic(result.diagnostics, QStringLiteral("invalid_binding_id"),
                        QStringLiteral("bindingId must be a unique UUID."),
                        binding_pointer + QStringLiteral("/bindingId"));
                }
                else ids.insert(binding.id);
                if (!is_supported_path(binding.path))
                {
                    add_diagnostic(result.diagnostics, QStringLiteral("unsupported_control"),
                        QStringLiteral("Binding uses an unsupported canonical control path."),
                        binding_pointer + QStringLiteral("/path"));
                }
                if (!std::isfinite(binding.scale) || binding.scale < -16.0 || binding.scale > 16.0)
                {
                    add_diagnostic(result.diagnostics, QStringLiteral("invalid_scale"),
                        QStringLiteral("Binding scale must be finite and between -16 and 16."),
                        binding_pointer + QStringLiteral("/scale"));
                }
                if (!std::isfinite(binding.dead_zone) || binding.dead_zone < 0.0
                    || binding.dead_zone >= 1.0)
                {
                    add_diagnostic(result.diagnostics, QStringLiteral("invalid_dead_zone"),
                        QStringLiteral("Binding deadZone must be in [0, 1)."),
                        binding_pointer + QStringLiteral("/deadZone"));
                }
                action.bindings.push_back(std::move(binding));
            }
            map.actions.push_back(std::move(action));
        }
        document.control_maps.push_back(std::move(map));
    }
    if (!active_map_found)
    {
        add_diagnostic(result.diagnostics, QStringLiteral("missing_active_map"),
            QStringLiteral("activeControlMapId does not identify a control map."),
            QStringLiteral("/activeControlMapId"));
    }

    if (result.diagnostics.isEmpty())
    {
        document.source_hash = sha256(bytes);
        result.document = std::move(document);
    }
    return result;
}

InputMapSaveResult InputMapService::save(
    const QString& source_path,
    const QString& project_root,
    const InputMapDocument& document,
    const QByteArray& expected_source_hash) const
{
    InputMapSaveResult result;
    if (!contained_existing_file(project_root, source_path))
    {
        add_diagnostic(result.diagnostics, QStringLiteral("unsafe_path"),
            QStringLiteral("Input map must remain a regular contained project file."));
        return result;
    }
    QFile existing{source_path};
    if (!existing.open(QIODevice::ReadOnly))
    {
        add_diagnostic(result.diagnostics, QStringLiteral("unreadable"),
            QStringLiteral("Input map could not be opened for compare-before-write."));
        return result;
    }
    const auto current_bytes = existing.readAll();
    existing.close();
    if (sha256(current_bytes) != expected_source_hash)
    {
        add_diagnostic(result.diagnostics, QStringLiteral("source_changed"),
            QStringLiteral("Input map changed on disk; reload before saving."));
        return result;
    }

    const auto bytes = serialize(document);
    const auto validation = parse(bytes);
    if (!validation.succeeded())
    {
        result.diagnostics = validation.diagnostics;
        return result;
    }
    QSaveFile file{source_path};
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    {
        add_diagnostic(result.diagnostics, QStringLiteral("save_failed"),
            QStringLiteral("Input map could not be atomically replaced."));
        return result;
    }
    result.saved = true;
    result.source_hash = sha256(bytes);
    return result;
}

QHash<QString, EvaluatedInputAction> InputMapService::evaluate(
    const InputMapDocument& document,
    const QHash<QString, double>& controls) const
{
    QHash<QString, EvaluatedInputAction> result;
    const auto iterator = std::find_if(
        document.control_maps.cbegin(), document.control_maps.cend(),
        [&document](const InputControlMap& map) {
            return map.id == document.active_control_map_id;
        });
    if (iterator == document.control_maps.cend() || !iterator->enabled) return result;
    for (const auto& action : iterator->actions)
    {
        double value = 0.0;
        for (const auto& binding : action.bindings)
        {
            auto sample = controls.value(binding.path, 0.0);
            if (!std::isfinite(sample)) sample = 0.0;
            const auto magnitude = std::abs(sample);
            if (magnitude <= binding.dead_zone)
            {
                sample = 0.0;
            }
            else if (binding.dead_zone > 0.0)
            {
                sample = std::copysign(
                    (magnitude - binding.dead_zone) / (1.0 - binding.dead_zone), sample);
            }
            value += sample * binding.scale;
        }
        value = std::clamp(value, -1.0, 1.0);
        if (action.kind == InputActionKind::button) value = std::abs(value) > 0.0 ? 1.0 : 0.0;
        result.insert(action.name, EvaluatedInputAction{action.kind, value});
    }
    return result;
}

bool InputMapService::uses_control_path(
    const InputMapDocument& document,
    const QString& path) const
{
    const auto map = std::find_if(
        document.control_maps.cbegin(), document.control_maps.cend(),
        [&document](const InputControlMap& candidate) {
            return candidate.id == document.active_control_map_id && candidate.enabled;
        });
    if (map == document.control_maps.cend()) return false;
    return std::any_of(map->actions.cbegin(), map->actions.cend(),
        [&path](const InputAction& action) {
            return std::any_of(action.bindings.cbegin(), action.bindings.cend(),
                [&path](const InputBinding& binding) { return binding.path == path; });
        });
}

QByteArray InputMapService::serialize(const InputMapDocument& document) const
{
    auto root = document.extensions;
    root.insert(QStringLiteral("$schema"),
        QStringLiteral("https://dragonpixel.dev/schemas/v1/input-map.schema.json"));
    root.insert(QStringLiteral("format"), QString::fromLatin1(input_map_format));
    root.insert(QStringLiteral("formatVersion"), input_map_format_version);
    root.insert(QStringLiteral("inputMapId"), document.id);
    root.insert(QStringLiteral("name"), document.name);
    root.insert(QStringLiteral("activeControlMapId"), document.active_control_map_id);
    QJsonArray maps;
    for (const auto& map : document.control_maps) maps.push_back(serialize_control_map(map));
    root.insert(QStringLiteral("controlMaps"), maps);
    return QJsonDocument{root}.toJson(QJsonDocument::Indented);
}

InputMapDocument InputMapService::compatibility_map() const
{
    const auto parsed = parse(R"json({
  "$schema": "https://dragonpixel.dev/schemas/v1/input-map.schema.json",
  "format": "dpe.inputmap",
  "formatVersion": 1,
  "inputMapId": "caa6f2d9-7239-4dfa-b3be-faf3c33efb30",
  "name": "Compatibility Input",
  "activeControlMapId": "ffca168d-bf6d-4ac8-9eca-73f860711b0d",
  "controlMaps": [
    {
      "mapId": "ffca168d-bf6d-4ac8-9eca-73f860711b0d",
      "name": "Gameplay",
      "enabled": true,
      "actions": [
        {"actionId":"dc982a23-d7f0-4aaa-a22a-f4392bce79f0","name":"move.x","kind":"axis1d","bindings":[
          {"bindingId":"ca1b79d7-da47-4351-8cef-ef4ac5b24d1f","path":"keyboard/d","scale":1},
          {"bindingId":"ea20da15-2851-473a-99a1-f7245412c5d7","path":"keyboard/right","scale":1},
          {"bindingId":"70a95e3a-882f-42d8-a883-4164ef28927b","path":"keyboard/a","scale":-1},
          {"bindingId":"6ef594dc-f59f-41aa-a231-2e4108f093db","path":"keyboard/left","scale":-1}
        ]},
        {"actionId":"c732054d-905c-4b76-a5c4-cd8e7513e40c","name":"move.y","kind":"axis1d","bindings":[
          {"bindingId":"6ce9fa48-8bf2-4dde-9dcf-aad84ff51afe","path":"keyboard/w","scale":1},
          {"bindingId":"7ac0da50-03c5-43fe-a76f-63d49f633a0b","path":"keyboard/up","scale":1},
          {"bindingId":"cd5fa16f-c393-45bf-a9ff-9d6a85078cd6","path":"keyboard/s","scale":-1},
          {"bindingId":"2d2e54db-c70c-4b2f-93b3-6b8c230e998c","path":"keyboard/down","scale":-1}
        ]},
        {"actionId":"b092cf34-7b8f-413a-a3c8-6868fd7e16f4","name":"jump","kind":"button","bindings":[
          {"bindingId":"dcd2d08b-2b6f-4c96-a8e4-e50e21e97e2e","path":"keyboard/space","scale":1}
        ]}
      ]
    }
  ]
})json");
    return parsed.document.value();
}

QString InputMapService::action_kind_name(InputActionKind kind)
{
    return kind == InputActionKind::button
        ? QStringLiteral("button") : QStringLiteral("axis1d");
}

QStringList InputMapService::supported_control_paths()
{
    QStringList result;
    for (QChar key = QLatin1Char('a'); key <= QLatin1Char('z'); key = QChar{key.unicode() + 1})
    {
        result.push_back(QStringLiteral("keyboard/") + key);
    }
    for (QChar key = QLatin1Char('0'); key <= QLatin1Char('9'); key = QChar{key.unicode() + 1})
    {
        result.push_back(QStringLiteral("keyboard/") + key);
    }
    result.append({
        QStringLiteral("keyboard/space"), QStringLiteral("keyboard/up"),
        QStringLiteral("keyboard/down"), QStringLiteral("keyboard/left"),
        QStringLiteral("keyboard/right"), QStringLiteral("keyboard/enter"),
        QStringLiteral("keyboard/tab"), QStringLiteral("keyboard/shift"),
        QStringLiteral("keyboard/control"), QStringLiteral("keyboard/alt"),
        QStringLiteral("mouse/left"), QStringLiteral("mouse/right"),
        QStringLiteral("mouse/middle"), QStringLiteral("mouse/back"),
        QStringLiteral("mouse/forward"), QStringLiteral("mouse/delta-x"),
        QStringLiteral("mouse/delta-y"), QStringLiteral("mouse/wheel-x"),
        QStringLiteral("mouse/wheel-y"), QStringLiteral("gamepad/left-x"),
        QStringLiteral("gamepad/left-y"), QStringLiteral("gamepad/right-x"),
        QStringLiteral("gamepad/right-y"), QStringLiteral("gamepad/left-trigger"),
        QStringLiteral("gamepad/right-trigger"), QStringLiteral("gamepad/south"),
        QStringLiteral("gamepad/east"), QStringLiteral("gamepad/west"),
        QStringLiteral("gamepad/north"), QStringLiteral("gamepad/back"),
        QStringLiteral("gamepad/guide"), QStringLiteral("gamepad/start"),
        QStringLiteral("gamepad/left-stick"), QStringLiteral("gamepad/right-stick"),
        QStringLiteral("gamepad/left-shoulder"), QStringLiteral("gamepad/right-shoulder"),
        QStringLiteral("gamepad/dpad-up"), QStringLiteral("gamepad/dpad-down"),
        QStringLiteral("gamepad/dpad-left"), QStringLiteral("gamepad/dpad-right"),
    });
    return result;
}
