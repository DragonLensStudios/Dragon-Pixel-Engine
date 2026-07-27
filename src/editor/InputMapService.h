#pragma once

#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

enum class InputActionKind
{
    button,
    axis1d,
};

struct InputBinding final
{
    QString id;
    QString path;
    double scale{1.0};
    double dead_zone{};
    QJsonObject extensions;
};

struct InputAction final
{
    QString id;
    QString name;
    InputActionKind kind{InputActionKind::button};
    QVector<InputBinding> bindings;
    QJsonObject extensions;
};

struct InputControlMap final
{
    QString id;
    QString name;
    bool enabled{true};
    QVector<InputAction> actions;
    QJsonObject extensions;
};

struct InputMapDocument final
{
    QString id;
    QString name;
    QString active_control_map_id;
    QVector<InputControlMap> control_maps;
    QJsonObject extensions;
    QByteArray source_hash;
};

struct InputMapDiagnostic final
{
    QString code;
    QString message;
    QString json_pointer;
};

struct InputMapLoadResult final
{
    std::optional<InputMapDocument> document;
    QVector<InputMapDiagnostic> diagnostics;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return document.has_value() && diagnostics.isEmpty();
    }
};

struct InputMapSaveResult final
{
    bool saved{};
    QByteArray source_hash;
    QVector<InputMapDiagnostic> diagnostics;
};

struct EvaluatedInputAction final
{
    InputActionKind kind{InputActionKind::button};
    double value{};
};

class InputMapService final
{
public:
    [[nodiscard]] InputMapLoadResult load(
        const QString& source_path,
        const QString& project_root) const;
    [[nodiscard]] InputMapLoadResult parse(const QByteArray& bytes) const;
    [[nodiscard]] InputMapSaveResult save(
        const QString& source_path,
        const QString& project_root,
        const InputMapDocument& document,
        const QByteArray& expected_source_hash) const;

    [[nodiscard]] QHash<QString, EvaluatedInputAction> evaluate(
        const InputMapDocument& document,
        const QHash<QString, double>& controls) const;
    [[nodiscard]] bool uses_control_path(
        const InputMapDocument& document,
        const QString& path) const;
    [[nodiscard]] QByteArray serialize(const InputMapDocument& document) const;
    [[nodiscard]] InputMapDocument compatibility_map() const;

    [[nodiscard]] static QString action_kind_name(InputActionKind kind);
    [[nodiscard]] static QStringList supported_control_paths();
};
