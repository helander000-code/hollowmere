// HOLLOWMERE - HLSL (shader model 5). Compiled at start-up with D3DCompile.
#pragma once

struct FrameCB {
    float viewProj[16];
    float lightVP[16];
    float camPos[4];     // xyz, time
    float camFwd[4];     // xyz, near plane
    float camRight[4];   // xyz, tan(half fov) * aspect
    float camUp[4];      // xyz, tan(half fov)
    float flashPos[4];   // xyz, intensity
    float flashDir[4];   // xyz, cos of the outer cone
    float params[4];     // fog density, interior, moon strength, ambient
    float fogCol[4];     // rgb, far plane
    float post0[4];      // night vision, glitch, vignette, grain
    float post1[4];      // fade, battery flicker, window glow, exposure
    float screen[4];     // w, h, 1/w, 1/h
    float lzn[4];        // light near, light far, shadow texel, unused
    float pl[8][4];      // lamp position, radius
    float pc[8][4];      // lamp colour * intensity
};

static const char* HLSL_COMMON = R"HLSL(
cbuffer Frame : register(b0) {
    row_major float4x4 viewProj;
    row_major float4x4 lightVP;
    float4 camPos, camFwd, camRight, camUp, flashPos, flashDir, params, fogCol, post0, post1, screen, lzn;
    float4 pl[8];
    float4 pc[8];
};
Texture2D fontTex : register(t0);
Texture2D sceneTex : register(t1);
Texture2D<float> shadowTex : register(t2);
Texture2D<float> depthTex : register(t3);
Texture2DArray<float4> texD : register(t4);      // photoscanned albedo (sRGB), one slice per material
Texture2DArray<float4> texN : register(t5);      // xy = normal (GL), z = roughness
SamplerState lin : register(s0);
SamplerState pnt : register(s1);
SamplerState aniso : register(s2);

// triplanar projection of a scanned material with whiteout normal blending
void triplanar(int s, float3 p, float3 N, float tile, out float3 alb, out float3 Nw, out float rough) {
    float3 w = pow(abs(N), 4.0); w /= (w.x + w.y + w.z);
    float3 sg = step(0.0, N) * 2 - 1;
    float2 ux = p.zy / tile, uy = p.xz / tile, uz = p.xy / tile;
    float3 dx = texD.Sample(aniso, float3(ux, s)).rgb, dy = texD.Sample(aniso, float3(uy, s)).rgb, dz = texD.Sample(aniso, float3(uz, s)).rgb;
    float4 nx = texN.Sample(aniso, float3(ux, s)), ny = texN.Sample(aniso, float3(uy, s)), nz = texN.Sample(aniso, float3(uz, s));
    alb = dx * w.x + dy * w.y + dz * w.z;
    rough = nx.b * w.x + ny.b * w.y + nz.b * w.z;
    float3 tx = float3(nx.xy * 2 - 1, 0), ty = float3(ny.xy * 2 - 1, 0), tz = float3(nz.xy * 2 - 1, 0);
    tx.z = sqrt(saturate(1 - dot(tx.xy, tx.xy))); ty.z = sqrt(saturate(1 - dot(ty.xy, ty.xy))); tz.z = sqrt(saturate(1 - dot(tz.xy, tz.xy)));
    tx.x *= sg.x; ty.x *= sg.y; tz.x *= -sg.z;
    tx = float3(tx.xy + N.zy, abs(tx.z) * N.x);
    ty = float3(ty.xy + N.xz, abs(ty.z) * N.y);
    tz = float3(tz.xy + N.xy, abs(tz.z) * N.z);
    Nw = normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
}

struct VIn  { float3 pos : POSITION; float4 nrm : NORMAL; float4 col : COLOR; float2 uv : TEXCOORD; };
struct VOut { float4 sv : SV_Position; float3 wpos : TEXCOORD0; float3 nrm : TEXCOORD1; float4 col : COLOR; float2 uv : TEXCOORD2; float mat : TEXCOORD3; };

float hash(float2 p) { p = frac(p * float2(123.34, 456.21)); p += dot(p, p + 45.32); return frac(p.x * p.y); }
float vn(float2 p) {
    float2 i = floor(p), f = frac(p); f = f * f * (3 - 2 * f);
    return lerp(lerp(hash(i), hash(i + float2(1, 0)), f.x), lerp(hash(i + float2(0, 1)), hash(i + float2(1, 1)), f.x), f.y);
}
float fbm(float2 p) { float a = 0.5, s = 0; [unroll] for (int i = 0; i < 4; i++) { s += a * vn(p); p = p * 2.03 + float2(17.1, -9.7); a *= 0.5; } return s; }

static const float3 MOON = float3(-0.35, 0.8, 0.45);

// cellular leaf shapes: returns coverage, and writes a per-leaf random in id
float leafCells(float2 q, float s, out float id) {
    float2 p = q * s, i = floor(p), f = frac(p) - 0.5;
    float h = hash(i); id = hash(i + 7.3);
    float a = h * 6.2831; float2 r = float2(cos(a) * f.x + sin(a) * f.y, -sin(a) * f.x + cos(a) * f.y);
    float d = length(r * float2(1.0, 2.0));
    float vein = abs(r.y) < 0.012 ? 0.0 : 1.0;
    return (1 - smoothstep(0.2, 0.34, d)) * (h < 0.8 ? 1.0 : 0.0) * (0.75 + 0.25 * vein);
}
float linLight(float d) { float zn = lzn.x, zf = lzn.y; return zn * zf / (zf - d * (zf - zn)); }

static const float2 POIS[8] = { float2(-0.613, 0.617), float2(0.170, -0.040), float2(-0.299, -0.793), float2(0.645, 0.493),
                                float2(-0.651, 0.717 * -0.4), float2(0.421, -0.771), float2(0.034, 0.915), float2(-0.929, -0.109) };

float flashShadow(float3 wp, float3 N, int taps) {
    float4 lp = mul(float4(wp + N * 0.04, 1), lightVP);
    if (lp.w < 0.05) return 1;
    float3 nd = lp.xyz / lp.w;
    float2 uv = nd.xy * float2(0.5, -0.5) + 0.5;
    if (uv.x < 0 || uv.x > 1 || uv.y < 0 || uv.y > 1) return 1;
    float recv = lp.w, s = 0;
    float ang = hash(wp.xz * 13.7 + wp.y * 7.3) * 6.2831;
    float2 rot = float2(cos(ang), sin(ang));
    float rad = (2.8 + recv * 0.09) * lzn.z;
    float bias = 0.06 + recv * 0.012;
    for (int i = 0; i < taps; i++) {
        float2 o = POIS[i] * rad; o = float2(o.x * rot.x - o.y * rot.y, o.x * rot.y + o.y * rot.x);
        float lin_ = linLight(shadowTex.SampleLevel(pnt, uv + o, 0));
        s += (recv - bias > lin_) ? 0.0 : 1.0;
    }
    return s / taps;
}
// brightness of the torch cone at a point (no distance falloff)
float spotCone(float3 p) {
    float3 d = p - flashPos.xyz; float l = max(length(d), 0.001);
    float c = dot(d / l, flashDir.xyz);
    return smoothstep(flashDir.w - 0.035, flashDir.w + 0.075, c) * (0.4 + 0.6 * smoothstep(flashDir.w - 0.02, 0.996, c));
}

VOut VS_scene(VIn v) {
    VOut o;
    o.sv = mul(float4(v.pos, 1), viewProj);
    o.wpos = v.pos; o.nrm = v.nrm.xyz * 2 - 1; o.col = v.col; o.uv = v.uv; o.mat = v.nrm.w * 255.0;
    return o;
}
float4 VS_shadow(VIn v) : SV_Position { return mul(float4(v.pos, 1), lightVP); }

struct FS { float4 sv : SV_Position; float2 uv : TEXCOORD0; };
FS VS_full(uint id : SV_VertexID) {
    FS o; float2 p = float2((id << 1) & 2, id & 2);
    o.sv = float4(p * 2 - 1, 0, 1); o.uv = float2(p.x, 1 - p.y); return o;
}
float3 rayDir(float2 uv) {
    float2 n = float2(uv.x * 2 - 1, 1 - uv.y * 2);
    return camFwd.xyz + camRight.xyz * (n.x * camRight.w) + camUp.xyz * (n.y * camUp.w);
}
)HLSL";

static const char* HLSL_SCENE = R"HLSL(
float4 PS_scene(VOut i) : SV_Target {
    int mat = (int)(i.mat + 0.5);
    float3 wp = i.wpos;
    float3 Ng = normalize(i.nrm);
    float3 V = camPos.xyz - wp; float dist = length(V); V /= dist;
    if (dot(Ng, V) < 0) Ng = -Ng;
    float3 N = Ng;
    float3 alb = i.col.rgb;
    float h = 0, bump = 0, spec = 0.03, rough = 0.8, emit = 0, a = i.col.a;
    float2 uv = i.uv;
    float along = abs(Ng.x) > abs(Ng.z) ? wp.z : wp.x;
    float hy = wp.y > 3.45 ? wp.y - 3.6 : wp.y;

    // which scanned material (if any) this surface uses, and its tile size in metres
    int slot = -1; float tile = 1.0;
    if (mat == 1) { slot = 0; tile = 3.0; }
    else if (mat == 2) { slot = 1; tile = 1.3; }
    else if (mat == 3) { if (a < 0.2) { slot = 2; tile = 1.8; } else if (a < 0.6) { slot = 3; tile = 1.6; } else { slot = 5; tile = 0.9; } }
    else if (mat == 4) { slot = 4; tile = 1.6; }
    else if (mat == 7) { slot = 6; tile = 1.6; }
    else if (mat == 14) { slot = 7; tile = 1.6; }
    else if (mat == 12) { slot = 8; tile = 1.2; }
    else if (mat == 8) { slot = 9; tile = 1.0; }
    float3 tA, tN; float tR;
    triplanar(max(slot, 0), wp, Ng, tile, tA, tN, tR);

    if (mat == 1) {                                                   // ground: mud, leaf litter, moss, trail
        float2 q = wp.xz;
        float n1 = fbm(q * 0.8), n2 = fbm(q * 3.3 + 7), n3 = vn(q * 15.0);
        float leaf = smoothstep(0.3, 0.7, fbm(q * 1.7 + 3.0)) * (1 - a * 0.7);
        float lid1, lid2;
        float lc1 = leafCells(q, 7.0, lid1), lc2 = leafCells(q * 1.37 + 4.1, 11.0, lid2);
        float3 lcol1 = lerp(float3(0.30, 0.12, 0.03), float3(0.12, 0.07, 0.02), lid1) * (lid1 > 0.85 ? 1.8 : 1.0);
        float3 lcol2 = lerp(float3(0.20, 0.10, 0.03), float3(0.07, 0.05, 0.02), lid2);
        float3 leaves = lerp(float3(0.11, 0.07, 0.03), lcol2, lc2);
        leaves = lerp(leaves, lcol1, lc1);
        float twig = smoothstep(0.93, 0.97, vn(q * float2(3.0, 40.0) + n1 * 3)) * 0.5;
        alb = lerp(alb * (0.7 + 0.6 * n2), leaves * (0.7 + 0.6 * n3), leaf);
        alb = lerp(alb, float3(0.05, 0.03, 0.02), twig * leaf);
        alb = lerp(alb, float3(0.03, 0.07, 0.025), smoothstep(0.62, 0.82, n1) * 0.7);
        float wet = smoothstep(0.4, 0.7, fbm(q * 0.3 + 11.0));
        h = n2 * 0.5 + n3 * 0.3 + leaf * (0.25 + lc1 * 0.5 + lc2 * 0.3); bump = 2.0; spec = 0.05 + wet * 0.25; rough = lerp(0.8, 0.35, wet);
    } else if (mat == 2) {                                            // bark
        float streak = fbm(float2(uv.x * 2.2, uv.y * 0.6));
        float ridge = 1 - abs(2 * fbm(float2(uv.x * 3.5 + streak * 2, uv.y * 0.35)) - 1);
        float fine = vn(uv * float2(22, 5));
        float crev = smoothstep(0.3, 0.62, streak) * (0.5 + 0.5 * ridge);
        alb *= (0.3 + 1.2 * crev) * (0.8 + 0.4 * fine);
        float moss = smoothstep(0.45, 0.7, fbm(wp.xz * 0.6 + wp.y * 0.25)) * (1 - smoothstep(0.5, 3.5, wp.y));
        alb = lerp(alb, float3(0.035, 0.08, 0.028) * (0.6 + fine), moss * 0.7);
        float lichen = smoothstep(0.62, 0.72, fbm(wp.xz * 2.1 + wp.y * 0.9 + 3));
        alb = lerp(alb, float3(0.16, 0.18, 0.14), lichen * 0.5);
        float ao = lerp(0.35, 1.0, smoothstep(0.0, 1.2, wp.y));    // dirt and shadow where the trunk meets the ground
        alb *= ao;
        h = streak * 1.4 + ridge * 1.0 + fine * 0.3; bump = 3.0; spec = 0.04 + lichen * 0.0;
    } else if (mat == 3) {                                            // wood
        if (a < 0.2) {                                                // floor boards
            float2 q = wp.xz; float row = floor(q.y * 5.0), bx = q.x * 0.85 + hash(float2(row, 1)) * 9;
            float seam = max(step(0.465, abs(frac(q.y * 5.0) - 0.5)), step(0.985, frac(bx)));
            float grain = fbm(float2(q.x * 2.0 + row * 3.7, q.y * 28.0));
            float wear = fbm(q * 0.7);
            alb = alb * (0.45 + 1.1 * grain) * (0.55 + 0.9 * wear) * (1 - 0.8 * seam);
            h = grain * 0.4 - seam * 0.8; bump = 0.8; spec = 0.08; rough = 0.55;
        } else if (a < 0.6) {                                         // weathered siding / painted boards
            float board = frac(wp.y * 5.0);
            float edge = smoothstep(0.0, 0.06, board) * (1 - smoothstep(0.94, 1.0, board));
            float grain = fbm(float2(along * 2.5, wp.y * 38.0));
            float streak = vn(float2(along * 7.0, wp.y * 0.4));
            float peel = smoothstep(0.55, 0.7, fbm(float2(along, wp.y) * 1.3 + 5));
            float3 bare = float3(0.12, 0.10, 0.085);
            alb = lerp(alb * (0.55 + 0.7 * grain), bare * (0.6 + grain), peel);
            alb *= 0.45 + 0.7 * streak;
            alb *= 0.25 + 0.75 * edge;
            alb = lerp(alb, float3(0.02, 0.05, 0.02), smoothstep(0.5, 0.7, fbm(float2(along, wp.y) * 0.5 + 9)) * (1 - smoothstep(0, 2.5, wp.y)) * 0.5);
            h = grain * 0.5 + edge * 0.9; bump = 1.1; spec = 0.04;
        } else {                                                      // furniture, trim
            float grain = fbm(float2((wp.x + wp.z) * 3.0, wp.y * 26.0 + (wp.x - wp.z) * 0.6));
            alb *= 0.6 + 0.9 * grain; h = grain * 0.3; bump = 0.5; spec = 0.12; rough = 0.45;
        }
    } else if (mat == 4) {                                            // wallpaper, panelled below, stained
        float3 wpap = alb;
        float stripe = 0.78 + 0.22 * sin(along * 24.0);
        float2 g = float2(along * 5.0, hy * 5.0);
        float damask = smoothstep(0.35, 0.5, vn(g + floor(g.y) * 3.0)) * 0.18;
        float stain = fbm(float2(along * 0.8, hy * 0.6) + 11);
        float drip = smoothstep(0.45, 0.8, vn(float2(along * 5.0, 1.0))) * smoothstep(1.0, 3.2, hy);
        wpap = wpap * stripe * (1 - damask) * (0.45 + 1.1 * stain) * (1 - 0.5 * drip);
        float mold = smoothstep(0.45, 0.75, fbm(float2(along, hy) * 1.6 + 3)) * (1 - smoothstep(0.1, 1.4, hy));
        wpap = lerp(wpap, float3(0.02, 0.03, 0.02), mold * 0.7);
        float peel = smoothstep(0.62, 0.75, fbm(float2(along, hy) * 2.1 + 40)) * smoothstep(0.5, 2.0, hy);
        wpap = lerp(wpap, float3(0.12, 0.11, 0.09), peel);
        float pan = step(hy, 1.0);                                    // wainscot
        float grain = fbm(float2(along * 30.0, hy * 3.0));
        float3 wood = float3(0.07, 0.05, 0.035) * (0.5 + grain) * (0.8 + 0.2 * step(0.04, abs(frac(along * 1.6) - 0.5)));
        alb = lerp(wpap, wood, pan);
        h = stripe * 0.15 + peel * -0.4 + pan * 0.2; bump = 0.5; spec = 0.02;
    } else if (mat == 5) {                                            // emissive: window glow, flames, embers
        float fl = 0.8 + 0.4 * vn(float2(camPos.w * 9.0, wp.x * 3.1 + wp.z * 2.3));
        emit = (a < 0.5) ? 3.0 * fl : 3.5 * post1.z;
        alb = i.col.rgb;
    } else if (mat == 6) {                                            // foliage cards, alpha cut by noise
        float2 q = uv; float m = 0;
        if (a < 0.35) {                                               // broadleaf cluster of individual leaves
            float2 c = q - 0.5; float id1, id2;
            float l1 = leafCells(q + hash(wp.xz) * 3.0, 9.0, id1), l2 = leafCells(q * 1.3 + 2.1 + hash(wp.zx) * 3.0, 13.0, id2);
            float cov = max(l1, l2 * 0.95);
            m = cov - 0.45 + (0.5 - length(c)) * 0.9;
            float id = l1 >= l2 * 0.95 ? id1 : id2;
            float3 tint = lerp(float3(0.8, 1.0, 0.7), float3(1.3, 0.8, 0.4), id);
            alb *= tint * (0.45 + 0.9 * id);
            h = cov;
        } else if (a < 0.65) {                                        // fern fronds
            float x = abs(q.x - 0.5), y = q.y;
            float serr = 0.5 + 0.5 * sin(y * 70.0);
            float w = 0.5 * pow(saturate(1 - y), 0.6) * (0.3 + 0.7 * serr) * smoothstep(0.0, 0.1, y);
            m = (w - x) * 8.0 + (vn(q * 30) - 0.5) * 0.3;
            alb *= 0.6 + 0.8 * y;
        } else {                                                      // pine needles
            float f = fbm(q * float2(18, 6) + wp.xz);
            m = f - 0.36 - (1 - q.y) * 0.15 + (0.5 - abs(q.x - 0.5)) * 0.35;
            alb *= 0.6 + 0.8 * vn(q * 25.0);
        }
        clip(m);
        N = normalize(float3(0, 1, 0) * 0.6 + Ng * 0.4);
        spec = 0.0; h = 0;
    } else if (mat == 7) {                                            // stone
        float n = fbm(wp.xz * 3.1 + wp.y * 2.3), n2 = vn(wp.xz * 16.0 + wp.y * 12.0);
        float blocks = step(0.03, abs(frac(wp.y * 3.0) - 0.5)) * step(0.03, abs(frac(along * 1.7 + floor(wp.y * 3.0) * 0.5) - 0.5));
        alb *= (0.45 + 0.9 * n) * (0.75 + 0.5 * n2) * (0.35 + 0.65 * blocks);
        alb = lerp(alb, float3(0.03, 0.07, 0.025), smoothstep(0.55, 0.75, fbm(wp.xz * 1.1 + wp.y * 0.5)) * 0.55);
        h = n * 0.8 + n2 * 0.3 + blocks * 0.4; bump = 1.8; spec = 0.03;
    } else if (mat == 8) {                                            // fabric
        float w = vn(wp.xz * 90.0 + wp.y * 90.0) * 0.4 + vn(wp.xz * 22.0 + wp.y * 22.0) * 0.6;
        float st = fbm(wp.xz * 1.5 + wp.y * 2.0);
        alb *= (0.5 + 0.9 * w) * (0.4 + 1.0 * st);
        h = w * 0.3; bump = 0.4; spec = 0.0;
    } else if (mat == 9) {                                            // the entity's skin
        float dis = hash(wp.xy * 40.0 + wp.z * 17.0);
        clip(a * 1.2 - dis * 0.6 - 0.3 + (a > 0.99 ? 1 : 0));
        float v = fbm(wp.xy * 9.0 + wp.z * 7.0);
        alb = alb * (0.6 + 0.8 * v);
        float vein = smoothstep(0.48, 0.5, vn(wp.xy * 25.0 + wp.z * 20.0)) * 0.25;
        alb *= 1 - vein; h = v * 0.5; bump = 0.7; spec = 0.2; rough = 0.35;
    } else if (mat == 10) {                                           // painted sigil
        float2 p = uv; float r = length(p);
        float m = step(abs(r - 0.88), 0.045) + step(abs(r - 0.58), 0.03);
        for (int k = 0; k < 5; k++) {
            float a0 = 1.5708 + k * 2.5133, a1 = 1.5708 + ((k + 2) % 5) * 2.5133;
            float2 A = float2(cos(a0), sin(a0)) * 0.86, B = float2(cos(a1), sin(a1)) * 0.86;
            float2 ab = B - A; float t = saturate(dot(p - A, ab) / dot(ab, ab));
            m += step(length(p - A - ab * t), 0.035);
        }
        float ang = atan2(p.y, p.x);
        m += step(abs(r - 0.73), 0.1) * step(frac(ang * 1.9099 + 0.5), 0.12);
        float eye = step(r, 0.17) * (1 - step(abs(p.y) * 3.0, abs(p.x) * 0.9 - 0.05 + 0.2) * 0);
        m += eye * step(0.06, abs(p.y) + abs(p.x) * 0.2);
        m *= 0.55 + 0.8 * fbm(p * 9.0);
        clip(m - 0.5);
        alb = i.col.rgb * (0.6 + 0.6 * fbm(p * 14.0)); spec = 0.35; rough = 0.3; h = 0.5; bump = 0.3;
        if (a < 0.9) emit = 2.2 * (0.8 + 0.4 * sin(camPos.w * 3.0 + wp.x));
    } else if (mat == 11) {                                           // dirty window glass
        float d = fbm(wp.xy * 2.0 + wp.z * 2.0);
        alb = float3(0.004, 0.006, 0.008) + d * 0.01; spec = 0.9; rough = 0.08;
    } else if (mat == 12) {                                           // rusted metal
        float n = fbm(wp.xz * 4.0 + wp.y * 4.0);
        alb = lerp(alb, float3(0.2, 0.08, 0.03), smoothstep(0.5, 0.75, n) * 0.8) * (0.6 + 0.6 * n);
        spec = 0.45; rough = 0.35; h = n * 0.3; bump = 0.4;
    } else if (mat == 13) {                                           // tent nylon
        float w = vn(wp.xz * 60.0 + wp.y * 60.0);
        alb *= 0.6 + 0.6 * w; spec = 0.1;
    } else if (mat == 14) {                                           // shingles
        float2 g = float2(wp.x * 2.4 + floor(wp.y * 3.0 + wp.z * 0.0) * 0.5, wp.y * 3.6 + wp.z * 0.5);
        float2 f = frac(g);
        float edge = smoothstep(0.0, 0.1, f.y) * smoothstep(0.0, 0.05, min(f.x, 1 - f.x));
        float n = fbm(wp.xz * 1.3 + wp.y);
        alb *= (0.5 + 0.9 * n) * (0.25 + 0.75 * edge);
        alb = lerp(alb, float3(0.03, 0.07, 0.03), smoothstep(0.55, 0.72, fbm(wp.xz * 0.6)) * 0.6);
        h = edge * 0.6 + n * 0.3; bump = 1.2; spec = 0.03;
    } else if (mat == 15) { emit = 7.0; }

    // blend in the photoscan: it supplies the real detail, the procedural layer keeps moss, stains, leaf litter and room tints
    if (slot >= 0) {
        float pm = slot == 0 ? 0.45 : slot == 1 ? 0.3 : slot == 2 ? 0.3 : slot == 3 ? 0.4 : slot == 4 ? 0.4 : slot == 5 ? 0.25 : slot == 6 ? 0.3 : slot == 7 ? 0.3 : 0.4;
        float tint = slot == 4 ? 0.75 : (slot == 2 || slot == 3 || slot == 5 || slot == 9) ? 0.5 : 0.2;
        float3 base = tA * 0.8 * lerp(float3(1, 1, 1), saturate(i.col.rgb * 3.4), tint);
        alb = lerp(base, alb, pm);
        N = tN; bump = 0;
        rough = tR; spec = max(spec, 0.03 + (1 - tR) * 0.3);
    }

    // bump from a procedural height field, using screen-space derivatives
    if (bump > 0) {
        float3 dpdx = ddx(wp), dpdy = ddy(wp); float dhx = ddx(h), dhy = ddy(h);
        float3 r1 = cross(dpdy, Ng), r2 = cross(Ng, dpdx); float det = dot(dpdx, r1);
        float3 grad = sign(det) * (dhx * r1 + dhy * r2);
        N = normalize(abs(det) * Ng - grad * bump * 0.02);
    }

    // ---- lighting
    float inside = step(abs(wp.x), 12.0) * step(abs(wp.z), 8.0) * step(wp.y, 7.0);
    float3 L = 0;
    float3 ml = normalize(MOON);
    float canopy = 0.25 + 1.0 * smoothstep(0.42, 0.68, fbm(wp.xz * 0.28 + 4.0));
    float3 moon = float3(0.55, 0.7, 1.0) * params.z * canopy * saturate(dot(N, ml) * 0.8 + 0.2) * (1 - inside);
    float3 amb = float3(0.5, 0.62, 1.0) * params.w * (0.55 + 0.45 * N.y) * (inside > 0.5 ? 0.85 : 1.0);
    L += moon + amb;

    float3 toL = flashPos.xyz - wp; float fd = length(toL); toL /= fd;
    float cone = spotCone(wp);
    float3 spc = 0;
    if (flashPos.w > 0.001 && cone > 0.001) {
        float att = 1.0 / (1.0 + 0.05 * fd + 0.016 * fd * fd);
        float nl = saturate(dot(N, toL));
        float sh = flashShadow(wp, Ng, 8);
        float3 fcol = float3(1.0, 0.93, 0.78) * 5.5 * flashPos.w;
        L += fcol * cone * att * nl * sh;
        float3 Hh = normalize(toL + V);
        spc += fcol * cone * att * sh * pow(saturate(dot(N, Hh)), lerp(8, 120, 1 - rough)) * spec * nl;
    }
    for (int k = 0; k < 8; k++) {
        float3 pd = pl[k].xyz - wp; float d = length(pd);
        float at = saturate(1 - d / max(pl[k].w, 0.01)); at *= at;
        float nl = saturate(dot(N, pd / max(d, 0.001))) * 0.85 + 0.15;
        L += pc[k].rgb * at * nl;
        spc += pc[k].rgb * at * pow(saturate(dot(N, normalize(pd / max(d, 0.001) + V))), 30.0) * spec;
    }
    if (post0.x > 0.5) {                                              // night-vision infrared illuminator
        L += float3(1, 1, 1) * 2.2 * saturate(dot(N, V) * 0.8 + 0.2) / (1 + 0.04 * dist * dist);
    }
    if (mat == 9) {                                                   // cold rim on the entity
        L += float3(0.5, 0.6, 0.8) * pow(1 - saturate(dot(N, V)), 3.0) * 0.12;
    }
    float3 col = alb * L + spc + alb * emit;
    if (mat == 5) col = i.col.rgb * emit;

    // distance fog, a touch lighter for emissive things so lights read from far away
    float fogd = params.x * (1 + 1.2 * exp(-max(wp.y, 0) * 0.35));
    float f = 1 - exp(-pow(fogd * dist, 2.0));
    if (emit > 0) f *= 0.35;
    col = lerp(col, fogCol.rgb, f);
    return float4(col, 1);
}

float4 PS_sky(FS i) : SV_Target {
    float3 d = normalize(rayDir(i.uv));
    float y = saturate(d.y);
    float3 c = lerp(fogCol.rgb * 1.2, float3(0.004, 0.007, 0.016), pow(y, 0.45));
    float2 q = d.xz / (d.y + 0.35) * 1.4 + camPos.w * 0.004;
    float cl = smoothstep(0.35, 0.8, fbm(q));
    float3 ml = normalize(MOON);
    float md = saturate(dot(d, ml));
    float moon = smoothstep(0.9994, 0.9998, md) * 4.0 + pow(md, 60.0) * 0.4 + pow(md, 6.0) * 0.04;
    c += float3(0.5, 0.62, 0.85) * moon * (1 - cl * 0.85) * smoothstep(0, 0.1, d.y);
    c = lerp(c, float3(0.02, 0.026, 0.036) * (0.4 + cl), cl * 0.5 * smoothstep(0, 0.2, d.y));
    return float4(c, 1);
}
)HLSL";

static const char* HLSL_POST = R"HLSL(
float4 PS_post(FS i) : SV_Target {
    float t = camPos.w, g = post0.y;
    float2 uv0 = i.uv, uv = i.uv;
    // tracking glitches
    float wob = sin(uv.y * 620.0 + t * 3.0) * 0.0004 + sin(uv.y * 35.0 + t * 1.7) * 0.0007 * (0.4 + g * 3.0);
    float band = step(0.99 - g * 0.12, hash(float2(floor(uv.y * 60.0), floor(t * 14.0))));
    uv.x += wob + band * (hash(float2(floor(t * 30.0), 3.0)) - 0.5) * 0.14 * (0.25 + g);
    float2 cd = uv - 0.5;
    float ca = (0.0015 + 0.006 * g) * dot(cd, cd) * 4.0 + 0.0004;
    float3 col;
    col.r = sceneTex.SampleLevel(lin, uv - cd * ca * 2.0, 0).r;
    col.g = sceneTex.SampleLevel(lin, uv, 0).g;
    col.b = sceneTex.SampleLevel(lin, uv + cd * ca * 2.0, 0).b;

    // volumetric torch beam: march the view ray through the cone, testing the shadow map
    float dd = depthTex.Load(int3(i.sv.xy, 0));
    float zn = camFwd.w, zf = fogCol.w, A = zf / (zf - zn), B = -zn * zf / (zf - zn);
    float vz = dd >= 0.99999 ? zf : B / (dd - A);
    float3 rd = rayDir(uv0);
    float maxT = min(vz, 30.0);
    float jit = frac(hash(i.sv.xy + frac(t) * 71.0) + 0.0);
    // cheap edge smoothing (FXAA-like)
    {
        float2 px = float2(screen.z, screen.w);
        float3 nN = sceneTex.SampleLevel(lin, uv + float2(0, -px.y), 0).rgb, nS = sceneTex.SampleLevel(lin, uv + float2(0, px.y), 0).rgb;
        float3 nE = sceneTex.SampleLevel(lin, uv + float2(px.x, 0), 0).rgb, nW = sceneTex.SampleLevel(lin, uv + float2(-px.x, 0), 0).rgb;
        float4 L4 = float4(dot(nN, 0.33), dot(nS, 0.33), dot(nE, 0.33), dot(nW, 0.33)); L4 = L4 / (1 + L4);
        float lc = dot(col, 0.33); lc = lc / (1 + lc);
        float edge = saturate(max(max(abs(lc - L4.x), abs(lc - L4.y)), max(abs(lc - L4.z), abs(lc - L4.w))) * 7.0);
        col = lerp(col, (nN + nS + nE + nW) * 0.25 * 0.6 + col * 0.4, edge * 0.7);
    }
    // screen-space ambient occlusion from the depth buffer
    if (dd < 0.99999) {
        float rpx = clamp(0.7 * (screen.y * 0.5 / camUp.w) / max(vz, 0.3), 2.0, 36.0);
        float a0 = hash(i.sv.xy) * 6.2831, so = 0;
        [unroll] for (int k = 0; k < 10; k++) {
            float a = a0 + k * 2.399, r = rpx * (0.25 + 0.75 * frac(k * 0.618 + 0.1));
            int2 pp = clamp(int2(i.sv.xy + float2(cos(a), sin(a)) * r), int2(0, 0), int2(screen.xy) - 1);
            float d2 = depthTex.Load(int3(pp, 0));
            float z2 = d2 >= 0.99999 ? zf : B / (d2 - A);
            float df = vz - z2;
            so += (df > 0.04 && df < 1.6) ? (1 - df / 1.6) : 0.0;
        }
        col *= lerp(1.0, 1 - so / 10.0 * 0.85, saturate(1 - vz / 60.0));
    }
    // low-lying moonlit mist that drifts between the trunks
    {
        float mist = 0, mistLit = 0, mt = min(vz, 50.0), mdt = mt / 14.0;
        for (int k = 0; k < 14; k++) {
            float3 p = camPos.xyz + rd * ((k + jit) * mdt);
            float n = fbm(p.xz * 0.05 + float2(t * 0.010, t * 0.006));
            float dens = exp(-max(p.y, 0.0) * 0.5) * smoothstep(0.3, 0.75, n) * (1 - params.y * 0.9);
            mist += dens * mdt;
            if (flashPos.w > 0.001) {
                float cn = spotCone(p);
                if (cn > 0.001) { float fl = length(p - flashPos.xyz); mistLit += dens * mdt * cn / (1 + 0.06 * fl + 0.02 * fl * fl); }
            }
        }
        float tr = exp(-mist * 0.08);
        col = col * tr + float3(0.035, 0.05, 0.075) * (1 - tr) + float3(1.0, 0.93, 0.78) * mistLit * 0.045 * flashPos.w;
    }
    float acc = 0;
    if (flashPos.w > 0.001) {
        const int NS = 24;
        float dt = maxT / NS;
        for (int k = 0; k < NS; k++) {
            float tt = (k + jit) * dt;
            float3 p = camPos.xyz + rd * tt;
            float cn = spotCone(p);
            if (cn > 0.001) {
                float fl = length(p - flashPos.xyz);
                float at = 1.0 / (1.0 + 0.06 * fl + 0.02 * fl * fl);
                float sh = flashShadow(p, float3(0, 0, 0), 1);
                acc += cn * at * sh * dt;
            }
        }
        float dens = 0.045 * (params.x / 0.04);
        col += float3(1.0, 0.93, 0.78) * acc * dens * 0.55 * flashPos.w;
    }
    // lamps scatter a little too
    for (int l = 0; l < 4; l++) {
        float3 pd = pl[l].xyz - camPos.xyz; float pdl = length(pd);
        float3 dirn = pd / max(pdl, 0.001);
        float al = saturate(dot(normalize(rd), dirn));
        float glow = pow(al, 120.0) * saturate(1 - pdl / (pl[l].w * 2.0)) * (pdl < vz ? 1 : 0);
        col += pc[l].rgb * glow * 0.35;
    }

    col *= post1.w;
    // glow on bright pixels
    float3 bl = 0; float rad = 7.0 * screen.z * 2.0;
    [unroll] for (int b = 0; b < 8; b++) {
        float a = b * 0.785398 + t;
        float2 o = float2(cos(a), sin(a)) * rad * (1 + (b & 1));
        bl += max(sceneTex.SampleLevel(lin, uv + o, 0).rgb * post1.w - 1.2, 0);
    }
    col += bl / 8.0 * 0.5;

    float lum0 = dot(col, float3(0.299, 0.587, 0.114));
    // filmic curve
    col = (col * (2.51 * col + 0.03)) / (col * (2.43 * col + 0.59) + 0.14);
    float lum = dot(col, float3(0.299, 0.587, 0.114));
    col = lerp(lum.xxx, col, 0.72);                                   // muted, tape-like colour
    col *= float3(0.96, 1.0, 1.03);
    col = lerp(col, col * float3(0.85, 1.05, 1.0), 0.4);

    float n = hash(i.sv.xy + frac(t * 3.17) * 311.0);
    float n2 = hash(i.sv.xy * 0.5 + frac(t * 1.31) * 97.0);
    if (post0.x > 0.5) {                                              // night-vision mode
        float v = lum0 * 2.4 + 0.015;
        v = v / (1 + v);
        v = saturate(v * 1.25 + (n - 0.5) * 0.22 + (n2 - 0.5) * 0.08);
        col = float3(0.18, 1.0, 0.32) * v * (0.9 + 0.1 * sin(i.sv.y * 1.5 + t * 20.0));
    }
    col += (n - 0.5) * post0.w * (1.0 - lum * 0.6) + (n2 - 0.5) * post0.w * 0.4;
    float vg = smoothstep(1.0, 0.25, length(cd) * 1.45);
    col *= lerp(1.0, vg, post0.z);
    col *= 0.955 + 0.045 * sin(i.sv.y * 3.14159);                      // scanlines
    col *= 1 - post1.y * step(0.88, hash(float2(floor(t * 18.0), 7.0))) * 0.85;
    col += g * band * 0.25 * float3(0.7, 0.8, 1.0);
    col *= post1.x;
    return float4(saturate(col), 1);
}

struct HOut { float4 sv : SV_Position; float4 col : COLOR; float2 uv : TEXCOORD0; };
HOut VS_hud(VIn v) {
    HOut o;
    o.sv = float4(v.pos.x * screen.z * 2 - 1, 1 - v.pos.y * screen.w * 2, 0, 1);
    o.col = v.col; o.uv = v.uv; return o;
}
float4 PS_hud(HOut i) : SV_Target {
    float a = fontTex.Sample(lin, i.uv).r;
    return float4(i.col.rgb, i.col.a * a);
}
)HLSL";
