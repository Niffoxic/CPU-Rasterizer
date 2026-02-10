#ifndef FOXRASTERIZER_DYNAMIC_MESH_H
#define FOXRASTERIZER_DYNAMIC_MESH_H

#include "optimized/optimized_renderer.h"
#include "texture_cache.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

struct dynamic_mesh_instance;

class dynamic_mesh
{
public:
    struct vertex_weights
    {
        std::uint32_t bone[4]{};
        float         weight[4]{};
        std::uint32_t count = 0;
    };

    bool load(const char* path, texture_cache* tex_cache = nullptr);
    void build_instances(fecs::world& w, const matrix& base_world, const colour& col, float ka, float kd);
    void build_instances(
        fecs::world& w,
        const matrix& base_world,
        const colour& col,
        float ka,
        float kd,
        std::vector<fecs::entity>& out_entities);
    void build_instances(
        fecs::world& w,
        const matrix& base_world,
        const colour& col,
        float ka,
        float kd,
        std::vector<fecs::entity>& out_entities,
        std::vector<matrix>& out_locals) const;

    // create a per entity animation instance
    // so that one mesh can be used to with multiple animations
    dynamic_mesh_instance create_instance() const;

    void tick_skinning(dynamic_mesh_instance& instance, double elapsed_seconds) const;

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] bool has_animation() const noexcept { return has_animation_; }

    [[nodiscard]] const vec4& bounds_min() const noexcept { return bounds_min_; }
    [[nodiscard]] const vec4& bounds_max() const noexcept { return bounds_max_; }

    [[nodiscard]] std::size_t mesh_count() const noexcept { return meshes_.size(); }
    [[nodiscard]] std::size_t instance_count() const noexcept { return instances_.size(); }
    [[nodiscard]] std::size_t node_count() const noexcept { return node_count_; }

    [[nodiscard]] bool sample_node_world(matrix& out) const noexcept;

    [[nodiscard]] std::size_t animation_count() const noexcept { return scene_ ? (std::size_t)scene_->mNumAnimations : 0; }

private:
    struct mesh_data
    {
        MeshAssetPN asset{};

        std::vector<aiVector3D> base_positions{};
        std::vector<aiVector3D> base_normals{};
        std::vector<float> base_uvs{};       // 2 floats per vertex (u,v) empty if no UVs

        std::vector<triIndices> triangles{};
        std::vector<vertex_weights> weights{};
        TextureRef tex_ref{};                // texture for this sub-mesh
    };

    struct mesh_instance
    {
        std::size_t mesh_index = 0;
        matrix      node_world{};
    };

    Assimp::Importer importer_{};
    const aiScene*   scene_ = nullptr;

    std::vector<mesh_data>     meshes_{};
    std::vector<mesh_instance> instances_{};

    vec4 bounds_min_{};
    vec4 bounds_max_{};

    bool loaded_ = false;
    bool has_animation_ = false;
    std::size_t node_count_ = 0;

    aiMatrix4x4 global_inverse_{};

    std::vector<aiMatrix4x4> mesh_node_inverse_{};
    std::vector<bool>        mesh_node_inverse_set_{};

    std::unordered_map<std::string, std::uint32_t> bone_map_{};
    std::vector<aiMatrix4x4> bone_offsets_{};

    static matrix to_matrix(const aiMatrix4x4& m);
    static void update_bounds(vec4& min_v, vec4& max_v, const vec4& p);

    static void gather_instances(
        const aiNode* node,
        const aiMatrix4x4& parent,
        std::vector<mesh_instance>& instances,
        std::size_t& node_count,
        std::vector<aiMatrix4x4>& mesh_node_inverse,
        std::vector<bool>& mesh_node_inverse_set);

    static void add_weight(vertex_weights& vw, std::uint32_t bone, float weight);

    friend struct dynamic_mesh_instance;
};

// Per entity animation instance state
struct dynamic_mesh_instance
{
    // Current animation clip index
    std::size_t anim_index = 0;

    // Per entity bone transforms
    std::vector<aiMatrix4x4> bone_transforms{};

    // Per entity skinned mesh data
    struct mesh_instance_data
    {
        std::vector<aiVector3D> skinned_positions{};
        std::vector<aiVector3D> skinned_normals{};
        MeshAssetPN asset{};  // Per entity render asset
    };
    std::vector<mesh_instance_data> mesh_data{};

    const dynamic_mesh* parent_mesh = nullptr;
};

#endif // FOXRASTERIZER_DYNAMIC_MESH_H
