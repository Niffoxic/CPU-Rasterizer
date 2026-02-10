#include "game/static_mesh.h"

#include <assimp/material.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cstdio>
#include <limits>

matrix static_mesh::to_matrix(const aiMatrix4x4& m)
{
    matrix out;
    out(0, 0) = m.a1; out(0, 1) = m.a2; out(0, 2) = m.a3; out(0, 3) = m.a4;
    out(1, 0) = m.b1; out(1, 1) = m.b2; out(1, 2) = m.b3; out(1, 3) = m.b4;
    out(2, 0) = m.c1; out(2, 1) = m.c2; out(2, 2) = m.c3; out(2, 3) = m.c4;
    out(3, 0) = m.d1; out(3, 1) = m.d2; out(3, 2) = m.d3; out(3, 3) = m.d4;
    return out;
}

void static_mesh::update_bounds(vec4& min_v, vec4& max_v, const vec4& p)
{
    min_v[0] = (std::min)(min_v[0], p[0]);
    min_v[1] = (std::min)(min_v[1], p[1]);
    min_v[2] = (std::min)(min_v[2], p[2]);
    max_v[0] = (std::max)(max_v[0], p[0]);
    max_v[1] = (std::max)(max_v[1], p[1]);
    max_v[2] = (std::max)(max_v[2], p[2]);
}

void static_mesh::build_asset_from_buffers(mesh_data& data)
{
    const std::uint32_t tri_count = static_cast<std::uint32_t>(data.triangles.size());
    const bool has_uvs = !data.uvs.empty();
    data.asset.allocate(tri_count, has_uvs);

    for (std::uint32_t t = 0; t < tri_count; ++t)
    {
        const triIndices& tri = data.triangles[t];
        const std::size_t base = static_cast<std::size_t>(t) * 3u;

        data.asset.positions[base + 0] = data.positions[tri.v0];
        data.asset.positions[base + 1] = data.positions[tri.v1];
        data.asset.positions[base + 2] = data.positions[tri.v2];

        data.asset.normals[base + 0] = data.normals[tri.v0];
        data.asset.normals[base + 1] = data.normals[tri.v1];
        data.asset.normals[base + 2] = data.normals[tri.v2];

        if (has_uvs)
        {
            data.asset.uvs[(base + 0) * 2 + 0] = data.uvs[tri.v0 * 2 + 0];
            data.asset.uvs[(base + 0) * 2 + 1] = data.uvs[tri.v0 * 2 + 1];
            data.asset.uvs[(base + 1) * 2 + 0] = data.uvs[tri.v1 * 2 + 0];
            data.asset.uvs[(base + 1) * 2 + 1] = data.uvs[tri.v1 * 2 + 1];
            data.asset.uvs[(base + 2) * 2 + 0] = data.uvs[tri.v2 * 2 + 0];
            data.asset.uvs[(base + 2) * 2 + 1] = data.uvs[tri.v2 * 2 + 1];
        }
    }
}

void static_mesh::gather_instances(
    const aiNode* node,
    const aiMatrix4x4& parent,
    std::vector<mesh_instance>& instances,
    std::size_t& node_count)
{
    if (!node)
        return;

    ++node_count;
    const aiMatrix4x4 global = parent * node->mTransformation;

    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
    {
        const unsigned int mesh_index = node->mMeshes[i];
        mesh_instance inst{};
        inst.mesh_index = mesh_index;
        inst.node_world = to_matrix(global);
        instances.push_back(inst);
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        gather_instances(node->mChildren[i], global, instances, node_count);
}

bool static_mesh::load(const char* path, texture_cache* tex_cache)
{
    meshes_.clear();
    instances_.clear();
    node_count_ = 0;
    loaded_ = false;

    scene_ = importer_.ReadFile(
        path,
        aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_JoinIdenticalVertices
    );
    if (!scene_ || !scene_->HasMeshes())
    {
        std::printf("static_mesh: Assimp failed to load %s\n", path);
        return false;
    }

    meshes_.resize(scene_->mNumMeshes);
    for (unsigned int mi = 0; mi < scene_->mNumMeshes; ++mi)
    {
        const aiMesh* mesh_src = scene_->mMeshes[mi];
        if (!mesh_src)
            continue;

        mesh_data& data = meshes_[mi];
        const std::size_t vertex_count = mesh_src->mNumVertices;
        const std::size_t tri_count = mesh_src->mNumFaces;
        data.positions.reserve(vertex_count);
        data.normals.reserve(vertex_count);
        data.triangles.reserve(tri_count);

        const bool has_normals = mesh_src->HasNormals();
        const bool has_uvs = mesh_src->HasTextureCoords(0);

        if (has_uvs)
            data.uvs.reserve(vertex_count * 2u);

        for (std::size_t vi = 0; vi < vertex_count; ++vi)
        {
            const aiVector3D& pos = mesh_src->mVertices[vi];
            aiVector3D nrm(0.f, 0.f, 1.f);
            if (has_normals)
                nrm = mesh_src->mNormals[vi];
            data.positions.emplace_back(pos.x, pos.y, pos.z, 1.f);
            data.normals.emplace_back(nrm.x, nrm.y, nrm.z, 0.f);

            if (has_uvs)
            {
                const aiVector3D& uv = mesh_src->mTextureCoords[0][vi];
                data.uvs.push_back(uv.x);
                data.uvs.push_back(uv.y);
            }
        }

        for (std::size_t fi = 0; fi < mesh_src->mNumFaces; ++fi)
        {
            const aiFace& face = mesh_src->mFaces[fi];
            if (face.mNumIndices != 3)
                continue;
            data.triangles.emplace_back(
                static_cast<std::uint32_t>(face.mIndices[0]),
                static_cast<std::uint32_t>(face.mIndices[1]),
                static_cast<std::uint32_t>(face.mIndices[2])
            );
        }

        // Load texture from material
        if (tex_cache && mesh_src->mMaterialIndex < scene_->mNumMaterials)
        {
            const aiMaterial* mat = scene_->mMaterials[mesh_src->mMaterialIndex];
            aiString tex_path;
            if (mat->GetTexture(aiTextureType_DIFFUSE, 0, &tex_path) == AI_SUCCESS)
            {
                const aiTexture* embedded = scene_->GetEmbeddedTexture(tex_path.C_Str());
                const TextureRGBA8* loaded = nullptr;

                if (embedded)
                {
                    if (embedded->mHeight == 0)
                    {
                        // Compressed embedded texture
                        std::string key = std::string(path) + "::" + tex_path.C_Str();
                        loaded = tex_cache->load_memory(key, embedded->pcData, embedded->mWidth);
                    }
                }
                else
                {
                    // resolve relative to model directory
                    std::string dir = path;
                    const auto slash = dir.find_last_of("/\\");
                    if (slash != std::string::npos)
                        dir = dir.substr(0, slash + 1);
                    else
                        dir.clear();
                    loaded = tex_cache->load_file(dir + tex_path.C_Str());
                }

                if (loaded && loaded->valid())
                {
                    data.tex_ref.pixels = loaded->pixels;
                    data.tex_ref.tex_w = loaded->width;
                    data.tex_ref.tex_h = loaded->height;
                }
            }

            // checkerboard for meshes with UVs but no loaded texture
            if (!data.tex_ref.valid() && has_uvs)
            {
                const TextureRGBA8* checker = tex_cache->checkerboard();
                data.tex_ref.pixels = checker->pixels;
                data.tex_ref.tex_w = checker->width;
                data.tex_ref.tex_h = checker->height;
            }
        }

        build_asset_from_buffers(data);
    }

    if (scene_->mRootNode)
        gather_instances(scene_->mRootNode, aiMatrix4x4(), instances_, node_count_);

    bounds_min_ = vec4(
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        (std::numeric_limits<float>::max)(),
        1.f
    );
    bounds_max_ = vec4(
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(),
        (std::numeric_limits<float>::lowest)(),
        1.f
    );

    for (const auto& inst : instances_)
    {
        if (inst.mesh_index >= meshes_.size())
            continue;
        const auto& data = meshes_[inst.mesh_index];
        for (const auto& p : data.positions)
        {
            const vec4 wp = inst.node_world * p;
            update_bounds(bounds_min_, bounds_max_, wp);
        }
    }

    loaded_ = !instances_.empty();
    return loaded_;
}

void static_mesh::build_instances(fecs::world& w, const matrix& base_world, const colour& col, float ka, float kd)
{
    std::vector<fecs::entity> ignored{};
    ignored.reserve(instances_.size());
    build_instances(w, base_world, col, ka, kd, ignored);
}

void static_mesh::build_instances(
    fecs::world& w,
    const matrix& base_world,
    const colour& col,
    float ka,
    float kd,
    std::vector<fecs::entity>& out_entities)
{
    std::vector<matrix> ignored_locals{};
    ignored_locals.reserve(instances_.size());
    build_instances(w, base_world, col, ka, kd, out_entities, ignored_locals);
}

void static_mesh::build_instances(
    fecs::world& w,
    const matrix& base_world,
    const colour& col,
    float ka,
    float kd,
    std::vector<fecs::entity>& out_entities,
    std::vector<matrix>& out_locals)
{
    if (!loaded_)
        return;

    {
        std::vector<fecs::component_id> ids{
            w.registry().get_id<MeshRefPN>(),
            w.registry().get_id<Transform>(),
            w.registry().get_id<Material>(),
            w.registry().get_id<TextureRef>()
        };
        const fecs::table_id tid = w.storage().get_or_create_table(fecs::make_archetype_key(std::move(ids)));
        fecs::table& table = w.storage().get_table(tid);
        table.reserve(table.size() + instances_.size());
    }

    out_entities.reserve(out_entities.size() + instances_.size());
    out_locals.reserve(out_locals.size() + instances_.size());
    for (const auto& inst : instances_)
    {
        if (inst.mesh_index >= meshes_.size())
            continue;
        const matrix world = base_world * inst.node_world;
        const fecs::entity e = spawn_instance(w, meshes_[inst.mesh_index].asset, world, col, ka, kd,
                                               meshes_[inst.mesh_index].tex_ref);
        out_entities.push_back(e);
        out_locals.push_back(inst.node_world);
    }
}

bool static_mesh::sample_node_world(matrix& out) const noexcept
{
    if (instances_.empty())
        return false;
    out = instances_[0].node_world;
    return true;
}
