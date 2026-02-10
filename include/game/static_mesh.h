#ifndef FOXRASTERIZER_STATIC_MESH_H
#define FOXRASTERIZER_STATIC_MESH_H

#include "optimized/optimized_renderer.h"
#include "texture_cache.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>

#include <cstddef>
#include <vector>

class static_mesh
{
public:
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
        std::vector<matrix>& out_locals);

    [[nodiscard]] bool loaded() const noexcept { return loaded_; }
    [[nodiscard]] const vec4& bounds_min() const noexcept { return bounds_min_; }
    [[nodiscard]] const vec4& bounds_max() const noexcept { return bounds_max_; }
    [[nodiscard]] std::size_t mesh_count() const noexcept { return meshes_.size(); }
    [[nodiscard]] std::size_t instance_count() const noexcept { return instances_.size(); }
    [[nodiscard]] std::size_t node_count() const noexcept { return node_count_; }
    [[nodiscard]] bool sample_node_world(matrix& out) const noexcept;

private:
    struct mesh_data
    {
        MeshAssetPN asset{};
        std::vector<vec4> positions{};
        std::vector<vec4> normals{};
        std::vector<float> uvs{};           // 2 floats per vertex (u,v), empty if no UVs
        std::vector<triIndices> triangles{};
        TextureRef tex_ref{};               // texture for this sub-mesh
    };

    struct mesh_instance
    {
        std::size_t mesh_index = 0;
        matrix node_world{};
    };

    Assimp::Importer importer_{};
    const aiScene* scene_ = nullptr;
    std::vector<mesh_data> meshes_{};
    std::vector<mesh_instance> instances_{};
    vec4 bounds_min_{};
    vec4 bounds_max_{};
    bool loaded_ = false;
    std::size_t node_count_ = 0;

    static matrix to_matrix(const aiMatrix4x4& m);
    static void update_bounds(vec4& min_v, vec4& max_v, const vec4& p);
    static void build_asset_from_buffers(mesh_data& data);
    static void gather_instances(
        const aiNode* node,
        const aiMatrix4x4& parent,
        std::vector<mesh_instance>& instances,
        std::size_t& node_count);
};

#endif // FOXRASTERIZER_STATIC_MESH_H
