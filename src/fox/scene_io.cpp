#include "fox/scene_io.h"

#include <algorithm>
#include <cstdio>
#include <unordered_map>
#include <utility>
#include <vector>

namespace
{
    constexpr int k_scene_version = 1;
}

namespace fox
{
    scene_io::scene_io(fecs::world& world, render_queue& queue)
        : world_(world)
        , queue_(queue)
    {
    }

    void scene_io::set_post_processing_callbacks(
        std::function<void(scene_post_processing_settings&)> read_callback,
        std::function<void(const scene_post_processing_settings&)> write_callback)
    {
        read_post_processing_callback_ = std::move(read_callback);
        write_post_processing_callback_ = std::move(write_callback);
    }

    bool scene_io::save_scene(const scene_save_desc& desc)
    {
        clear_error();

        if (desc.path.empty())
        {
            set_error("scene_io: save path is empty");
            return false;
        }

        std::unordered_map<std::uint64_t, scene_object_record> records;
        world_.query<editor_object_component>().each_entity([&](fecs::entity, editor_object_component& obj)
        {
            if (obj.object_id == 0)
                return;
            if (records.find(obj.object_id) != records.end())
                return;

            scene_object_record record{};
            record.id = obj.object_id;
            record.name = obj.name;
            record.model = obj.model;
            record.is_dynamic = obj.is_dynamic;
            record.position = obj.position;
            record.rotation = obj.rotation;
            record.scale = obj.scale;
            record.anim.enabled = obj.anim_enabled;
            record.anim.paused = obj.anim_paused;
            record.anim.index = obj.anim_index;
            record.anim.playback_speed = obj.playback_speed;
            record.anim.time_offset = obj.time_offset;
            record.anim.anim_time = obj.anim_time;
            records.emplace(record.id, std::move(record));
        });

        world_.query<editor_object_component, static_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, static_mesh_component& mesh)
        {
            auto it = records.find(obj.object_id);
            if (it == records.end())
                return;
            it->second.visible = mesh.visible;
        });

        world_.query<editor_object_component, dynamic_mesh_component>().each_entity([&](fecs::entity, editor_object_component& obj, dynamic_mesh_component& mesh)
        {
            auto it = records.find(obj.object_id);
            if (it == records.end())
                return;
            it->second.visible = mesh.visible;
            it->second.anim = mesh.anim;
        });

        std::vector<scene_object_record> ordered;
        ordered.reserve(records.size());
        for (const auto& entry : records)
            ordered.push_back(entry.second);

        std::sort(ordered.begin(), ordered.end(), [](const scene_object_record& a, const scene_object_record& b)
        {
            return a.id < b.id;
        });

        JsonLoader root;
        JsonLoader& scene = root["scene"];
        scene["version"] = k_scene_version;
        scene["name"] = desc.scene_name.empty() ? "scene" : desc.scene_name;

        JsonLoader& objects = scene["objects"];
        for (std::size_t i = 0; i < ordered.size(); ++i)
        {
            const scene_object_record& record = ordered[i];
            JsonLoader& node = objects[std::to_string(i)];
            node["id"] = static_cast<std::int64_t>(record.id);
            node["name"] = record.name;
            node["type"] = record.is_dynamic ? "dynamic_mesh" : "static_mesh";
            node["asset"] = record.model;
            node["visible"] = record.visible;

            JsonLoader& transform = node["transform"];
            JsonLoader& pos = transform["position"];
            pos["x"] = static_cast<double>(record.position.x);
            pos["y"] = static_cast<double>(record.position.y);
            pos["z"] = static_cast<double>(record.position.z);

            JsonLoader& rot = transform["rotation"];
            rot["x"] = static_cast<double>(record.rotation.x);
            rot["y"] = static_cast<double>(record.rotation.y);
            rot["z"] = static_cast<double>(record.rotation.z);

            JsonLoader& scale = transform["scale"];
            scale["x"] = static_cast<double>(record.scale.x);
            scale["y"] = static_cast<double>(record.scale.y);
            scale["z"] = static_cast<double>(record.scale.z);

            if (record.is_dynamic)
            {
                JsonLoader& dynamic = node["dynamic"];
                dynamic["anim_enabled"] = record.anim.enabled;
                dynamic["anim_paused"] = record.anim.paused;
                dynamic["anim_index"] = static_cast<std::int64_t>(record.anim.index);
                dynamic["playback_speed"] = static_cast<double>(record.anim.playback_speed);
                dynamic["time_offset"] = static_cast<double>(record.anim.time_offset);
                dynamic["anim_time"] = static_cast<double>(record.anim.anim_time);
            }
        }

        write_globals(scene, desc.include_globals);

        root.Save(desc.path);
        return true;
    }

    bool scene_io::load_scene(const scene_load_desc& desc)
    {
        clear_error();

        if (desc.path.empty())
        {
            set_error("scene_io: load path is empty");
            return false;
        }

        JsonLoader root;
        root.Load(desc.path);
        if (!root.IsValid())
        {
            set_error("scene_io: failed to load scene json");
            return false;
        }

        const JsonLoader& scene = root["scene"];
        if (!scene.IsValid())
        {
            set_error("scene_io: missing root 'scene' node");
            return false;
        }

        const int version = scene["version"].AsInt(-1);
        if (version != k_scene_version)
        {
            set_error("scene_io: unsupported scene version");
            return false;
        }

        if (desc.clear_existing)
            clear_existing_editor_objects();

        const JsonLoader& objects = scene["objects"];
        if (objects.IsValid())
        {
            std::uint64_t max_id = queue_.next_object_id();
        for (const auto& item : objects)
        {
            const JsonLoader& node = item.second;
            if (!node.IsValid())
                continue;

            scene_object_record record{};
            if (!parse_scene_object(node, record))
                continue;

            render_queue::object_id spawned = 0;
            if (record.is_dynamic)
            {
                render_queue::dynamic_mesh_desc rq_desc{};
                rq_desc.path = record.model;
                rq_desc.position = record.position;
                rq_desc.rotation = record.rotation;
                rq_desc.scale = record.scale;
                rq_desc.visible = record.visible;
                rq_desc.name = record.name;
                rq_desc.anim_enabled = record.anim.enabled;
                rq_desc.anim_paused = record.anim.paused;
                rq_desc.anim_index = record.anim.index;
                rq_desc.playback_speed = record.anim.playback_speed;
                rq_desc.time_offset = record.anim.time_offset;
                rq_desc.anim_time = record.anim.anim_time;
                rq_desc.forced_id = record.id;
                spawned = queue_.add_dynamic_mesh(rq_desc);
            }
            else
            {
                render_queue::static_mesh_desc rq_desc{};
                rq_desc.path = record.model;
                rq_desc.position = record.position;
                rq_desc.rotation = record.rotation;
                rq_desc.scale = record.scale;
                rq_desc.visible = record.visible;
                rq_desc.name = record.name;
                rq_desc.forced_id = record.id;
                spawned = queue_.add_static_mesh(rq_desc);
            }

            if (spawned == 0)
                continue;

            for (const auto e : queue_.entities(spawned))
                world_.add_component<editor_tag>(e);

            if (spawned > max_id)
                max_id = spawned;
        }

            if (max_id >= queue_.next_object_id())
                queue_.set_next_object_id(max_id + 1);
        }

        read_globals(scene);

        return true;
    }

    void scene_io::write_globals(JsonLoader& scene, const bool include_globals)
    {
        if (!include_globals)
            return;

        JsonLoader& globals = scene["globals"];
        write_post_processing(globals);
    }

    void scene_io::read_globals(const JsonLoader& scene)
    {
        const JsonLoader& globals = scene["globals"];
        if (!globals.IsValid())
            return;

        read_post_processing(globals);
    }

    void scene_io::write_post_processing(JsonLoader& globals)
    {
        if (!read_post_processing_callback_)
            return;

        scene_post_processing_settings settings{};
        read_post_processing_callback_(settings);

        JsonLoader& post = globals["post_processing"];

        JsonLoader& basic = post["post_process"];
        basic["enabled"] = settings.post_process.enabled;
        basic["exposure_enabled"] = settings.post_process.exposure_enabled;
        basic["exposure"] = static_cast<double>(settings.post_process.exposure);
        basic["contrast_enabled"] = settings.post_process.contrast_enabled;
        basic["contrast"] = static_cast<double>(settings.post_process.contrast);
        basic["saturation_enabled"] = settings.post_process.saturation_enabled;
        basic["saturation"] = static_cast<double>(settings.post_process.saturation);
        basic["vignette_enabled"] = settings.post_process.vignette_enabled;
        basic["vignette_strength"] = static_cast<double>(settings.post_process.vignette_strength);
        basic["vignette_power"] = static_cast<double>(settings.post_process.vignette_power);

        JsonLoader& rainy = post["rainy_effect"];
        rainy["enabled"] = settings.rainy_effect.enabled;
        rainy["intensity"] = static_cast<double>(settings.rainy_effect.intensity);
        rainy["streak_density"] = static_cast<double>(settings.rainy_effect.streak_density);
        rainy["streak_length"] = static_cast<double>(settings.rainy_effect.streak_length);
        rainy["streak_speed"] = static_cast<double>(settings.rainy_effect.streak_speed);
        rainy["streak_probability"] = static_cast<double>(settings.rainy_effect.streak_probability);
        rainy["depth_weight"] = static_cast<double>(settings.rainy_effect.depth_weight);
        rainy["depth_bias"] = static_cast<double>(settings.rainy_effect.depth_bias);
        rainy["wind"] = static_cast<double>(settings.rainy_effect.wind);
        rainy["darken"] = static_cast<double>(settings.rainy_effect.darken);
        write_colour(rainy["tint"], settings.rainy_effect.tint);

        JsonLoader& advanced = post["advanced_effects"];
        advanced["enabled"] = settings.advanced_effects.enabled;
        advanced["bloom_enabled"] = settings.advanced_effects.bloom_enabled;
        advanced["bloom_threshold"] = static_cast<double>(settings.advanced_effects.bloom_threshold);
        advanced["bloom_intensity"] = static_cast<double>(settings.advanced_effects.bloom_intensity);
        advanced["film_grain_enabled"] = settings.advanced_effects.film_grain_enabled;
        advanced["film_grain_strength"] = static_cast<double>(settings.advanced_effects.film_grain_strength);
        advanced["film_grain_speed"] = static_cast<double>(settings.advanced_effects.film_grain_speed);
        advanced["motion_blur_enabled"] = settings.advanced_effects.motion_blur_enabled;
        advanced["motion_blur_strength"] = static_cast<double>(settings.advanced_effects.motion_blur_strength);
        advanced["fog_enabled"] = settings.advanced_effects.fog_enabled;
        write_colour(advanced["fog_colour"], settings.advanced_effects.fog_colour);
        advanced["fog_start"] = static_cast<double>(settings.advanced_effects.fog_start);
        advanced["fog_end"] = static_cast<double>(settings.advanced_effects.fog_end);
        advanced["ssr_enabled"] = settings.advanced_effects.ssr_enabled;
        advanced["ssr_strength"] = static_cast<double>(settings.advanced_effects.ssr_strength);
        advanced["depth_of_field_enabled"] = settings.advanced_effects.depth_of_field_enabled;
        advanced["dof_focus"] = static_cast<double>(settings.advanced_effects.dof_focus);
        advanced["dof_range"] = static_cast<double>(settings.advanced_effects.dof_range);
        advanced["god_rays_enabled"] = settings.advanced_effects.god_rays_enabled;
        advanced["god_rays_strength"] = static_cast<double>(settings.advanced_effects.god_rays_strength);
        write_vec4_xy(advanced["god_rays_screen_pos"], settings.advanced_effects.god_rays_screen_pos);
    }

    void scene_io::read_post_processing(const JsonLoader& globals)
    {
        if (!write_post_processing_callback_)
            return;

        scene_post_processing_settings settings{};
        if (read_post_processing_callback_)
            read_post_processing_callback_(settings);

        const JsonLoader& post = globals["post_processing"];
        if (!post.IsValid())
        {
            write_post_processing_callback_(settings);
            return;
        }

        const JsonLoader& basic = post["post_process"];
        settings.post_process.enabled = basic["enabled"].AsBool(settings.post_process.enabled);
        settings.post_process.exposure_enabled = basic["exposure_enabled"].AsBool(settings.post_process.exposure_enabled);
        settings.post_process.exposure = basic["exposure"].AsFloat(settings.post_process.exposure);
        settings.post_process.contrast_enabled = basic["contrast_enabled"].AsBool(settings.post_process.contrast_enabled);
        settings.post_process.contrast = basic["contrast"].AsFloat(settings.post_process.contrast);
        settings.post_process.saturation_enabled = basic["saturation_enabled"].AsBool(settings.post_process.saturation_enabled);
        settings.post_process.saturation = basic["saturation"].AsFloat(settings.post_process.saturation);
        settings.post_process.vignette_enabled = basic["vignette_enabled"].AsBool(settings.post_process.vignette_enabled);
        settings.post_process.vignette_strength = basic["vignette_strength"].AsFloat(settings.post_process.vignette_strength);
        settings.post_process.vignette_power = basic["vignette_power"].AsFloat(settings.post_process.vignette_power);

        const JsonLoader& rainy = post["rainy_effect"];
        settings.rainy_effect.enabled = rainy["enabled"].AsBool(settings.rainy_effect.enabled);
        settings.rainy_effect.intensity = rainy["intensity"].AsFloat(settings.rainy_effect.intensity);
        settings.rainy_effect.streak_density = rainy["streak_density"].AsFloat(settings.rainy_effect.streak_density);
        settings.rainy_effect.streak_length = rainy["streak_length"].AsFloat(settings.rainy_effect.streak_length);
        settings.rainy_effect.streak_speed = rainy["streak_speed"].AsFloat(settings.rainy_effect.streak_speed);
        settings.rainy_effect.streak_probability = rainy["streak_probability"].AsFloat(settings.rainy_effect.streak_probability);
        settings.rainy_effect.depth_weight = rainy["depth_weight"].AsFloat(settings.rainy_effect.depth_weight);
        settings.rainy_effect.depth_bias = rainy["depth_bias"].AsFloat(settings.rainy_effect.depth_bias);
        settings.rainy_effect.wind = rainy["wind"].AsFloat(settings.rainy_effect.wind);
        settings.rainy_effect.darken = rainy["darken"].AsFloat(settings.rainy_effect.darken);
        read_colour(rainy["tint"], settings.rainy_effect.tint);

        const JsonLoader& advanced = post["advanced_effects"];
        settings.advanced_effects.enabled = advanced["enabled"].AsBool(settings.advanced_effects.enabled);
        settings.advanced_effects.bloom_enabled = advanced["bloom_enabled"].AsBool(settings.advanced_effects.bloom_enabled);
        settings.advanced_effects.bloom_threshold = advanced["bloom_threshold"].AsFloat(settings.advanced_effects.bloom_threshold);
        settings.advanced_effects.bloom_intensity = advanced["bloom_intensity"].AsFloat(settings.advanced_effects.bloom_intensity);
        settings.advanced_effects.film_grain_enabled = advanced["film_grain_enabled"].AsBool(settings.advanced_effects.film_grain_enabled);
        settings.advanced_effects.film_grain_strength = advanced["film_grain_strength"].AsFloat(settings.advanced_effects.film_grain_strength);
        settings.advanced_effects.film_grain_speed = advanced["film_grain_speed"].AsFloat(settings.advanced_effects.film_grain_speed);
        settings.advanced_effects.motion_blur_enabled = advanced["motion_blur_enabled"].AsBool(settings.advanced_effects.motion_blur_enabled);
        settings.advanced_effects.motion_blur_strength = advanced["motion_blur_strength"].AsFloat(settings.advanced_effects.motion_blur_strength);
        settings.advanced_effects.fog_enabled = advanced["fog_enabled"].AsBool(settings.advanced_effects.fog_enabled);
        read_colour(advanced["fog_colour"], settings.advanced_effects.fog_colour);
        settings.advanced_effects.fog_start = advanced["fog_start"].AsFloat(settings.advanced_effects.fog_start);
        settings.advanced_effects.fog_end = advanced["fog_end"].AsFloat(settings.advanced_effects.fog_end);
        settings.advanced_effects.ssr_enabled = advanced["ssr_enabled"].AsBool(settings.advanced_effects.ssr_enabled);
        settings.advanced_effects.ssr_strength = advanced["ssr_strength"].AsFloat(settings.advanced_effects.ssr_strength);
        settings.advanced_effects.depth_of_field_enabled = advanced["depth_of_field_enabled"].AsBool(settings.advanced_effects.depth_of_field_enabled);
        settings.advanced_effects.dof_focus = advanced["dof_focus"].AsFloat(settings.advanced_effects.dof_focus);
        settings.advanced_effects.dof_range = advanced["dof_range"].AsFloat(settings.advanced_effects.dof_range);
        settings.advanced_effects.god_rays_enabled = advanced["god_rays_enabled"].AsBool(settings.advanced_effects.god_rays_enabled);
        settings.advanced_effects.god_rays_strength = advanced["god_rays_strength"].AsFloat(settings.advanced_effects.god_rays_strength);
        read_vec4_xy(advanced["god_rays_screen_pos"], settings.advanced_effects.god_rays_screen_pos);

        write_post_processing_callback_(settings);
    }

    void scene_io::write_colour(JsonLoader& node, const colour& value)
    {
        node["r"] = static_cast<double>(value.r);
        node["g"] = static_cast<double>(value.g);
        node["b"] = static_cast<double>(value.b);
    }

    void scene_io::read_colour(const JsonLoader& node, colour& value)
    {
        value.r = node["r"].AsFloat(value.r);
        value.g = node["g"].AsFloat(value.g);
        value.b = node["b"].AsFloat(value.b);
    }

    void scene_io::write_vec4_xy(JsonLoader& node, const vec4& value)
    {
        node["x"] = static_cast<double>(value.x);
        node["y"] = static_cast<double>(value.y);
    }

    void scene_io::read_vec4_xy(const JsonLoader& node, vec4& value)
    {
        value.x = node["x"].AsFloat(value.x);
        value.y = node["y"].AsFloat(value.y);
    }

    void scene_io::set_error(const std::string& message)
    {
        last_error_ = message;
        std::printf("%s\n", message.c_str());
    }

    void scene_io::clear_error()
    {
        last_error_.clear();
    }

    void scene_io::clear_existing_editor_objects()
    {
        std::vector<fecs::entity> to_destroy;
        world_.query<editor_tag>().each_entity([&](fecs::entity e, editor_tag&)
        {
            to_destroy.push_back(e);
        });

        for (const auto e : to_destroy)
            world_.destroy_entity(e);
    }

    bool scene_io::parse_scene_object(const JsonLoader& node, scene_object_record& out) const
    {
        out.id = parse_u64(node["id"], 0);
        out.name = node["name"].GetValue();
        out.model = node["asset"].GetValue();
        out.visible = node["visible"].AsBool(true);
        if (out.model.empty())
            return false;

        const std::string type = node["type"].GetValue();
        out.is_dynamic = (type == "dynamic_mesh" || type == "dynamic");

        const JsonLoader& transform = node["transform"];
        const JsonLoader& pos = transform["position"];
        const JsonLoader& rot = transform["rotation"];
        const JsonLoader& scale = transform["scale"];

        out.position = vec4(pos["x"].AsFloat(0.f), pos["y"].AsFloat(0.f), pos["z"].AsFloat(0.f), 1.f);
        out.rotation = vec4(rot["x"].AsFloat(0.f), rot["y"].AsFloat(0.f), rot["z"].AsFloat(0.f), 0.f);
        out.scale = vec4(scale["x"].AsFloat(1.f), scale["y"].AsFloat(1.f), scale["z"].AsFloat(1.f), 0.f);

        const JsonLoader& dynamic = node["dynamic"];
        out.anim.enabled = dynamic["anim_enabled"].AsBool(true);
        out.anim.paused = dynamic["anim_paused"].AsBool(false);
        out.anim.index = static_cast<std::size_t>(dynamic["anim_index"].AsUInt(0));
        out.anim.playback_speed = dynamic["playback_speed"].AsFloat(1.0f);
        out.anim.time_offset = dynamic["time_offset"].AsFloat(0.0f);
        out.anim.anim_time = dynamic["anim_time"].AsFloat(0.0f);

        return true;
    }

    std::uint64_t scene_io::parse_u64(const JsonLoader& node, std::uint64_t fallback) const
    {
        if (!node.IsValid())
            return fallback;

        try
        {
            return static_cast<std::uint64_t>(std::stoull(node.GetValue()));
        }
        catch (...)
        {
            return fallback;
        }
    }
}
