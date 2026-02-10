#include "game/level_builder_ui.h"

#include "imgui.h"
#include "optimized/imgui_hook.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>

namespace
{
    constexpr float kRadToDeg = 180.0f / fox_math::pi_f;
    constexpr float kDegToRad = fox_math::pi_f / 180.0f;

    float snap_value(float v, float step) noexcept
    {
        if (step <= 0.0f)
            return v;
        return std::round(v / step) * step;
    }

    static int InputTextCallback(ImGuiInputTextCallbackData* data)
    {
        auto* str = static_cast<std::string*>(data->UserData);
        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            str->resize(static_cast<std::size_t>(data->BufTextLen));
            data->Buf = str->data();
        }
        return 0;
    }

    bool InputText(std::string& s, const char* label, ImGuiInputTextFlags flags = 0)
    {
        flags |= ImGuiInputTextFlags_CallbackResize;
        if (s.empty())
            s.push_back('\0');

        return ImGui::InputText(label, s.data(), s.size() + 1, flags, InputTextCallback, &s);
    }

    bool DragVec3(const char* label, vec4& v, float speed, float w_value, const char* fmt = "%.3f")
    {
        float a[3]{ v.x, v.y, v.z };
        if (!ImGui::DragFloat3(label, a, speed, 0.0f, 0.0f, fmt))
            return false;
        v = vec4(a[0], a[1], a[2], w_value);
        return true;
    }

    bool DragEulerDeg3(const char* label, vec4& rot_rad, float speed_deg, float w_value, const char* fmt = "%.2f")
    {
        float a[3]{ rot_rad.x * kRadToDeg, rot_rad.y * kRadToDeg, rot_rad.z * kRadToDeg };
        if (!ImGui::DragFloat3(label, a, speed_deg, 0.0f, 0.0f, fmt))
            return false;
        rot_rad = vec4(a[0] * kDegToRad, a[1] * kDegToRad, a[2] * kDegToRad, w_value);
        return true;
    }

    void SnapVec3(vec4& v, float step)
    {
        v.x = snap_value(v.x, step);
        v.y = snap_value(v.y, step);
        v.z = snap_value(v.z, step);
    }

    void SnapEulerDeg3(vec4& rot_rad, float step_deg)
    {
        vec4 deg(rot_rad.x * kRadToDeg, rot_rad.y * kRadToDeg, rot_rad.z * kRadToDeg, 0.f);
        deg.x = snap_value(deg.x, step_deg);
        deg.y = snap_value(deg.y, step_deg);
        deg.z = snap_value(deg.z, step_deg);
        rot_rad = vec4(deg.x * kDegToRad, deg.y * kDegToRad, deg.z * kDegToRad, 0.f);
    }


    std::vector<const char*> MakeCStrItems(const std::vector<std::string>& v)
    {
        std::vector<const char*> out;
        out.reserve(v.size());
        for (const auto& s : v)
            out.push_back(s.c_str());
        return out;
    }

    std::vector<std::string> scan_assets(const std::filesystem::path& root)
    {
        std::vector<std::string> out;
        if (!std::filesystem::exists(root))
            return out;

        const std::filesystem::path base = std::filesystem::current_path();
        for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
        {
            if (!entry.is_regular_file())
                continue;

            const auto ext = entry.path().extension().string();
            if (ext != ".glb" && ext != ".fbx")
                continue;

            std::filesystem::path rel = entry.path();
            if (rel.is_absolute())
                rel = std::filesystem::relative(rel, base);

            out.push_back(rel.generic_string());
        }

        std::sort(out.begin(), out.end());
        return out;
    }
}

namespace fox
{
    void level_builder_ui::init(
        fecs::world* world,
        std::unordered_map<std::string, std::unique_ptr<static_mesh>>* static_cache,
        texture_cache* tex_cache,
        float normalize_size)
    {
        world_ = world;
        static_cache_ = static_cache;
        tex_cache_ = tex_cache;
        normalize_size_ = normalize_size;
    }

    void level_builder_ui::set_render_queue(render_queue* queue)
    {
        render_queue_ = queue;
    }

    void level_builder_ui::set_save_scene_callback(const std::function<void()>& callback)
    {
        save_scene_callback_ = callback;
    }

    void level_builder_ui::init_world_editor(const world_editor_callbacks& callbacks)
    {
        world_callbacks_ = callbacks;
    }

    void level_builder_ui::setup_imgui_views()
    {
        imgui_hook::instance().set_enabled(true);

        imgui_hook::instance().add_view("Fox Debug", [this]()
        {
            if (!world_callbacks_.read_debug_state || !world_callbacks_.write_debug_state)
                return;

            world_callbacks_.read_debug_state(debug_state_);
            if (world_callbacks_.read_render_settings)
                world_callbacks_.read_render_settings(render_state_);

            ImGui::Text("FPS: %.1f", debug_state_.fps);
            ImGui::Text("dt:  %.4f ms", double(debug_state_.delta_time_s) * 1000.0);
            ImGui::Separator();
            ImGui::Checkbox("Free Camera (F)", &debug_state_.free_camera_enabled);
            ImGui::Checkbox("FPS Mouse (SPACE)", &debug_state_.fps_mouse_enabled);
            ImGui::SliderFloat("Mouse Sens", &debug_state_.camera_mouse_sensitivity, 0.001f, 0.05f, "%.4f");
            if (ImGui::Checkbox("Light Cycle Enabled", &debug_state_.light_cycle_enabled) && debug_state_.light_cycle_enabled)
                debug_state_.light_color_override = false;
            ImGui::SliderFloat("Light Cycle Speed", &debug_state_.light_cycle_speed, 0.0f, 3.0f, "%.2f");
            if (ImGui::ColorEdit3("Light Diffuse", &debug_state_.light.diffuse.r))
            {
                debug_state_.light_color_override = true;
                debug_state_.light_cycle_enabled = false;
            }
            if (ImGui::ColorEdit3("Light Ambient", &debug_state_.light.ambient.r))
            {
                debug_state_.light_color_override = true;
                debug_state_.light_cycle_enabled = false;
            }
            ImGui::InputFloat3("Cam Pos", &debug_state_.camera_pos.x);
            if (ImGui::Button("Snap Cam To Player") && world_callbacks_.snap_camera_to_player)
                world_callbacks_.snap_camera_to_player();
            ImGui::Separator();
            ImGui::Text("Textures");
            ImGui::Checkbox("Render Textures", &render_state_.textures_enabled);
            ImGui::Checkbox("Flip V", &render_state_.flip_v);
            ImGui::Text("Cached textures: %zu", debug_state_.cached_texture_count);

            world_callbacks_.write_debug_state(debug_state_);
            if (world_callbacks_.write_render_settings)
                world_callbacks_.write_render_settings(render_state_);
        });

        imgui_hook::instance().add_view("Level Editor", [this]()
        {
            if (!world_callbacks_.read_render_settings || !world_callbacks_.write_render_settings)
                return;

            world_callbacks_.read_render_settings(render_state_);

            if (ImGui::BeginTabBar("LevelEditorTabs"))
            {
                if (ImGui::BeginTabItem("Post Processing"))
                {
                    ImGui::Separator();
                    ImGui::Text("Post Processing");
                    ImGui::Checkbox("Enable Post FX", &render_state_.post_process.enabled);
                    ImGui::Checkbox("Exposure Pass", &render_state_.post_process.exposure_enabled);
                    ImGui::SliderFloat("Exposure", &render_state_.post_process.exposure, 0.1f, 4.0f, "%.2f");
                    ImGui::Checkbox("Contrast Pass", &render_state_.post_process.contrast_enabled);
                    ImGui::SliderFloat("Contrast", &render_state_.post_process.contrast, 0.5f, 2.0f, "%.2f");
                    ImGui::Checkbox("Saturation Pass", &render_state_.post_process.saturation_enabled);
                    ImGui::SliderFloat("Saturation", &render_state_.post_process.saturation, 0.0f, 2.0f, "%.2f");
                    ImGui::Checkbox("Vignette Pass", &render_state_.post_process.vignette_enabled);
                    ImGui::SliderFloat("Vignette Strength", &render_state_.post_process.vignette_strength, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Vignette Power", &render_state_.post_process.vignette_power, 0.1f, 4.0f, "%.2f");
                    ImGui::Separator();
                    ImGui::Text("Rain FX");
                    ImGui::Checkbox("Enable Rain", &render_state_.rainy_effect.enabled);
                    ImGui::SliderFloat("Rain Intensity", &render_state_.rainy_effect.intensity, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Streak Density", &render_state_.rainy_effect.streak_density, 0.0f, 0.1f, "%.3f");
                    ImGui::SliderFloat("Streak Length", &render_state_.rainy_effect.streak_length, 0.05f, 0.5f, "%.2f");
                    ImGui::SliderFloat("Streak Speed", &render_state_.rainy_effect.streak_speed, -5.0f, 0.0f, "%.2f");
                    ImGui::SliderFloat("Streak Probability", &render_state_.rainy_effect.streak_probability, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Depth Weight", &render_state_.rainy_effect.depth_weight, 0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Depth Bias", &render_state_.rainy_effect.depth_bias, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Wind", &render_state_.rainy_effect.wind, -2.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Darken", &render_state_.rainy_effect.darken, 0.0f, 1.0f, "%.2f");
                    ImGui::ColorEdit3("Rain Tint", &render_state_.rainy_effect.tint.r);
                    ImGui::Separator();
                    ImGui::Text("Advanced FX");
                    ImGui::Checkbox("Enable Advanced FX", &render_state_.advanced_effects.enabled);
                    ImGui::Checkbox("Bloom", &render_state_.advanced_effects.bloom_enabled);
                    ImGui::SliderFloat("Bloom Threshold", &render_state_.advanced_effects.bloom_threshold, 0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat("Bloom Intensity", &render_state_.advanced_effects.bloom_intensity, 0.0f, 2.0f, "%.2f");
                    ImGui::Checkbox("Film Grain", &render_state_.advanced_effects.film_grain_enabled);
                    ImGui::SliderFloat("Grain Strength", &render_state_.advanced_effects.film_grain_strength, 0.0f, 0.2f, "%.3f");
                    ImGui::SliderFloat("Grain Speed", &render_state_.advanced_effects.film_grain_speed, 0.0f, 5.0f, "%.2f");
                    ImGui::Checkbox("Motion Blur", &render_state_.advanced_effects.motion_blur_enabled);
                    ImGui::SliderFloat("Motion Blur Strength", &render_state_.advanced_effects.motion_blur_strength, 0.0f, 1.0f, "%.2f");
                    ImGui::Checkbox("Fog / Atmosphere", &render_state_.advanced_effects.fog_enabled);
                    ImGui::ColorEdit3("Fog Color", &render_state_.advanced_effects.fog_colour.r);
                    ImGui::SliderFloat("Fog Start", &render_state_.advanced_effects.fog_start, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("Fog End", &render_state_.advanced_effects.fog_end, 0.0f, 1.0f, "%.2f");
                    ImGui::Checkbox("Screen-Space Reflections", &render_state_.advanced_effects.ssr_enabled);
                    ImGui::SliderFloat("SSR Strength", &render_state_.advanced_effects.ssr_strength, 0.0f, 1.0f, "%.2f");
                    ImGui::Checkbox("Depth of Field", &render_state_.advanced_effects.depth_of_field_enabled);
                    ImGui::SliderFloat("DoF Focus", &render_state_.advanced_effects.dof_focus, 0.0f, 1.0f, "%.2f");
                    ImGui::SliderFloat("DoF Range", &render_state_.advanced_effects.dof_range, 0.01f, 1.0f, "%.2f");
                    ImGui::Checkbox("God Rays", &render_state_.advanced_effects.god_rays_enabled);
                    ImGui::SliderFloat("God Rays Strength", &render_state_.advanced_effects.god_rays_strength, 0.0f, 2.0f, "%.2f");
                    ImGui::SliderFloat2("God Rays Screen Pos", &render_state_.advanced_effects.god_rays_screen_pos.x, 0.0f, 1.0f, "%.2f");
                    ImGui::EndTabItem();
                }

                if (ImGui::BeginTabItem("Scene"))
                {
                    draw_runtime_ui();
                    ImGui::EndTabItem();
                }

                ImGui::EndTabBar();
            }

            world_callbacks_.write_render_settings(render_state_);
        });
    }

    void level_builder_ui::draw_runtime_ui()
    {
        runtime_imgui_wants_mouse_ = ImGui::GetIO().WantCaptureMouse;
        draw_create_view();
        ImGui::Separator();
        draw_edit_save_view();
    }

    void level_builder_ui::draw_create_view()
    {
        ImGui::Text("Create Object");

        if (ImGui::Button("Refresh Asset List"))
            refresh_asset_lists();

        ImGui::Checkbox("Dynamic / Animated", &runtime_new_dynamic_);

        InputText(runtime_new_name_, "Name");
        InputText(runtime_new_model_path_, "Model Path");

        if (runtime_new_dynamic_)
        {
            const auto items = MakeCStrItems(runtime_dynamic_assets_);
            if (!items.empty())
                ImGui::Combo("Dynamic Assets", &runtime_dynamic_asset_index_, items.data(), (int)items.size());

            if (ImGui::Button("Use Selected Dynamic") &&
                runtime_dynamic_asset_index_ >= 0 &&
                runtime_dynamic_asset_index_ < (int)runtime_dynamic_assets_.size())
            {
                runtime_new_model_path_ = runtime_dynamic_assets_[runtime_dynamic_asset_index_];
            }
        }
        else
        {
            const auto items = MakeCStrItems(runtime_static_assets_);
            if (!items.empty())
                ImGui::Combo("Static Assets", &runtime_static_asset_index_, items.data(), (int)items.size());

            if (ImGui::Button("Use Selected Static") &&
                runtime_static_asset_index_ >= 0 &&
                runtime_static_asset_index_ < (int)runtime_static_assets_.size())
            {
                runtime_new_model_path_ = runtime_static_assets_[runtime_static_asset_index_];
            }
        }

        DragVec3("Spawn Position", runtime_new_pos_, 0.1f, 1.f);
        DragEulerDeg3("Spawn Rotation (deg)", runtime_new_rot_, 0.5f, 0.f);
        DragVec3("Spawn Scale", runtime_new_scale_, 0.01f, 0.f, "%.3f");

        if (runtime_new_dynamic_)
        {
            ImGui::Checkbox("Animation Enabled", &runtime_new_anim_enabled_);
            ImGui::Checkbox("Animation Paused", &runtime_new_anim_paused_);

            int anim_index_i = (int)runtime_new_anim_index_;
            if (ImGui::InputInt("Anim Clip Index", &anim_index_i))
            {
                if (anim_index_i < 0) anim_index_i = 0;
                runtime_new_anim_index_ = static_cast<std::size_t>(anim_index_i);
            }

            ImGui::InputFloat("Playback Speed", &runtime_new_playback_speed_);
            ImGui::InputFloat("Time Offset", &runtime_new_time_offset_);
            ImGui::InputFloat("Start Time", &runtime_new_anim_time_);
        }

        if (ImGui::Button("Spawn Object") && render_queue_)
        {
            render_queue::object_id new_id = 0;
            if (runtime_new_dynamic_)
            {
                render_queue::dynamic_mesh_desc desc{};
                desc.path = runtime_new_model_path_;
                desc.position = runtime_new_pos_;
                desc.rotation = runtime_new_rot_;
                desc.scale = runtime_new_scale_;
                desc.anim_enabled = runtime_new_anim_enabled_;
                desc.anim_index = runtime_new_anim_index_;
                desc.playback_speed = runtime_new_playback_speed_;
                desc.time_offset = runtime_new_time_offset_;
                desc.anim_time = runtime_new_anim_time_;
                desc.anim_paused = runtime_new_anim_paused_;
                desc.name = runtime_new_name_;
                new_id = render_queue_->add_dynamic_mesh(desc);
            }
            else
            {
                render_queue::static_mesh_desc desc{};
                desc.path = runtime_new_model_path_;
                desc.position = runtime_new_pos_;
                desc.rotation = runtime_new_rot_;
                desc.scale = runtime_new_scale_;
                desc.name = runtime_new_name_;
                new_id = render_queue_->add_static_mesh(desc);
            }

            if (new_id != 0)
                selected_runtime_object_id_ = new_id;
        }

    }

    void level_builder_ui::draw_edit_save_view()
    {
        runtime_objects_.clear();
        if (render_queue_)
            render_queue_->enumerate_objects(runtime_objects_);

        const auto selected_it = std::ranges::find_if(runtime_objects_, [&](const render_queue::object_info& v)
        {
            return v.id == selected_runtime_object_id_;
        });
        if (selected_it == runtime_objects_.end())
            selected_runtime_object_id_ = 0;

        ImGui::Text("Edit / Save");
        ImGui::Text("Scene Objects");
        ImGui::BeginChild("scene_object_list", ImVec2(0, 200), true);
        for (const auto& obj : runtime_objects_)
        {
            std::string label = obj.name.empty() ? ("Object " + std::to_string(obj.id)) : obj.name;
            label += obj.is_dynamic ? " (Dynamic)" : " (Static)";
            label += "##" + std::to_string(obj.id);

            if (ImGui::Selectable(label.c_str(), selected_runtime_object_id_ == obj.id))
                selected_runtime_object_id_ = obj.id;
        }
        ImGui::EndChild();

        if (selected_runtime_object_id_ != 0)
        {
            ImGui::SameLine();
            if (ImGui::Button("Delete Selected") && render_queue_)
            {
                render_queue_->remove(selected_runtime_object_id_);
                selected_runtime_object_id_ = 0;
            }
        }

        ImGui::Separator();
        ImGui::Text("Transform Selected");
        ImGui::Checkbox("Move with mouse", &runtime_move_tool_enabled_);
        ImGui::InputFloat("Translate Snap", &runtime_translate_snap_);
        ImGui::InputFloat("Rotate Snap (deg)", &runtime_rotate_snap_deg_);
        ImGui::InputFloat("Scale Snap", &runtime_scale_snap_);

        const auto selected_edit_it = std::ranges::find_if(runtime_objects_, [&](const render_queue::object_info& v)
        {
            return v.id == selected_runtime_object_id_;
        });

        if (selected_edit_it == runtime_objects_.end())
        {
            ImGui::TextDisabled("Select an object to edit transforms.");
        }
        else
        {
            editor_object_component* selected = nullptr;
            if (!render_queue_ || !render_queue_->try_get_editor_object(selected_runtime_object_id_, selected) || !selected)
            {
                ImGui::TextDisabled("Selected object is unavailable.");
                ImGui::Separator();
                if (ImGui::Button("Save Scene") && save_scene_callback_)
                    save_scene_callback_();
                return;
            }

            if (DragVec3("Position", selected->position, 0.1f, 1.f) && render_queue_)
            {
                SnapVec3(selected->position, runtime_translate_snap_);
                render_queue_->set_transform(selected->object_id, selected->position, selected->rotation, selected->scale);
            }

            if (DragEulerDeg3("Rotation (deg)", selected->rotation, 0.5f, 0.f) && render_queue_)
            {
                SnapEulerDeg3(selected->rotation, runtime_rotate_snap_deg_);
                render_queue_->set_transform(selected->object_id, selected->position, selected->rotation, selected->scale);
            }

            if (DragVec3("Scale", selected->scale, 0.01f, 0.f, "%.3f") && render_queue_)
            {
                SnapVec3(selected->scale, runtime_scale_snap_);
                render_queue_->set_transform(selected->object_id, selected->position, selected->rotation, selected->scale);
            }

            bool is_dynamic = false;
            dynamic_mesh_component* dyn = nullptr;
            if (render_queue_)
                is_dynamic = render_queue_->try_get_dynamic(selected->object_id, dyn);

            if (is_dynamic)
            {
                ImGui::Separator();
                ImGui::Text("Animation Controls");

                if (ImGui::Checkbox("Enabled", &selected->anim_enabled))
                    render_queue_->set_animation_enabled(selected->object_id, selected->anim_enabled);
                if (ImGui::Checkbox("Paused", &selected->anim_paused))
                    render_queue_->set_animation_paused(selected->object_id, selected->anim_paused);

                int clip_i = (int)selected->anim_index;
                if (ImGui::InputInt("Clip Index", &clip_i))
                {
                    if (clip_i < 0) clip_i = 0;
                    selected->anim_index = static_cast<std::size_t>(clip_i);
                    render_queue_->set_animation_index(selected->object_id, selected->anim_index);
                }

                if (ImGui::InputFloat("Playback Speed", &selected->playback_speed))
                    render_queue_->set_playback_speed(selected->object_id, selected->playback_speed);
                if (ImGui::InputFloat("Time Offset", &selected->time_offset))
                    render_queue_->set_time_offset(selected->object_id, selected->time_offset);
                if (ImGui::InputFloat("Current Time", &selected->anim_time))
                    render_queue_->set_anim_time(selected->object_id, selected->anim_time);

                if (ImGui::Button("Restart Animation"))
                {
                    selected->anim_time = 0.0f;
                    render_queue_->set_anim_time(selected->object_id, selected->anim_time);
                }
            }
        }

        ImGui::Separator();
        if (ImGui::Button("Save Scene") && save_scene_callback_)
            save_scene_callback_();
    }

    void level_builder_ui::draw_ui()
    {
        draw_runtime_ui();
    }

    void level_builder_ui::refresh_asset_lists()
    {
        runtime_static_assets_ = scan_assets("assets/static");
        runtime_dynamic_assets_ = scan_assets("assets/dynamic");
        if (!runtime_static_assets_.empty() && runtime_static_asset_index_ < 0)
            runtime_static_asset_index_ = 0;
        if (!runtime_dynamic_assets_.empty() && runtime_dynamic_asset_index_ < 0)
            runtime_dynamic_asset_index_ = 0;
    }

    std::size_t level_builder_ui::object_count() const
    {
        if (!render_queue_)
            return 0;

        std::vector<render_queue::object_info> objects;
        render_queue_->enumerate_objects(objects);
        return objects.size();
    }
} // namespace fox
