#ifndef FOXRASTERIZER_MESH_H
#define FOXRASTERIZER_MESH_H

#pragma once

#include <vector>
#include <cstdint>
#include <cmath>
#include <algorithm>
#include "fmath.h"

struct alignas(16) Vertex
{
    vec4   p;
    vec4   normal;
    colour rgb;
};

struct alignas(16) Position
{
    float _position[4];
};
static_assert(sizeof(Position) == 16 && "Positon Must Align by 16 bits");

struct triIndices
{
    union
    {
        std::uint32_t v[3];
        struct
        {
            std::uint32_t v0, v1, v2;
        };
    };

    triIndices() = default;
    triIndices(std::uint32_t a, std::uint32_t b, std::uint32_t c) : v0(a), v1(b), v2(c) {}
};

class Mesh
{
public:
    colour col{};
    float  kd{ 0.75f };
    float  ka{ 0.75f };
    matrix world{};

    std::vector<Vertex>      vertices{};
    std::vector<triIndices>  triangles{};

    void reserve(std::size_t vertexCount, std::size_t triCount)
    {
        vertices.reserve(vertexCount);
        triangles.reserve(triCount);
    }

    void clear()
    {
        vertices.clear();
        triangles.clear();
    }

    void setColour(const colour& c, float _ka, float _kd)
    {
        col = c;
        ka  = _ka;
        kd  = _kd;
    }

    void addVertex(const vec4& position, const vec4& nrm)
    {
        Vertex v;
        v.p      = position;
        v.normal = nrm;
        v.rgb    = col;
        vertices.emplace_back(v);
    }

    void addTriangle(std::uint32_t a, std::uint32_t b, std::uint32_t c)
    {
        triangles.emplace_back(a, b, c);
    }

    static Mesh makeRectangle(float x1, float y1, float x2, float y2)
    {
        Mesh mesh;
        mesh.reserve(4, 2);

        vec4 v1(x1, y1, 0);
        vec4 v2(x2, y1, 0);
        vec4 v3(x2, y2, 0);
        vec4 v4(x1, y2, 0);

        vec4 edge1 = v2 - v1;
        vec4 edge2 = v4 - v1;
        vec4 n = vec4::cross(edge1, edge2);
        n.normalise();
        n[3] = 0.f;

        mesh.addVertex(v1, n);
        mesh.addVertex(v2, n);
        mesh.addVertex(v3, n);
        mesh.addVertex(v4, n);

        mesh.addTriangle(0, 2, 1);
        mesh.addTriangle(0, 3, 2);

        return mesh;
    }

    static Mesh makeCube(float size)
    {
        Mesh mesh;
        mesh.reserve(24, 12);

        const float h = size * 0.5f;

        vec4 positions[8] = {
            vec4(-h, -h, -h),
            vec4( h, -h, -h),
            vec4( h,  h, -h),
            vec4(-h,  h, -h),
            vec4(-h, -h,  h),
            vec4( h, -h,  h),
            vec4( h,  h,  h),
            vec4(-h,  h,  h)
        };

        vec4 normals[6] = {
            vec4( 0,  0, -1, 0),
            vec4( 0,  0,  1, 0),
            vec4(-1,  0,  0, 0),
            vec4( 1,  0,  0, 0),
            vec4( 0, -1,  0, 0),
            vec4( 0,  1,  0, 0)
        };

        int faces[6][4] = {
            {1, 0, 3, 2},
            {4, 5, 6, 7},
            {3, 0, 4, 7},
            {5, 1, 2, 6},
            {0, 1, 5, 4},
            {2, 3, 7, 6}
        };

        for (int i = 0; i < 6; ++i)
        {
            const int a = faces[i][0];
            const int b = faces[i][1];
            const int c = faces[i][2];
            const int d = faces[i][3];

            const std::uint32_t base = static_cast<std::uint32_t>(mesh.vertices.size());
            mesh.addVertex(positions[a], normals[i]);
            mesh.addVertex(positions[b], normals[i]);
            mesh.addVertex(positions[c], normals[i]);
            mesh.addVertex(positions[d], normals[i]);

            mesh.addTriangle(base + 0, base + 2, base + 1);
            mesh.addTriangle(base + 0, base + 3, base + 2);
        }

        return mesh;
    }

    static Mesh makeSphere(float radius, int latitudeDivisions, int longitudeDivisions)
    {
        Mesh mesh;

        if (latitudeDivisions < 2)  latitudeDivisions = 2;
        if (longitudeDivisions < 3) longitudeDivisions = 3;

        const int latN = latitudeDivisions;
        const int lonN = longitudeDivisions;

        const std::size_t vCount = static_cast<std::size_t>((latN + 1) * (lonN + 1));
        const std::size_t tCount = static_cast<std::size_t>(latN * lonN * 2);

        mesh.reserve(vCount, tCount);

        constexpr float PI = 3.14159265358979323846f;
        constexpr float TWO_PI = 6.28318530717958647692f;

        for (int lat = 0; lat <= latN; ++lat)
        {
            const float theta = PI * (float)lat / (float)latN;
            const float st = std::sin(theta);
            const float ct = std::cos(theta);

            for (int lon = 0; lon <= lonN; ++lon)
            {
                const float phi = TWO_PI * (float)lon / (float)lonN;
                const float sp = std::sin(phi);
                const float cp = std::cos(phi);

                vec4 pos(
                    radius * st * cp,
                    radius * st * sp,
                    radius * ct,
                    1.0f
                );

                vec4 n = pos;
                n.normalise();
                n[3] = 0.f;

                mesh.addVertex(pos, n);
            }
        }

        for (int lat = 0; lat < latN; ++lat)
        {
            for (int lon = 0; lon < lonN; ++lon)
            {
                const std::uint32_t v0 = (std::uint32_t)(lat * (lonN + 1) + lon);
                const std::uint32_t v1 = v0 + 1;
                const std::uint32_t v2 = (std::uint32_t)((lat + 1) * (lonN + 1) + lon);
                const std::uint32_t v3 = v2 + 1;

                mesh.addTriangle(v0, v1, v2);
                mesh.addTriangle(v1, v3, v2);
            }
        }

        return mesh;
    }
};

#endif // FOXRASTERIZER_MESH_H
