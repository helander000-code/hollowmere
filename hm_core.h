// HOLLOWMERE - core math, noise and procedural mesh builder.
#pragma once
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>

static const float PI = 3.14159265f;

struct V3 { float x, y, z; };
static inline V3 operator+(V3 a, V3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
static inline V3 operator-(V3 a, V3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
static inline V3 operator*(V3 a, float k) { return { a.x * k, a.y * k, a.z * k }; }
static inline V3 operator-(V3 a) { return { -a.x, -a.y, -a.z }; }
static inline float dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline V3 cross(V3 a, V3 b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
static inline float len(V3 a) { return sqrtf(dot(a, a)); }
static inline V3 norm(V3 a) { float l = len(a); return l > 1e-6f ? a * (1.0f / l) : V3{ 0, 1, 0 }; }
static inline V3 lerp3(V3 a, V3 b, float t) { return a + (b - a) * t; }

struct Col { float r, g, b; };
static inline Col C(int r, int g, int b) { return { r / 255.0f, g / 255.0f, b / 255.0f }; }
static inline float clampf(float v, float a, float b) { return v < a ? a : (v > b ? b : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float sstep(float a, float b, float x) { float t = clampf((x - a) / (b - a), 0, 1); return t * t * (3 - 2 * t); }
static inline Col mixc(Col a, Col b, float t) { return { lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t) }; }
static inline Col scalec(Col c, float k) { return { c.r * k, c.g * k, c.b * k }; }

// ------------------------------------------------------------ random + noise
static uint32_t rngS = 2463534242u;
static inline uint32_t rnd() { rngS ^= rngS << 13; rngS ^= rngS >> 17; rngS ^= rngS << 5; return rngS; }
static inline float frand() { return (rnd() & 0xFFFFFF) / 16777216.0f; }
static inline float frr(float a, float b) { return a + (b - a) * frand(); }
static inline float hash2(int x, int y) {
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}
static float vnoise(float x, float y) {
    int xi = (int)floorf(x), yi = (int)floorf(y);
    float fx = x - xi, fy = y - yi;
    fx = fx * fx * (3 - 2 * fx); fy = fy * fy * (3 - 2 * fy);
    return lerpf(lerpf(hash2(xi, yi), hash2(xi + 1, yi), fx), lerpf(hash2(xi, yi + 1), hash2(xi + 1, yi + 1), fx), fy);
}
static float fbm2(float x, float y, int oct = 4) {
    float a = 0.5f, s = 0;
    for (int i = 0; i < oct; i++) { s += a * vnoise(x, y); x = x * 2.03f + 17.1f; y = y * 2.03f - 9.7f; a *= 0.5f; }
    return s;
}

// ------------------------------------------------------------ vertex + mesh
struct Vtx { float p[3]; uint8_t n[4]; uint8_t c[4]; float uv[2]; };   // 24 bytes
struct Mesh { std::vector<Vtx> v; };
static Mesh* M = nullptr;                                              // where the builders write

// materials (stored in normal.w)
enum Mat { MAT_PLAIN = 0, MAT_GROUND = 1, MAT_BARK = 2, MAT_WOOD = 3, MAT_PAPER = 4, MAT_EMIT = 5, MAT_LEAF = 6,
           MAT_STONE = 7, MAT_FABRIC = 8, MAT_SKIN = 9, MAT_SIGIL = 10, MAT_GLASS = 11, MAT_METAL = 12, MAT_TENT = 13,
           MAT_ROOF = 14, MAT_LIGHT = 15 };

// current transform: yaw about Y, then translate
static float xo = 0, yo = 0, zo = 0, xc = 1, xs = 0;
static void xfSet(float x, float y, float z, float yaw) { xo = x; yo = y; zo = z; xc = cosf(yaw); xs = sinf(yaw); }
static void xfId() { xfSet(0, 0, 0, 0); }
static inline V3 xfP(V3 p) { return { xc * p.x + xs * p.z + xo, p.y + yo, -xs * p.x + xc * p.z + zo }; }
static inline V3 xfN(V3 n) { return { xc * n.x + xs * n.z, n.y, -xs * n.x + xc * n.z }; }
static inline uint8_t pk(float v) { return (uint8_t)(clampf(v * 0.5f + 0.5f, 0, 1) * 255.0f + 0.5f); }
static inline uint8_t pu(float v) { return (uint8_t)(clampf(v, 0, 1) * 255.0f + 0.5f); }

static void vtx(V3 p, V3 n, Col c, int mat, float a, float u, float v) {
    V3 q = xfP(p), m = xfN(n);
    Vtx o;
    o.p[0] = q.x; o.p[1] = q.y; o.p[2] = q.z;
    o.n[0] = pk(m.x); o.n[1] = pk(m.y); o.n[2] = pk(m.z); o.n[3] = (uint8_t)mat;
    o.c[0] = pu(c.r); o.c[1] = pu(c.g); o.c[2] = pu(c.b); o.c[3] = pu(a);
    o.uv[0] = u; o.uv[1] = v;
    M->v.push_back(o);
}
static void quadv(V3 a, V3 b, V3 c, V3 d, V3 n, Col col, int mat, float al, float u0, float v0, float u1, float v1) {
    vtx(a, n, col, mat, al, u0, v0); vtx(b, n, col, mat, al, u1, v0); vtx(c, n, col, mat, al, u1, v1);
    vtx(a, n, col, mat, al, u0, v0); vtx(c, n, col, mat, al, u1, v1); vtx(d, n, col, mat, al, u0, v1);
}
// quad with the normal worked out from the corners
static void quadAuto(V3 a, V3 b, V3 c, V3 d, Col col, int mat, float al = 1, float us = 1) {
    V3 n = norm(cross(b - a, d - a));
    quadv(a, b, c, d, n, col, mat, al, 0, 0, len(b - a) * us, len(d - a) * us);
}
// box spanned by three edge vectors from corner o; texture coordinates are metres along the edges
static void boxAxes(V3 o, V3 ex, V3 ey, V3 ez, Col col, int mat, float al = 1, float us = 1) {
    V3 E[3] = { ex, ey, ez };
    V3 ctr = o + (ex + ey + ez) * 0.5f;
    for (int k = 0; k < 3; k++) {
        int i = (k + 1) % 3, j = (k + 2) % 3;
        for (int s = 0; s < 2; s++) {
            V3 base = o + (s ? E[k] : V3{ 0, 0, 0 });
            V3 p0 = base, p1 = base + E[i], p2 = base + E[i] + E[j], p3 = base + E[j];
            V3 n = norm(cross(E[i], E[j]));
            V3 fc = base + (E[i] + E[j]) * 0.5f;
            if (dot(n, fc - ctr) < 0) n = -n;
            V3 ni = norm(E[i]), nj = norm(E[j]);
            auto uv = [&](V3 p, float& u, float& v) { u = dot(p, ni) * us; v = dot(p, nj) * us; };
            float u[4], v[4];
            uv(p0, u[0], v[0]); uv(p1, u[1], v[1]); uv(p2, u[2], v[2]); uv(p3, u[3], v[3]);
            vtx(p0, n, col, mat, al, u[0], v[0]); vtx(p1, n, col, mat, al, u[1], v[1]); vtx(p2, n, col, mat, al, u[2], v[2]);
            vtx(p0, n, col, mat, al, u[0], v[0]); vtx(p2, n, col, mat, al, u[2], v[2]); vtx(p3, n, col, mat, al, u[3], v[3]);
        }
    }
}
static void box(V3 mn, V3 mx, Col c, int mat, float al = 1, float us = 1) {
    boxAxes(mn, { mx.x - mn.x, 0, 0 }, { 0, mx.y - mn.y, 0 }, { 0, 0, mx.z - mn.z }, c, mat, al, us);
}
static void boxC(V3 ctr, V3 half, Col c, int mat, float al = 1, float us = 1) { box(ctr - half, ctr + half, c, mat, al, us); }

// tapered tube along a polyline
static void tube(const V3* p, const float* r, int n, int sides, Col col, int mat, float al = 1, float vs = 1) {
    std::vector<V3> ring((size_t)n * (sides + 1)), nr((size_t)n * (sides + 1));
    float acc = 0;
    std::vector<float> vv(n);
    for (int i = 0; i < n; i++) {
        V3 t = norm(p[std::min(i + 1, n - 1)] - p[std::max(i - 1, 0)]);
        V3 ref = fabsf(t.y) > 0.9f ? V3{ 1, 0, 0 } : V3{ 0, 1, 0 };
        V3 a = norm(cross(t, ref)), b = cross(t, a);
        if (i) acc += len(p[i] - p[i - 1]);
        vv[i] = acc * vs;
        for (int s = 0; s <= sides; s++) {
            float ang = s * 2 * PI / sides;
            V3 d = a * cosf(ang) + b * sinf(ang);
            ring[(size_t)i * (sides + 1) + s] = p[i] + d * r[i];
            nr[(size_t)i * (sides + 1) + s] = d;
        }
    }
    for (int i = 0; i + 1 < n; i++) for (int s = 0; s < sides; s++) {
        size_t a0 = (size_t)i * (sides + 1) + s, a1 = a0 + 1, b0 = a0 + (sides + 1), b1 = b0 + 1;
        float u0 = (float)s, u1 = (float)(s + 1);
        vtx(ring[a0], nr[a0], col, mat, al, u0, vv[i]); vtx(ring[b0], nr[b0], col, mat, al, u0, vv[i + 1]); vtx(ring[b1], nr[b1], col, mat, al, u1, vv[i + 1]);
        vtx(ring[a0], nr[a0], col, mat, al, u0, vv[i]); vtx(ring[b1], nr[b1], col, mat, al, u1, vv[i + 1]); vtx(ring[a1], nr[a1], col, mat, al, u1, vv[i]);
    }
}
static void tube2(V3 a, V3 b, float ra, float rb, int sides, Col col, int mat, float al = 1) {
    V3 p[2] = { a, b }; float r[2] = { ra, rb };
    tube(p, r, 2, sides, col, mat, al, 1);
}
static void ellipsoid(V3 c, V3 rad, Col col, int mat, float al = 1, int ST = 8, int SL = 12) {
    auto pt = [&](int i, int j, V3& n) {
        float th = i * PI / ST, ph = j * 2 * PI / SL;
        n = { sinf(th) * cosf(ph), cosf(th), sinf(th) * sinf(ph) };
        return V3{ c.x + n.x * rad.x, c.y + n.y * rad.y, c.z + n.z * rad.z };
    };
    for (int i = 0; i < ST; i++) for (int j = 0; j < SL; j++) {
        V3 n00, n01, n10, n11;
        V3 p00 = pt(i, j, n00), p01 = pt(i, j + 1, n01), p10 = pt(i + 1, j, n10), p11 = pt(i + 1, j + 1, n11);
        float u0 = j / (float)SL * 4, u1 = (j + 1) / (float)SL * 4, v0 = i / (float)ST * 2, v1 = (i + 1) / (float)ST * 2;
        vtx(p00, n00, col, mat, al, u0, v0); vtx(p10, n10, col, mat, al, u0, v1); vtx(p11, n11, col, mat, al, u1, v1);
        vtx(p00, n00, col, mat, al, u0, v0); vtx(p11, n11, col, mat, al, u1, v1); vtx(p01, n01, col, mat, al, u1, v0);
    }
}
// two crossed leaf cards (type goes in alpha: 0.2 broadleaf, 0.5 fern, 0.8 pine)
static void cards(V3 c, float w, float h, float yaw, int n, Col col, float type, V3 nrm = { 0, 1, 0 }) {
    for (int i = 0; i < n; i++) {
        float a = yaw + i * PI / n;
        V3 d = { cosf(a) * w * 0.5f, 0, sinf(a) * w * 0.5f };
        V3 up = { 0, h, 0 };
        quadv(c - d, c + d, c + d + up, c - d + up, nrm, col, MAT_LEAF, type, 0, 0, 1, 1);
    }
}
