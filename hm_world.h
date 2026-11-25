// HOLLOWMERE - world data and procedural generation: terrain, forest, camp, the mansion.
#pragma once
#include "hm_core.h"

static const int NC = 10;                 // chunk grid
static const float CELL = 24.0f, WHALF = 120.0f;
static const int MCH = NC * NC, NCH = NC * NC + 1;
static Mesh chMesh[NCH];

static int cellOf(float x, float z) {
    int cx = (int)clampf(floorf((x + WHALF) / CELL), 0, NC - 1), cz = (int)clampf(floorf((z + WHALF) / CELL), 0, NC - 1);
    return cz * NC + cx;
}
static void selectAt(float x, float z) { M = &chMesh[cellOf(x, z)]; }

// ------------------------------------------------------------ layout
static const V3 CAMP = { -7, 0, -54 };
static const V3 CAR = { 0, 0, -112 };
static const float MHX = 12, MHZ = 8;                       // mansion half extents (centred on the origin)
static const float F0 = 0.04f, F1 = 3.6f, H0 = 3.4f, H1 = 6.8f;
static const float STAIR_X = 1.1f, STAIR_Z0 = -0.5f, STAIR_Z1 = 6.0f;
static const int STEPS = 13;
static const V3 TRAIL[6] = { { -7, 0, -54 }, { -3, 0, -41 }, { 3, 0, -29 }, { 4, 0, -19 }, { 0, 0, -13 }, { 0, 0, -9 } };

static float distToTrail(float x, float z) {
    float best = 1e9f;
    for (int i = 0; i < 5; i++) {
        float ax = TRAIL[i].x, az = TRAIL[i].z, bx = TRAIL[i + 1].x, bz = TRAIL[i + 1].z;
        float dx = bx - ax, dz = bz - az, t = clampf(((x - ax) * dx + (z - az) * dz) / (dx * dx + dz * dz), 0, 1);
        float px = ax + dx * t - x, pz = az + dz * t - z;
        best = std::min(best, sqrtf(px * px + pz * pz));
    }
    return best;
}
static bool inMansion(float x, float z, float m = 0) { return fabsf(x) < MHX + m && fabsf(z) < MHZ + m; }
static bool inPorch(float x, float z) { return fabsf(x) < 3.3f && z > -11.6f && z < -7.8f; }

static float groundH(float x, float z) {
    float h = (fbm2(x * 0.018f + 3.1f, z * 0.018f - 7.7f) - 0.5f) * 7.0f + (fbm2(x * 0.07f, z * 0.07f) - 0.5f) * 1.3f;
    float dx = std::max(fabsf(x) - 13.0f, 0.0f), dz = std::max(fabsf(z) - 9.0f, 0.0f);
    float fm = sstep(2, 18, sqrtf(dx * dx + dz * dz));
    float fc = sstep(4, 12, hypotf(x - CAMP.x, z - CAMP.z));
    float fr = sstep(6, 20, hypotf(x - CAR.x, z - CAR.z));
    return h * fm * fc * fr;
}
static float floorYOut(float x, float z) { return (inMansion(x, z) || inPorch(x, z)) ? F0 : groundH(x, z); }

// ------------------------------------------------------------ gameplay data
struct Wall { float x0, z0, x1, z1; int lvl; int door; };      // lvl 0 ground, 1 upper, 2 both
struct Circ { float x, z, r; };
struct Lamp { V3 p; Col c; float r; float flick; float on; int kind; float seed; float base; };
struct Door { V3 hinge, u, v; float w, h, open, target, speed; int lvl; bool locked; Col col; float thick; };
struct Pick { int type; int id; V3 p; int lvl; bool got; };     // type 0 sigil, 1 battery, 2 note
struct Node { float x, z; int lvl; };

static std::vector<Wall> walls;
static std::vector<Circ> circs;
static std::vector<int> cellCircs[NC * NC];
static std::vector<Lamp> lamps;
static std::vector<Door> doors;
static std::vector<Pick> picks;
static std::vector<Node> nodes;

static void addCirc(float x, float z, float r) {
    circs.push_back({ x, z, r });
    cellCircs[cellOf(x, z)].push_back((int)circs.size() - 1);
}
static void addLamp(V3 p, Col c, float r, float flick, int kind) {
    Lamp l; l.p = p; l.c = c; l.r = r; l.flick = flick; l.on = 1; l.kind = kind; l.seed = frand() * 100; l.base = 1; lamps.push_back(l);
}

// ------------------------------------------------------------ props
static void addBoxCol(float cx, float cz, float hw, float hd, float yaw, int lvl) {
    float c = fabsf(cosf(yaw)), s = fabsf(sinf(yaw));
    float ex = hw * c + hd * s, ez = hw * s + hd * c;
    walls.push_back({ cx - ex, cz - ez, cx + ex, cz + ez, lvl, -1 });
}
static void fbox(float cx, float cz, float w, float d, float y0, float h, float yaw, Col c, int mat, float va, int lvl, bool col = true) {
    xfSet(cx, 0, cz, yaw);
    box({ -w / 2, y0, -d / 2 }, { w / 2, y0 + h, d / 2 }, c, mat, va);
    xfId();
    if (col && h > 0.3f) addBoxCol(cx, cz, w / 2, d / 2, yaw, lvl);
}

static void stickFigure(float x, float z, float yaw) {
    float gy = groundH(x, z); selectAt(x, z);
    Col wood = C(98, 80, 58);
    xfSet(x, gy, z, yaw);
    tube2({ 0, -0.1f, 0 }, { 0, 2.0f, 0 }, 0.035f, 0.03f, 5, wood, MAT_BARK);
    tube2({ -0.7f, 1.55f, 0 }, { 0.7f, 1.62f, 0 }, 0.025f, 0.022f, 4, wood, MAT_BARK);
    tube2({ 0, 1.1f, 0 }, { -0.4f, 0.15f, 0.03f }, 0.025f, 0.018f, 4, wood, MAT_BARK);
    tube2({ 0, 1.1f, 0 }, { 0.38f, 0.2f, -0.03f }, 0.025f, 0.018f, 4, wood, MAT_BARK);
    tube2({ -0.7f, 1.55f, 0 }, { -0.82f, 1.15f, 0.04f }, 0.02f, 0.012f, 4, wood, MAT_BARK);
    tube2({ 0.7f, 1.62f, 0 }, { 0.8f, 1.2f, -0.04f }, 0.02f, 0.012f, 4, wood, MAT_BARK);
    ellipsoid({ 0, 1.92f, 0 }, { 0.08f, 0.11f, 0.08f }, wood, MAT_BARK, 1, 5, 6);
    box({ -0.04f, 1.12f, -0.05f }, { 0.04f, 1.2f, 0.05f }, C(30, 22, 16), MAT_FABRIC);
    xfId();
}
static void cairn(float x, float z) {
    float gy = groundH(x, z); selectAt(x, z);
    float y = gy; float rx = 0.38f;
    for (int i = 0; i < 5; i++) {
        float h = 0.1f + frand() * 0.05f;
        ellipsoid({ x + frr(-0.04f, 0.04f), y + h * 0.6f, z + frr(-0.04f, 0.04f) }, { rx, h, rx * frr(0.8f, 1.0f) }, C(70 + rnd() % 20, 72 + rnd() % 18, 66), MAT_STONE, 1, 5, 7);
        y += h * 1.5f; rx *= 0.78f;
    }
    addCirc(x, z, 0.4f);
}
static void rock(float x, float z, float s) {
    float gy = groundH(x, z); selectAt(x, z);
    float r = s * frr(0.7f, 1.2f);
    ellipsoid({ x, gy + r * 0.25f, z }, { r, r * 0.55f, r * frr(0.7f, 1.1f) }, C(58 + rnd() % 18, 60 + rnd() % 18, 54 + rnd() % 14), MAT_STONE, 1, 5, 8);
    if (s > 0.45f) addCirc(x, z, r * 0.85f);
}
static void fallenLog(float x, float z, float yaw) {
    float L = frr(3, 6), r = frr(0.2f, 0.34f);
    V3 a = { x - cosf(yaw) * L / 2, 0, z - sinf(yaw) * L / 2 }, b = { x + cosf(yaw) * L / 2, 0, z + sinf(yaw) * L / 2 };
    a.y = groundH(a.x, a.z) + r * 0.7f; b.y = groundH(b.x, b.z) + r * 0.7f;
    selectAt(x, z);
    tube2(a, b, r, r * 0.85f, 7, C(60, 48, 38), MAT_BARK);
    for (int i = 0; i < 3; i++) { V3 p = lerp3(a, b, i / 2.0f); addCirc(p.x, p.z, r * 1.05f); }
}

static void genTree(float x, float z) {
    float gy = groundH(x, z); selectAt(x, z);
    bool pine = frand() < 0.38f;
    float H = pine ? frr(15, 23) : frr(12, 18);
    float r0 = frr(0.22f, 0.46f);
    Col bark = pine ? C(66, 48, 38) : mixc(C(58, 52, 46), C(78, 64, 50), frand());
    const int n = 9;
    V3 p[n]; float r[n];
    float lx = frr(-0.05f, 0.05f), lz = frr(-0.05f, 0.05f);
    for (int i = 0; i < n; i++) {
        float t = i / (float)(n - 1);
        p[i] = { x + lx * H * t * t + frr(-0.07f, 0.07f) * (i > 0), gy - 0.25f + H * t, z + lz * H * t * t + frr(-0.07f, 0.07f) * (i > 0) };
        r[i] = r0 * (1 - 0.7f * t) * (i == 0 ? 1.3f : 1.0f);
    }
    tube(p, r, n, 9, bark, MAT_BARK, 1, 0.5f);
    addCirc(x, z, r0 * 1.25f + 0.08f);
    for (int rt = 0; rt < 5; rt++) {                              // buttress roots gripping the ground
        float ang = rt * 2 * PI / 5 + frr(-0.4f, 0.4f), L = r0 * frr(2.5f, 4.5f);
        V3 d = { cosf(ang), 0, sinf(ang) };
        V3 rp[3] = { { x + d.x * r0 * 0.4f, gy + r0 * 0.9f, z + d.z * r0 * 0.4f }, { x + d.x * L * 0.5f, gy + r0 * 0.28f, z + d.z * L * 0.5f }, { x + d.x * L, gy - 0.05f, z + d.z * L } };
        float rr[3] = { r0 * 0.38f, r0 * 0.2f, r0 * 0.06f };
        tube(rp, rr, 3, 5, bark, MAT_BARK, 1, 0.5f);
    }
    auto trunkAt = [&](float t) { float f = t * (n - 1); int i = std::min((int)f, n - 2); return lerp3(p[i], p[i + 1], f - i); };
    Col leaf = mixc(C(34, 40, 18), C(88, 52, 14), frand() * 0.8f);
    if (!pine) {
        int nb = 9 + rnd() % 5;
        for (int b = 0; b < nb; b++) {
            float t = frr(0.3f, 0.92f), ang = frr(0, 2 * PI), L = frr(2.2f, 5.0f) * (1.25f - 0.6f * t);
            V3 base = trunkAt(t), d = { cosf(ang), 0, sinf(ang) };
            V3 bp[3] = { base, base + d * (L * 0.5f) + V3{ 0, 0.5f, 0 }, base + d * L + V3{ 0, 1.3f, 0 } };
            float br[3] = { r0 * 0.24f * (1 - t * 0.5f), r0 * 0.14f, r0 * 0.05f };
            tube(bp, br, 3, 5, bark, MAT_BARK, 1, 0.5f);
            for (int tw = 0; tw < 2; tw++) {                          // twigs
                V3 tp = lerp3(bp[1], bp[2], frand()), td = { cosf(ang + frr(-1, 1)), frr(0.1f, 0.8f), sinf(ang + frr(-1, 1)) };
                tube2(tp, tp + td * frr(0.8f, 1.6f), br[1] * 0.45f, 0.006f, 3, bark, MAT_BARK);
            }
            leaf = mixc(leaf, mixc(C(34, 40, 18), C(100, 56, 14), frand()), 0.35f);
            if (frand() < 0.85f) cards(bp[2] + V3{ 0, 0.3f, 0 }, frr(1.8f, 2.8f), 1.7f, ang, 3, leaf, 0.2f);
            if (frand() < 0.7f) cards(bp[1] + V3{ 0, 0.4f, 0 }, frr(1.6f, 2.4f), 1.5f, ang + 1.0f, 3, leaf, 0.2f);
            if (frand() < 0.5f) cards(lerp3(bp[1], bp[2], 0.5f) + V3{ 0, 0.2f, 0 }, frr(1.4f, 2.0f), 1.3f, ang + 2.0f, 2, leaf, 0.2f);
        }
        cards(p[n - 1] + V3{ 0, -0.5f, 0 }, 4.5f, 3.0f, frr(0, 3), 3, leaf, 0.2f);
    } else {
        for (int k = 0; k < 15; k++) {
            float t = 0.2f + k * 0.052f, rad = (1.0f - t) * 2.9f + 0.5f;
            V3 c = trunkAt(t);
            cards(c + V3{ 0, -0.8f, 0 }, rad * 2.0f, 1.7f, frr(0, 3), 4, mixc(C(16, 28, 18), C(34, 48, 26), frand()), 0.8f);
        }
        for (int b = 0; b < 4; b++) {                    // dead lower branches
            float t = frr(0.08f, 0.2f), ang = frr(0, 2 * PI); V3 base = trunkAt(t), d = { cosf(ang), 0, sinf(ang) };
            tube2(base, base + d * frr(1.0f, 2.0f) + V3{ 0, -0.2f, 0 }, r0 * 0.12f, r0 * 0.03f, 4, bark, MAT_BARK);
        }
    }
}

static void genForest() {
    int seedTrees = 0;
    auto clear = [&](float x, float z, float margin) {
        if (inMansion(x, z, 7 + margin)) return false;
        if (inPorch(x, z + 5)) return false;
        if (hypotf(x - CAMP.x, z - CAMP.z) < 7 + margin) return false;
        if (hypotf(x - CAR.x, z - CAR.z) < 15 + margin) return false;
        if (distToTrail(x, z) < 2.4f + margin) return false;
        return true;
    };
    const float R = 117;
    const float sp = 3.5f;
    for (float gx = -R; gx <= R; gx += sp) for (float gz = -R; gz <= R; gz += sp) {
        float x = gx + frr(-1.6f, 1.6f), z = gz + frr(-1.6f, 1.6f);
        if (x * x + z * z > R * R) continue;
        if (frand() < 0.07f) continue;
        if (!clear(x, z, 0)) continue;
        genTree(x, z); seedTrees++;
    }
    // undergrowth
    for (int i = 0; i < 4500; i++) {                     // saplings and thin young trees
        float a = frr(0, 2 * PI), d = sqrtf(frand()) * 118; float x = cosf(a) * d, z = sinf(a) * d;
        if (!clear(x, z, 0.2f)) continue;
        selectAt(x, z); float gy = groundH(x, z), H = frr(1.6f, 4.5f);
        V3 tp = { x + frr(-0.3f, 0.3f), gy + H, z + frr(-0.3f, 0.3f) };
        tube2({ x, gy - 0.1f, z }, tp, 0.04f, 0.012f, 4, C(60, 50, 40), MAT_BARK);
        cards(tp + V3{ 0, -0.5f, 0 }, frr(1.0f, 1.7f), 1.0f, frr(0, 3), 3, mixc(C(30, 40, 16), C(100, 56, 14), frand()), 0.2f);
    }
    for (int i = 0; i < 17000; i++) {
        float a = frr(0, 2 * PI), d = sqrtf(frand()) * 118; float x = cosf(a) * d, z = sinf(a) * d;
        if (!clear(x, z, -1.2f)) continue;
        selectAt(x, z); float gy = groundH(x, z);
        Col c = mixc(C(22, 34, 14), C(58, 46, 18), frand());
        cards({ x, gy - 0.05f, z }, frr(0.9f, 1.7f), frr(0.5f, 0.95f), frr(0, 3), 3, c, 0.5f);
    }
    for (int i = 0; i < 2200; i++) {
        float a = frr(0, 2 * PI), d = sqrtf(frand()) * 118; float x = cosf(a) * d, z = sinf(a) * d;
        if (!clear(x, z, 0.5f)) continue;
        selectAt(x, z); float gy = groundH(x, z);
        cards({ x, gy - 0.1f, z }, frr(1.6f, 2.6f), frr(1.1f, 1.8f), frr(0, 3), 3, mixc(C(30, 36, 16), C(90, 50, 14), frand()), 0.2f);
    }
    for (int i = 0; i < 520; i++) {
        float a = frr(0, 2 * PI), d = sqrtf(frand()) * 118; float x = cosf(a) * d, z = sinf(a) * d;
        if (clear(x, z, 0)) rock(x, z, frr(0.25f, 1.0f));
    }
    for (int i = 0; i < 110; i++) {
        float a = frr(0, 2 * PI), d = sqrtf(frand()) * 115; float x = cosf(a) * d, z = sinf(a) * d;
        if (clear(x, z, 1)) fallenLog(x, z, frr(0, PI));
    }
    // the totems: sticks and stones, thick along the way to the house
    for (int i = 0; i < 34; i++) {
        float x, z;
        if (i < 16) { int k = rnd() % 5; float t = frand(); x = lerpf(TRAIL[k].x, TRAIL[k + 1].x, t) + frr(-1, 1) * 5.5f; z = lerpf(TRAIL[k].z, TRAIL[k + 1].z, t) + frr(-1, 1) * 5.5f; }
        else { float a = frr(0, 2 * PI), d = frr(14, 95); x = cosf(a) * d; z = sinf(a) * d; }
        if (clear(x, z, 0.6f)) { if (frand() < 0.7f) stickFigure(x, z, frr(0, 6.28f)); else cairn(x, z); }
    }
    for (int i = 0; i < 12; i++) {                       // a ring of figures around the house
        float a = i * 2 * PI / 12 + frr(-0.1f, 0.1f), d = frr(20, 24);
        float x = cosf(a) * d * 1.2f, z = sinf(a) * d;
        if (clear(x, z, 0.2f)) stickFigure(x, z, a + PI / 2);
    }
}

// ------------------------------------------------------------ terrain
static void genTerrain() {
    const int N = 24; const float st = CELL / N;
    auto vert = [&](float x, float z) {
        float h = groundH(x, z);
        float hx = groundH(x + 0.6f, z) - groundH(x - 0.6f, z), hz = groundH(x, z + 0.6f) - groundH(x, z - 0.6f);
        V3 n = norm({ -hx, 1.2f, -hz });
        float nz = fbm2(x * 0.11f, z * 0.11f), tr = 1 - sstep(0.8f, 3.5f, distToTrail(x, z));
        Col c = mixc(mixc(C(30, 24, 18), C(52, 32, 15), nz), C(40, 32, 24), tr * 0.8f);
        vtx({ x, h, z }, n, c, MAT_GROUND, tr, x, z);
    };
    for (int cz = 0; cz < NC; cz++) for (int cx = 0; cx < NC; cx++) {
        M = &chMesh[cz * NC + cx];
        float x0 = -WHALF + cx * CELL, z0 = -WHALF + cz * CELL;
        for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
            float xa = x0 + i * st, xb = xa + st, za = z0 + j * st, zb = za + st;
            vert(xa, za); vert(xa, zb); vert(xb, zb);
            vert(xa, za); vert(xb, zb); vert(xb, za);
        }
    }
}

// ------------------------------------------------------------ camp and car
static void genCamp() {
    float cx = CAMP.x, cz = CAMP.z;
    selectAt(cx, cz);
    // tent
    float tx = cx - 4.2f, tz = cz + 1.2f;
    xfSet(tx, 0.02f, tz, 0.35f);
    Col tc = C(120, 72, 28);
    quadAuto({ -1.0f, 0, -1.3f }, { -1.0f, 0, 1.3f }, { 0, 1.45f, 1.3f }, { 0, 1.45f, -1.3f }, tc, MAT_TENT);
    quadAuto({ 1.0f, 0, 1.3f }, { 1.0f, 0, -1.3f }, { 0, 1.45f, -1.3f }, { 0, 1.45f, 1.3f }, tc, MAT_TENT);
    quadAuto({ -1.0f, 0, -1.3f }, { 1.0f, 0, -1.3f }, { 0, 1.45f, -1.3f }, { 0, 1.45f, -1.3f }, tc, MAT_TENT);
    quadAuto({ 1.0f, 0, 1.3f }, { -1.0f, 0, 1.3f }, { -0.06f, 1.45f, 1.3f }, { 0.06f, 1.45f, 1.3f }, tc, MAT_TENT);
    quadv({ -0.45f, 0, 1.31f }, { 0.45f, 0, 1.31f }, { 0.05f, 1.2f, 1.31f }, { -0.05f, 1.2f, 1.31f }, { 0, 0, 1 }, C(6, 5, 4), MAT_PLAIN, 1, 0, 0, 1, 1);   // dark doorway
    box({ -1.0f, -0.05f, -1.3f }, { 1.0f, 0.03f, 1.3f }, C(40, 60, 70), MAT_TENT);
    tube2({ -1.4f, 0, 1.9f }, { -0.05f, 1.45f, 1.3f }, 0.008f, 0.008f, 3, C(160, 150, 130), MAT_PLAIN);
    tube2({ 1.4f, 0, 1.9f }, { 0.05f, 1.45f, 1.3f }, 0.008f, 0.008f, 3, C(160, 150, 130), MAT_PLAIN);
    xfId();
    addBoxCol(tx, tz, 1.0f, 1.3f, 0.35f, 0);
    // fire ring, charred logs, log seats, backpack
    float fx = cx, fz = cz;
    for (int i = 0; i < 9; i++) { float a = i * 2 * PI / 9; ellipsoid({ fx + cosf(a) * 0.6f, 0.1f, fz + sinf(a) * 0.6f }, { 0.17f, 0.13f, 0.15f }, C(70, 70, 64), MAT_STONE, 1, 5, 6); }
    tube2({ fx - 0.4f, 0.14f, fz - 0.1f }, { fx + 0.4f, 0.2f, fz + 0.15f }, 0.07f, 0.06f, 6, C(14, 11, 9), MAT_BARK);
    tube2({ fx - 0.1f, 0.14f, fz + 0.4f }, { fx + 0.15f, 0.2f, fz - 0.4f }, 0.07f, 0.06f, 6, C(14, 11, 9), MAT_BARK);
    ellipsoid({ fx, 0.1f, fz }, { 0.3f, 0.06f, 0.3f }, C(255, 90, 20), MAT_EMIT, 0.0f, 4, 8);
    addLamp({ fx, 0.35f, fz }, C(255, 110, 40), 9, 0.5f, 1);
    addCirc(fx, fz, 0.8f);
    tube2({ fx + 1.8f, 0.2f, fz - 0.9f }, { fx + 1.5f, 0.2f, fz + 1.2f }, 0.2f, 0.2f, 8, C(60, 48, 38), MAT_BARK);
    tube2({ fx - 1.6f, 0.2f, fz - 1.0f }, { fx - 1.9f, 0.2f, fz + 1.0f }, 0.2f, 0.2f, 8, C(60, 48, 38), MAT_BARK);
    addCirc(fx + 1.65f, fz + 0.1f, 0.5f); addCirc(fx - 1.75f, fz, 0.5f);
    xfSet(fx + 2.7f, 0, fz + 1.8f, 0.6f);
    box({ -0.25f, 0, -0.18f }, { 0.25f, 0.6f, 0.18f }, C(40, 56, 40), MAT_FABRIC);
    ellipsoid({ 0, 0.62f, 0 }, { 0.22f, 0.12f, 0.16f }, C(36, 50, 36), MAT_FABRIC, 1, 5, 8);
    xfId();
}
static void genCar() {
    selectAt(CAR.x, CAR.z);
    float cx = CAR.x, cz = CAR.z;
    xfSet(cx, 0, cz, 0);
    Col body = C(30, 36, 52);
    box({ -0.9f, 0.32f, -2.1f }, { 0.9f, 1.05f, 2.1f }, body, MAT_METAL);
    box({ -0.8f, 1.05f, -1.1f }, { 0.8f, 1.65f, 1.0f }, body, MAT_METAL);
    box({ -0.81f, 1.12f, -1.0f }, { 0.81f, 1.58f, 0.9f }, C(8, 12, 14), MAT_GLASS);
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2)
        tube2({ sx * 0.78f, 0.34f, sz * 1.3f }, { sx * 1.0f, 0.34f, sz * 1.3f }, 0.34f, 0.34f, 12, C(10, 10, 10), MAT_PLAIN);
    xfId();
    addBoxCol(cx, cz, 0.95f, 2.1f, 0, 0);
    box({ -25, 0.02f, cz - 3.0f }, { 25, 0.05f, cz + 3.0f }, C(48, 42, 34), MAT_GROUND, 0.5f);
}

// ------------------------------------------------------------ the mansion
struct Op { float a, b, yb, yt; bool door; float swing; bool glow; };

static void wallGen(bool alongX, float c, float a0, float a1, float y0, float y1, int lvl, std::vector<Op> ops,
                    int matA, Col cA, float vA, int matB, Col cB, float vB, float T = 0.3f) {
    std::sort(ops.begin(), ops.end(), [](const Op& p, const Op& q) { return p.a < q.a; });
    float h = T * 0.5f;
    auto piece = [&](float p0, float p1, float yy0, float yy1) {
        if (p1 - p0 < 0.001f || yy1 - yy0 < 0.001f) return;
        if (alongX) { box({ p0, yy0, c - h }, { p1, yy1, c }, cA, matA, vA); box({ p0, yy0, c }, { p1, yy1, c + h }, cB, matB, vB); }
        else { box({ c - h, yy0, p0 }, { c, yy1, p1 }, cA, matA, vA); box({ c, yy0, p0 }, { c + h, yy1, p1 }, cB, matB, vB); }
    };
    auto bar = [&](float p0, float p1, float yy0, float yy1, float th, Col col) {
        if (alongX) box({ p0, yy0, c - th }, { p1, yy1, c + th }, col, MAT_WOOD, 0.8f);
        else box({ c - th, yy0, p0 }, { c + th, yy1, p1 }, col, MAT_WOOD, 0.8f);
    };
    auto solid = [&](float p0, float p1) {
        if (p1 - p0 < 0.01f) return;
        if (alongX) walls.push_back({ p0, c - h, p1, c + h, lvl, -1 }); else walls.push_back({ c - h, p0, c + h, p1, lvl, -1 });
    };
    float cur = a0, solidStart = a0;
    Col frame = C(26, 20, 16);
    for (auto& o : ops) {
        piece(cur, o.a, y0, y1);
        piece(o.a, o.b, y0, o.yb);
        piece(o.a, o.b, o.yt, y1);
        cur = o.b;
        if (o.door) {
            solid(solidStart, o.a); solidStart = o.b;
            Wall w = alongX ? Wall{ o.a, c - h, o.b, c + h, lvl, (int)doors.size() } : Wall{ c - h, o.a, c + h, o.b, lvl, (int)doors.size() };
            walls.push_back(w);
            Door d = {};
            d.hinge = alongX ? V3{ o.a, o.yb, c } : V3{ c, o.yb, o.a };
            d.u = alongX ? V3{ 1, 0, 0 } : V3{ 0, 0, 1 };
            d.v = (alongX ? V3{ 0, 0, 1 } : V3{ 1, 0, 0 }) * o.swing;
            d.w = o.b - o.a; d.h = o.yt - o.yb; d.lvl = lvl; d.thick = 0.06f; d.col = C(46, 34, 24); d.speed = 3.0f;
            doors.push_back(d);
            bar(o.a - 0.07f, o.a, o.yb, o.yt + 0.07f, h + 0.03f, frame); bar(o.b, o.b + 0.07f, o.yb, o.yt + 0.07f, h + 0.03f, frame);
            bar(o.a - 0.07f, o.b + 0.07f, o.yt, o.yt + 0.07f, h + 0.03f, frame);
        } else {                                          // window: glass, frame, mullions
            Col gl = o.glow ? C(255, 170, 70) : C(10, 14, 18);
            int gm = o.glow ? MAT_EMIT : MAT_GLASS;
            float ga = o.glow ? 0.9f : 1.0f;
            if (alongX) box({ o.a, o.yb, c - 0.015f }, { o.b, o.yt, c + 0.015f }, gl, gm, ga);
            else box({ c - 0.015f, o.yb, o.a }, { c + 0.015f, o.yt, o.b }, gl, gm, ga);
            float mx = (o.a + o.b) * 0.5f, my = (o.yb + o.yt) * 0.5f;
            bar(o.a - 0.05f, o.a + 0.03f, o.yb - 0.05f, o.yt + 0.05f, h + 0.02f, frame); bar(o.b - 0.03f, o.b + 0.05f, o.yb - 0.05f, o.yt + 0.05f, h + 0.02f, frame);
            bar(o.a, o.b, o.yb - 0.05f, o.yb + 0.03f, h + 0.05f, frame); bar(o.a, o.b, o.yt - 0.03f, o.yt + 0.05f, h + 0.02f, frame);
            bar(mx - 0.02f, mx + 0.02f, o.yb, o.yt, h + 0.01f, frame); bar(o.a, o.b, my - 0.02f, my + 0.02f, h + 0.01f, frame);
        }
    }
    piece(cur, a1, y0, y1);
    solid(solidStart, a1);
}

static void chair(float cx, float cz, float yaw, float y0, Col c, int lvl) {
    xfSet(cx, y0, cz, yaw);
    box({ -0.22f, 0.42f, -0.22f }, { 0.22f, 0.47f, 0.22f }, c, MAT_WOOD, 0.8f);
    box({ -0.22f, 0.47f, 0.19f }, { 0.22f, 0.98f, 0.23f }, c, MAT_WOOD, 0.8f);
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) box({ sx * 0.2f - 0.02f, 0, sz * 0.2f - 0.02f }, { sx * 0.2f + 0.02f, 0.42f, sz * 0.2f + 0.02f }, c, MAT_WOOD, 0.8f);
    xfId(); addBoxCol(cx, cz, 0.22f, 0.22f, yaw, lvl);
}
static void table(float cx, float cz, float w, float d, float h, float yaw, float y0, Col c, int lvl) {
    xfSet(cx, y0, cz, yaw);
    box({ -w / 2, h - 0.06f, -d / 2 }, { w / 2, h, d / 2 }, c, MAT_WOOD, 0.8f);
    for (int sx = -1; sx <= 1; sx += 2) for (int sz = -1; sz <= 1; sz += 2) box({ sx * (w / 2 - 0.1f) - 0.04f, 0, sz * (d / 2 - 0.1f) - 0.04f }, { sx * (w / 2 - 0.1f) + 0.04f, h - 0.06f, sz * (d / 2 - 0.1f) + 0.04f }, c, MAT_WOOD, 0.8f);
    xfId(); addBoxCol(cx, cz, w / 2, d / 2, yaw, lvl);
}
static void bookshelf(float cx, float cz, float yaw, float w, float h, float y0, int lvl) {
    xfSet(cx, y0, cz, yaw);
    Col wood = C(40, 28, 20);
    box({ -w / 2, 0, -0.16f }, { w / 2, h, -0.13f }, wood, MAT_WOOD, 0.8f);
    box({ -w / 2, 0, -0.16f }, { -w / 2 + 0.04f, h, 0.16f }, wood, MAT_WOOD, 0.8f);
    box({ w / 2 - 0.04f, 0, -0.16f }, { w / 2, h, 0.16f }, wood, MAT_WOOD, 0.8f);
    int rows = (int)(h / 0.38f);
    for (int r = 0; r <= rows; r++) {
        float y = r * (h - 0.04f) / rows;
        box({ -w / 2, y, -0.16f }, { w / 2, y + 0.035f, 0.16f }, wood, MAT_WOOD, 0.8f);
        if (r == rows) break;
        float x = -w / 2 + 0.06f, top = y + 0.035f;
        while (x < w / 2 - 0.08f) {
            float bw = frr(0.025f, 0.06f), bh = frr(0.2f, 0.33f);
            if (frand() < 0.06f) { x += frr(0.1f, 0.25f); continue; }
            Col bc = mixc(C(60, 20, 18), C(34, 40, 52), frand()); if (frand() < 0.3f) bc = C(52 + rnd() % 30, 40 + rnd() % 20, 28);
            box({ x, top, -0.12f }, { x + bw, top + bh, 0.11f }, bc, MAT_FABRIC);
            x += bw + 0.004f;
        }
    }
    xfId(); addBoxCol(cx, cz, w / 2, 0.17f, yaw, lvl);
}
static void bed(float cx, float cz, float yaw, float y0, int lvl) {
    xfSet(cx, y0, cz, yaw);
    Col wood = C(44, 30, 22);
    box({ -0.85f, 0.2f, -1.1f }, { 0.85f, 0.45f, 1.1f }, wood, MAT_WOOD, 0.8f);
    box({ -0.8f, 0.45f, -1.05f }, { 0.8f, 0.68f, 1.05f }, C(120, 112, 100), MAT_FABRIC);
    box({ -0.7f, 0.68f, -1.0f }, { 0.7f, 0.74f, 0.4f }, C(70, 40, 40), MAT_FABRIC);
    box({ -0.55f, 0.68f, 0.65f }, { 0.55f, 0.82f, 1.0f }, C(130, 125, 115), MAT_FABRIC);
    box({ -0.9f, 0.0f, 1.08f }, { 0.9f, 1.35f, 1.18f }, wood, MAT_WOOD, 0.8f);
    box({ -0.9f, 0.0f, -1.18f }, { 0.9f, 0.7f, -1.08f }, wood, MAT_WOOD, 0.8f);
    xfId(); addBoxCol(cx, cz, 0.95f, 1.2f, yaw, lvl);
}
static void sigilDecal(float x, float y, float z, float yaw, float s, Col c) {
    xfSet(x, y, z, yaw);
    quadv({ -s, -s, 0 }, { s, -s, 0 }, { s, s, 0 }, { -s, s, 0 }, { 0, 0, 1 }, c, MAT_SIGIL, 1, -1, -1, 1, 1);
    xfId();
}
static void frameArt(float x, float y, float z, float yaw, float w, float h, bool face) {
    xfSet(x, y, z, yaw);
    Col fr = C(52, 38, 22);
    box({ -w / 2 - 0.05f, -h / 2 - 0.05f, 0 }, { w / 2 + 0.05f, h / 2 + 0.05f, 0.04f }, fr, MAT_WOOD, 0.8f);
    box({ -w / 2, -h / 2, 0.03f }, { w / 2, h / 2, 0.045f }, C(26, 20, 14), MAT_FABRIC);
    if (face) {
        ellipsoid({ 0, h * 0.12f, 0.05f }, { w * 0.16f, h * 0.2f, 0.01f }, C(120, 104, 84), MAT_FABRIC, 1, 5, 8);
        ellipsoid({ 0, -h * 0.28f, 0.05f }, { w * 0.28f, h * 0.18f, 0.01f }, C(20, 16, 14), MAT_FABRIC, 1, 5, 8);
        for (int i = 0; i < 4; i++) {                         // scratches over the face
            float a = frr(-0.3f, 0.3f);
            tube2({ -w * 0.2f, h * 0.3f + a, 0.062f }, { w * 0.2f, h * 0.0f - a, 0.062f }, 0.004f, 0.004f, 3, C(8, 6, 5), MAT_PLAIN);
        }
    }
    xfId();
}

static void genMansion() {
    M = &chMesh[MCH];
    xfId();
    const Col sid = C(74, 66, 58);                            // weathered siding
    const Col pLiv = C(52, 66, 46), pDin = C(82, 34, 34), pLib = C(62, 46, 30), pKit = C(88, 92, 70), pHall = C(90, 80, 56),
              pUp = C(62, 70, 84), pMas = C(88, 62, 62), pNur = C(84, 84, 52), pStu = C(36, 62, 62), pBat = C(92, 100, 100);
    const Col trim = C(28, 20, 15);

    // ---- floors, slabs
    box({ -MHX, -0.3f, -MHZ }, { MHX, F0, MHZ }, C(52, 40, 30), MAT_WOOD, 0.0f);
    box({ -MHX, H0, -MHZ }, { -STAIR_X, F1, MHZ }, C(56, 42, 32), MAT_WOOD, 0.0f);
    box({ STAIR_X, H0, -MHZ }, { MHX, F1, MHZ }, C(56, 42, 32), MAT_WOOD, 0.0f);
    box({ -STAIR_X, H0, -MHZ }, { STAIR_X, F1, STAIR_Z0 }, C(56, 42, 32), MAT_WOOD, 0.0f);
    box({ -STAIR_X, H0, STAIR_Z1 }, { STAIR_X, F1, MHZ }, C(56, 42, 32), MAT_WOOD, 0.0f);
    box({ -MHX, H1, -MHZ }, { MHX, H1 + 0.2f, MHZ }, C(40, 32, 26), MAT_WOOD, 0.0f);
    // foundation
    box({ -MHX - 0.3f, -0.5f, -MHZ - 0.3f }, { MHX + 0.3f, 0.5f, -MHZ + 0.0f }, C(70, 66, 60), MAT_STONE);
    box({ -MHX - 0.3f, -0.5f, MHZ }, { MHX + 0.3f, 0.5f, MHZ + 0.3f }, C(70, 66, 60), MAT_STONE);
    box({ -MHX - 0.3f, -0.5f, -MHZ }, { -MHX, 0.5f, MHZ }, C(70, 66, 60), MAT_STONE);
    box({ MHX, -0.5f, -MHZ }, { MHX + 0.3f, 0.5f, MHZ }, C(70, 66, 60), MAT_STONE);

    auto D = [](float a, float b, float sw) { return Op{ a, b, 0, 2.4f, true, sw, false }; };
    auto W = [](float c, float yb, float yt, bool glow = false) { return Op{ c - 0.6f, c + 0.6f, yb, yt, false, 0, glow }; };
    const int MW = MAT_WOOD, MP = MAT_PAPER;

    // ---- ground floor walls (side A = lower coordinate)
    float y0 = 0.0f, y1 = H0;
    wallGen(true, -MHZ, -12, -4, y0, y1, 0, { W(-8, 0.9f, 2.3f) }, MW, sid, 0.4f, MP, pLiv, 0);
    wallGen(true, -MHZ, -4, 4, y0, y1, 0, { { -1.1f, 1.1f, 0, 2.5f, true, 1.0f, false }, { -3.3f, -2.3f, 0.9f, 2.3f, false, 0, true }, { 2.3f, 3.3f, 0.9f, 2.3f, false, 0, true } }, MW, sid, 0.4f, MP, pHall, 0);
    wallGen(true, -MHZ, 4, 12, y0, y1, 0, { W(8, 0.9f, 2.3f) }, MW, sid, 0.4f, MP, pLib, 0);
    wallGen(true, MHZ, -12, -4, y0, y1, 0, { W(-8, 0.9f, 2.3f) }, MP, pDin, 0, MW, sid, 0.4f);
    wallGen(true, MHZ, -4, 4, y0, y1, 0, { { 0.5f, 1.7f, 0.9f, 2.3f, false, 0, false } }, MP, pHall, 0, MW, sid, 0.4f);
    wallGen(true, MHZ, 4, 12, y0, y1, 0, { W(8, 0.9f, 2.3f) }, MP, pKit, 0, MW, sid, 0.4f);
    wallGen(false, -MHX, -8, 0, y0, y1, 0, {}, MW, sid, 0.4f, MP, pLiv, 0);
    wallGen(false, -MHX, 0, 8, y0, y1, 0, { W(4, 0.9f, 2.3f) }, MW, sid, 0.4f, MP, pDin, 0);
    wallGen(false, MHX, -8, 0, y0, y1, 0, { W(-4, 0.9f, 2.3f) }, MP, pLib, 0, MW, sid, 0.4f);
    wallGen(false, MHX, 0, 8, y0, y1, 0, { W(4, 0.9f, 2.3f) }, MP, pKit, 0, MW, sid, 0.4f);
    wallGen(false, -4, -8, 0, y0, y1, 0, { D(-4.5f, -3.5f, -1.0f) }, MP, pLiv, 0, MP, pHall, 0);
    wallGen(false, -4, 0, 8, y0, y1, 0, { D(3.5f, 4.5f, -1.0f) }, MP, pDin, 0, MP, pHall, 0);
    wallGen(false, 4, -8, 0, y0, y1, 0, { D(-4.5f, -3.5f, 1.0f) }, MP, pHall, 0, MP, pLib, 0);
    wallGen(false, 4, 0, 8, y0, y1, 0, { D(3.5f, 4.5f, 1.0f) }, MP, pHall, 0, MP, pKit, 0);
    wallGen(true, 0, -12, -4, y0, y1, 0, {}, MP, pLiv, 0, MP, pDin, 0);
    wallGen(true, 0, 4, 12, y0, y1, 0, {}, MP, pLib, 0, MP, pKit, 0);

    // ---- upper floor walls
    y0 = F1; y1 = H1;
    wallGen(true, -MHZ, -12, -4, y0, y1, 1, { W(-8, F1 + 0.9f, F1 + 2.3f) }, MW, sid, 0.4f, MP, pMas, 0);
    wallGen(true, -MHZ, -4, 4, y0, y1, 1, { { -0.9f, 0.9f, F1 + 0.8f, F1 + 2.6f, false, 0, false } }, MW, sid, 0.4f, MP, pUp, 0);
    wallGen(true, -MHZ, 4, 12, y0, y1, 1, { W(8, F1 + 0.9f, F1 + 2.3f) }, MW, sid, 0.4f, MP, pStu, 0);
    wallGen(true, MHZ, -12, -4, y0, y1, 1, { W(-8, F1 + 0.9f, F1 + 2.3f) }, MP, pNur, 0, MW, sid, 0.4f);
    wallGen(true, MHZ, -4, 4, y0, y1, 1, { { -0.6f, 0.6f, F1 + 0.9f, F1 + 2.4f, false, 0, false } }, MP, pUp, 0, MW, sid, 0.4f);
    wallGen(true, MHZ, 4, 12, y0, y1, 1, { W(8, F1 + 0.9f, F1 + 2.3f) }, MP, pBat, 0, MW, sid, 0.4f);
    wallGen(false, -MHX, -8, 0, y0, y1, 1, { W(-4, F1 + 0.9f, F1 + 2.3f) }, MW, sid, 0.4f, MP, pMas, 0);
    wallGen(false, -MHX, 0, 8, y0, y1, 1, { W(4, F1 + 0.9f, F1 + 2.3f) }, MW, sid, 0.4f, MP, pNur, 0);
    wallGen(false, MHX, -8, 0, y0, y1, 1, { W(-4, F1 + 0.9f, F1 + 2.3f) }, MP, pStu, 0, MW, sid, 0.4f);
    wallGen(false, MHX, 0, 8, y0, y1, 1, { W(4, F1 + 0.9f, F1 + 2.3f) }, MP, pBat, 0, MW, sid, 0.4f);
    wallGen(false, -4, -8, 0, y0, y1, 1, { { -4.5f, -3.5f, F1, F1 + 2.4f, true, -1.0f, false } }, MP, pMas, 0, MP, pUp, 0);
    wallGen(false, -4, 0, 8, y0, y1, 1, { { 3.5f, 4.5f, F1, F1 + 2.4f, true, -1.0f, false } }, MP, pNur, 0, MP, pUp, 0);
    wallGen(false, 4, -8, 0, y0, y1, 1, { { -4.5f, -3.5f, F1, F1 + 2.4f, true, 1.0f, false } }, MP, pUp, 0, MP, pStu, 0);
    wallGen(false, 4, 0, 8, y0, y1, 1, { { 3.5f, 4.5f, F1, F1 + 2.4f, true, 1.0f, false } }, MP, pUp, 0, MP, pBat, 0);
    wallGen(true, 0, -12, -4, y0, y1, 1, {}, MP, pMas, 0, MP, pNur, 0);
    wallGen(true, 0, 4, 12, y0, y1, 1, {}, MP, pStu, 0, MP, pBat, 0);
    // door indices: 0 front, 1 living, 2 dining, 3 library, 4 kitchen, 5 master, 6 nursery, 7 study, 8 bath

    // ---- stairs and balustrade
    float sw = STAIR_X * 2, rise = F1 / STEPS, run = (STAIR_Z1 - STAIR_Z0) / STEPS;
    for (int i = 0; i < STEPS; i++)
        box({ -STAIR_X, 0, STAIR_Z0 + i * run }, { STAIR_X, (i + 1) * rise, STAIR_Z0 + (i + 1) * run }, C(60, 44, 32), MAT_WOOD, 0.0f);
    for (int s = -1; s <= 1; s += 2) {                        // stair rails
        float rx = s * (STAIR_X - 0.05f);
        for (int i = 0; i <= STEPS; i += 2) box({ rx - 0.025f, i * rise, STAIR_Z0 + i * run - 0.02f }, { rx + 0.025f, i * rise + 0.95f, STAIR_Z0 + i * run + 0.02f }, trim, MAT_WOOD, 0.8f);
        boxAxes({ rx - 0.03f, 0.95f, STAIR_Z0 }, { 0.06f, 0, 0 }, { 0, F1, 0 }, { 0, 0, STAIR_Z1 - STAIR_Z0 }, trim, MAT_WOOD, 0.8f);
    }
    for (int s = -1; s <= 1; s += 2) {                        // upstairs balustrade around the hole
        float rx = s * STAIR_X;
        for (float z = STAIR_Z0; z <= STAIR_Z1; z += 0.7f) box({ rx - 0.025f, F1, z - 0.025f }, { rx + 0.025f, F1 + 0.95f, z + 0.025f }, trim, MAT_WOOD, 0.8f);
        box({ rx - 0.04f, F1 + 0.95f, STAIR_Z0 }, { rx + 0.04f, F1 + 1.02f, STAIR_Z1 }, trim, MAT_WOOD, 0.8f);
    }
    for (float x = -STAIR_X; x <= STAIR_X; x += 0.7f) box({ x - 0.025f, F1, STAIR_Z0 - 0.025f }, { x + 0.025f, F1 + 0.95f, STAIR_Z0 + 0.025f }, trim, MAT_WOOD, 0.8f);
    box({ -STAIR_X, F1 + 0.95f, STAIR_Z0 - 0.04f }, { STAIR_X, F1 + 1.02f, STAIR_Z0 + 0.04f }, trim, MAT_WOOD, 0.8f);
    // stair colliders: rails both levels, cap (ground only) at the top, south rail (upper only)
    walls.push_back({ -STAIR_X - 0.06f, STAIR_Z0, -STAIR_X + 0.06f, STAIR_Z1, 2, -1 });
    walls.push_back({ STAIR_X - 0.06f, STAIR_Z0, STAIR_X + 0.06f, STAIR_Z1, 2, -1 });
    walls.push_back({ -STAIR_X, STAIR_Z1, STAIR_X, STAIR_Z1 + 0.1f, 0, -1 });
    walls.push_back({ -STAIR_X, STAIR_Z0 - 0.06f, STAIR_X, STAIR_Z0 + 0.06f, 1, -1 });

    // ---- roof, gables, chimney
    Col roofC = C(34, 30, 30);
    quadAuto({ -13.2f, H1 + 0.05f, -9.6f }, { 13.2f, H1 + 0.05f, -9.6f }, { 13.2f, 11.6f, 0 }, { -13.2f, 11.6f, 0 }, roofC, MAT_ROOF);
    quadAuto({ 13.2f, H1 + 0.05f, 9.6f }, { -13.2f, H1 + 0.05f, 9.6f }, { -13.2f, 11.6f, 0 }, { 13.2f, 11.6f, 0 }, roofC, MAT_ROOF);
    for (int s = -1; s <= 1; s += 2) {
        float x = s * (MHX + 0.15f);
        vtx({ x, H1, -8.4f }, { (float)s, 0, 0 }, sid, MAT_WOOD, 0.4f, -8.4f, H1); vtx({ x, H1, 8.4f }, { (float)s, 0, 0 }, sid, MAT_WOOD, 0.4f, 8.4f, H1); vtx({ x, 11.2f, 0 }, { (float)s, 0, 0 }, sid, MAT_WOOD, 0.4f, 0, 11.2f);
    }
    box({ -12.7f, 0, -5.0f }, { -11.3f, 12.9f, -3.0f }, C(66, 60, 56), MAT_STONE);
    box({ -12.85f, 12.9f, -5.15f }, { -11.15f, 13.15f, -2.85f }, C(52, 48, 46), MAT_STONE);

    // ---- porch
    box({ -3.3f, -0.4f, -11.7f }, { 3.3f, 0.0f, -MHZ }, C(52, 40, 30), MAT_WOOD, 0.0f);
    box({ -3.3f, 0.0f, -11.7f }, { 3.3f, F0, -MHZ }, C(54, 42, 32), MAT_WOOD, 0.0f);
    for (int s = -1; s <= 1; s += 2) {
        box({ s * 3.0f - 0.14f, F0, -11.5f }, { s * 3.0f + 0.14f, 3.4f, -11.22f }, C(60, 56, 50), MAT_WOOD, 0.4f);
        addCirc(s * 3.0f, -11.36f, 0.22f);
    }
    box({ -3.4f, 3.4f, -11.8f }, { 3.4f, 3.6f, -MHZ }, C(44, 36, 30), MAT_WOOD, 0.4f);
    quadAuto({ -3.6f, 3.6f, -11.9f }, { 3.6f, 3.6f, -11.9f }, { 3.6f, 4.6f, -8.0f }, { -3.6f, 4.6f, -8.0f }, roofC, MAT_ROOF);
    box({ -3.0f, F0 + 0.8f, -11.5f }, { -1.6f, F0 + 0.86f, -11.45f }, C(40, 30, 22), MAT_WOOD, 0.8f);   // a bit of railing
    box({ 1.6f, F0 + 0.8f, -11.5f }, { 3.0f, F0 + 0.86f, -11.45f }, C(40, 30, 22), MAT_WOOD, 0.8f);
    box({ -2.0f, -0.3f, -12.3f }, { 2.0f, 0.0f, -11.7f }, C(70, 66, 60), MAT_STONE);                     // step
    // shutters, hanging crooked
    for (float x : { -8.0f, 8.0f }) {
        xfSet(x - 0.95f, 0, -MHZ - 0.2f, frr(-0.1f, 0.1f)); box({ -0.3f, 0.9f, 0 }, { 0.3f, 2.3f, 0.05f }, C(34, 40, 34), MAT_WOOD, 0.8f);
        xfSet(x + 0.95f, 0, -MHZ - 0.2f, frr(-0.35f, 0.1f)); box({ -0.3f, 0.9f, 0 }, { 0.3f, 2.3f, 0.05f }, C(34, 40, 34), MAT_WOOD, 0.8f); xfId();
    }

    // ================= ground floor rooms
    // --- living room (x -12..-4, z -8..0): fireplace on the west wall
    box({ -11.9f, 0, -5.2f }, { -10.9f, 1.3f, -2.8f }, C(68, 62, 56), MAT_STONE);
    box({ -11.95f, 1.3f, -5.35f }, { -10.75f, 1.42f, -2.65f }, C(40, 30, 22), MAT_WOOD, 0.8f);
    box({ -11.9f, 1.42f, -4.7f }, { -11.3f, H0, -3.3f }, C(68, 62, 56), MAT_STONE);
    quadv({ -10.89f, 0.1f, -4.3f }, { -10.89f, 0.1f, -3.7f }, { -10.89f, 0.95f, -3.7f }, { -10.89f, 0.95f, -4.3f }, { 1, 0, 0 }, C(3, 2, 2), MAT_PLAIN, 1, 0, 0, 1, 1);
    walls.push_back({ -11.95f, -5.35f, -10.75f, -2.65f, 0, -1 });
    sigilDecal(-11.84f, 2.3f, -4.0f, -PI / 2, 0.8f, C(120, 10, 8));
    box({ -9.4f, 0.01f, -6.2f }, { -6.4f, 0.04f, -1.8f }, C(76, 30, 30), MAT_FABRIC);                       // rug
    fbox(-8.1f, -4.0f, 0.9f, 2.4f, 0, 0.4f, 0, C(66, 56, 44), MAT_FABRIC, 0, 0);                              // couch seat
    fbox(-8.55f, -4.0f, 0.25f, 2.4f, 0.4f, 0.55f, 0, C(66, 56, 44), MAT_FABRIC, 0, 0, false);
    fbox(-6.4f, -5.9f, 0.9f, 0.9f, 0, 0.45f, 0.5f, C(52, 44, 36), MAT_FABRIC, 0, 0);                          // armchair
    table(-9.8f, -4.0f, 0.7f, 1.2f, 0.45f, 0, 0, C(44, 30, 22), 0);
    bookshelf(-5.0f, -7.55f, 0, 1.6f, 2.2f, 0, 0);
    frameArt(-4.12f, 1.9f, -2.0f, -PI / 2, 0.7f, 0.9f, true);
    addLamp({ -11.0f, 1.6f, -4.0f }, C(255, 80, 20), 6, 0.6f, 0);
    // --- dining room (x -12..-4, z 0..8)
    table(-8.0f, 4.0f, 3.2f, 1.2f, 0.78f, 0, 0, C(48, 32, 22), 0);
    for (int i = 0; i < 3; i++) { chair(-9.2f + i * 1.2f, 2.95f, PI, 0, C(40, 28, 20), 0); chair(-9.2f + i * 1.2f, 5.05f, 0, 0, C(40, 28, 20), 0); }
    chair(-10.0f, 4.0f, -PI / 2 + 0.3f, 0, C(40, 28, 20), 0); chair(-6.0f, 4.0f, PI / 2, 0, C(40, 28, 20), 0);
    fbox(-11.5f, 4.0f, 0.55f, 2.6f, 0, 0.95f, 0, C(40, 28, 20), MAT_WOOD, 0.8f, 0);
    fbox(-8.0f, 0.55f, 1.8f, 0.5f, 0, 0.9f, 0, C(40, 28, 20), MAT_WOOD, 0.8f, 0);
    box({ -9.3f, 0.78f, 3.7f }, { -6.7f, 0.80f, 4.3f }, C(150, 140, 120), MAT_FABRIC);                      // runner
    for (float cx : { -9.0f, -7.0f }) { tube2({ cx, 0.78f, 4.0f }, { cx, 1.1f, 4.0f }, 0.02f, 0.02f, 5, C(200, 190, 160), MAT_PLAIN); ellipsoid({ cx, 1.13f, 4.0f }, { 0.012f, 0.03f, 0.012f }, C(255, 190, 80), MAT_EMIT, 0.9f, 4, 5); }
    addLamp({ -8.0f, 1.25f, 4.0f }, C(255, 170, 80), 8, 0.35f, 0);
    frameArt(-8.0f, 1.9f, 7.82f, PI, 1.0f, 1.3f, true);
    // --- library (x 4..12, z -8..0)
    bookshelf(11.6f, -2.0f, -PI / 2, 2.4f, 2.5f, 0, 0); bookshelf(11.6f, -6.0f, -PI / 2, 2.4f, 2.5f, 0, 0);
    bookshelf(8.0f, -7.55f, 0, 2.6f, 2.5f, 0, 0); bookshelf(5.0f, -7.55f, 0, 1.6f, 2.5f, 0, 0); bookshelf(11.0f, -7.55f, 0, 1.6f, 2.5f, 0, 0);
    table(7.5f, -3.5f, 1.8f, 0.9f, 0.76f, 0.2f, 0, C(36, 26, 20), 0); chair(7.4f, -2.4f, PI, 0, C(40, 28, 20), 0);
    fbox(6.0f, -0.9f, 0.9f, 0.9f, 0, 0.5f, -0.3f, C(56, 40, 30), MAT_FABRIC, 0, 0);
    box({ 5.5f, 0.01f, -5.5f }, { 9.0f, 0.04f, -2.0f }, C(46, 54, 66), MAT_FABRIC);
    sigilDecal(4.16f, 1.9f, -2.0f, PI / 2, 0.5f, C(110, 8, 8));
    // --- kitchen (x 4..12, z 0..8)
    fbox(8.0f, 7.5f, 7.0f, 0.65f, 0, 0.92f, 0, C(80, 80, 70), MAT_WOOD, 0.8f, 0);
    fbox(11.6f, 4.5f, 0.65f, 5.0f, 0, 0.92f, 0, C(74, 74, 66), MAT_WOOD, 0.8f, 0);
    fbox(5.0f, 7.4f, 1.0f, 0.75f, 0, 1.0f, 0, C(28, 28, 30), MAT_METAL, 0, 0);                              // stove
    table(8.0f, 3.8f, 2.0f, 1.1f, 0.85f, 0, 0, C(86, 82, 70), 0);
    chair(7.2f, 2.8f, PI, 0, C(70, 56, 40), 0); chair(8.8f, 2.8f, PI + 0.4f, 0, C(70, 56, 40), 0);
    box({ 7.4f, 0.85f, 3.5f }, { 7.9f, 0.9f, 4.0f }, C(130, 120, 100), MAT_FABRIC);
    // --- hall
    fbox(0, 7.5f, 2.4f, 0.5f, 0, 0.8f, 0, C(40, 28, 20), MAT_WOOD, 0.8f, 0);                                  // console table
    fbox(3.3f, 6.0f, 0.55f, 0.45f, 0, 2.1f, 0, C(36, 26, 20), MAT_WOOD, 0.8f, 0);                             // grandfather clock
    box({ 3.15f, 1.5f, 5.77f }, { 3.45f, 1.85f, 5.78f }, C(140, 130, 100), MAT_FABRIC);
    box({ -2.4f, 0.01f, -7.5f }, { 2.4f, 0.04f, 7.4f }, C(66, 26, 28), MAT_FABRIC);                           // hall carpet
    frameArt(-3.84f, 1.9f, -6.0f, PI / 2, 0.8f, 1.0f, true); frameArt(3.84f, 1.9f, -6.0f, -PI / 2, 0.8f, 1.0f, true);
    frameArt(-3.84f, 1.9f, 6.5f, PI / 2, 0.7f, 0.9f, true);
    sigilDecal(0, 2.4f, 7.84f, PI, 0.9f, C(130, 8, 6));
    addLamp({ 0, 2.6f, -5.5f }, C(180, 190, 255), 7, 0.15f, 0);
    // ================= upper floor
    float u = F1;
    // --- master bedroom (west south)
    bed(-9.5f, -4.0f, -PI / 2, u, 1);
    fbox(-11.4f, -1.5f, 0.6f, 1.6f, 0, 1.8f, 0, C(40, 28, 20), MAT_WOOD, 0.8f, 1);
    fbox(-6.0f, -7.3f, 1.2f, 0.5f, u, 0.8f, 0, C(40, 28, 20), MAT_WOOD, 0.8f, 1);
    boxC({ -4.3f, u + 1.2f, -5.0f }, { 0.04f, 0.8f, 0.5f }, C(8, 12, 14), MAT_GLASS);                          // mirror
    box({ -9.0f, u + 0.01f, -6.0f }, { -6.0f, u + 0.04f, -2.0f }, C(60, 50, 70), MAT_FABRIC);
    sigilDecal(-11.84f, u + 2.2f, -4.0f, -PI / 2, 0.8f, C(120, 10, 8));
    // --- nursery (west north)
    xfSet(-10.5f, u, 5.0f, 0.0f);
    box({ -0.5f, 0.3f, -0.9f }, { 0.5f, 0.38f, 0.9f }, C(70, 56, 40), MAT_WOOD, 0.8f);
    for (float z = -0.85f; z <= 0.9f; z += 0.17f) { box({ -0.5f, 0.38f, z - 0.01f }, { -0.47f, 1.0f, z + 0.01f }, C(70, 56, 40), MAT_WOOD, 0.8f); box({ 0.47f, 0.38f, z - 0.01f }, { 0.5f, 1.0f, z + 0.01f }, C(70, 56, 40), MAT_WOOD, 0.8f); }
    box({ -0.5f, 0.38f, -0.9f }, { 0.5f, 1.05f, -0.87f }, C(70, 56, 40), MAT_WOOD, 0.8f); box({ -0.5f, 0.38f, 0.87f }, { 0.5f, 1.05f, 0.9f }, C(70, 56, 40), MAT_WOOD, 0.8f);
    xfId(); addBoxCol(-10.5f, 5.0f, 0.55f, 0.95f, 0, 1);
    xfSet(-7.0f, u, 6.0f, 0.5f);                                                                               // rocking horse
    box({ -0.5f, 0.0f, -0.05f }, { 0.5f, 0.05f, 0.05f }, C(70, 50, 36), MAT_WOOD, 0.8f);
    ellipsoid({ 0, 0.6f, 0 }, { 0.4f, 0.22f, 0.15f }, C(90, 80, 70), MAT_FABRIC, 1, 5, 8); ellipsoid({ 0.45f, 0.85f, 0 }, { 0.14f, 0.24f, 0.1f }, C(90, 80, 70), MAT_FABRIC, 1, 5, 8);
    xfId(); addBoxCol(-7.0f, 6.0f, 0.5f, 0.2f, 0.5f, 1);
    box({ -9.0f, u + 0.01f, 2.0f }, { -6.0f, u + 0.04f, 5.0f }, C(70, 82, 90), MAT_FABRIC);
    sigilDecal(-8.0f, u + 2.0f, 7.84f, PI, 0.9f, C(120, 10, 8));
    // --- study (east south)
    table(9.5f, -5.5f, 2.0f, 1.0f, 0.78f, 0, u, C(36, 26, 20), 1); chair(9.5f, -4.5f, PI, u, C(40, 28, 20), 1);
    bookshelf(11.6f, -2.5f, -PI / 2, 2.0f, 2.3f, u, 1); bookshelf(7.0f, -7.55f, 0, 2.0f, 2.3f, u, 1);
    box({ 5.5f, u + 0.01f, -7.0f }, { 8.5f, u + 0.04f, -2.5f }, C(60, 36, 30), MAT_FABRIC);
    // --- bath (east north)
    fbox(10.9f, 5.8f, 0.8f, 2.0f, u, 0.55f, 0, C(150, 150, 140), MAT_STONE, 0, 1);
    fbox(5.2f, 7.4f, 0.6f, 0.45f, u, 0.9f, 0, C(150, 150, 140), MAT_STONE, 0, 1);
    boxC({ 4.3f, u + 1.5f, 3.6f }, { 0.04f, 0.5f, 0.35f }, C(8, 12, 14), MAT_GLASS);
    // --- upstairs hall
    frameArt(0, u + 2.1f, -7.84f, 0, 0.9f, 1.2f, true);
    frameArt(3.84f, u + 2.0f, 2.0f, -PI / 2, 0.7f, 0.9f, true); frameArt(-3.84f, u + 2.0f, -6.0f, PI / 2, 0.7f, 0.9f, true);
    box({ -3.5f, u + 0.01f, -7.5f }, { -1.5f, u + 0.04f, 7.5f }, C(40, 52, 66), MAT_FABRIC);
    addLamp({ 0, u + 2.5f, -6.0f }, C(170, 190, 255), 7, 0.2f, 0);

    // ---- pickups
    picks.push_back({ 0, 0, { -11.3f, 1.62f, -4.0f }, 0, false });
    picks.push_back({ 0, 1, { -8.0f, 1.0f, 4.0f }, 0, false });
    picks.push_back({ 0, 2, { -9.5f, u + 1.0f, -4.0f }, 1, false });
    picks.push_back({ 0, 3, { -10.5f, u + 1.25f, 5.0f }, 1, false });
    picks.push_back({ 1, 0, { 11.55f, 0.97f, 4.0f }, 0, false });
    picks.push_back({ 1, 1, { 9.5f, u + 0.82f, -5.7f }, 1, false });
    picks.push_back({ 1, 2, { 5.2f, u + 0.98f, 7.4f }, 1, false });
    picks.push_back({ 2, 0, { 7.2f, 0.78f, -3.5f }, 0, false });
    picks.push_back({ 2, 1, { 9.9f, u + 0.8f, -5.5f }, 1, false });
    picks.push_back({ 2, 2, { -6.0f, u + 0.84f, -7.3f }, 1, false });

    // ---- spawn nodes for the entity's blinks
    for (Node n : { Node{ -8, -4, 0 }, Node{ -8, 4, 0 }, Node{ 8, -4, 0 }, Node{ 8, 4, 0 }, Node{ 0, -5, 0 }, Node{ 0, -2, 0 }, Node{ 2.5f, 4, 0 }, Node{ -3, 6.5f, 0 },
                    Node{ 0, -4, 1 }, Node{ 2.5f, 3, 1 }, Node{ -2.5f, 3, 1 }, Node{ 0, 7, 1 }, Node{ -8, -4, 1 }, Node{ -8, 4, 1 }, Node{ 8, -4, 1 }, Node{ 8, 4, 1 },
                    Node{ 0, -11, 0 }, Node{ -6, -12, 0 }, Node{ 6, -12, 0 }, Node{ 0, -18, 0 } }) nodes.push_back(n);
    xfId();
}

static void buildWorld() {
    rngS = 0xC0FFEEu;
    genTerrain();
    genForest();
    genCamp();
    genCar();
    genMansion();
    for (size_t i = 0; i < doors.size(); i++) {               // navigation nodes either side of every doorway
        Door& d = doors[i];
        V3 c = d.hinge + d.u * (d.w * 0.5f);
        for (int s = -1; s <= 1; s += 2) nodes.push_back({ c.x + d.v.x * 1.3f * s, c.z + d.v.z * 1.3f * s, d.lvl });
    }
}
