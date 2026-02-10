#ifndef FOXRASTERIZER_VEC_H
#define FOXRASTERIZER_VEC_H

#include <cstdint>
#include <cmath>
#include <algorithm>

#ifdef USE_SIMD
#include <immintrin.h>
#endif

class alignas(16) vec4
{
public:
    union
    {
        struct { float x, y, z, w; };
        float v[4];
    };

public:
    vec4(float _x = 0.f, float _y = 0.f, float _z = 0.f, float _w = 1.f) noexcept
        : x(_x), y(_y), z(_z), w(_w) {}

    float* data() noexcept { return v; }
    const float* data() const noexcept { return v; }

    float& operator[](unsigned int index) noexcept { return v[index]; }
    float  operator[](unsigned int index) const noexcept { return v[index]; }

    vec4 operator*(float scalar) const noexcept
    {
#ifdef USE_SIMD
        __m128 vv = _mm_load_ps(v);
        __m128 ss = _mm_set1_ps(scalar);
        __m128 rr = _mm_mul_ps(vv, ss);
        vec4 out;
        _mm_store_ps(out.v, rr);
        return out;
#else
        return { x * scalar, y * scalar, z * scalar, w * scalar };
#endif
    }

    vec4& operator*=(float scalar) noexcept
    {
#ifdef USE_SIMD
        __m128 vv = _mm_load_ps(v);
        __m128 ss = _mm_set1_ps(scalar);
        __m128 rr = _mm_mul_ps(vv, ss);
        _mm_store_ps(v, rr);
#else
        x *= scalar;
        y *= scalar;
        z *= scalar;
        w *= scalar;
#endif
        return *this;
    }

    vec4& operator+=(const vec4& other) noexcept
    {
#ifdef USE_SIMD
        __m128 a = _mm_load_ps(v);
        __m128 b = _mm_load_ps(other.v);
        __m128 r = _mm_add_ps(a, b);
        _mm_store_ps(v, r);
#else
        x += other.x;
        y += other.y;
        z += other.z;
        w += other.w;
#endif
        return *this;
    }

    vec4 operator-(const vec4& other) const noexcept
    {
#ifdef USE_SIMD
        __m128 a = _mm_load_ps(v);
        __m128 b = _mm_load_ps(other.v);
        __m128 r = _mm_sub_ps(a, b);
        vec4 out;
        _mm_store_ps(out.v, r);
        out.w = 0.0f;
        return out;
#else
        return vec4(x - other.x, y - other.y, z - other.z, 0.0f);
#endif
    }

    vec4 operator+(const vec4& other) const noexcept
    {
#ifdef USE_SIMD
        __m128 a = _mm_load_ps(v);
        __m128 b = _mm_load_ps(other.v);
        __m128 r = _mm_add_ps(a, b);
        vec4 out;
        _mm_store_ps(out.v, r);
        out.w = 0.0f;
        return out;
#else
        return vec4(x + other.x, y + other.y, z + other.z, 0.0f);
#endif
    }

    void divideW() noexcept
    {
#ifdef USE_SIMD
        const float ww = (w != 0.f) ? w : 1.f;
        __m128 vv = _mm_load_ps(v);
        __m128 inv = _mm_set1_ps(1.0f / ww);
        __m128 r = _mm_mul_ps(vv, inv);
        vec4 out;
        _mm_store_ps(out.v, r);
        x = out.x;
        y = out.y;
        z = out.z;
        w = 1.f;
#else
        x /= w;
        y /= w;
        z /= w;
        w = 1.f;
#endif
    }

    static vec4 cross(const vec4& a, const vec4& b) noexcept
    {
        return vec4(
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x,
            0.0f
        );
    }

    static float dot(const vec4& a, const vec4& b) noexcept
    {
#ifdef USE_SIMD
        __m128 av = _mm_load_ps(a.v);
        __m128 bv = _mm_load_ps(b.v);
        __m128 dp = _mm_dp_ps(av, bv, 0x71);
        return _mm_cvtss_f32(dp);
#else
        return (a.x * b.x) + (a.y * b.y) + (a.z * b.z);
#endif
    }

    float length() const noexcept
    {
#ifdef USE_SIMD
        __m128 vv   = _mm_load_ps(v);
        __m128 mask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
        __m128 xyz  = _mm_and_ps(vv, mask);
        __m128 len2 = _mm_dp_ps(xyz, xyz, 0x7F);
        __m128 len  = _mm_sqrt_ps(len2);
        return _mm_cvtss_f32(len);
#else
        return std::sqrt(x * x + y * y + z * z);
#endif
    }

    void normalise() noexcept
    {
#ifdef USE_SIMD
        __m128 vv = _mm_load_ps(v);
        __m128 mask = _mm_castsi128_ps(_mm_set_epi32(0, -1, -1, -1));
        __m128 xyz = _mm_and_ps(vv, mask);

        __m128 len2 = _mm_dp_ps(xyz, xyz, 0x7F);
        __m128 inv  = _mm_rsqrt_ps(len2);
        __m128 nxyz = _mm_mul_ps(xyz, inv);

        alignas(16) float t[4];
        _mm_store_ps(t, nxyz);
        x = t[0];
        y = t[1];
        z = t[2];
#else
        const float len = std::sqrt(x * x + y * y + z * z);
        const float inv = (len != 0.f) ? (1.0f / len) : 0.f;
        x *= inv;
        y *= inv;
        z *= inv;
#endif
    }
};

struct alignas(16) colour
{
    union
    {
        struct { float r, g, b; };
        float rgb[3];
    };

    enum Channel : int { RED = 0, GREEN = 1, BLUE = 2 };

    constexpr colour() noexcept : r(0.f), g(0.f), b(0.f) {}
    constexpr colour(float rr, float gg, float bb) noexcept : r(rr), g(gg), b(bb) {}

    constexpr void set(float rr, float gg, float bb) noexcept { r = rr; g = gg; b = bb; }

    constexpr float& operator[](Channel c) noexcept { return rgb[(int)c]; }
    constexpr const float& operator[](Channel c) const noexcept { return rgb[(int)c]; }

    static constexpr float clamp01(float v) noexcept { return (v < 0.f) ? 0.f : (v > 1.f ? 1.f : v); }

    constexpr void clampColour() noexcept
    {
        r = clamp01(r);
        g = clamp01(g);
        b = clamp01(b);
    }

    static constexpr std::uint8_t to_u8(float v) noexcept
    {
        v = clamp01(v);
        return static_cast<std::uint8_t>(v * 255.0f + 0.5f);
    }

    constexpr void toRGB(unsigned char& cr, unsigned char& cg, unsigned char& cb) const noexcept
    {
        cr = (unsigned char)to_u8(r);
        cg = (unsigned char)to_u8(g);
        cb = (unsigned char)to_u8(b);
    }

    friend constexpr colour operator+(const colour& a, const colour& b) noexcept { return { a.r + b.r, a.g + b.g, a.b + b.b }; }
    constexpr colour& operator+=(const colour& o) noexcept { r += o.r; g += o.g; b += o.b; return *this; }

    friend constexpr colour operator*(const colour& c, float s) noexcept { return { c.r * s, c.g * s, c.b * s }; }
    friend constexpr colour operator*(float s, const colour& c) noexcept { return c * s; }
    friend constexpr colour operator*(const colour& a, const colour& b) noexcept { return { a.r * b.r, a.g * b.g, a.b * b.b }; }

    constexpr colour& operator*=(float s) noexcept { r *= s; g *= s; b *= s; return *this; }
    constexpr colour& operator*=(const colour& o) noexcept { r *= o.r; g *= o.g; b *= o.b; return *this; }
};

class alignas(16) matrix
{
    union
    {
        float m[4][4];
        float a[16];
    };

public:
    matrix() noexcept { identity(); }

    float& operator()(unsigned int row, unsigned int col) noexcept { return m[row][col]; }
    const float& operator()(unsigned int row, unsigned int col) const noexcept { return m[row][col]; }

    vec4 operator*(const vec4& v) const noexcept
    {
#ifdef USE_SIMD
        __m128 vec = _mm_load_ps(v.data());
        __m128 r0 = _mm_dp_ps(_mm_load_ps(&a[0]), vec, 0xF1);
        __m128 r1 = _mm_dp_ps(_mm_load_ps(&a[4]), vec, 0xF2);
        __m128 r2 = _mm_dp_ps(_mm_load_ps(&a[8]), vec, 0xF4);
        __m128 r3 = _mm_dp_ps(_mm_load_ps(&a[12]), vec, 0xF8);
        __m128 xy = _mm_or_ps(r0, r1);
        __m128 zw = _mm_or_ps(r2, r3);
        __m128 res = _mm_or_ps(xy, zw);

        vec4 out;
        _mm_store_ps(out.data(), res);
        return out;
#else
        vec4 r;
        r[0] = a[0]  * v[0] + a[1]  * v[1] + a[2]  * v[2] + a[3]  * v[3];
        r[1] = a[4]  * v[0] + a[5]  * v[1] + a[6]  * v[2] + a[7]  * v[3];
        r[2] = a[8]  * v[0] + a[9]  * v[1] + a[10] * v[2] + a[11] * v[3];
        r[3] = a[12] * v[0] + a[13] * v[1] + a[14] * v[2] + a[15] * v[3];
        return r;
#endif
    }

    matrix operator*(const matrix& b) const noexcept
    {
#ifdef USE_SIMD
        matrix r;

        const __m128 b0 = _mm_load_ps(&b.a[ 0]);
        const __m128 b1 = _mm_load_ps(&b.a[ 4]);
        const __m128 b2 = _mm_load_ps(&b.a[ 8]);
        const __m128 b3 = _mm_load_ps(&b.a[12]);

        auto mul_row = [&](const float* ar, float* rr) noexcept
        {
            const __m128 arow = _mm_load_ps(ar);

            const __m128 xxxx = _mm_shuffle_ps(arow, arow, _MM_SHUFFLE(0,0,0,0));
            const __m128 yyyy = _mm_shuffle_ps(arow, arow, _MM_SHUFFLE(1,1,1,1));
            const __m128 zzzz = _mm_shuffle_ps(arow, arow, _MM_SHUFFLE(2,2,2,2));
            const __m128 wwww = _mm_shuffle_ps(arow, arow, _MM_SHUFFLE(3,3,3,3));

            __m128 res = _mm_mul_ps(xxxx, b0);
            res = _mm_add_ps(res, _mm_mul_ps(yyyy, b1));
            res = _mm_add_ps(res, _mm_mul_ps(zzzz, b2));
            res = _mm_add_ps(res, _mm_mul_ps(wwww, b3));

            _mm_store_ps(rr, res);
        };

        mul_row(&a[ 0], &r.a[ 0]);
        mul_row(&a[ 4], &r.a[ 4]);
        mul_row(&a[ 8], &r.a[ 8]);
        mul_row(&a[12], &r.a[12]);

        return r;
#else
        matrix r;
        r.a[ 0] = a[ 0]*b.a[ 0] + a[ 1]*b.a[ 4] + a[ 2]*b.a[ 8] + a[ 3]*b.a[12];
        r.a[ 1] = a[ 0]*b.a[ 1] + a[ 1]*b.a[ 5] + a[ 2]*b.a[ 9] + a[ 3]*b.a[13];
        r.a[ 2] = a[ 0]*b.a[ 2] + a[ 1]*b.a[ 6] + a[ 2]*b.a[10] + a[ 3]*b.a[14];
        r.a[ 3] = a[ 0]*b.a[ 3] + a[ 1]*b.a[ 7] + a[ 2]*b.a[11] + a[ 3]*b.a[15];

        r.a[ 4] = a[ 4]*b.a[ 0] + a[ 5]*b.a[ 4] + a[ 6]*b.a[ 8] + a[ 7]*b.a[12];
        r.a[ 5] = a[ 4]*b.a[ 1] + a[ 5]*b.a[ 5] + a[ 6]*b.a[ 9] + a[ 7]*b.a[13];
        r.a[ 6] = a[ 4]*b.a[ 2] + a[ 5]*b.a[ 6] + a[ 6]*b.a[10] + a[ 7]*b.a[14];
        r.a[ 7] = a[ 4]*b.a[ 3] + a[ 5]*b.a[ 7] + a[ 6]*b.a[11] + a[ 7]*b.a[15];

        r.a[ 8] = a[ 8]*b.a[ 0] + a[ 9]*b.a[ 4] + a[10]*b.a[ 8] + a[11]*b.a[12];
        r.a[ 9] = a[ 8]*b.a[ 1] + a[ 9]*b.a[ 5] + a[10]*b.a[ 9] + a[11]*b.a[13];
        r.a[10] = a[ 8]*b.a[ 2] + a[ 9]*b.a[ 6] + a[10]*b.a[10] + a[11]*b.a[14];
        r.a[11] = a[ 8]*b.a[ 3] + a[ 9]*b.a[ 7] + a[10]*b.a[11] + a[11]*b.a[15];

        r.a[12] = a[12]*b.a[ 0] + a[13]*b.a[ 4] + a[14]*b.a[ 8] + a[15]*b.a[12];
        r.a[13] = a[12]*b.a[ 1] + a[13]*b.a[ 5] + a[14]*b.a[ 9] + a[15]*b.a[13];
        r.a[14] = a[12]*b.a[ 2] + a[13]*b.a[ 6] + a[14]*b.a[10] + a[15]*b.a[14];
        r.a[15] = a[12]*b.a[ 3] + a[13]*b.a[ 7] + a[14]*b.a[11] + a[15]*b.a[15];
        return r;
#endif
    }

    static matrix makePerspective(float fov, float aspect, float n, float f) noexcept
    {
        matrix m;
        m.zero();
        const float tanHalfFov = std::tan(fov * 0.5f);

        m.a[0]  = 1.0f / (aspect * tanHalfFov);
        m.a[5]  = 1.0f / tanHalfFov;
        m.a[10] = -f / (f - n);
        m.a[11] = -(f * n) / (f - n);
        m.a[14] = -1.0f;
        return m;
    }

    static matrix makeTranslation(float tx, float ty, float tz) noexcept
    {
        matrix m;
        m.identity();
        m.a[3]  = tx;
        m.a[7]  = ty;
        m.a[11] = tz;
        return m;
    }

    static matrix makeRotateZ(float aRad) noexcept
    {
        matrix m;
        m.identity();
        m.a[0] = std::cos(aRad);
        m.a[1] = -std::sin(aRad);
        m.a[4] = std::sin(aRad);
        m.a[5] = std::cos(aRad);
        return m;
    }

    static matrix makeRotateX(float aRad) noexcept
    {
        matrix m;
        m.identity();
        m.a[5]  = std::cos(aRad);
        m.a[6]  = -std::sin(aRad);
        m.a[9]  = std::sin(aRad);
        m.a[10] = std::cos(aRad);
        return m;
    }

    static matrix makeRotateY(float aRad) noexcept
    {
        matrix m;
        m.identity();
        m.a[0]  = std::cos(aRad);
        m.a[2]  = std::sin(aRad);
        m.a[8]  = -std::sin(aRad);
        m.a[10] = std::cos(aRad);
        return m;
    }

    static matrix makeRotateXYZ(float x, float y, float z) noexcept
    {
        return makeRotateX(x) * makeRotateY(y) * makeRotateZ(z);
    }

    static matrix makeScale(float s) noexcept
    {
        matrix m;
        s = (std::max)(s, 0.01f);
        m.identity();
        m.a[0]  = s;
        m.a[5]  = s;
        m.a[10] = s;
        return m;
    }

    static matrix makeIdentity() noexcept
    {
        matrix m;
        m.identity();
        return m;
    }

    static inline matrix makeTranslationZ(float tz) noexcept
    {
        matrix m;
        m.identity();
        m.a[11] = tz;
        return m;
    }

    static inline matrix makeCameraDollyZ(float zoffset) noexcept
    {
        return makeTranslationZ(-zoffset);
    }

    inline void setTranslationZ_fast(float tz) noexcept
    {
        a[11] = tz;
    }

    inline void setTranslationX_fast(float tx) noexcept
    {
        a[3] = tx;
    }

    inline void setTranslationY_fast(float ty) noexcept
    {
        a[7] = ty;
    }

private:
    void zero() noexcept
    {
        a[0]=a[1]=a[2]=a[3]=0.f;
        a[4]=a[5]=a[6]=a[7]=0.f;
        a[8]=a[9]=a[10]=a[11]=0.f;
        a[12]=a[13]=a[14]=a[15]=0.f;
    }

    void identity() noexcept
    {
        a[0]=1.f; a[1]=0.f; a[2]=0.f; a[3]=0.f;
        a[4]=0.f; a[5]=1.f; a[6]=0.f; a[7]=0.f;
        a[8]=0.f; a[9]=0.f; a[10]=1.f; a[11]=0.f;
        a[12]=0.f; a[13]=0.f; a[14]=0.f; a[15]=1.f;
    }
};

namespace fox_math
{
    __forceinline static matrix look_at(const vec4& eye, const vec4& target, const vec4& up) noexcept
    {
        vec4 forward = eye - target;
        forward.normalise();

        vec4 right = vec4::cross(up, forward);
        right.normalise();

        vec4 new_up = vec4::cross(forward, right);

        matrix view = matrix::makeIdentity();
        view(0, 0) = right.x;
        view(0, 1) = right.y;
        view(0, 2) = right.z;
        view(0, 3) = -vec4::dot(right, eye);

        view(1, 0) = new_up.x;
        view(1, 1) = new_up.y;
        view(1, 2) = new_up.z;
        view(1, 3) = -vec4::dot(new_up, eye);

        view(2, 0) = forward.x;
        view(2, 1) = forward.y;
        view(2, 2) = forward.z;
        view(2, 3) = -vec4::dot(forward, eye);

        view(3, 0) = 0.f;
        view(3, 1) = 0.f;
        view(3, 2) = 0.f;
        view(3, 3) = 1.f;

        return view;
    }

    template <typename T>
    struct constants
    {

        static constexpr T pi        = T(3.14159265358979323846264338327950288L);
        static constexpr T two_pi    = T(2) * pi;
        static constexpr T half_pi   = pi / T(2);
        static constexpr T quarter_pi= pi / T(4);

        static constexpr T inv_pi    = T(1) / pi;
        static constexpr T inv_two_pi= T(1) / two_pi;

        static constexpr T deg_to_rad= pi / T(180);
        static constexpr T rad_to_deg= T(180) / pi;

        static constexpr T epsilon   = T(1e-6);
    };

    inline constexpr float  pi_f  = constants<float>::pi;
    inline constexpr double pi_d  = constants<double>::pi;

    inline constexpr float  two_pi_f = constants<float>::two_pi;
}

#endif // FOXRASTERIZER_VEC_H
