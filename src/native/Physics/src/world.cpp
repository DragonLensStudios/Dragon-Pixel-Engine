#include <dragonpixel/physics/world.h>

#include <box2d/box2d.h>

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/GroupFilterTable.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/StaticCompoundShape.h>
#include <Jolt/Physics/PhysicsSystem.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace dragonpixel::physics
{
namespace
{
constexpr double minimum_extent = 0.0001;
constexpr std::uint64_t invalid_user_data = std::numeric_limits<std::uint64_t>::max();

bool finite(double value)
{
    return std::isfinite(value);
}

bool finite(const vector3& value)
{
    return finite(value.x) && finite(value.y) && finite(value.z);
}

bool finite(const quaternion& value)
{
    return finite(value.x) && finite(value.y) && finite(value.z) && finite(value.w);
}

double length_squared(const quaternion& value)
{
    return value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w;
}

quaternion normalized(const quaternion& value)
{
    const auto inverse_length = 1.0 / std::sqrt(length_squared(value));
    return {
        value.x * inverse_length,
        value.y * inverse_length,
        value.z * inverse_length,
        value.w * inverse_length,
    };
}

bool settings_valid(const world_settings& settings)
{
    return finite(settings.gravity_2d.x) && finite(settings.gravity_2d.y)
        && finite(settings.gravity_3d)
        && finite(settings.fixed_time_step_seconds)
        && settings.fixed_time_step_seconds > 0.0
        && settings.maximum_catch_up_ticks > 0
        && settings.box2d_solver_substeps > 0
        && settings.jolt_collision_steps > 0;
}

bool layers_collide(const collider_descriptor& first, const collider_descriptor& second)
{
    const auto first_category = static_cast<std::uint16_t>(std::uint16_t{1}
        << std::min<std::uint16_t>(first.layer, 15));
    const auto second_category = static_cast<std::uint16_t>(std::uint16_t{1}
        << std::min<std::uint16_t>(second.layer, 15));
    return (first.mask & second_category) != 0 && (second.mask & first_category) != 0;
}

bool bodies_collide(const body_descriptor& first, const body_descriptor& second)
{
    for (const auto& first_collider : first.colliders)
    {
        for (const auto& second_collider : second.colliders)
        {
            if (layers_collide(first_collider, second_collider))
            {
                return true;
            }
        }
    }
    return false;
}

core::diagnostic error(std::string code, std::string message, std::string context = {})
{
    return {
        core::diagnostic_severity::error,
        std::move(code),
        std::move(message),
        std::move(context),
    };
}

namespace jolt_layers
{
constexpr JPH::ObjectLayer non_moving = 0;
constexpr JPH::ObjectLayer moving = 1;
}

namespace jolt_broad_phase_layers
{
const JPH::BroadPhaseLayer non_moving{0};
const JPH::BroadPhaseLayer moving{1};
constexpr std::uint32_t count = 2;
}

class object_layer_pair_filter final : public JPH::ObjectLayerPairFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer first, JPH::ObjectLayer second) const override
    {
        return first == jolt_layers::moving || second == jolt_layers::moving;
    }
};

class broad_phase_layer_interface final : public JPH::BroadPhaseLayerInterface
{
public:
    std::uint32_t GetNumBroadPhaseLayers() const override
    {
        return jolt_broad_phase_layers::count;
    }

    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override
    {
        return layer == jolt_layers::non_moving
            ? jolt_broad_phase_layers::non_moving
            : jolt_broad_phase_layers::moving;
    }

#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(JPH::BroadPhaseLayer layer) const override
    {
        return layer == jolt_broad_phase_layers::non_moving ? "NON_MOVING" : "MOVING";
    }
#endif
};

class object_vs_broad_phase_filter final : public JPH::ObjectVsBroadPhaseLayerFilter
{
public:
    bool ShouldCollide(JPH::ObjectLayer object_layer, JPH::BroadPhaseLayer broad_phase_layer) const override
    {
        return object_layer == jolt_layers::moving
            || broad_phase_layer == jolt_broad_phase_layers::moving;
    }
};

class jolt_runtime final
{
public:
    static void acquire()
    {
        std::scoped_lock lock{mutex_};
        if (references_++ == 0)
        {
            JPH::RegisterDefaultAllocator();
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        }
    }

    static void release()
    {
        std::scoped_lock lock{mutex_};
        if (--references_ == 0)
        {
            JPH::UnregisterTypes();
            delete JPH::Factory::sInstance;
            JPH::Factory::sInstance = nullptr;
        }
    }

private:
    static inline std::mutex mutex_;
    static inline std::uint32_t references_{};
};

struct pending_jolt_contact final
{
    std::uint64_t user_a{invalid_user_data};
    std::uint64_t user_b{invalid_user_data};
    bool begin{};
    bool sensor{};
    vector3 point{};
    vector3 normal{};
};

class jolt_contact_listener final : public JPH::ContactListener
{
public:
    void OnContactAdded(
        const JPH::Body& first,
        const JPH::Body& second,
        const JPH::ContactManifold& manifold,
        JPH::ContactSettings&) override
    {
        const auto point = manifold.mRelativeContactPointsOn1.empty()
            ? manifold.mBaseOffset
            : manifold.GetWorldSpaceContactPointOn1(0);
        std::scoped_lock lock{mutex_};
        events_.push_back({
            first.GetUserData(),
            second.GetUserData(),
            true,
            first.IsSensor() || second.IsSensor(),
            {point.GetX(), point.GetY(), point.GetZ()},
            {
                manifold.mWorldSpaceNormal.GetX(),
                manifold.mWorldSpaceNormal.GetY(),
                manifold.mWorldSpaceNormal.GetZ(),
            },
        });
    }

    void OnContactRemoved(const JPH::SubShapeIDPair& pair) override
    {
        std::scoped_lock lock{mutex_};
        const auto found_a = users_.find(pair.GetBody1ID().GetIndexAndSequenceNumber());
        const auto found_b = users_.find(pair.GetBody2ID().GetIndexAndSequenceNumber());
        events_.push_back({
            found_a == users_.end() ? invalid_user_data : found_a->second,
            found_b == users_.end() ? invalid_user_data : found_b->second,
            false,
            (found_a != users_.end() && sensors_.contains(found_a->first))
                || (found_b != users_.end() && sensors_.contains(found_b->first)),
        });
    }

    void register_body(const JPH::BodyID& body_id, std::uint64_t user, bool sensor)
    {
        std::scoped_lock lock{mutex_};
        const auto key = body_id.GetIndexAndSequenceNumber();
        users_[key] = user;
        if (sensor)
        {
            sensors_.insert(key);
        }
    }

    std::vector<pending_jolt_contact> drain()
    {
        std::scoped_lock lock{mutex_};
        auto result = std::move(events_);
        events_.clear();
        return result;
    }

    void clear()
    {
        std::scoped_lock lock{mutex_};
        users_.clear();
        sensors_.clear();
        events_.clear();
    }

private:
    std::mutex mutex_;
    std::unordered_map<std::uint32_t, std::uint64_t> users_;
    std::unordered_set<std::uint32_t> sensors_;
    std::vector<pending_jolt_contact> events_;
};

JPH::EMotionType jolt_motion(body_mode mode)
{
    switch (mode)
    {
        case body_mode::static_body: return JPH::EMotionType::Static;
        case body_mode::kinematic: return JPH::EMotionType::Kinematic;
        case body_mode::dynamic: return JPH::EMotionType::Dynamic;
    }
    return JPH::EMotionType::Static;
}

b2BodyType box2d_motion(body_mode mode)
{
    switch (mode)
    {
        case body_mode::static_body: return b2_staticBody;
        case body_mode::kinematic: return b2_kinematicBody;
        case body_mode::dynamic: return b2_dynamicBody;
    }
    return b2_staticBody;
}

quaternion quaternion_from_z_angle(double angle)
{
    const auto half = angle * 0.5;
    return {0.0, 0.0, std::sin(half), std::cos(half)};
}
}

class physics_world::implementation final
{
public:
    explicit implementation(world_settings settings) : settings_(settings)
    {
        if (!settings_valid(settings_))
        {
            throw std::invalid_argument("physics world settings are invalid");
        }
        jolt_runtime::acquire();
        try
        {
            create_worlds();
        }
        catch (...)
        {
            destroy_worlds();
            jolt_runtime::release();
            throw;
        }
    }

    ~implementation()
    {
        destroy_worlds();
        jolt_runtime::release();
    }

    static std::vector<core::diagnostic> validate(std::span<const body_descriptor> bodies)
    {
        std::vector<core::diagnostic> diagnostics;
        std::unordered_set<core::uuid, core::uuid_hash> unique;
        for (const auto& body : bodies)
        {
            if (body.entity_id.is_nil() || !unique.insert(body.entity_id).second)
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.INVALID_ENTITY",
                    "Physics entity IDs must be non-nil and unique.",
                    body.entity_id.to_string()));
                continue;
            }
            if (!finite(body.position) || !finite(body.rotation) || !finite(body.linear_velocity)
                || !finite(body.angular_velocity) || !finite(body.linear_damping)
                || !finite(body.angular_damping) || !finite(body.gravity_scale)
                || body.linear_damping < 0.0 || body.angular_damping < 0.0
                || body.colliders.empty() || length_squared(body.rotation) <= minimum_extent)
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.INVALID_BODY",
                    "Physics body values were non-finite, negative where prohibited, or had no collider.",
                    body.entity_id.to_string()));
                continue;
            }
            const auto colliders_valid = std::all_of(body.colliders.begin(), body.colliders.end(), [](const auto& collider) {
                return finite(collider.size) && finite(collider.offset) && finite(collider.density)
                    && finite(collider.friction) && finite(collider.restitution)
                    && collider.size.x > minimum_extent && collider.size.y > minimum_extent
                    && collider.size.z > minimum_extent && collider.density > 0.0
                    && collider.friction >= 0.0 && collider.restitution >= 0.0;
            });
            if (!colliders_valid)
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.INVALID_COLLIDER",
                    "Collider dimensions and material values must be finite and positive.",
                    body.entity_id.to_string()));
                continue;
            }
            if (body.dimension == body_dimension::three_d && body.colliders.size() != 1)
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.UNSUPPORTED_3D_COLLIDER_COUNT",
                    "A 3D body currently requires exactly one collider until SubShapeID-aware material and sensor semantics are implemented.",
                    body.entity_id.to_string()));
            }
        }

        return diagnostics;
    }

    void populate(std::span<const body_descriptor> bodies)
    {
        descriptors_.assign(bodies.begin(), bodies.end());
        ids_.clear();
        ids_.reserve(descriptors_.size());
        const auto box2d_count = static_cast<std::size_t>(std::count_if(
            descriptors_.begin(), descriptors_.end(), [](const auto& body) {
                return body.dimension == body_dimension::two_d;
            }));
        const auto jolt_count = descriptors_.size() - box2d_count;
        box2d_bodies_.reserve(box2d_count);
        box2d_users_.reserve(box2d_count);
        box2d_by_entity_.reserve(box2d_count);
        jolt_bodies_.reserve(jolt_count);
        jolt_users_.reserve(jolt_count);
        jolt_by_entity_.reserve(jolt_count);
        descriptor_index_by_id_.reserve(descriptors_.size());
        for (const auto& body : descriptors_)
        {
            ids_.push_back(body.entity_id);
            descriptor_index_by_id_.emplace(body.entity_id, ids_.size() - 1);
        }

        std::vector<std::size_t> jolt_descriptor_indices;
        for (std::size_t index = 0; index < descriptors_.size(); ++index)
        {
            if (descriptors_[index].dimension == body_dimension::three_d)
            {
                jolt_descriptor_indices.push_back(index);
            }
        }
        if (!jolt_descriptor_indices.empty())
        {
            jolt_group_filter_ = new JPH::GroupFilterTable(
                static_cast<JPH::uint>(jolt_descriptor_indices.size()));
            for (std::size_t first = 0; first < jolt_descriptor_indices.size(); ++first)
            {
                for (std::size_t second = first + 1; second < jolt_descriptor_indices.size(); ++second)
                {
                    if (!bodies_collide(
                            descriptors_[jolt_descriptor_indices[first]],
                            descriptors_[jolt_descriptor_indices[second]]))
                    {
                        jolt_group_filter_->DisableCollision(
                            static_cast<JPH::uint>(first),
                            static_cast<JPH::uint>(second));
                    }
                }
            }
        }

        std::size_t jolt_subgroup = 0;
        for (std::size_t index = 0; index < descriptors_.size(); ++index)
        {
            const auto& body = descriptors_[index];
            const auto user_data = static_cast<std::uint64_t>(index);
            if (body.dimension == body_dimension::two_d)
            {
                add_box2d_body(body, user_data);
            }
            else
            {
                add_jolt_body(body, user_data, static_cast<JPH::uint>(jolt_subgroup++));
            }
        }
        tick_ = 0;
        accumulator_ = 0.0;
    }

    [[nodiscard]] const world_settings& settings() const noexcept
    {
        return settings_;
    }

    std::vector<core::diagnostic> apply_commands(std::span<const physics_command> commands)
    {
        std::vector<core::diagnostic> diagnostics;
        for (const auto& command : commands)
        {
            const auto descriptor = descriptor_index_by_id_.find(command.entity_id);
            if (command.entity_id.is_nil() || descriptor == descriptor_index_by_id_.end())
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.COMMAND_ENTITY_NOT_FOUND",
                    "Physics command referenced an unknown entity.",
                    command.entity_id.to_string()));
                continue;
            }
            if (!finite(command.value)
                || (command.kind == command_kind::teleport
                    && (!finite(command.rotation) || length_squared(command.rotation) <= minimum_extent)))
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.INVALID_COMMAND",
                    "Physics command values must be finite and teleport rotations must be non-zero.",
                    command.entity_id.to_string()));
                continue;
            }
            if ((command.kind == command_kind::force || command.kind == command_kind::impulse)
                && descriptors_[descriptor->second].mode != body_mode::dynamic)
            {
                diagnostics.push_back(error(
                    "DPE.PHYSICS.INVALID_BODY_MODE",
                    "Forces and impulses require a dynamic body.",
                    command.entity_id.to_string()));
            }
        }
        if (!diagnostics.empty())
        {
            return diagnostics;
        }

        auto& body_interface = jolt_system_->GetBodyInterface();
        for (const auto& command : commands)
        {
            const auto descriptor_index = descriptor_index_by_id_.at(command.entity_id);
            auto& descriptor = descriptors_[descriptor_index];
            if (const auto body = box2d_by_entity_.find(command.entity_id); body != box2d_by_entity_.end())
            {
                switch (command.kind)
                {
                    case command_kind::force:
                        b2Body_ApplyForceToCenter(
                            body->second,
                            {static_cast<float>(command.value.x), static_cast<float>(command.value.y)},
                            true);
                        break;
                    case command_kind::impulse:
                        b2Body_ApplyLinearImpulseToCenter(
                            body->second,
                            {static_cast<float>(command.value.x), static_cast<float>(command.value.y)},
                            true);
                        break;
                    case command_kind::set_linear_velocity:
                        b2Body_SetLinearVelocity(
                            body->second,
                            {static_cast<float>(command.value.x), static_cast<float>(command.value.y)});
                        break;
                    case command_kind::set_angular_velocity:
                        b2Body_SetAngularVelocity(body->second, static_cast<float>(command.value.z));
                        break;
                    case command_kind::teleport:
                    {
                        const auto rotation = normalized(command.rotation);
                        b2Body_SetTransform(
                            body->second,
                            {static_cast<float>(command.value.x), static_cast<float>(command.value.y)},
                            b2MakeRot(static_cast<float>(2.0 * std::atan2(rotation.z, rotation.w))));
                        descriptor.position.z = command.value.z;
                        break;
                    }
                }
                continue;
            }

            const auto body = jolt_by_entity_.at(command.entity_id);
            const JPH::Vec3 value{
                static_cast<float>(command.value.x),
                static_cast<float>(command.value.y),
                static_cast<float>(command.value.z),
            };
            switch (command.kind)
            {
                case command_kind::force:
                    body_interface.AddForce(body, value, JPH::EActivation::Activate);
                    break;
                case command_kind::impulse:
                    body_interface.AddImpulse(body, value);
                    body_interface.ActivateBody(body);
                    break;
                case command_kind::set_linear_velocity:
                    body_interface.SetLinearVelocity(body, value);
                    body_interface.ActivateBody(body);
                    break;
                case command_kind::set_angular_velocity:
                    body_interface.SetAngularVelocity(body, value);
                    body_interface.ActivateBody(body);
                    break;
                case command_kind::teleport:
                {
                    const auto rotation = normalized(command.rotation);
                    body_interface.SetPositionAndRotation(
                        body,
                        JPH::RVec3{
                            static_cast<JPH::Real>(command.value.x),
                            static_cast<JPH::Real>(command.value.y),
                            static_cast<JPH::Real>(command.value.z),
                        },
                        JPH::Quat{
                            static_cast<float>(rotation.x),
                            static_cast<float>(rotation.y),
                            static_cast<float>(rotation.z),
                            static_cast<float>(rotation.w),
                        },
                        JPH::EActivation::Activate);
                    break;
                }
            }
        }
        return diagnostics;
    }

    step_result advance(double elapsed_seconds)
    {
        step_result result;
        if (!finite(elapsed_seconds) || elapsed_seconds < 0.0)
        {
            result.diagnostics.push_back(error(
                "DPE.PHYSICS.INVALID_ELAPSED",
                "Elapsed time must be finite and non-negative."));
            return result;
        }

        accumulator_ += elapsed_seconds;
        const auto fixed_step = settings_.fixed_time_step_seconds;
        while (accumulator_ + std::numeric_limits<double>::epsilon() >= fixed_step
            && result.ticks < settings_.maximum_catch_up_ticks)
        {
            b2World_Step(
                box2d_world_,
                static_cast<float>(fixed_step),
                static_cast<int>(settings_.box2d_solver_substeps));
            jolt_system_->Update(
                static_cast<float>(fixed_step),
                static_cast<int>(settings_.jolt_collision_steps),
                jolt_temp_allocator_.get(),
                jolt_jobs_.get());
            accumulator_ -= fixed_step;
            ++result.ticks;
            ++tick_;
            collect_contacts(result.contacts);
        }

        if (accumulator_ >= fixed_step)
        {
            const auto retained = std::fmod(accumulator_, fixed_step);
            result.dropped_seconds = accumulator_ - retained;
            accumulator_ = retained;
            result.diagnostics.push_back({
                core::diagnostic_severity::warning,
                "DPE.PHYSICS.CATCH_UP_DROPPED",
                "Physics exceeded its maximum catch-up ticks; excess accumulated time was dropped.",
                std::to_string(result.dropped_seconds),
            });
        }

        collect_transforms(result.transforms);
        return result;
    }

    raycast_hit raycast_2d(vector2 origin, vector2 direction, double distance) const
    {
        raycast_hit result;
        if (!finite(origin.x) || !finite(origin.y) || !finite(direction.x) || !finite(direction.y)
            || !finite(distance) || distance <= 0.0)
        {
            return result;
        }
        const auto direction_length = std::hypot(direction.x, direction.y);
        if (direction_length <= minimum_extent)
        {
            return result;
        }
        const b2QueryFilter filter = b2DefaultQueryFilter();
        const auto cast = b2World_CastRayClosest(
            box2d_world_,
            {static_cast<float>(origin.x), static_cast<float>(origin.y)},
            {
                static_cast<float>(direction.x / direction_length * distance),
                static_cast<float>(direction.y / direction_length * distance),
            },
            filter);
        if (!cast.hit)
        {
            return result;
        }
        const auto user = reinterpret_cast<std::uintptr_t>(b2Shape_GetUserData(cast.shapeId));
        if (user == 0 || user - 1 >= ids_.size())
        {
            return result;
        }
        result.hit = true;
        result.entity_id = ids_[user - 1];
        result.fraction = cast.fraction;
        result.point = {cast.point.x, cast.point.y, 0.0};
        result.normal = {cast.normal.x, cast.normal.y, 0.0};
        return result;
    }

    raycast_hit raycast_3d(vector3 origin, vector3 direction, double distance) const
    {
        raycast_hit result;
        if (!finite(origin) || !finite(direction) || !finite(distance) || distance <= 0.0)
        {
            return result;
        }
        const auto direction_length = std::sqrt(
            direction.x * direction.x + direction.y * direction.y + direction.z * direction.z);
        if (direction_length <= minimum_extent)
        {
            return result;
        }
        JPH::RRayCast ray{
            JPH::RVec3{
                static_cast<JPH::Real>(origin.x),
                static_cast<JPH::Real>(origin.y),
                static_cast<JPH::Real>(origin.z),
            },
            JPH::Vec3{
                static_cast<float>(direction.x / direction_length * distance),
                static_cast<float>(direction.y / direction_length * distance),
                static_cast<float>(direction.z / direction_length * distance),
            },
        };
        JPH::RayCastResult hit;
        if (!jolt_system_->GetNarrowPhaseQuery().CastRay(ray, hit))
        {
            return result;
        }
        const auto user = jolt_system_->GetBodyInterface().GetUserData(hit.mBodyID);
        if (user >= ids_.size())
        {
            return result;
        }
        const auto point = ray.GetPointOnRay(hit.mFraction);
        result.hit = true;
        result.entity_id = ids_[user];
        result.fraction = hit.mFraction;
        result.point = {point.GetX(), point.GetY(), point.GetZ()};
        JPH::BodyLockRead lock{jolt_system_->GetBodyLockInterface(), hit.mBodyID};
        if (lock.Succeeded())
        {
            const auto normal = lock.GetBody().GetWorldSpaceSurfaceNormal(hit.mSubShapeID2, point);
            result.normal = {normal.GetX(), normal.GetY(), normal.GetZ()};
        }
        return result;
    }

    std::size_t body_count() const noexcept
    {
        return box2d_bodies_.size() + jolt_bodies_.size();
    }

    std::uint64_t tick() const noexcept
    {
        return tick_;
    }

private:
    void create_worlds()
    {
        auto world_definition = b2DefaultWorldDef();
        world_definition.gravity = {
            static_cast<float>(settings_.gravity_2d.x),
            static_cast<float>(settings_.gravity_2d.y),
        };
        box2d_world_ = b2CreateWorld(&world_definition);
        if (B2_IS_NULL(box2d_world_))
        {
            throw std::bad_alloc{};
        }

        const auto threads = std::clamp(
            static_cast<int>(std::thread::hardware_concurrency()) - 1,
            1,
            8);
        jolt_jobs_ = std::make_unique<JPH::JobSystemThreadPool>(
            JPH::cMaxPhysicsJobs,
            JPH::cMaxPhysicsBarriers,
            threads);
        jolt_temp_allocator_ = std::make_unique<JPH::TempAllocatorImpl>(10 * 1024 * 1024);
        jolt_system_ = std::make_unique<JPH::PhysicsSystem>();
        jolt_system_->Init(
            65536,
            0,
            65536,
            10240,
            jolt_broad_phase_,
            jolt_object_vs_broad_phase_,
            jolt_object_pair_filter_);
        jolt_system_->SetGravity(JPH::Vec3{
            static_cast<float>(settings_.gravity_3d.x),
            static_cast<float>(settings_.gravity_3d.y),
            static_cast<float>(settings_.gravity_3d.z),
        });
        jolt_system_->SetContactListener(&jolt_contacts_);
    }

    void destroy_worlds() noexcept
    {
        if (B2_IS_NON_NULL(box2d_world_))
        {
            b2DestroyWorld(box2d_world_);
            box2d_world_ = b2_nullWorldId;
        }
        if (jolt_system_ && !jolt_bodies_.empty())
        {
            auto& body_interface = jolt_system_->GetBodyInterface();
            for (const auto& body : jolt_bodies_)
            {
                if (body_interface.IsAdded(body))
                {
                    body_interface.RemoveBody(body);
                }
                body_interface.DestroyBody(body);
            }
        }
        box2d_bodies_.clear();
        jolt_bodies_.clear();
        box2d_users_.clear();
        jolt_users_.clear();
        box2d_by_entity_.clear();
        jolt_by_entity_.clear();
        descriptor_index_by_id_.clear();
        jolt_contacts_.clear();
        jolt_system_.reset();
        jolt_group_filter_ = nullptr;
        jolt_temp_allocator_.reset();
        jolt_jobs_.reset();
    }

    void add_box2d_body(const body_descriptor& body, std::uint64_t user)
    {
        const auto rotation = normalized(body.rotation);
        auto body_definition = b2DefaultBodyDef();
        body_definition.type = box2d_motion(body.mode);
        body_definition.position = {
            static_cast<float>(body.position.x),
            static_cast<float>(body.position.y),
        };
        body_definition.rotation = b2MakeRot(static_cast<float>(2.0 * std::atan2(rotation.z, rotation.w)));
        body_definition.linearVelocity = {
            static_cast<float>(body.linear_velocity.x),
            static_cast<float>(body.linear_velocity.y),
        };
        body_definition.angularVelocity = static_cast<float>(body.angular_velocity.z);
        body_definition.linearDamping = static_cast<float>(body.linear_damping);
        body_definition.angularDamping = static_cast<float>(body.angular_damping);
        body_definition.gravityScale = static_cast<float>(body.gravity_scale);
        body_definition.isBullet = body.continuous_collision;
        const auto body_id = b2CreateBody(box2d_world_, &body_definition);
        if (B2_IS_NULL(body_id))
        {
            throw std::bad_alloc{};
        }

        try
        {
            for (const auto& collider : body.colliders)
            {
                auto shape_definition = b2DefaultShapeDef();
                shape_definition.userData = reinterpret_cast<void*>(static_cast<std::uintptr_t>(user + 1));
                shape_definition.density = static_cast<float>(collider.density);
                shape_definition.material.friction = static_cast<float>(collider.friction);
                shape_definition.material.restitution = static_cast<float>(collider.restitution);
                shape_definition.isSensor = collider.sensor;
                shape_definition.enableSensorEvents = true;
                shape_definition.enableContactEvents = true;
                shape_definition.filter.categoryBits = std::uint64_t{1} << std::min<std::uint16_t>(collider.layer, 15);
                shape_definition.filter.maskBits = collider.mask;
                b2ShapeId shape_id = b2_nullShapeId;
                if (collider.shape == collider_shape::box)
                {
                    const auto polygon = b2MakeOffsetBox(
                        static_cast<float>(collider.size.x * 0.5),
                        static_cast<float>(collider.size.y * 0.5),
                        {static_cast<float>(collider.offset.x), static_cast<float>(collider.offset.y)},
                        b2Rot_identity);
                    shape_id = b2CreatePolygonShape(body_id, &shape_definition, &polygon);
                }
                else
                {
                    const b2Circle circle{
                        {static_cast<float>(collider.offset.x), static_cast<float>(collider.offset.y)},
                        static_cast<float>(collider.size.x),
                    };
                    shape_id = b2CreateCircleShape(body_id, &shape_definition, &circle);
                }
                if (B2_IS_NULL(shape_id))
                {
                    throw std::bad_alloc{};
                }
            }
            box2d_bodies_.push_back(body_id);
            box2d_users_.push_back(user);
            box2d_by_entity_.emplace(body.entity_id, body_id);
        }
        catch (...)
        {
            box2d_by_entity_.erase(body.entity_id);
            if (!box2d_bodies_.empty() && B2_ID_EQUALS(box2d_bodies_.back(), body_id))
            {
                box2d_bodies_.pop_back();
            }
            if (box2d_users_.size() > box2d_bodies_.size())
            {
                box2d_users_.pop_back();
            }
            b2DestroyBody(body_id);
            throw;
        }
    }

    void add_jolt_body(const body_descriptor& body, std::uint64_t user, JPH::uint subgroup)
    {
        JPH::StaticCompoundShapeSettings compound;
        for (const auto& collider : body.colliders)
        {
            JPH::ShapeRefC child;
            if (collider.shape == collider_shape::box)
            {
                child = new JPH::BoxShape(JPH::Vec3{
                    static_cast<float>(collider.size.x * 0.5),
                    static_cast<float>(collider.size.y * 0.5),
                    static_cast<float>(collider.size.z * 0.5),
                });
            }
            else
            {
                child = new JPH::SphereShape(static_cast<float>(collider.size.x));
            }
            compound.AddShape(
                JPH::Vec3{
                    static_cast<float>(collider.offset.x),
                    static_cast<float>(collider.offset.y),
                    static_cast<float>(collider.offset.z),
                },
                JPH::Quat::sIdentity(),
                child.GetPtr());
        }
        const auto shape_result = compound.Create();
        if (shape_result.HasError())
        {
            throw std::runtime_error(std::string{"Jolt shape creation failed: "} + shape_result.GetError().c_str());
        }
        const JPH::ShapeRefC shape = shape_result.Get();
        const auto& material = body.colliders.front();
        const auto sensor = std::any_of(body.colliders.begin(), body.colliders.end(), [](const auto& collider) {
            return collider.sensor;
        });
        const auto rotation = normalized(body.rotation);
        const auto motion = jolt_motion(body.mode);
        JPH::BodyCreationSettings body_settings{
            shape,
            JPH::RVec3{
                static_cast<JPH::Real>(body.position.x),
                static_cast<JPH::Real>(body.position.y),
                static_cast<JPH::Real>(body.position.z),
            },
            JPH::Quat{
                static_cast<float>(rotation.x),
                static_cast<float>(rotation.y),
                static_cast<float>(rotation.z),
                static_cast<float>(rotation.w),
            },
            motion,
            motion == JPH::EMotionType::Static ? jolt_layers::non_moving : jolt_layers::moving,
        };
        body_settings.mUserData = user;
        body_settings.mCollisionGroup = JPH::CollisionGroup{jolt_group_filter_.GetPtr(), 0, subgroup};
        body_settings.mLinearVelocity = JPH::Vec3{
            static_cast<float>(body.linear_velocity.x),
            static_cast<float>(body.linear_velocity.y),
            static_cast<float>(body.linear_velocity.z),
        };
        body_settings.mAngularVelocity = JPH::Vec3{
            static_cast<float>(body.angular_velocity.x),
            static_cast<float>(body.angular_velocity.y),
            static_cast<float>(body.angular_velocity.z),
        };
        body_settings.mLinearDamping = static_cast<float>(body.linear_damping);
        body_settings.mAngularDamping = static_cast<float>(body.angular_damping);
        body_settings.mGravityFactor = static_cast<float>(body.gravity_scale);
        body_settings.mFriction = static_cast<float>(material.friction);
        body_settings.mRestitution = static_cast<float>(material.restitution);
        body_settings.mIsSensor = sensor;
        body_settings.mMotionQuality = body.continuous_collision
            ? JPH::EMotionQuality::LinearCast
            : JPH::EMotionQuality::Discrete;

        auto& body_interface = jolt_system_->GetBodyInterface();
        const auto body_id = body_interface.CreateAndAddBody(
            body_settings,
            motion == JPH::EMotionType::Static ? JPH::EActivation::DontActivate : JPH::EActivation::Activate);
        if (!body_id.IsInvalid())
        {
            try
            {
                jolt_bodies_.push_back(body_id);
                jolt_users_.push_back(user);
                jolt_by_entity_.emplace(body.entity_id, body_id);
                jolt_contacts_.register_body(body_id, user, sensor);
            }
            catch (...)
            {
                jolt_by_entity_.erase(body.entity_id);
                if (!jolt_bodies_.empty() && jolt_bodies_.back() == body_id)
                {
                    jolt_bodies_.pop_back();
                }
                if (jolt_users_.size() > jolt_bodies_.size())
                {
                    jolt_users_.pop_back();
                }
                if (body_interface.IsAdded(body_id))
                {
                    body_interface.RemoveBody(body_id);
                }
                body_interface.DestroyBody(body_id);
                throw;
            }
        }
        else
        {
            throw std::runtime_error("Jolt could not allocate a physics body");
        }
    }

    void collect_contacts(std::vector<contact_event>& destination)
    {
        const auto box2d_contacts = b2World_GetContactEvents(box2d_world_);
        for (int index = 0; index < box2d_contacts.beginCount; ++index)
        {
            const auto& event = box2d_contacts.beginEvents[index];
            const vector3 point = event.manifold.pointCount > 0
                ? vector3{event.manifold.points[0].point.x, event.manifold.points[0].point.y, 0.0}
                : vector3{};
            append_box2d_contact(
                destination,
                event.shapeIdA,
                event.shapeIdB,
                true,
                false,
                point,
                {event.manifold.normal.x, event.manifold.normal.y, 0.0});
        }
        for (int index = 0; index < box2d_contacts.endCount; ++index)
        {
            const auto& event = box2d_contacts.endEvents[index];
            append_box2d_contact(destination, event.shapeIdA, event.shapeIdB, false, false);
        }
        const auto box2d_sensors = b2World_GetSensorEvents(box2d_world_);
        for (int index = 0; index < box2d_sensors.beginCount; ++index)
        {
            const auto& event = box2d_sensors.beginEvents[index];
            append_box2d_contact(destination, event.sensorShapeId, event.visitorShapeId, true, true);
        }
        for (int index = 0; index < box2d_sensors.endCount; ++index)
        {
            const auto& event = box2d_sensors.endEvents[index];
            append_box2d_contact(destination, event.sensorShapeId, event.visitorShapeId, false, true);
        }

        for (const auto& event : jolt_contacts_.drain())
        {
            if (event.user_a >= ids_.size() || event.user_b >= ids_.size())
            {
                continue;
            }
            destination.push_back({
                tick_,
                event.sensor
                    ? (event.begin ? contact_kind::begin_trigger : contact_kind::end_trigger)
                    : (event.begin ? contact_kind::begin_contact : contact_kind::end_contact),
                ids_[event.user_a],
                ids_[event.user_b],
                event.point,
                event.normal,
            });
        }
    }

    void append_box2d_contact(
        std::vector<contact_event>& destination,
        b2ShapeId shape_a,
        b2ShapeId shape_b,
        bool begin,
        bool sensor_override,
        vector3 point = {},
        vector3 normal = {})
    {
        const auto user_a = reinterpret_cast<std::uintptr_t>(b2Shape_GetUserData(shape_a));
        const auto user_b = reinterpret_cast<std::uintptr_t>(b2Shape_GetUserData(shape_b));
        if (user_a == 0 || user_b == 0 || user_a - 1 >= ids_.size() || user_b - 1 >= ids_.size())
        {
            return;
        }
        const auto sensor = sensor_override || b2Shape_IsSensor(shape_a) || b2Shape_IsSensor(shape_b);
        destination.push_back({
            tick_,
            sensor
                ? (begin ? contact_kind::begin_trigger : contact_kind::end_trigger)
                : (begin ? contact_kind::begin_contact : contact_kind::end_contact),
            ids_[user_a - 1],
            ids_[user_b - 1],
            point,
            normal,
        });
    }

    void collect_transforms(std::vector<body_transform>& destination) const
    {
        destination.reserve(box2d_bodies_.size() + jolt_bodies_.size());
        for (std::size_t index = 0; index < box2d_bodies_.size(); ++index)
        {
            const auto position = b2Body_GetPosition(box2d_bodies_[index]);
            const auto rotation = b2Rot_GetAngle(b2Body_GetRotation(box2d_bodies_[index]));
            const auto velocity = b2Body_GetLinearVelocity(box2d_bodies_[index]);
            destination.push_back({
                ids_[box2d_users_[index]],
                {position.x, position.y, descriptors_[box2d_users_[index]].position.z},
                quaternion_from_z_angle(rotation),
                {velocity.x, velocity.y, 0.0},
            });
        }
        const auto& body_interface = jolt_system_->GetBodyInterface();
        for (std::size_t index = 0; index < jolt_bodies_.size(); ++index)
        {
            const auto position = body_interface.GetPosition(jolt_bodies_[index]);
            const auto rotation = body_interface.GetRotation(jolt_bodies_[index]);
            const auto velocity = body_interface.GetLinearVelocity(jolt_bodies_[index]);
            destination.push_back({
                ids_[jolt_users_[index]],
                {position.GetX(), position.GetY(), position.GetZ()},
                {rotation.GetX(), rotation.GetY(), rotation.GetZ(), rotation.GetW()},
                {velocity.GetX(), velocity.GetY(), velocity.GetZ()},
            });
        }
        std::sort(destination.begin(), destination.end(), [](const auto& left, const auto& right) {
            return left.entity_id.to_string() < right.entity_id.to_string();
        });
    }

    world_settings settings_;
    b2WorldId box2d_world_{b2_nullWorldId};
    std::vector<b2BodyId> box2d_bodies_;
    std::vector<std::uint64_t> box2d_users_;
    std::unordered_map<core::uuid, b2BodyId, core::uuid_hash> box2d_by_entity_;

    broad_phase_layer_interface jolt_broad_phase_;
    object_vs_broad_phase_filter jolt_object_vs_broad_phase_;
    object_layer_pair_filter jolt_object_pair_filter_;
    JPH::Ref<JPH::GroupFilterTable> jolt_group_filter_;
    std::unique_ptr<JPH::PhysicsSystem> jolt_system_;
    std::unique_ptr<JPH::TempAllocatorImpl> jolt_temp_allocator_;
    std::unique_ptr<JPH::JobSystemThreadPool> jolt_jobs_;
    jolt_contact_listener jolt_contacts_;
    std::vector<JPH::BodyID> jolt_bodies_;
    std::vector<std::uint64_t> jolt_users_;
    std::unordered_map<core::uuid, JPH::BodyID, core::uuid_hash> jolt_by_entity_;

    std::vector<body_descriptor> descriptors_;
    std::vector<core::uuid> ids_;
    std::unordered_map<core::uuid, std::size_t, core::uuid_hash> descriptor_index_by_id_;
    double accumulator_{};
    std::uint64_t tick_{};
};

physics_world::physics_world(world_settings settings)
    : implementation_(std::make_unique<implementation>(settings))
{
}

physics_world::~physics_world() = default;
physics_world::physics_world(physics_world&&) noexcept = default;
physics_world& physics_world::operator=(physics_world&&) noexcept = default;

std::vector<core::diagnostic> physics_world::rebuild(std::span<const body_descriptor> bodies)
{
    auto diagnostics = implementation::validate(bodies);
    if (!diagnostics.empty())
    {
        return diagnostics;
    }

    auto candidate = std::make_unique<implementation>(implementation_->settings());
    candidate->populate(bodies);
    implementation_ = std::move(candidate);
    return diagnostics;
}

std::vector<core::diagnostic> physics_world::apply_commands(std::span<const physics_command> commands)
{
    return implementation_->apply_commands(commands);
}

step_result physics_world::advance(double elapsed_seconds)
{
    return implementation_->advance(elapsed_seconds);
}

raycast_hit physics_world::raycast_2d(vector2 origin, vector2 direction, double distance) const
{
    return implementation_->raycast_2d(origin, direction, distance);
}

raycast_hit physics_world::raycast_3d(vector3 origin, vector3 direction, double distance) const
{
    return implementation_->raycast_3d(origin, direction, distance);
}

void physics_world::clear()
{
    auto candidate = std::make_unique<implementation>(implementation_->settings());
    implementation_ = std::move(candidate);
}

std::size_t physics_world::body_count() const noexcept
{
    return implementation_->body_count();
}

std::uint64_t physics_world::tick() const noexcept
{
    return implementation_->tick();
}
}
