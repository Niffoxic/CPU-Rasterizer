#include "scenes/scene1.h"
#include "scenes/scene_manager.h"
#include "fgen.h"

matrix Scene1::makeRandomRotation() noexcept
{
    RandomNumberGenerator& rng = RandomNumberGenerator::getInstance();
    const unsigned int r = (unsigned int)rng.getRandomInt(0, 3);
    if (r == 0) return matrix::makeRotateX(rng.getRandomFloat(0.f, 2.0f * (float)M_PI));
    if (r == 1) return matrix::makeRotateY(rng.getRandomFloat(0.f, 2.0f * (float)M_PI));
    if (r == 2) return matrix::makeRotateZ(rng.getRandomFloat(0.f, 2.0f * (float)M_PI));
    return matrix::makeIdentity();
}

void Scene1::build(optimized_renderer_core& r)
{
    cubes.clear();
    cube_tr.clear();
    init.clear();

    constexpr unsigned int columns = 20;
    const std::size_t cube_count = columns * 2u;
    cubes.reserve(cube_count);
    init.reserve(cube_count);
    r.world.reserve<MeshRefPN, Transform, Material>(cube_count);

    for (unsigned int i = 0; i < columns; ++i)
    {
        const float z = (-3.0f * (float)i);

        {
            CubeInit ci{};
            ci.base_T = matrix::makeTranslation(-2.0f, 0.0f, z);
            ci.base_R = makeRandomRotation();
            const fecs::entity e = spawn_instance(
                r.world,
                r.cube_asset,
                ci.base_T * ci.base_R,
                cube_col,
                common_data.ka,
                common_data.kd
            );
            cubes.push_back(e);
            init.push_back(ci);
        }

        {
            CubeInit ci{};
            ci.base_T = matrix::makeTranslation(2.0f, 0.0f, z);
            ci.base_R = makeRandomRotation();
            const fecs::entity e = spawn_instance(
                r.world,
                r.cube_asset,
                ci.base_T * ci.base_R,
                cube_col,
                common_data.ka,
                common_data.kd
            );
            cubes.push_back(e);
            init.push_back(ci);
        }
    }

    cube_tr.resize(cubes.size());
    for (std::size_t i = 0; i < cubes.size(); ++i)
        cube_tr[i] = get_transform_ptr(r.world, cubes[i]);

    zoffset = 8.0f;
    step    = -0.1f;
    common_data.cycle_edges = 0;
    common_data.last_cycle_ms = 0.0;
    common_data.cycle_start = scene_common_state::hclock::now();

    rot_step0 = matrix::makeRotateXYZ(0.1f, 0.1f, 0.0f);
    rot_step1 = matrix::makeRotateXYZ(0.0f, 0.1f, 0.2f);

    common_data.cam_cached = matrix::makeCameraDollyZ(zoffset);
}

bool Scene1::tick_cycle_completed(fox::platform_window* windows) noexcept
{
    FOX_ZONE_N("tick_cycle_completed");

    if (cube_tr.size() >= 2)
    {
        init[0].base_R = init[0].base_R * rot_step0;
        cube_tr[0]->world = init[0].base_T * init[0].base_R;

        init[1].base_R = init[1].base_R * rot_step1;
        cube_tr[1]->world = init[1].base_T * init[1].base_R;
    }

    simd_add_scalar(zoffset, step);

    if (zoffset < -60.f || zoffset > 8.f)
    {
        step *= -1.f;
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

matrix Scene1::camera_matrix() const noexcept
{
    common_data.cam_cached.setTranslationZ_fast(-zoffset);
    return common_data.cam_cached;
}

bool Scene1::cycle_complete_once() const noexcept
{
    return common_data.cycle_edges >= 2;
}

void scene_1_realtime()
{
    run_realtime_scene<Scene1>("SCENE_1", "Scene1 Realtime");
}

void scene_1_offload()
{
    run_offload_scene<Scene1>(
        "SCENE_1",
        "Scene1 Offload",
        1,      // record every frame
        2000    // exactly 2000 frames
    );
}
