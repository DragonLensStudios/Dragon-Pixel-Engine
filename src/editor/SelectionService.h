#pragma once

#include <dragonpixel/core/uuid.h>

#include <QObject>
#include <QList>
#include <QString>

#include <optional>

enum class SelectionOrigin
{
    hierarchy,
    scene_view,
    inspector,
    command,
    project_lifecycle,
};

struct SelectionSnapshot final
{
    QString project_id;
    QString scene_id;
    QList<dragonpixel::core::uuid> ordered_entity_ids;
    std::optional<dragonpixel::core::uuid> active_entity_id;
    SelectionOrigin origin{SelectionOrigin::hierarchy};
    quint64 revision{};
};

class ISelectionService
{
public:
    virtual ~ISelectionService() = default;

    [[nodiscard]] virtual const SelectionSnapshot& snapshot() const noexcept = 0;
    virtual void publish(
        QString project_id,
        QString scene_id,
        QList<dragonpixel::core::uuid> ordered_entity_ids,
        std::optional<dragonpixel::core::uuid> active_entity_id,
        SelectionOrigin origin) = 0;
    virtual void clear(SelectionOrigin origin) = 0;
};

class SelectionService final : public QObject, public ISelectionService
{
    Q_OBJECT

public:
    explicit SelectionService(QObject* parent = nullptr);

    [[nodiscard]] const SelectionSnapshot& snapshot() const noexcept override;
    void publish(
        QString project_id,
        QString scene_id,
        QList<dragonpixel::core::uuid> ordered_entity_ids,
        std::optional<dragonpixel::core::uuid> active_entity_id,
        SelectionOrigin origin) override;
    void clear(SelectionOrigin origin) override;

signals:
    void selection_changed();

private:
    SelectionSnapshot snapshot_;
};
