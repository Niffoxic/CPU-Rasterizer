#include "scenes/scene2.h"
#include "scenes/scene_manager.h"
#include "fgen.h"

void Scene2::build(optimized_renderer_core& r)
{
    cubes.clear();
    cube_tr.clear();
    rot_step.clear();
    base_T.clear();

    sphere = {};
    sphere_tr = nullptr;

    sphereOffset = -6.f;
    sphereStep   =  0.1f;

    common_data.cycle_edges = 0;
    common_data.cycle_start = scene_common_state::hclock::now();
    common_data.last_cycle_ms = 0.0;

    if (!built_assets)
    {
        sphere_asset = build_asset_from_indexed_mesh(Mesh::makeSphere(1.0f, 10, 20));
        built_assets = true;
    }

    const std::size_t count = (std::size_t)grid_h * (std::size_t)grid_w;

    cubes.reserve(count);
    cube_tr.reserve(count);
    rot_step.reserve(count);
    base_T.reserve(count);

    r.world.reserve<MeshRefPN, Transform, Material>(count + 1u);

    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();

    // spawn grid cubes
    for (std::uint32_t gy = 0; gy < grid_h; ++gy)
    {
        const float py = 5.0f - (float)gy * 2.f;

        for (std::uint32_t gx = 0; gx < grid_w; ++gx)
        {
            const float px = -7.0f + (float)gx * 2.f;
            constexpr float pz = -8.0f;

            const matrix T = matrix::makeTranslation(px, py, pz);
            const fecs::entity e = spawn_instance(
                r.world,
                r.cube_asset,
                T,
                cube_col,
                common_data.ka,
                common_data.kd
            );

            cubes.push_back(e);
            base_T.push_back(T);

            // cache per cube rotation step matrix ONCE
            const float rx = rng.getRandomFloat(-0.1f, 0.1f);
            const float ry = rng.getRandomFloat(-0.1f, 0.1f);
            const float rz = rng.getRandomFloat(-0.1f, 0.1f);
            rot_step.push_back(matrix::makeRotateXYZ(rx, ry, rz));
        }
    }

    // sphere base transform
    sphere_base_T = matrix::makeTranslation(sphereOffset, 0.f, -6.f);

    sphere = spawn_instance(
        r.world,
        sphere_asset,
        sphere_base_T,
        sphere_col,
        common_data.ka,
        common_data.kd
    );

    // cache Transform* pointers
    cube_tr.resize(cubes.size());
    for (std::size_t i = 0; i < cubes.size(); ++i)
        cube_tr[i] = get_transform_ptr(r.world, cubes[i]);

    sphere_tr = get_transform_ptr(r.world, sphere);

    // cache camera
    common_data.cam_cached = matrix::makeIdentity();
}

bool Scene2::tick_cycle_completed(fox::platform_window* windows) noexcept
{
    FOX_ZONE_N("tick_cycle_completed");

    const std::size_t n = cube_tr.size();

    // Apply incremental rotation using cached step matrices.
    for (std::size_t i = 0; i < n; ++i)
        cube_tr[i]->world = cube_tr[i]->world * rot_step[i];

    // Move sphere
    simd_add_scalar(sphereOffset, sphereStep);

    // Update cached base translation X component fast.
    sphere_base_T.setTranslationX_fast(sphereOffset);
    sphere_tr->world = sphere_base_T;

    if (sphereOffset > 6.0f || sphereOffset < -6.0f)
    {
        sphereStep *= -1.f;

        ++common_data.cycle_edges;
        if ((common_data.cycle_edges & 1) == 0)
        {
            const auto now = scene_common_state::hclock::now();
            common_data.last_cycle_ms =
                std::chrono::duration<double, std::milli>(now - common_data.cycle_start).count();
            common_data.cycle_start = now;
            return true;
        }
    }

    return false;
}

matrix Scene2::camera_matrix() const noexcept
{
    return common_data.cam_cached;
}

bool Scene2::cycle_complete_once() const noexcept
{
    return common_data.cycle_edges >= 2;
}

void scene_2_realtime()
{
    run_realtime_scene<Scene2>("SCENE_2", "Scene2 Realtime");
}

void scene_2_offload()
{
    run_offload_scene<Scene2>(
        "SCENE_2",
        "Scene2 Offload",
        1,      // record every frame
        2000    // keep exactly 2000 frames
    );
}
