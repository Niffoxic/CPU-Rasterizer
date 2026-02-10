#include "game/dynamic_mesh.h"

#include <assimp/anim.h>
#include <assimp/material.h>
#include <assimp/matrix3x3.h>
#include <assimp/postprocess.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace
{
    aiNodeAnim* find_node_anim(const aiAnimation* animation, const aiString& node_name)
    {
        if (!animation) return nullptr;

        for (unsigned int i = 0; i < animation->mNumChannels; ++i)
        {
            aiNodeAnim* channel = animation->mChannels[i];
            if (channel && channel->mNodeName == node_name)
                return channel;
        }
        return nullptr;
    }

    aiVector3D interpolate_vec3(double anim_time, const aiVectorKey* keys, unsigned int key_count)
    {
        if (!keys || key_count == 0) return aiVector3D(0.f, 0.f, 0.f);
        if (key_count == 1) return keys[0].mValue;

        unsigned int index = 0;
        while (index + 1 < key_count && anim_time >= keys[index + 1].mTime)
            ++index;

        const unsigned int next = (index + 1 < key_count) ? (index + 1) : index;
        if (next == index) return keys[index].mValue;

        const double delta  = keys[next].mTime - keys[index].mTime;
        const double factor = (delta > 0.0) ? (anim_time - keys[index].mTime) / delta : 0.0;

        const aiVector3D& start = keys[index].mValue;
        const aiVector3D& end   = keys[next].mValue;
        return start + static_cast<float>(factor) * (end - start);
    }

    aiQuaternion interpolate_quat(double anim_time, const aiQuatKey* keys, unsigned int key_count)
    {
        if (!keys || key_count == 0) return aiQuaternion(1.f, 0.f, 0.f, 0.f);
        if (key_count == 1) return keys[0].mValue;

        unsigned int index = 0;
        while (index + 1 < key_count && anim_time >= keys[index + 1].mTime)
            ++index;

        const unsigned int next = (index + 1 < key_count) ? (index + 1) : index;
        if (next == index) return keys[index].mValue;

        const double delta  = keys[next].mTime - keys[index].mTime;
        const double factor = (delta > 0.0) ? (anim_time - keys[index].mTime) / delta : 0.0;

        aiQuaternion out;
        aiQuaternion::Interpolate(out, keys[index].mValue, keys[next].mValue, static_cast<float>(factor));
        return out.Normalize();
    }

    aiMatrix4x4 make_node_transform(const aiNodeAnim* channel, double anim_time)
    {
        if (!channel) return aiMatrix4x4();

        const aiVector3D scale = interpolate_vec3(anim_time, channel->mScalingKeys, channel->mNumScalingKeys);
        const aiQuaternion rot = interpolate_quat(anim_time, channel->mRotationKeys, channel->mNumRotationKeys);
        const aiVector3D pos   = interpolate_vec3(anim_time, channel->mPositionKeys, channel->mNumPositionKeys);

        aiMatrix4x4 S; aiMatrix4x4::Scaling(scale, S);
        aiMatrix4x4 R = aiMatrix4x4(rot.GetMatrix());
        aiMatrix4x4 T; aiMatrix4x4::Translation(pos, T);

        return T * R * S;
    }

    void read_node_hierarchy_with_global_inverse(
        double anim_time,
        const aiNode* node,
        const aiMatrix4x4& parent_transform,
        const aiAnimation* animation,
        const std::unordered_map<std::string, std::uint32_t>& bone_map,
        const std::vector<aiMatrix4x4>& bone_offsets,
        std::vector<aiMatrix4x4>& out_bone_final,
        const aiMatrix4x4& global_inverse)
    {
        if (!node) return;

        aiMatrix4x4 node_local = node->mTransformation;
        if (const aiNodeAnim* ch = find_node_anim(animation, node->mName))
            node_local = make_node_transform(ch, anim_time);

        const aiMatrix4x4 global = parent_transform * node_local;

        auto it = bone_map.find(node->mName.C_Str());
        if (it != bone_map.end())
        {
            const std::uint32_t bi = it->second;
            if (bi < out_bone_final.size())
                out_bone_final[bi] = global_inverse * global * bone_offsets[bi];
        }

        for (unsigned int i = 0; i < node->mNumChildren; ++i)
            read_node_hierarchy_with_global_inverse(anim_time, node->mChildren[i], global,
                                                    animation, bone_map, bone_offsets, out_bone_final,
                                                    global_inverse);
    }
}

matrix dynamic_mesh::to_matrix(const aiMatrix4x4& m)
{
    matrix out{};
    out(0, 0) = m.a1; out(0, 1) = m.a2; out(0, 2) = m.a3; out(0, 3) = m.a4;
    out(1, 0) = m.b1; out(1, 1) = m.b2; out(1, 2) = m.b3; out(1, 3) = m.b4;
    out(2, 0) = m.c1; out(2, 1) = m.c2; out(2, 2) = m.c3; out(2, 3) = m.c4;
    out(3, 0) = m.d1; out(3, 1) = m.d2; out(3, 2) = m.d3; out(3, 3) = m.d4;
    return out;
}

void dynamic_mesh::update_bounds(vec4& min_v, vec4& max_v, const vec4& p)
{
    min_v[0] = (std::min)(min_v[0], p[0]);
    min_v[1] = (std::min)(min_v[1], p[1]);
    min_v[2] = (std::min)(min_v[2], p[2]);
    max_v[0] = (std::max)(max_v[0], p[0]);
    max_v[1] = (std::max)(max_v[1], p[1]);
    max_v[2] = (std::max)(max_v[2], p[2]);
}

void dynamic_mesh::gather_instances(
    const aiNode* node,
    const aiMatrix4x4& parent,
    std::vector<mesh_instance>& instances,
    std::size_t& node_count,
    std::vector<aiMatrix4x4>& mesh_node_inverse,
    std::vector<bool>& mesh_node_inverse_set)
{
    if (!node) return;

    ++node_count;
    const aiMatrix4x4 global = parent * node->mTransformation;

    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
    {
        const unsigned int mesh_index = node->mMeshes[i];

        mesh_instance inst{};
        inst.mesh_index = static_cast<std::size_t>(mesh_index);
        inst.node_world = to_matrix(global);
        instances.push_back(inst);

        if (mesh_index < mesh_node_inverse.size() && !mesh_node_inverse_set[mesh_index])
        {
            aiMatrix4x4 inv = global;
            inv.Inverse();
            mesh_node_inverse[mesh_index] = inv;
            mesh_node_inverse_set[mesh_index] = true;
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        gather_instances(node->mChildren[i], global, instances, node_count, mesh_node_inverse, mesh_node_inverse_set);
}

void dynamic_mesh::add_weight(vertex_weights& vw, std::uint32_t bone, float weight)
{
    if (vw.count < 4)
    {
        vw.bone[vw.count] = bone;
        vw.weight[vw.count] = weight;
        ++vw.count;
        return;
    }

    std::uint32_t min_i = 0;
    float min_w = vw.weight[0];
    for (std::uint32_t i = 1; i < 4; ++i)
    {
        if (vw.weight[i] < min_w)
        {
            min_w = vw.weight[i];
            min_i = i;
        }
    }

    if (weight > min_w)
    {
        vw.bone[min_i] = bone;
        vw.weight[min_i] = weight;
    }
}

bool dynamic_mesh::load(const char* path, texture_cache* tex_cache)
{
    meshes_.clear();
    instances_.clear();
    bone_map_.clear();
    bone_offsets_.clear();
    mesh_node_inverse_.clear();
    mesh_node_inverse_set_.clear();

    node_count_ = 0;
    loaded_ = false;
    has_animation_ = false;

    scene_ = importer_.ReadFile(
        path,
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_GlobalScale
    );

    if (!scene_ || !scene_->HasMeshes())
    {
        std::printf("dynamic_mesh: Assimp failed to load %s: %s\n", path, importer_.GetErrorString());
        return false;
    }

    const unsigned int anim_count = scene_->mNumAnimations;
    std::printf("dynamic_mesh: animations=%u\n", anim_count);

    for (unsigned int ai = 0; ai < anim_count; ++ai)
    {
        const aiAnimation* A = scene_->mAnimations[ai];
        if (!A)
        {
            std::printf("ERROR: NOT found [%u] <null>\n", ai);
            continue;
        }

        const char* name = (A->mName.length > 0) ? A->mName.C_Str() : "<unnamed>";
        const double tps = (A->mTicksPerSecond != 0.0) ? A->mTicksPerSecond : 25.0;
        const double duration_ticks = A->mDuration;
        const double duration_sec = (tps != 0.0) ? (duration_ticks / tps) : 0.0;

        std::printf(
            "  [%u] name=%s | channels=%u | duration_ticks=%.3f | tps=%.3f | duration_sec=%.3f\n",
            ai,
            name,
            A->mNumChannels,
            duration_ticks,
            tps,
            duration_sec
        );
    }

    global_inverse_ = aiMatrix4x4();
    if (scene_->mRootNode)
    {
        global_inverse_ = scene_->mRootNode->mTransformation;
        global_inverse_.Inverse();
    }

    meshes_.resize(scene_->mNumMeshes);

    mesh_node_inverse_.resize(scene_->mNumMeshes);
    mesh_node_inverse_set_.assign(scene_->mNumMeshes, false);

    for (unsigned int mi = 0; mi < scene_->mNumMeshes; ++mi)
    {
        const aiMesh* mesh_src = scene_->mMeshes[mi];
        if (!mesh_src) continue;

        mesh_data& data = meshes_[mi];

        const std::size_t vertex_count = mesh_src->mNumVertices;
        const std::size_t face_count   = mesh_src->mNumFaces;

        data.base_positions.reserve(vertex_count);
        data.base_normals.reserve(vertex_count);
        data.weights.resize(vertex_count);
        data.triangles.reserve(face_count);

        const bool has_normals = mesh_src->HasNormals();
        const bool has_uvs = mesh_src->HasTextureCoords(0);

        if (has_uvs)
            data.base_uvs.reserve(vertex_count * 2u);

        for (std::size_t vi = 0; vi < vertex_count; ++vi)
        {
            const aiVector3D& pos = mesh_src->mVertices[vi];
            aiVector3D nrm(0.f, 0.f, 1.f);
            if (has_normals) nrm = mesh_src->mNormals[vi];

            data.base_positions.push_back(pos);
            data.base_normals.push_back(nrm);

            if (has_uvs)
            {
                const aiVector3D& uv = mesh_src->mTextureCoords[0][vi];
                data.base_uvs.push_back(uv.x);
                data.base_uvs.push_back(uv.y);
            }
        }

        for (std::size_t fi = 0; fi < face_count; ++fi)
        {
            const aiFace& face = mesh_src->mFaces[fi];
            if (face.mNumIndices != 3) continue;

            data.triangles.emplace_back(
                (std::uint32_t)face.mIndices[0],
                (std::uint32_t)face.mIndices[1],
                (std::uint32_t)face.mIndices[2]
            );
        }

        if (mesh_src->HasBones())
        {
            for (unsigned int bi = 0; bi < mesh_src->mNumBones; ++bi)
            {
                const aiBone* bone = mesh_src->mBones[bi];
                if (!bone) continue;

                const std::string bone_name = bone->mName.C_Str();

                auto it = bone_map_.find(bone_name);
                std::uint32_t bone_index = 0;

                if (it == bone_map_.end())
                {
                    bone_index = static_cast<std::uint32_t>(bone_offsets_.size());
                    bone_map_.emplace(bone_name, bone_index);
                    bone_offsets_.push_back(bone->mOffsetMatrix);
                }
                else
                {
                    bone_index = it->second;
                }

                for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi)
                {
                    const aiVertexWeight& w = bone->mWeights[wi];
                    if (w.mVertexId >= data.weights.size()) continue;
                    add_weight(data.weights[w.mVertexId], bone_index, w.mWeight);
                }
            }
        }

        for (auto& vw : data.weights)
        {
            float sum = 0.f;
            for (std::uint32_t i = 0; i < vw.count; ++i) sum += vw.weight[i];
            if (sum > 0.f)
                for (std::uint32_t i = 0; i < vw.count; ++i) vw.weight[i] /= sum;
        }

        data.asset.allocate(static_cast<std::uint32_t>(data.triangles.size()), has_uvs);
        for (std::size_t t = 0; t < data.triangles.size(); ++t)
        {
            const triIndices& tri = data.triangles[t];
            const std::size_t base = t * 3u;

            const aiVector3D& p0 = data.base_positions[tri.v0];
            const aiVector3D& p1 = data.base_positions[tri.v1];
            const aiVector3D& p2 = data.base_positions[tri.v2];

            const aiVector3D& n0 = data.base_normals[tri.v0];
            const aiVector3D& n1 = data.base_normals[tri.v1];
            const aiVector3D& n2 = data.base_normals[tri.v2];

            data.asset.positions[base + 0] = vec4(p0.x, p0.y, p0.z, 1.f);
            data.asset.positions[base + 1] = vec4(p1.x, p1.y, p1.z, 1.f);
            data.asset.positions[base + 2] = vec4(p2.x, p2.y, p2.z, 1.f);

            data.asset.normals[base + 0] = vec4(n0.x, n0.y, n0.z, 0.f);
            data.asset.normals[base + 1] = vec4(n1.x, n1.y, n1.z, 0.f);
            data.asset.normals[base + 2] = vec4(n2.x, n2.y, n2.z, 0.f);

            if (has_uvs)
            {
                data.asset.uvs[(base + 0) * 2 + 0] = data.base_uvs[tri.v0 * 2 + 0];
                data.asset.uvs[(base + 0) * 2 + 1] = data.base_uvs[tri.v0 * 2 + 1];
                data.asset.uvs[(base + 1) * 2 + 0] = data.base_uvs[tri.v1 * 2 + 0];
                data.asset.uvs[(base + 1) * 2 + 1] = data.base_uvs[tri.v1 * 2 + 1];
                data.asset.uvs[(base + 2) * 2 + 0] = data.base_uvs[tri.v2 * 2 + 0];
                data.asset.uvs[(base + 2) * 2 + 1] = data.base_uvs[tri.v2 * 2 + 1];
            }
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
                        std::string key = std::string(path) + "::" + tex_path.C_Str();
                        loaded = tex_cache->load_memory(key, embedded->pcData, embedded->mWidth);
                    }
                }
                else
                {
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

            if (!data.tex_ref.valid() && has_uvs)
            {
                const TextureRGBA8* checker = tex_cache->checkerboard();
                data.tex_ref.pixels = checker->pixels;
                data.tex_ref.tex_w = checker->width;
                data.tex_ref.tex_h = checker->height;
            }
        }
    }

    if (scene_->mRootNode)
        gather_instances(scene_->mRootNode, aiMatrix4x4(), instances_, node_count_, mesh_node_inverse_, mesh_node_inverse_set_);

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
        if (inst.mesh_index >= meshes_.size()) continue;
        const auto& data = meshes_[inst.mesh_index];

        for (const auto& p : data.base_positions)
        {
            const vec4 wp = inst.node_world * vec4(p.x, p.y, p.z, 1.f);
            update_bounds(bounds_min_, bounds_max_, wp);
        }
    }

    has_animation_ = scene_->HasAnimations() && !bone_offsets_.empty();

    loaded_ = !instances_.empty();

    //~ test to see if the loaded transform is starting or skipping root
    std::printf(
        "GLB root transform:\n"
        "  [%f %f %f %f]\n"
        "  [%f %f %f %f]\n"
        "  [%f %f %f %f]\n"
        "  [%f %f %f %f]\n",
        scene_->mRootNode->mTransformation.a1,
        scene_->mRootNode->mTransformation.a2,
        scene_->mRootNode->mTransformation.a3,
        scene_->mRootNode->mTransformation.a4,
        scene_->mRootNode->mTransformation.b1,
        scene_->mRootNode->mTransformation.b2,
        scene_->mRootNode->mTransformation.b3,
        scene_->mRootNode->mTransformation.b4,
        scene_->mRootNode->mTransformation.c1,
        scene_->mRootNode->mTransformation.c2,
        scene_->mRootNode->mTransformation.c3,
        scene_->mRootNode->mTransformation.c4,
        scene_->mRootNode->mTransformation.d1,
        scene_->mRootNode->mTransformation.d2,
        scene_->mRootNode->mTransformation.d3,
        scene_->mRootNode->mTransformation.d4
    );

    return loaded_;
}

void dynamic_mesh::build_instances(fecs::world& w, const matrix& base_world, const colour& col, float ka, float kd)
{
    std::vector<fecs::entity> ignored{};
    ignored.reserve(instances_.size());
    build_instances(w, base_world, col, ka, kd, ignored);
}

void dynamic_mesh::build_instances(
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

void dynamic_mesh::build_instances(
    fecs::world& w,
    const matrix& base_world,
    const colour& col,
    float ka,
    float kd,
    std::vector<fecs::entity>& out_entities,
    std::vector<matrix>& out_locals) const
{
    if (!loaded_) return;

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
        if (inst.mesh_index >= meshes_.size()) continue;

        const matrix world = base_world * inst.node_world;
        const fecs::entity e = spawn_instance(w, meshes_[inst.mesh_index].asset, world, col, ka, kd,
                                               meshes_[inst.mesh_index].tex_ref);
        out_entities.push_back(e);
        out_locals.push_back(inst.node_world);
    }
}

dynamic_mesh_instance dynamic_mesh::create_instance() const
{
    dynamic_mesh_instance inst{};
    inst.parent_mesh = this;
    inst.anim_index = 0;
    inst.bone_transforms.resize(bone_offsets_.size());

    // Allocate per entity mesh data
    inst.mesh_data.resize(meshes_.size());
    for (std::size_t mi = 0; mi < meshes_.size(); ++mi)
    {
        const auto& src = meshes_[mi];
        auto& dst = inst.mesh_data[mi];

        dst.skinned_positions.resize(src.base_positions.size());
        dst.skinned_normals.resize(src.base_normals.size());

        const bool has_uvs = !src.base_uvs.empty();
        dst.asset.allocate((std::uint32_t)src.triangles.size(), has_uvs);

        // Initialize with base mesh data
        for (std::size_t t = 0; t < src.triangles.size(); ++t)
        {
            const triIndices& tri = src.triangles[t];
            const std::size_t base = t * 3u;

            const aiVector3D& p0 = src.base_positions[tri.v0];
            const aiVector3D& p1 = src.base_positions[tri.v1];
            const aiVector3D& p2 = src.base_positions[tri.v2];

            const aiVector3D& n0 = src.base_normals[tri.v0];
            const aiVector3D& n1 = src.base_normals[tri.v1];
            const aiVector3D& n2 = src.base_normals[tri.v2];

            dst.asset.positions[base + 0] = vec4(p0.x, p0.y, p0.z, 1.f);
            dst.asset.positions[base + 1] = vec4(p1.x, p1.y, p1.z, 1.f);
            dst.asset.positions[base + 2] = vec4(p2.x, p2.y, p2.z, 1.f);

            dst.asset.normals[base + 0] = vec4(n0.x, n0.y, n0.z, 0.f);
            dst.asset.normals[base + 1] = vec4(n1.x, n1.y, n1.z, 0.f);
            dst.asset.normals[base + 2] = vec4(n2.x, n2.y, n2.z, 0.f);

            if (has_uvs)
            {
                dst.asset.uvs[(base + 0) * 2 + 0] = src.base_uvs[tri.v0 * 2 + 0];
                dst.asset.uvs[(base + 0) * 2 + 1] = src.base_uvs[tri.v0 * 2 + 1];
                dst.asset.uvs[(base + 1) * 2 + 0] = src.base_uvs[tri.v1 * 2 + 0];
                dst.asset.uvs[(base + 1) * 2 + 1] = src.base_uvs[tri.v1 * 2 + 1];
                dst.asset.uvs[(base + 2) * 2 + 0] = src.base_uvs[tri.v2 * 2 + 0];
                dst.asset.uvs[(base + 2) * 2 + 1] = src.base_uvs[tri.v2 * 2 + 1];
            }
        }
    }

    return inst;
}

void dynamic_mesh::tick_skinning(dynamic_mesh_instance& instance, double elapsed_seconds) const
{
    if (!has_animation_ || !scene_ || !scene_->HasAnimations())
        return;

    const std::size_t anim_count = scene_->mNumAnimations;
    if (anim_count == 0) return;

    const std::size_t idx = (instance.anim_index < anim_count) ? instance.anim_index : 0;
    const aiAnimation* anim = scene_->mAnimations[static_cast<unsigned int>(idx)];
    if (!anim) return;

    const double tps = (anim->mTicksPerSecond != 0.0) ? anim->mTicksPerSecond : 25.0;
    const double time_in_ticks = elapsed_seconds * tps;
    const double anim_time = std::fmod(time_in_ticks, anim->mDuration);

    if (instance.bone_transforms.size() != bone_offsets_.size())
        instance.bone_transforms.resize(bone_offsets_.size());

    read_node_hierarchy_with_global_inverse(
        anim_time,
        scene_->mRootNode,
        aiMatrix4x4(),
        anim,
        bone_map_,
        bone_offsets_,
        instance.bone_transforms,
        global_inverse_
    );

    for (std::size_t mi = 0; mi < meshes_.size(); ++mi)
    {
        const auto& src = meshes_[mi];
        if (mi >= instance.mesh_data.size()) continue;
        auto& dst = instance.mesh_data[mi];

        if (src.base_positions.size() != src.weights.size())
            continue;

        dst.skinned_positions.resize(src.base_positions.size());
        dst.skinned_normals.resize(src.base_normals.size());

        for (std::size_t i = 0; i < src.base_positions.size(); ++i)
        {
            const auto& vw = src.weights[i];

            if (vw.count == 0)
            {
                dst.skinned_positions[i] = src.base_positions[i];
                dst.skinned_normals[i]   = src.base_normals[i];
                continue;
            }

            aiVector3D pos(0.f, 0.f, 0.f);
            aiVector3D nrm(0.f, 0.f, 0.f);

            for (std::uint32_t j = 0; j < vw.count; ++j)
            {
                const std::uint32_t bi = vw.bone[j];
                if (bi >= instance.bone_transforms.size()) continue;

                const float w = vw.weight[j];

                const aiMatrix4x4& skinM = instance.bone_transforms[bi];
                pos += (skinM * src.base_positions[i]) * w;

                const aiMatrix3x3 N(skinM);
                nrm += (N * src.base_normals[i]) * w;
            }

            nrm.Normalize();
            dst.skinned_positions[i] = pos;
            dst.skinned_normals[i]   = nrm;
        }

        for (std::size_t t = 0; t < src.triangles.size(); ++t)
        {
            const triIndices& tri = src.triangles[t];
            const std::size_t base = t * 3u;

            const aiVector3D& p0 = dst.skinned_positions[tri.v0];
            const aiVector3D& p1 = dst.skinned_positions[tri.v1];
            const aiVector3D& p2 = dst.skinned_positions[tri.v2];

            const aiVector3D& n0 = dst.skinned_normals[tri.v0];
            const aiVector3D& n1 = dst.skinned_normals[tri.v1];
            const aiVector3D& n2 = dst.skinned_normals[tri.v2];

            dst.asset.positions[base + 0] = vec4(p0.x, p0.y, p0.z, 1.f);
            dst.asset.positions[base + 1] = vec4(p1.x, p1.y, p1.z, 1.f);
            dst.asset.positions[base + 2] = vec4(p2.x, p2.y, p2.z, 1.f);

            dst.asset.normals[base + 0] = vec4(n0.x, n0.y, n0.z, 0.f);
            dst.asset.normals[base + 1] = vec4(n1.x, n1.y, n1.z, 0.f);
            dst.asset.normals[base + 2] = vec4(n2.x, n2.y, n2.z, 0.f);
        }
    }
}

bool dynamic_mesh::sample_node_world(matrix& out) const noexcept
{
    if (instances_.empty()) return false;
    out = instances_[0].node_world;
    return true;
}
