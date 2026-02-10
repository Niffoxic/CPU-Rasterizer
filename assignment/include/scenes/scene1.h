#ifndef FOXRASTERIZER_SCENE1_H
#define FOXRASTERIZER_SCENE1_H

#include "scenes/interface_scene.h"
#include "optimized/optimized_renderer.h"

#include <vector>

struct Scene1 : interface_scene
{
    struct CubeInit { matrix base_T; matrix base_R; };
    scene_common_state common_data{};

    std::vector<fecs::entity> cubes;
    std::vector<Transform*>   cube_tr;
    std::vector<CubeInit>     init;

    float zoffset = 8.0f;
    float step    = 0.1f;

    colour cube_col = colour(0.20f, 0.85f, 0.25f);

    matrix rot_step0{};
    matrix rot_step1{};

    static matrix makeRandomRotation() noexcept;

    scene_common_state& common() noexcept override { return common_data; }
    const scene_common_state& common() const noexcept override { return common_data; }

    void build(optimized_renderer_core& r);
    bool tick_cycle_completed(fox::platform_window* windows) noexcept;

    [[nodiscard]] matrix camera_matrix() const noexcept;
    [[nodiscard]] bool cycle_complete_once() const noexcept;
};

void scene_1_realtime();
void scene_1_offload();

#endif // FOXRASTERIZER_SCENE1_H
