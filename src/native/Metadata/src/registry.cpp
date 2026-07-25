#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/metadata/registry.h>

#include <dragonpixel/core/uuid.h>

#include <algorithm>
#include <utility>

namespace dragonpixel::metadata
{
bool registry::add(component_descriptor descriptor)
{
    if (!core::uuid::parse(descriptor.type_id) || descriptor.qualified_name.empty()
        || descriptor.display_name.empty() || descriptor.schema_version == 0)
    {
        return false;
    }
    std::sort(descriptor.properties.begin(), descriptor.properties.end(), [](const auto& left, const auto& right) {
        if (left.order != right.order)
        {
            return left.order < right.order;
        }
        return left.property_id < right.property_id;
    });
    const auto key = descriptor.type_id;
    return descriptors_.emplace(key, std::move(descriptor)).second;
}

const component_descriptor* registry::find(std::string_view type_id) const noexcept
{
    const auto found = descriptors_.find(std::string{type_id});
    return found == descriptors_.end() ? nullptr : &found->second;
}

std::vector<std::reference_wrapper<const component_descriptor>> registry::descriptors() const
{
    std::vector<std::reference_wrapper<const component_descriptor>> result;
    result.reserve(descriptors_.size());
    for (const auto& [type_id, descriptor] : descriptors_)
    {
        static_cast<void>(type_id);
        result.emplace_back(std::cref(descriptor));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.get().type_id < right.get().type_id;
    });
    return result;
}

registry registry::slice_one_defaults()
{
    registry result;
    const auto transform_added = result.add(component_descriptor{
        std::string{builtin_component_ids::transform},
        "DragonPixel.Native.TransformComponent",
        "Transform",
        2,
        runtime_owner::native,
        {
            {"dpe.transform.position", "Position", value_type::vector3, 0, false},
            {"dpe.transform.rotation", "Rotation", value_type::quaternion, 1, false},
            {"dpe.transform.scale", "Scale", value_type::vector3, 2, false},
        },
    });
    const auto rotator_added = result.add(component_descriptor{
        std::string{builtin_component_ids::rotator},
        "DragonPixel.Managed.RotatorComponent",
        "Rotator",
        1,
        runtime_owner::managed,
        {
            {"dpe.rotator.degrees_per_second", "Degrees per second", value_type::number, 0, false},
            {"dpe.rotator.target", "Target", value_type::entity_reference, 1, false},
        },
    });
    const auto camera_added = result.add(component_descriptor{
        std::string{builtin_component_ids::camera}, "DragonPixel.Native.CameraComponent", "Camera", 1, runtime_owner::native,
        {
            {"dpe.camera.projection", "Projection", value_type::string, 0, false},
            {"dpe.camera.field_of_view", "Field of view", value_type::number, 1, false},
            {"dpe.camera.near", "Near clip", value_type::number, 2, false},
            {"dpe.camera.far", "Far clip", value_type::number, 3, false},
        },
    });
    const auto sprite_added = result.add(component_descriptor{
        std::string{builtin_component_ids::sprite}, "DragonPixel.Native.SpriteComponent", "Sprite", 1, runtime_owner::native,
        {
            {"dpe.sprite.asset", "Texture", value_type::asset_reference, 0, false},
            {"dpe.sprite.color", "Tint", value_type::color, 1, false},
            {"dpe.sprite.layer", "Layer", value_type::integer, 2, false},
        },
    });
    const auto mesh_added = result.add(component_descriptor{
        std::string{builtin_component_ids::mesh}, "DragonPixel.Native.StaticMeshComponent", "Static Mesh", 1, runtime_owner::native,
        {
            {"dpe.mesh.asset", "Mesh", value_type::asset_reference, 0, false},
            {"dpe.mesh.material", "Material", value_type::asset_reference, 1, false},
        },
    });
    const auto material_added = result.add(component_descriptor{
        std::string{builtin_component_ids::material}, "DragonPixel.Native.MaterialComponent", "Material", 1, runtime_owner::native,
        {
            {"dpe.material.base_color", "Base color", value_type::color, 0, false},
            {"dpe.material.roughness", "Roughness", value_type::number, 1, false},
        },
    });
    const auto light_added = result.add(component_descriptor{
        std::string{builtin_component_ids::light}, "DragonPixel.Native.LightComponent", "Light", 1, runtime_owner::native,
        {
            {"dpe.light.kind", "Kind", value_type::string, 0, false},
            {"dpe.light.color", "Color", value_type::color, 1, false},
            {"dpe.light.intensity", "Intensity", value_type::number, 2, false},
        },
    });
    if (!transform_added || !rotator_added || !camera_added || !sprite_added || !mesh_added
        || !material_added || !light_added)
    {
        return registry{};
    }
    return result;
}
}
