#pragma once

#include <dragonpixel/core/diagnostic.h>
#include <dragonpixel/core/uuid.h>
#include <dragonpixel/scene/commands.h>
#include <dragonpixel/scene/entity.h>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace dragonpixel::scene
{
struct command_result final
{
    bool succeeded{};
    std::optional<core::diagnostic> diagnostic;
};

struct transaction_result final
{
    bool succeeded{};
    std::size_t applied_count{};
    std::optional<core::diagnostic> diagnostic;
};

class scene final
{
public:
    scene(core::uuid id, std::string name);
    scene(core::uuid id, std::string name, std::vector<entity> entities);

    [[nodiscard]] const core::uuid& id() const noexcept { return id_; }
    [[nodiscard]] const std::string& name() const noexcept { return name_; }
    [[nodiscard]] std::span<const entity> entities() const noexcept { return entities_; }
    [[nodiscard]] const entity* find_entity(const core::uuid& id) const noexcept;
    [[nodiscard]] command_result apply(const command& value);
    [[nodiscard]] transaction_result apply_transaction(std::span<const command> commands);
    [[nodiscard]] std::optional<core::diagnostic> validate() const;

private:
    [[nodiscard]] entity* find_entity_mutable(const core::uuid& id) noexcept;
    [[nodiscard]] bool would_create_cycle(const core::uuid& entity_id, const core::uuid& parent_id) const noexcept;
    [[nodiscard]] static command_result failure(std::string code, std::string message, std::string context = {});

    core::uuid id_;
    std::string name_;
    std::vector<entity> entities_;
};
}
