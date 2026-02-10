#ifndef FOXRASTERIZER_SCENE2_H
#define FOXRASTERIZER_SCENE2_H

#include "scenes/interface_scene.h"
#include "optimized/optimized_renderer.h"

#include <vector>

struct Scene2 : interface_scene
{
    MeshAssetPN sphere_asset{};
    bool built_assets = false;

    std::vector<fecs::entity> cubes;
    std::vector<Transform*>   cube_tr;

    fecs::entity sphere{};
    Transform*   sphere_tr = nullptr;

    std::vector<matrix> rot_step;
    std::vector<matrix> base_T;

    // sphere motion
    float sphereOffset = -6.f;
    float sphereStep   =  0.1f;

    std::uint32_t grid_h = 6;
    std::uint32_t grid_w = 8;

    colour cube_col   = colour(0.85f, 0.85f, 0.85f);
    colour sphere_col = colour(0.90f, 0.20f, 0.20f);
    scene_common_state common_data{};

    matrix sphere_base_T{};

    scene_common_state& common() noexcept override { return common_data; }
    const scene_common_state& common() const noexcept override { return common_data; }

    void build(optimized_renderer_core& r);
    bool tick_cycle_completed(fox::platform_window* windows) noexcept;

    [[nodiscard]] matrix camera_matrix() const noexcept;
    [[nodiscard]] bool cycle_complete_once() const noexcept;
};

void scene_2_realtime();
void scene_2_offload();

#endif // FOXRASTERIZER_SCENE2_H
