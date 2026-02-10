#include "game/render_queue.h"

#include <algorithm>
#include <filesystem>

namespace fox
{
    void render_queue::register_components(fecs::world& world)
    {
        world.register_component<animation_controller_component>();
        world.register_component<animation_instance_component>();
        world.register_component<static_tag>();
        world.register_component<camera_target>();
        world.register_component<level_object_tag>();
        world.register_component<editor_tag>();
        world.register_component<editor_object_component>();
        world.register_component<editor_local_component>();
        world.register_component<static_mesh_component>();
        world.register_component<dynamic_mesh_component>();
    }

    render_queue::render_queue(
        fecs::world& world,
        std::unordered_map<std::string, std::unique_ptr<static_mesh>>& static_cache,
        std::unordered_map<std::string, std::unique_ptr<dynamic_mesh>>& dynamic_cache,
        texture_cache* tex_cache,
        float normalize_size)
        : world_(world)
        , static_cache_(static_cache)
        , dynamic_cache_(dynamic_cache)
        , tex_cache_(tex_cache)
        , normalize_size_(normalize_size)
    {
    }

    float render_queue::clamp_scale_min(float v) noexcept { return (v < 0.01f) ? 0.01f : v; }

    matrix render_queue::make_scale_xyz(float sx, float sy, float sz) noexcept
    {
        matrix m = matrix::makeIdentity();
        m(0, 0) = sx;
        m(1, 1) = sy;
        m(2, 2) = sz;
        return m;
    }

    matrix render_queue::build_normalized_world(
        const vec4& pos,
        const vec4& rot,
        const vec4& scale,
        const vec4& bounds_min,
        const vec4& bounds_max) const noexcept
    {
        const vec4 extent = bounds_max - bounds_min;
        const float max_extent = (std::max)((std::max)(extent.x, extent.y), extent.z);
        const float safe_extent = (max_extent > 0.0001f) ? max_extent : 1.f;
        const float norm_scale = normalize_size_ / safe_extent;

        const vec4 center = (bounds_min + bounds_max) * 0.5f;
        const matrix c = matrix::makeTranslation(-center.x, -center.y, -center.z);
        const matrix s = make_scale_xyz(
            clamp_scale_min(scale.x * norm_scale),
            clamp_scale_min(scale.y * norm_scale),
            clamp_scale_min(scale.z * norm_scale));
        const matrix r = matrix::makeRotateXYZ(rot.x, rot.y, rot.z);
        const matrix t = matrix::makeTranslation(pos.x, pos.y, pos.z);
        return t * r * s * c;
    }

    static_mesh* render_queue::get_static_mesh(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        const auto it = static_cache_.find(path);
        if (it != static_cache_.end())
            return it->second.get();

        auto mesh = std::make_unique<static_mesh>();
        if (!mesh->load(path.c_str(), tex_cache_))
            return nullptr;

        static_mesh* ptr = mesh.get();
        static_cache_.emplace(path, std::move(mesh));
        return ptr;
    }

    dynamic_mesh* render_queue::get_dynamic_mesh(const std::string& path)
    {
        if (path.empty())
            return nullptr;

        const auto it = dynamic_cache_.find(path);
        if (it != dynamic_cache_.end())
            return it->second.get();

        auto mesh = std::make_unique<dynamic_mesh>();
        if (!mesh->load(path.c_str(), tex_cache_))
            return nullptr;

        dynamic_mesh* ptr = mesh.get();
        dynamic_cache_.emplace(path, std::move(mesh));
        return ptr;
    }

    render_queue::object_id render_queue::resolve_id(object_id forced_id)
    {
        const object_id object_id = (forced_id != 0) ? forced_id : next_object_id_++;
        if (object_id >= next_object_id_)
            next_object_id_ = object_id + 1;
        return object_id;
    }

    std::string render_queue::make_default_name(const std::string& path, object_id id)
    {
        std::filesystem::path p(path);
        std::string stem = p.stem().string();
        if (stem.empty())
            stem = "Object";
        return stem + "_" + std::to_string(id);
    }

    render_queue::object_id render_queue::add_static_mesh(const static_mesh_desc& desc)
    {
        static_mesh* mesh = get_static_mesh(desc.path);
        if (!mesh || !mesh->loaded())
            return 0;

        const object_id object_id = resolve_id(desc.forced_id);
        const std::string name = desc.name.empty() ? make_default_name(desc.path, object_id) : desc.name;

        std::vector<fecs::entity> ents;
        std::vector<matrix> locals;
        const matrix base_world = build_normalized_world(desc.position, desc.rotation, desc.scale, mesh->bounds_min(), mesh->bounds_max());
        mesh->build_instances(world_, base_world, desc.colour_tint, desc.ka, desc.kd, ents, locals);

        for (std::size_t i = 0; i < ents.size(); ++i)
        {
            const fecs::entity e = ents[i];
            world_.add_component<editor_local_component>(e, editor_local_component{ locals[i] });

            editor_object_component obj{};
            obj.object_id = object_id;
            obj.name = name;
            obj.model = desc.path;
            obj.position = desc.position;
            obj.rotation = desc.rotation;
            obj.scale = desc.scale;
            obj.is_dynamic = false;
            world_.add_component<editor_object_component>(e, obj);

            static_mesh_component c{};
            c.mesh = mesh;
            c.path = desc.path;
            c.visible = desc.visible;
            world_.add_component<static_mesh_component>(e, c);
        }

        return object_id;
    }

    render_queue::object_id render_queue::add_dynamic_mesh(const dynamic_mesh_desc& desc)
    {
        dynamic_mesh* mesh = get_dynamic_mesh(desc.path);
        if (!mesh || !mesh->loaded())
            return 0;

        const object_id object_id = resolve_id(desc.forced_id);
        const std::string name = desc.name.empty() ? make_default_name(desc.path, object_id) : desc.name;

        std::vector<fecs::entity> ents;
        std::vector<matrix> locals;
        const matrix base_world = build_normalized_world(desc.position, desc.rotation, desc.scale, mesh->bounds_min(), mesh->bounds_max());
        mesh->build_instances(world_, base_world, desc.colour_tint, desc.ka, desc.kd, ents, locals);

        const std::size_t clip_count = mesh->animation_count();
        const bool anim_enabled = desc.anim_enabled && clip_count > 0;
        const std::size_t anim_index = (clip_count > 0) ? (std::min)(desc.anim_index, clip_count - 1) : 0;

        for (std::size_t i = 0; i < ents.size(); ++i)
        {
            const fecs::entity e = ents[i];
            world_.add_component<editor_local_component>(e, editor_local_component{ locals[i] });

            editor_object_component obj{};
            obj.object_id = object_id;
            obj.name = name;
            obj.model = desc.path;
            obj.position = desc.position;
            obj.rotation = desc.rotation;
            obj.scale = desc.scale;
            obj.is_dynamic = true;
            obj.anim_enabled = anim_enabled;
            obj.anim_paused = desc.anim_paused;
            obj.anim_index = anim_index;
            obj.playback_speed = desc.playback_speed;
            obj.time_offset = desc.time_offset;
            obj.anim_time = desc.anim_time;
            world_.add_component<editor_object_component>(e, obj);

            dynamic_mesh_component dc{};
            dc.mesh = mesh;
            dc.path = desc.path;
            dc.visible = desc.visible;
            dc.anim.enabled = anim_enabled;
            dc.anim.paused = desc.anim_paused;
            dc.anim.index = anim_index;
            dc.anim.playback_speed = desc.playback_speed;
            dc.anim.time_offset = desc.time_offset;
            dc.anim.anim_time = desc.anim_time;
            world_.add_component<dynamic_mesh_component>(e, dc);

            animation_controller_component anim{};
            anim.mesh = mesh;
            anim.current_anim = anim_index;
            anim.playback_speed = desc.playback_speed;
            anim.time_offset = desc.time_offset;
            world_.add_component<animation_controller_component>(e, anim);

            if (mesh->has_animation())
            {
                dynamic_mesh_instance* inst = new dynamic_mesh_instance(mesh->create_instance());
                inst->anim_index = anim_index;
                animation_instance_component inst_comp{};
                inst_comp.instance = inst;
                world_.add_component<animation_instance_component>(e, inst_comp);

                if (anim_enabled)
                    mesh->tick_skinning(*inst, static_cast<double>((desc.anim_time + desc.time_offset) * desc.playback_speed));

                if (MeshRefPN* mesh_ref = world_.try_get_component<MeshRefPN>(e))
                {
                    for (std::size_t mi = 0; mi < inst->mesh_data.size(); ++mi)
                    {
                        const auto& inst_mesh = inst->mesh_data[mi];
                        mesh_ref->positions = inst_mesh.asset.positions;
                        mesh_ref->normals = inst_mesh.asset.normals;
                        mesh_ref->uvs = inst_mesh.asset.uvs;
                        mesh_ref->tri_count = inst_mesh.asset.tri_count;
                        mesh_ref->has_uvs = inst_mesh.asset.has_uvs;
                        break;
                    }
                }
            }
        }

        return object_id;
    }

    std::vector<fecs::entity> render_queue::entities(object_id id) const
    {
        std::vector<fecs::entity> out;
        world_.query<editor_object_component>().each_entity([&](fecs::entity e, editor_object_component& obj)
        {
            if (obj.object_id == id)
                out.push_back(e);
        });
        return out;
    }

    void render_queue::remove(object_id id)
    {
        for (const auto e : entities(id))
            world_.destroy_entity(e);
    }

    bool render_queue::exists(object_id id) const
    {
        bool found = false;
        world_.query<editor_object_component>().each_entity([&](fecs::entity, editor_object_component& obj)
        {
            if (obj.object_id == id)
                found = true;
        });
        return found;
    }

    void render_queue::apply_world_to_entities(object_id id, const matrix& base_world) const
    {
        world_.query<editor_object_component, editor_local_component, Transform>().each_entity(
            [&](fecs::entity, editor_object_component& obj, editor_local_component& local, Transform& tr)
            {
                if (obj.object_id != id)
                    return;
                tr.world = base_world * local.local;
            });
    }

    Transform render_queue::get_transform(object_id id) const
    {
        Transform out{};
        world_.query<editor_object_component, Transform>().each_entity(
            [&](fecs::entity, editor_object_component& obj, Transform& tr)
            {
                if (obj.object_id == id)
                    out = tr;
            });
        return out;
    }

    bool render_queue::try_get_transform(object_id id, Transform*& out)
    {
        out = nullptr;
        for (const auto e : entities(id))
        {
            out = world_.try_get_component<Transform>(e);
            if (out)
                return true;
        }
        return false;
    }

    bool render_queue::try_get_static(object_id id, static_mesh_component*& out)
    {
        out = nullptr;
        for (const auto e : entities(id))
        {
            out = world_.try_get_component<static_mesh_component>(e);
            if (out)
                return true;
        }
        return false;
    }

    bool render_queue::try_get_dynamic(object_id id, dynamic_mesh_component*& out)
    {
        out = nullptr;
        for (const auto e : entities(id))
        {
            out = world_.try_get_component<dynamic_mesh_component>(e);
            if (out)
                return true;
        }
        return false;
    }

    bool render_queue::try_get_editor_object(object_id id, editor_object_component*& out)
    {
        out = nullptr;
        for (const auto e : entities(id))
        {
            out = world_.try_get_component<editor_object_component>(e);
            if (out)
                return true;
        }
        return false;
    }

    void render_queue::enumerate_objects(std::vector<object_info>& out)
    {
        out.clear();
        std::unordered_map<object_id, object_info> uniq;
        world_.query<editor_object_component>().each_entity([&](fecs::entity, editor_object_component& obj)
        {
            if (obj.object_id == 0)
                return;

            auto [it, inserted] = uniq.emplace(obj.object_id, object_info{});
            if (!inserted)
                return;

            object_info& info = it->second;
            info.id = obj.object_id;
            info.name = obj.name;
            info.is_dynamic = obj.is_dynamic;
            info.asset_path = obj.model;
            info.visible = true;
        });

        out.reserve(uniq.size());
        for (auto& kv : uniq)
        {
            object_info info = kv.second;
            if (info.is_dynamic)
            {
                dynamic_mesh_component* dynamic = nullptr;
                if (try_get_dynamic(info.id, dynamic) && dynamic)
                    info.visible = dynamic->visible;
            }
            else
            {
                static_mesh_component* st = nullptr;
                if (try_get_static(info.id, st) && st)
                    info.visible = st->visible;
            }
            out.push_back(std::move(info));
        }

        std::sort(out.begin(), out.end(), [](const object_info& a, const object_info& b)
        {
            return a.id < b.id;
        });
    }

    bool render_queue::try_get_pick_radius(object_id id, float& out_radius) const
    {
        out_radius = 0.0f;

        editor_object_component* base = nullptr;
        for (const auto e : entities(id))
        {
            base = world_.try_get_component<editor_object_component>(e);
            if (base)
                break;
        }

        if (!base)
            return false;

        vec4 bmin{};
        vec4 bmax{};
        if (base->is_dynamic)
        {
            dynamic_mesh_component* dyn = nullptr;
            for (const auto e : entities(id))
            {
                dyn = world_.try_get_component<dynamic_mesh_component>(e);
                if (dyn)
                    break;
            }
            if (!dyn || !dyn->mesh)
                return false;
            bmin = dyn->mesh->bounds_min();
            bmax = dyn->mesh->bounds_max();
        }
        else
        {
            static_mesh_component* st = nullptr;
            for (const auto e : entities(id))
            {
                st = world_.try_get_component<static_mesh_component>(e);
                if (st)
                    break;
            }
            if (!st || !st->mesh)
                return false;
            bmin = st->mesh->bounds_min();
            bmax = st->mesh->bounds_max();
        }

        const float sx = bmax.x - bmin.x;
        const float sy = bmax.y - bmin.y;
        const float sz = bmax.z - bmin.z;
        const float max_extent = (std::max)((std::max)(sx, sy), sz);
        if (max_extent <= 0.0001f)
            return false;

        const float nx = sx / max_extent;
        const float ny = sy / max_extent;
        const float nz = sz / max_extent;
        const float base_radius = 0.5f * normalize_size_ * std::sqrt(nx * nx + ny * ny + nz * nz);
        const float scale = (std::max)({ std::fabs(base->scale.x), std::fabs(base->scale.y), std::fabs(base->scale.z), 0.01f });
        out_radius = base_radius * scale;
        return out_radius > 0.0001f;
    }

    void render_queue::set_transform(object_id id, const vec4& pos, const vec4& rot, const vec4& scale)
    {
        editor_object_component base{};
        bool found = false;
        world_.query<editor_object_component>().each_entity([&](fecs::entity, editor_object_component& obj)
        {
            if (!found && obj.object_id == id)
            {
                base = obj;
                found = true;
            }
            if (obj.object_id == id)
            {
                obj.position = pos;
                obj.rotation = rot;
                obj.scale = scale;
            }
        });

        if (!found)
            return;

        matrix base_world = matrix::makeIdentity();
        if (base.is_dynamic)
        {
            if (dynamic_mesh* mesh = get_dynamic_mesh(base.model))
                base_world = build_normalized_world(pos, rot, scale, mesh->bounds_min(), mesh->bounds_max());
        }
        else
        {
            if (static_mesh* mesh = get_static_mesh(base.model))
                base_world = build_normalized_world(pos, rot, scale, mesh->bounds_min(), mesh->bounds_max());
        }

        apply_world_to_entities(id, base_world);
    }

    void render_queue::set_visible(object_id id, bool visible)
    {
        world_.query<editor_object_component, static_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, static_mesh_component& c)
        {
            if (obj.object_id == id)
                c.visible = visible;
        });
        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c)
        {
            if (obj.object_id == id)
                c.visible = visible;
        });
    }

    void render_queue::set_animation_enabled(object_id id, bool enabled)
    {
        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c)
        {
            if (obj.object_id != id)
                return;
            obj.anim_enabled = enabled;
            c.anim.enabled = enabled;
        });
    }

    void render_queue::set_animation_paused(object_id id, bool paused)
    {
        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c)
        {
            if (obj.object_id != id)
                return;
            obj.anim_paused = paused;
            c.anim.paused = paused;
        });
    }

    void render_queue::set_animation_index(object_id id, std::size_t index)
    {
        world_.query<editor_object_component, dynamic_mesh_component, animation_controller_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c, animation_controller_component& anim)
        {
            if (obj.object_id != id)
                return;
            const std::size_t clip_count = c.mesh ? c.mesh->animation_count() : 0;
            const std::size_t safe = (clip_count > 0) ? (std::min)(index, clip_count - 1) : 0;
            obj.anim_index = safe;
            c.anim.index = safe;
            anim.current_anim = safe;
        });
    }

    void render_queue::set_playback_speed(object_id id, float speed)
    {
        world_.query<editor_object_component, dynamic_mesh_component, animation_controller_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c, animation_controller_component& anim)
        {
            if (obj.object_id != id)
                return;
            obj.playback_speed = speed;
            c.anim.playback_speed = speed;
            anim.playback_speed = speed;
        });
    }

    void render_queue::set_time_offset(object_id id, float offset)
    {
        world_.query<editor_object_component, dynamic_mesh_component, animation_controller_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c, animation_controller_component& anim)
        {
            if (obj.object_id != id)
                return;
            obj.time_offset = offset;
            c.anim.time_offset = offset;
            anim.time_offset = offset;
        });
    }

    void render_queue::set_anim_time(object_id id, float anim_time)
    {
        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c)
        {
            if (obj.object_id != id)
                return;
            obj.anim_time = anim_time;
            c.anim.anim_time = anim_time;
        });
    }

    void render_queue::reset_anim_time(object_id id)
    {
        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c)
        {
            if (obj.object_id != id)
                return;
            obj.anim_time = 0.0f;
            c.anim.anim_time = 0.0f;
        });
    }

    dynamic_anim_state render_queue::get_anim_state(object_id id) const
    {
        dynamic_anim_state s{};
        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& c)
        {
            if (obj.object_id == id)
                s = c.anim;
        });
        return s;
    }

    void render_queue::tick_dynamic_animations(float dt) noexcept
    {
        world_.query<editor_object_component, dynamic_mesh_component, animation_instance_component, MeshRefPN>().each_entity(
            [&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& dyn, animation_instance_component& inst_comp, MeshRefPN& mesh_ref)
            {
                if (!obj.is_dynamic || obj.object_id == 0 || obj.model.empty())
                    return;
                if (!obj.anim_enabled || !inst_comp.instance)
                    return;

                dynamic_mesh* mesh = dyn.mesh;
                if (!mesh || !mesh->has_animation())
                    return;

                const std::size_t clip_count = mesh->animation_count();
                if (clip_count == 0)
                    return;

                const std::size_t safe_index = (std::min)(obj.anim_index, clip_count - 1);
                if (!obj.anim_paused)
                    obj.anim_time += dt;
                dyn.anim.anim_time = obj.anim_time;
                dyn.anim.index = safe_index;
                dyn.anim.enabled = obj.anim_enabled;
                dyn.anim.paused = obj.anim_paused;
                dyn.anim.playback_speed = obj.playback_speed;
                dyn.anim.time_offset = obj.time_offset;

                dynamic_mesh_instance* inst = inst_comp.instance;
                inst->anim_index = safe_index;
                mesh->tick_skinning(*inst, static_cast<double>((obj.anim_time + obj.time_offset) * obj.playback_speed));

                for (std::size_t mi = 0; mi < inst->mesh_data.size(); ++mi)
                {
                    const auto& inst_mesh = inst->mesh_data[mi];
                    mesh_ref.positions = inst_mesh.asset.positions;
                    mesh_ref.normals = inst_mesh.asset.normals;
                    mesh_ref.uvs = inst_mesh.asset.uvs;
                    mesh_ref.tri_count = inst_mesh.asset.tri_count;
                    mesh_ref.has_uvs = inst_mesh.asset.has_uvs;
                    break;
                }
            });
    }
}
