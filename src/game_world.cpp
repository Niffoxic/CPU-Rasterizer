#include "game/game_world.h"

#include <cmath>
#include <cstdio>
#include <windows.h>

namespace
{
    inline static Light make_default_light() noexcept
    {
        return {
            vec4(0.f, 1.f, 1.f, 0.f),
            colour(1.f, 1.f, 1.f),
            colour(0.2f, 0.2f, 0.2f)
        };
    }

    static vec4 normalised_or_default(const vec4& v, const vec4& fallback) noexcept
    {
        vec4 out = v;
        const float len = out.length();
        if (len <= 0.0001f)
            return fallback;
        out *= (1.0f / len);
        return out;
    }

}

namespace fox
{
    game_world::game_world(const game_world_config& config)
        : config_(config)
        , renderer_(config.w, config.h, config.title)
    {}

    void game_world::init()
    {
        if (initialized_)
            return;

        renderer_.set_offline_rendering(config_.offline);

        default_light_ = make_default_light();
        light_dir_.normalise();

        fecs::world& world = renderer_.world;
        render_queue::register_components(world);

        render_queue_ = std::make_unique<render_queue>(world, static_mesh_cache_, dynamic_mesh_cache_, &tex_cache_, normalize_size_);
        scene_io_ = std::make_unique<scene_io>(world, *render_queue_);
        scene_io_->set_post_processing_callbacks(
            [this](scene_io::scene_post_processing_settings& settings)
            {
                settings = post_processing_settings();
            },
            [this](const scene_io::scene_post_processing_settings& settings)
            {
                apply_post_processing_settings(settings);
            });

        const scene_load_desc startup_scene_desc{ scene_paths::default_scene_path(), true };
        if (!scene_io_->load_scene(startup_scene_desc))
        {
            std::printf("%s not found or invalid; starting with empty scene.\n", scene_paths::default_scene_path());
        }

        level_editor_.init(&world, &static_mesh_cache_, &tex_cache_, normalize_size_);
        level_editor_.set_render_queue(render_queue_.get());
        level_editor_.set_save_scene_callback([this]()
        {
            if (!scene_io_)
                return;
            scene_save_desc desc{};
            desc.path = scene_paths::default_scene_path();
            scene_io_->save_scene(desc);
        });

        renderer_.pin_draw_query_once();

        camera_.init(
            vec4(0.f, 8.f, 18.f, 0.f),
            18.f,
            6.f,
            80.f
        );
        camera_.set_position(camera_pos_);

        renderer_.perspective = matrix::makePerspective(
            90.f * fox_math::pi_f / 180.f,
            static_cast<float>(config_.w) / static_cast<float>(config_.h),
            0.1f,
            5000.f
        );

        level_editor_.init_world_editor({
            [this](world_debug_state& state)
            {
                state.fps = fps_;
                state.delta_time_s = delta_time_s_;
                state.free_camera_enabled = free_camera_enabled_;
                state.fps_mouse_enabled = fps_mouse_enabled_;
                state.camera_mouse_sensitivity = camera_mouse_sensitivity_;
                state.light_cycle_enabled = light_cycle_enabled_;
                state.light_cycle_speed = light_cycle_speed_;
                state.light_color_override = light_color_override_;
                state.light = default_light_;
                state.camera_pos = camera_pos_;
                state.cached_texture_count = tex_cache_.size();
            },
            [this](const world_debug_state& state)
            {
                free_camera_enabled_ = state.free_camera_enabled;
                fps_mouse_enabled_ = state.fps_mouse_enabled;
                camera_mouse_sensitivity_ = state.camera_mouse_sensitivity;
                light_cycle_enabled_ = state.light_cycle_enabled;
                light_cycle_speed_ = state.light_cycle_speed;
                light_color_override_ = state.light_color_override;
                default_light_ = state.light;
                camera_pos_ = state.camera_pos;
                camera_.set_position(camera_pos_);
            },
            [this]()
            {
                camera_target_transform_ = nullptr;
                renderer_.world.query<Transform, camera_target>().each_entity([&](fecs::entity, Transform& tr, camera_target&)
                {
                    if (!camera_target_transform_)
                        camera_target_transform_ = &tr;
                });

                if (!camera_target_transform_)
                    return;

                camera_pos_.x = camera_target_transform_->world(0, 3);
                camera_pos_.y = camera_target_transform_->world(1, 3) + 8.f;
                camera_pos_.z = camera_target_transform_->world(2, 3) + 18.f;
                camera_.set_position(camera_pos_);
            },
            [this](world_render_settings& state)
            {
                state.textures_enabled = renderer_.textures_enabled;
                state.flip_v = renderer_.flip_v;
                const scene_io::scene_post_processing_settings post = post_processing_settings();
                state.post_process = post.post_process;
                state.rainy_effect = post.rainy_effect;
                state.advanced_effects = post.advanced_effects;
            },
            [this](const world_render_settings& state)
            {
                renderer_.textures_enabled = state.textures_enabled;
                renderer_.flip_v = state.flip_v;

                scene_io::scene_post_processing_settings post{};
                post.post_process = state.post_process;
                post.rainy_effect = state.rainy_effect;
                post.advanced_effects = state.advanced_effects;
                apply_post_processing_settings(post);
            }
        });
        level_editor_.setup_imgui_views();

        std::printf("Controls: WASD/QE move, Hold SHIFT faster, SPACE toggles FPS mouse look when enabled.\n");

        start_time_ = std::chrono::steady_clock::now();
        last_time_ = start_time_;
        initialized_ = true;
    }

    void game_world::run()
    {
        if (!initialized_)
            init();

        char  title_buf[256]{};
        float last_fps_shown = -1.f;

        while (true)
        {
            renderer_.windows.poll_messages();

            if (renderer_.windows.key_down(VK_ESCAPE))
                break;

            if (renderer_.windows.close_requested())
                break;

            const auto now = std::chrono::steady_clock::now();
            update_timing(now);

            if (fps_ != last_fps_shown)
            {
                std::snprintf(
                    title_buf,
                    sizeof(title_buf),
                    "%s | FPS: %.1f",
                    config_.title ? config_.title : "FoxGame",
                    fps_
                );

                renderer_.windows.set_window_title(title_buf);
                last_fps_shown = fps_;
            }

            if (render_queue_)
                render_queue_->tick_dynamic_animations(delta_time_s_);

            matrix focus_world = matrix::makeIdentity();
            camera_target_transform_ = nullptr;
            renderer_.world.query<Transform, camera_target>().each_entity([&](fecs::entity, Transform& tr, camera_target&)
            {
                if (!camera_target_transform_)
                    camera_target_transform_ = &tr;
            });
            if (camera_target_transform_)
                focus_world = camera_target_transform_->world;

            update_free_camera(delta_time_s_);
            if (!free_camera_enabled_)
            {
                camera_.update(delta_time_s_, &renderer_.windows, focus_world);
            }

            const int mouse_x = renderer_.windows.mouse_x();
            const int mouse_y = renderer_.windows.mouse_y();
            int mouse_dx = 0;
            int mouse_dy = 0;
            if (has_prev_mouse_)
            {
                mouse_dx = mouse_x - prev_mouse_x_;
                mouse_dy = mouse_y - prev_mouse_y_;
            }
            prev_mouse_x_ = mouse_x;
            prev_mouse_y_ = mouse_y;
            has_prev_mouse_ = true;

            const auto left_state = renderer_.windows.mouse_state(mouse_button::left);
            const bool left_down = renderer_.windows.mouse_pressed(mouse_button::left);
            const bool left_pressed = (left_state == mouse_button_state::down);
            const bool left_released = !left_down && prev_left_mouse_down_;
            prev_left_mouse_down_ = left_down;

            drag_move_tool_.set_enabled(level_editor_.move_tool_enabled() && is_mouse_safe_for_editing());
            drag_move_tool_.set_selected_object(level_editor_.selected_runtime_object_id());
            drag_move_tool_.on_mouse_button(left_down, left_pressed, left_released);
            drag_move_tool_.on_mouse_move(mouse_x, mouse_y, mouse_dx, mouse_dy);

            editor::editor_frame_context editor_ctx{};
            editor_ctx.viewport_w = static_cast<int>(renderer_.windows.width());
            editor_ctx.viewport_h = static_cast<int>(renderer_.windows.height());
            editor_ctx.mouse_x = mouse_x;
            editor_ctx.mouse_y = mouse_y;
            editor_ctx.view = camera_.view_matrix();
            editor_ctx.proj = renderer_.perspective;
            editor_ctx.camera_pos = camera_.position();
            editor_ctx.imgui_wants_mouse = level_editor_.imgui_wants_mouse();
            editor_ctx.window = &renderer_.windows;
            editor_ctx.rq = render_queue_.get();
            drag_move_tool_.tick(editor_ctx);

            if (!renderer_.begin_cpu_frame(config_.clear_rgba))
                continue;

            update_light_cycle(delta_time_s_);
            renderer_.draw_world(camera_.view_matrix(), default_light_, light_dir_);
            renderer_.apply_post_process(renderer_.post_process);
            renderer_.apply_rainy_effect(renderer_.rainy_effect, elapsed_time_s_);
            renderer_.apply_advanced_effects(renderer_.advanced_effects, elapsed_time_s_);
            renderer_.present();
        }

        renderer_.canvas.flush();
    }

    bool game_world::is_mouse_safe_for_editing() const noexcept
    {
        return !fps_mouse_enabled_;
    }

    void game_world::update_light_cycle(float dt) noexcept
    {
        if (!light_cycle_enabled_ || light_color_override_)
            return;

        static float light_phase = 0.f;
        light_phase += dt * light_cycle_speed_;

        const float x = std::cos(light_phase);
        const float z = std::sin(light_phase);
        const float y = 0.35f;

        light_dir_ = normalised_or_default(
            vec4(x, y, z, 0.f),
            vec4(0.f, 1.f, 0.f, 0.f));
    }

    void game_world::update_free_camera(float dt) noexcept
    {
        fox::platform_window& wnd = renderer_.windows;

        const bool space_down = wnd.key_down(VK_SPACE);
        if (space_down && !space_was_down_)
        {
            fps_mouse_enabled_ = !fps_mouse_enabled_;
            wnd.hide_cursor(fps_mouse_enabled_);
            wnd.clip_cursor_to_client(fps_mouse_enabled_);
        }
        space_was_down_ = space_down;

        const bool toggle_free = wnd.key_down('F');
        if (toggle_free && !free_cam_toggle_was_down_)
            free_camera_enabled_ = !free_camera_enabled_;
        free_cam_toggle_was_down_ = toggle_free;

        if (!free_camera_enabled_)
            return;

        float mouse_dx = 0.f;
        float mouse_dy = 0.f;

        if (fps_mouse_enabled_)
        {
            POINT cursor{};
            if (GetCursorPos(&cursor))
            {
                HWND hwnd = static_cast<HWND>(wnd.native_hwnd());
                if (hwnd)
                {
                    ScreenToClient(hwnd, &cursor);
                    RECT rect{};
                    GetClientRect(hwnd, &rect);
                    const int cx = (rect.right - rect.left) / 2;
                    const int cy = (rect.bottom - rect.top) / 2;
                    mouse_dx = static_cast<float>(cursor.x - cx) * camera_mouse_sensitivity_;
                    mouse_dy = static_cast<float>(cursor.y - cy) * camera_mouse_sensitivity_;

                    POINT center{ cx, cy };
                    ClientToScreen(hwnd, &center);
                    SetCursorPos(center.x, center.y);
                }
            }
        }

        camera_.set_position(camera_pos_);
        camera_.update_free_fly(dt, &wnd, mouse_dx, mouse_dy, fps_mouse_enabled_);
        camera_pos_ = camera_.position();
    }

    scene_io::scene_post_processing_settings game_world::post_processing_settings() const noexcept
    {
        scene_io::scene_post_processing_settings settings{};
        settings.post_process = renderer_.post_process;
        settings.rainy_effect = renderer_.rainy_effect;
        settings.advanced_effects = renderer_.advanced_effects;
        return settings;
    }

    void game_world::apply_post_processing_settings(const scene_io::scene_post_processing_settings& settings) noexcept
    {
        renderer_.post_process = settings.post_process;
        renderer_.rainy_effect = settings.rainy_effect;
        renderer_.advanced_effects = settings.advanced_effects;
    }

    void game_world::update_timing(std::chrono::steady_clock::time_point now) noexcept
    {
        delta_time_s_ = std::chrono::duration<float>(now - last_time_).count();
        elapsed_time_s_ = std::chrono::duration<float>(now - start_time_).count();
        last_time_ = now;

        fps_accum_s_ += static_cast<double>(delta_time_s_);
        ++fps_frames_;
        if (fps_accum_s_ >= 1.0)
        {
            fps_ = static_cast<float>(fps_frames_ / fps_accum_s_);
            fps_accum_s_ = 0.0;
            fps_frames_ = 0u;
        }
    }
}
