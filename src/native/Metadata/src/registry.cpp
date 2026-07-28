#include <dragonpixel/metadata/builtin_ids.h>
#include <dragonpixel/metadata/registry.h>

#include <dragonpixel/core/uuid.h>

#include <algorithm>
#include <utility>

namespace dragonpixel::metadata
{
namespace
{
property_descriptor authoring_property(
    std::string id,
    std::string name,
    value_type type,
    std::uint32_t order,
    std::string default_json,
    std::optional<double> minimum = std::nullopt,
    std::optional<double> maximum = std::nullopt,
    std::optional<double> step = std::nullopt,
    std::string units = {},
    std::vector<std::string> choices = {},
    bool nullable = false,
    std::string reference_filter = {},
    std::string category = {},
    std::string tooltip = {},
    std::string drawer = {})
{
    property_descriptor result{};
    result.property_id = std::move(id);
    result.display_name = std::move(name);
    result.type = type;
    result.order = order;
    result.default_json = std::move(default_json);
    result.minimum = minimum;
    result.maximum = maximum;
    result.step = step;
    result.units = std::move(units);
    result.enum_choices = std::move(choices);
    result.nullable = nullable;
    result.reference_filter = std::move(reference_filter);
    result.category = std::move(category);
    result.tooltip = std::move(tooltip);
    result.drawer_key = std::move(drawer);
    return result;
}

component_descriptor builtin_component(
    std::string type_id,
    std::string qualified_name,
    std::string display_name,
    std::uint32_t schema_version,
    runtime_owner owner,
    std::vector<property_descriptor> properties,
    std::string category = {},
    std::string tooltip = {},
    bool addable = true,
    bool removable = true,
    bool resettable = true,
    implementation_language language = implementation_language::cpp,
    std::string source_path = {},
    std::string runtime_module_id = {})
{
    component_descriptor result{};
    result.type_id = std::move(type_id);
    result.qualified_name = std::move(qualified_name);
    result.display_name = std::move(display_name);
    result.schema_version = schema_version;
    result.owner = owner;
    result.properties = std::move(properties);
    result.category = std::move(category);
    result.tooltip = std::move(tooltip);
    result.addable = addable;
    result.removable = removable;
    result.resettable = resettable;
    result.language = language;
    result.source_path = std::move(source_path);
    result.runtime_module_id = std::move(runtime_module_id);
    return result;
}

property_descriptor physics_property(
    std::string id,
    std::string name,
    value_type type,
    std::uint32_t order,
    std::string default_json,
    std::optional<double> minimum = std::nullopt,
    std::optional<double> maximum = std::nullopt,
    std::optional<double> step = std::nullopt,
    std::string units = {},
    std::vector<std::string> choices = {},
    std::string drawer = {})
{
    property_descriptor result{};
    result.property_id = std::move(id);
    result.display_name = std::move(name);
    result.type = type;
    result.order = order;
    result.default_json = std::move(default_json);
    result.minimum = minimum;
    result.maximum = maximum;
    result.step = step;
    result.units = std::move(units);
    result.enum_choices = std::move(choices);
    result.category = "Physics";
    result.drawer_key = std::move(drawer);
    return result;
}
}

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

bool registry::add_contract(contract_descriptor descriptor)
{
    if (!core::uuid::parse(descriptor.contract_id) || descriptor.qualified_name.empty()
        || descriptor.display_name.empty())
    {
        return false;
    }
    const auto key = descriptor.contract_id;
    return contracts_.emplace(key, std::move(descriptor)).second;
}

bool registry::add_object_type(object_type_descriptor descriptor)
{
    if (!core::uuid::parse(descriptor.type_id) || descriptor.qualified_name.empty()
        || descriptor.display_name.empty() || descriptor.schema_version == 0)
    {
        return false;
    }
    for (const auto& contract : descriptor.contracts)
    {
        if (!core::uuid::parse(contract))
        {
            return false;
        }
    }
    std::sort(descriptor.properties.begin(), descriptor.properties.end(), [](const auto& left, const auto& right) {
        return left.order != right.order ? left.order < right.order : left.property_id < right.property_id;
    });
    const auto key = descriptor.type_id;
    return object_types_.emplace(key, std::move(descriptor)).second;
}

const component_descriptor* registry::find(std::string_view type_id) const noexcept
{
    const auto found = descriptors_.find(std::string{type_id});
    return found == descriptors_.end() ? nullptr : &found->second;
}

const contract_descriptor* registry::find_contract(std::string_view contract_id) const noexcept
{
    const auto found = contracts_.find(std::string{contract_id});
    return found == contracts_.end() ? nullptr : &found->second;
}

const object_type_descriptor* registry::find_object_type(std::string_view type_id) const noexcept
{
    const auto found = object_types_.find(std::string{type_id});
    return found == object_types_.end() ? nullptr : &found->second;
}

std::vector<std::reference_wrapper<const object_type_descriptor>> registry::implementations(
    std::string_view contract_id) const
{
    std::vector<std::reference_wrapper<const object_type_descriptor>> result;
    for (const auto& [type_id, descriptor] : object_types_)
    {
        static_cast<void>(type_id);
        if (std::find(descriptor.contracts.begin(), descriptor.contracts.end(), contract_id)
            != descriptor.contracts.end())
        {
            result.emplace_back(std::cref(descriptor));
        }
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.get().display_name < right.get().display_name;
    });
    return result;
}

std::vector<std::reference_wrapper<const object_type_descriptor>> registry::object_types() const
{
    std::vector<std::reference_wrapper<const object_type_descriptor>> result;
    result.reserve(object_types_.size());
    for (const auto& [type_id, descriptor] : object_types_)
    {
        static_cast<void>(type_id);
        result.emplace_back(std::cref(descriptor));
    }
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.get().type_id < right.get().type_id;
    });
    return result;
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
    const auto transform_added = result.add(builtin_component(
        std::string{builtin_component_ids::transform},
        "DragonPixel.Native.TransformComponent",
        "Transform",
        2,
        runtime_owner::native,
        {
            authoring_property("dpe.transform.position", "Position", value_type::vector3, 0,
                R"({"x":0.0,"y":0.0,"z":0.0})", {}, {}, 0.1, "m", {}, false, {}, "Transform",
                "Local position in the canonical right-handed, Y-up coordinate system.", "transform-position"),
            authoring_property("dpe.transform.rotation", "Rotation", value_type::quaternion, 1,
                R"({"w":1.0,"x":0.0,"y":0.0,"z":0.0})", {}, {}, 1.0, "degrees", {}, false, {}, "Transform",
                "Local orientation, presented as Euler angles and stored as a normalized quaternion.", "transform-rotation"),
            authoring_property("dpe.transform.scale", "Scale", value_type::vector3, 2,
                R"({"x":1.0,"y":1.0,"z":1.0})", {}, {}, 0.1, {}, {}, false, {}, "Transform",
                "Local scale. Runtime physics does not support animated scale in Slice 2.", "transform-scale"),
        },
        "Core",
        "Required GameObject transform.",
        false,
        false,
        true));
    const auto rotator_added = result.add(builtin_component(
        std::string{builtin_component_ids::rotator},
        "DragonPixel.Managed.RotatorComponent",
        "Rotator",
        1,
        runtime_owner::managed,
        {
            authoring_property("dpe.rotator.degrees_per_second", "Degrees per second", value_type::number, 0,
                "30.0", -36000.0, 36000.0, 1.0, "degrees/s", {}, false, {}, "Behavior",
                "Signed angular speed applied by the managed sample component."),
            authoring_property("dpe.rotator.target", "Target", value_type::entity_reference, 1,
                "null", {}, {}, {}, {}, {}, true, "entity", "Behavior",
                "Optional GameObject rotated by this component."),
        }));
    const auto camera_added = result.add(builtin_component(
        std::string{builtin_component_ids::camera}, "DragonPixel.Native.CameraComponent", "Camera", 1, runtime_owner::native,
        {
            authoring_property("dpe.camera.primary", "Primary", value_type::boolean, 0, "true", {}, {}, {}, {}, {}, false, {}, "Camera",
                "Exactly one enabled primary camera is required for Play."),
            authoring_property("dpe.camera.projection", "Projection", value_type::string, 1, "\"perspective\"", {}, {}, {}, {},
                {"perspective", "orthographic"}, false, {}, "Camera", "Perspective or orthographic projection mode."),
            authoring_property("dpe.camera.field_of_view", "Field of view", value_type::number, 2, "60.0", 1.0, 179.0, 1.0,
                "degrees", {}, false, {}, "Camera", "Vertical perspective field of view."),
            authoring_property("dpe.camera.orthographic_size", "Orthographic size", value_type::number, 3, "10.0", 0.0001, {}, 0.1,
                "m", {}, false, {}, "Camera", "Vertical extent used by orthographic projection."),
            authoring_property("dpe.camera.near", "Near clip", value_type::number, 4, "0.1", 0.0001, {}, 0.01,
                "m", {}, false, {}, "Camera", "Near clipping distance."),
            authoring_property("dpe.camera.far", "Far clip", value_type::number, 5, "1000.0", 0.001, {}, 1.0,
                "m", {}, false, {}, "Camera", "Far clipping distance."),
        }));
    const auto sprite_added = result.add(builtin_component(
        std::string{builtin_component_ids::sprite}, "DragonPixel.Native.SpriteComponent", "Sprite", 1, runtime_owner::native,
        {
            authoring_property("dpe.sprite.asset", "Texture", value_type::asset_reference, 0, "\"builtin://default-sprite\"",
                {}, {}, {}, {}, {}, false, "sprite", "Rendering", "Sprite-compatible asset reference.", "asset-reference"),
            authoring_property("dpe.sprite.color", "Tint", value_type::color, 1,
                R"({"r":1.0,"g":1.0,"b":1.0,"a":1.0})", 0.0, 1.0, 0.01, {}, {}, false, {}, "Rendering",
                "Linear RGBA tint multiplied with the sprite.", "color"),
            authoring_property("dpe.sprite.layer", "Layer", value_type::integer, 2, "0", -32768.0, 32767.0, 1.0,
                {}, {}, false, {}, "Rendering", "Signed sprite ordering layer."),
        }));
    const auto mesh_added = result.add(builtin_component(
        std::string{builtin_component_ids::mesh}, "DragonPixel.Native.StaticMeshComponent", "Static Mesh", 1, runtime_owner::native,
        {
            authoring_property("dpe.mesh.asset", "Mesh", value_type::asset_reference, 0, "\"builtin://cube\"",
                {}, {}, {}, {}, {}, false, "mesh", "Rendering", "Static-mesh-compatible asset reference.", "asset-reference"),
            authoring_property("dpe.mesh.material", "Material", value_type::asset_reference, 1, "\"builtin://default-material\"",
                {}, {}, {}, {}, {}, false, "material", "Rendering", "Material-compatible asset reference.", "asset-reference"),
        }));
    const auto material_added = result.add(builtin_component(
        std::string{builtin_component_ids::material}, "DragonPixel.Native.MaterialComponent", "Material", 1, runtime_owner::native,
        {
            authoring_property("dpe.material.base_color", "Base color", value_type::color, 0,
                R"({"r":1.0,"g":1.0,"b":1.0,"a":1.0})", 0.0, 1.0, 0.01, {}, {}, false, {}, "Rendering",
                "Linear RGBA base color.", "color"),
            authoring_property("dpe.material.roughness", "Roughness", value_type::number, 1, "0.5", 0.0, 1.0, 0.01,
                {}, {}, false, {}, "Rendering", "Baseline material roughness."),
        }));
    const auto light_added = result.add(builtin_component(
        std::string{builtin_component_ids::light}, "DragonPixel.Native.LightComponent", "Light", 1, runtime_owner::native,
        {
            authoring_property("dpe.light.kind", "Kind", value_type::string, 0, "\"directional\"", {}, {}, {}, {},
                {"ambient", "directional", "point"}, false, {}, "Lighting", "Baseline light kind."),
            authoring_property("dpe.light.color", "Color", value_type::color, 1,
                R"({"r":1.0,"g":1.0,"b":1.0,"a":1.0})", 0.0, 1.0, 0.01, {}, {}, false, {}, "Lighting",
                "Linear RGBA light color.", "color"),
            authoring_property("dpe.light.intensity", "Intensity", value_type::number, 2, "1.0", 0.0, {}, 0.05,
                {}, {}, false, {}, "Lighting", "Non-negative light intensity."),
            authoring_property("dpe.light.range", "Range", value_type::number, 3, "10.0", 0.01, {}, 0.1,
                "m", {}, false, {}, "Lighting", "Point-light influence range."),
        }));
    const auto rigid_body_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::rigid_body_2d}, "DragonPixel.Native.RigidBody2DComponent", "Rigid Body 2D", 1, runtime_owner::native,
        {
            physics_property("dpe.physics2d.body_mode", "Body mode", value_type::string, 0, "\"dynamic\"", {}, {}, {}, {}, {"static", "kinematic", "dynamic"}),
            physics_property("dpe.physics2d.linear_damping", "Linear damping", value_type::number, 1, "0.0", 0.0, {}, 0.05),
            physics_property("dpe.physics2d.angular_damping", "Angular damping", value_type::number, 2, "0.0", 0.0, {}, 0.05),
            physics_property("dpe.physics2d.gravity_scale", "Gravity scale", value_type::number, 3, "1.0", -100.0, 100.0, 0.05),
            physics_property("dpe.physics2d.initial_velocity", "Initial velocity", value_type::vector2, 4, R"({"x":0.0,"y":0.0})", {}, {}, {}, "m/s", {}, "physics-vector2"),
            physics_property("dpe.physics2d.ccd", "Continuous collision", value_type::boolean, 5, "false"),
        }));
    const auto box_collider_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::box_collider_2d}, "DragonPixel.Native.BoxCollider2DComponent", "Box Collider 2D", 1, runtime_owner::native,
        {
            physics_property("dpe.physics2d.size", "Size", value_type::vector2, 0, R"({"x":1.0,"y":1.0})", 0.0001, {}, 0.05, "m", {}, "collider-size2"),
            physics_property("dpe.physics2d.offset", "Offset", value_type::vector2, 1, R"({"x":0.0,"y":0.0})", {}, {}, 0.05, "m"),
            physics_property("dpe.physics.sensor", "Sensor", value_type::boolean, 2, "false"),
            physics_property("dpe.physics.density", "Density", value_type::number, 3, "1.0", 0.0001, {}, 0.05),
            physics_property("dpe.physics.friction", "Friction", value_type::number, 4, "0.5", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.restitution", "Restitution", value_type::number, 5, "0.0", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.layer", "Layer", value_type::integer, 6, "0", 0.0, 15.0, 1.0),
            physics_property("dpe.physics.mask", "Mask", value_type::integer, 7, "65535", 0.0, 65535.0, 1.0),
        }));
    const auto circle_collider_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::circle_collider_2d}, "DragonPixel.Native.CircleCollider2DComponent", "Circle Collider 2D", 1, runtime_owner::native,
        {
            physics_property("dpe.physics2d.radius", "Radius", value_type::number, 0, "0.5", 0.0001, {}, 0.05, "m", {}, "collider-radius"),
            physics_property("dpe.physics2d.offset", "Offset", value_type::vector2, 1, R"({"x":0.0,"y":0.0})", {}, {}, 0.05, "m"),
            physics_property("dpe.physics.sensor", "Sensor", value_type::boolean, 2, "false"),
            physics_property("dpe.physics.density", "Density", value_type::number, 3, "1.0", 0.0001, {}, 0.05),
            physics_property("dpe.physics.friction", "Friction", value_type::number, 4, "0.5", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.restitution", "Restitution", value_type::number, 5, "0.0", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.layer", "Layer", value_type::integer, 6, "0", 0.0, 15.0, 1.0),
            physics_property("dpe.physics.mask", "Mask", value_type::integer, 7, "65535", 0.0, 65535.0, 1.0),
        }));
    const auto rigid_body_3d_added = result.add(builtin_component(
        std::string{builtin_component_ids::rigid_body_3d}, "DragonPixel.Native.RigidBody3DComponent", "Rigid Body 3D", 1, runtime_owner::native,
        {
            physics_property("dpe.physics3d.body_mode", "Body mode", value_type::string, 0, "\"dynamic\"", {}, {}, {}, {}, {"static", "kinematic", "dynamic"}),
            physics_property("dpe.physics3d.linear_damping", "Linear damping", value_type::number, 1, "0.05", 0.0, {}, 0.05),
            physics_property("dpe.physics3d.angular_damping", "Angular damping", value_type::number, 2, "0.05", 0.0, {}, 0.05),
            physics_property("dpe.physics3d.gravity_scale", "Gravity scale", value_type::number, 3, "1.0", -100.0, 100.0, 0.05),
            physics_property("dpe.physics3d.initial_velocity", "Initial velocity", value_type::vector3, 4, R"({"x":0.0,"y":0.0,"z":0.0})", {}, {}, {}, "m/s", {}, "physics-vector3"),
            physics_property("dpe.physics3d.ccd", "Continuous collision", value_type::boolean, 5, "false"),
        }));
    const auto box_collider_3d_added = result.add(builtin_component(
        std::string{builtin_component_ids::box_collider_3d}, "DragonPixel.Native.BoxCollider3DComponent", "Box Collider 3D", 1, runtime_owner::native,
        {
            physics_property("dpe.physics3d.size", "Size", value_type::vector3, 0, R"({"x":1.0,"y":1.0,"z":1.0})", 0.0001, {}, 0.05, "m", {}, "collider-size3"),
            physics_property("dpe.physics3d.offset", "Offset", value_type::vector3, 1, R"({"x":0.0,"y":0.0,"z":0.0})", {}, {}, 0.05, "m"),
            physics_property("dpe.physics.sensor", "Sensor", value_type::boolean, 2, "false"),
            physics_property("dpe.physics.friction", "Friction", value_type::number, 3, "0.5", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.restitution", "Restitution", value_type::number, 4, "0.0", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.layer", "Layer", value_type::integer, 5, "0", 0.0, 15.0, 1.0),
            physics_property("dpe.physics.mask", "Mask", value_type::integer, 6, "65535", 0.0, 65535.0, 1.0),
        }));
    const auto sphere_collider_3d_added = result.add(builtin_component(
        std::string{builtin_component_ids::sphere_collider_3d}, "DragonPixel.Native.SphereCollider3DComponent", "Sphere Collider 3D", 1, runtime_owner::native,
        {
            physics_property("dpe.physics3d.radius", "Radius", value_type::number, 0, "0.5", 0.0001, {}, 0.05, "m", {}, "collider-radius"),
            physics_property("dpe.physics3d.offset", "Offset", value_type::vector3, 1, R"({"x":0.0,"y":0.0,"z":0.0})", {}, {}, 0.05, "m"),
            physics_property("dpe.physics.sensor", "Sensor", value_type::boolean, 2, "false"),
            physics_property("dpe.physics.friction", "Friction", value_type::number, 3, "0.5", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.restitution", "Restitution", value_type::number, 4, "0.0", 0.0, 1.0, 0.01),
            physics_property("dpe.physics.layer", "Layer", value_type::integer, 5, "0", 0.0, 15.0, 1.0),
            physics_property("dpe.physics.mask", "Mask", value_type::integer, 6, "65535", 0.0, 65535.0, 1.0),
        }));
    const auto tilemap_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::tilemap_2d}, "DragonPixel.Native.Tilemap2DComponent", "Tilemap 2D", 1, runtime_owner::native,
        {
            authoring_property("dpe.tilemap.asset", "Tilemap", value_type::asset_reference, 0, "null",
                {}, {}, {}, {}, {}, true, "tilemap", "Tiles", "Reusable dpe.tilemap asset.", "asset-reference"),
            authoring_property("dpe.tilemap.tint", "Tint", value_type::color, 1,
                R"({"r":1.0,"g":1.0,"b":1.0,"a":1.0})", 0.0, 1.0, 0.01, {}, {}, false, {}, "Tiles",
                "Linear RGBA tint multiplied with every tile.", "color"),
            authoring_property("dpe.tilemap.layer", "Render layer", value_type::integer, 2, "0", -32768.0, 32767.0, 1.0,
                {}, {}, false, {}, "Tiles", "Base ordering layer for tilemap layers."),
        }));
    const auto tilemap_collider_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::tilemap_collider_2d}, "DragonPixel.Native.TilemapCollider2DComponent", "Tilemap Collider 2D", 1, runtime_owner::native,
        {
            physics_property("dpe.tilemap.collider.sensor", "Sensor", value_type::boolean, 0, "false"),
            physics_property("dpe.tilemap.collider.friction", "Friction", value_type::number, 1, "0.5", 0.0, 1.0, 0.01),
            physics_property("dpe.tilemap.collider.restitution", "Restitution", value_type::number, 2, "0.0", 0.0, 1.0, 0.01),
            physics_property("dpe.tilemap.collider.layer", "Layer", value_type::integer, 3, "0", 0.0, 15.0, 1.0),
            physics_property("dpe.tilemap.collider.mask", "Mask", value_type::integer, 4, "65535", 0.0, 65535.0, 1.0),
        }));
    const auto tile_object_placement_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::tile_object_placement_2d},
        "DragonPixel.Native.TileObjectPlacement2DComponent",
        "Tile Object Placement 2D",
        1,
        runtime_owner::native,
        {
            authoring_property("dpe.tileobject.map", "Tilemap", value_type::asset_reference, 0,
                "null", {}, {}, {}, {}, {}, true, "tilemap", "Tiles",
                "Tilemap asset that owns this brush-created object.", "asset-reference"),
            authoring_property("dpe.tileobject.layer", "Layer ID", value_type::string, 1,
                R"("")", {}, {}, {}, {}, {}, false, {}, "Tiles",
                "Stable active-layer identity at placement time."),
            authoring_property("dpe.tileobject.cell_x", "Cell X", value_type::integer, 2,
                "0", -1'000'000.0, 1'000'000.0, 1.0, {}, {}, false, {}, "Tiles"),
            authoring_property("dpe.tileobject.cell_y", "Cell Y", value_type::integer, 3,
                "0", -1'000'000.0, 1'000'000.0, 1.0, {}, {}, false, {}, "Tiles"),
            authoring_property("dpe.tileobject.source_kind", "Source kind", value_type::string, 4,
                R"("scene-object")", {}, {}, {}, {}, {"prefab", "scene-object"}, false, {}, "Tiles"),
            authoring_property("dpe.tileobject.source", "Source", value_type::string, 5,
                R"("")", {}, {}, {}, {}, {}, false, {}, "Tiles",
                "Prefab path or source GameObject UUID retained for diagnostics."),
        },
        "Tiles",
        "Internal placement ownership used by the GameObject Brush so Tilemap erase and move operations never affect unrelated GameObjects.",
        false,
        false,
        false));
    const auto input_motion_2d_added = result.add(builtin_component(
        std::string{builtin_component_ids::input_motion_2d}, "DragonPixel.Native.InputMotion2DComponent", "Input Motion 2D", 1, runtime_owner::native,
        {
            authoring_property("dpe.input.horizontal_action", "Horizontal action", value_type::string, 0, "\"move.x\"",
                {}, {}, {}, {}, {}, false, {}, "Input", "Portable action used for horizontal runtime-only motion."),
            authoring_property("dpe.input.vertical_action", "Vertical action", value_type::string, 1, "\"move.y\"",
                {}, {}, {}, {}, {}, false, {}, "Input", "Portable action used for vertical runtime-only motion."),
            authoring_property("dpe.input.speed", "Speed", value_type::number, 2, "5.0", 0.0, 1000.0, 0.1,
                "m/s", {}, false, {}, "Input", "Runtime-only speed while the focused Game view supplies actions."),
        },
        "Input",
        "Moves this GameObject in the isolated Play world from focused Game-view actions."));
    if (!transform_added || !rotator_added || !camera_added || !sprite_added || !mesh_added
        || !material_added || !light_added || !rigid_body_2d_added || !box_collider_2d_added
        || !circle_collider_2d_added || !rigid_body_3d_added || !box_collider_3d_added
        || !sphere_collider_3d_added || !tilemap_2d_added || !tilemap_collider_2d_added
        || !tile_object_placement_2d_added || !input_motion_2d_added)
    {
        return registry{};
    }
    return result;
}
}
