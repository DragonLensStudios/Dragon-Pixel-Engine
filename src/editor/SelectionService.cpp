#include "SelectionService.h"

#include <utility>

SelectionService::SelectionService(QObject* parent) : QObject(parent) {}

const SelectionSnapshot& SelectionService::snapshot() const noexcept
{
    return snapshot_;
}

void SelectionService::publish(
    QString project_id,
    QString scene_id,
    QList<dragonpixel::core::uuid> ordered_entity_ids,
    std::optional<dragonpixel::core::uuid> active_entity_id,
    SelectionOrigin origin)
{
    QList<dragonpixel::core::uuid> unique_ids;
    for (const auto& id : ordered_entity_ids)
    {
        if (!unique_ids.contains(id)) unique_ids.push_back(id);
    }
    if (active_entity_id && !unique_ids.contains(*active_entity_id))
    {
        active_entity_id = unique_ids.isEmpty()
            ? std::optional<dragonpixel::core::uuid>{}
            : std::optional<dragonpixel::core::uuid>{unique_ids.front()};
    }
    const auto unchanged = snapshot_.project_id == project_id
        && snapshot_.scene_id == scene_id
        && snapshot_.ordered_entity_ids == unique_ids
        && snapshot_.active_entity_id == active_entity_id
        && snapshot_.origin == origin;
    if (unchanged) return;
    snapshot_.project_id = std::move(project_id);
    snapshot_.scene_id = std::move(scene_id);
    snapshot_.ordered_entity_ids = std::move(unique_ids);
    snapshot_.active_entity_id = active_entity_id;
    snapshot_.origin = origin;
    ++snapshot_.revision;
    emit selection_changed();
}

void SelectionService::clear(SelectionOrigin origin)
{
    publish({}, {}, {}, {}, origin);
}
