// HOLLOWMERE - game state, player, entity AI, events, dynamic geometry.
#pragma once
#include "hm_world.h"
#include "hm_audio.h"
#include "hm_shaders.h"

enum { ST_MENU, ST_PLAY, ST_DEAD, ST_WIN, ST_PAUSE };
enum { MD_HIDDEN, MD_SIGHT, MD_HUNT };

struct Sub { std::string text; float t, dur; bool note; };
struct Ent { V3 pos; int level; float yaw; int mode; float alpha, seenT, unseenT, blinkT, anim, ttl, poseA, poseB, poseC; bool frozen; };

static struct Game {
    int state = ST_MENU, stage = 0, sigils = 0;
    float time = 0, playT = 0;
    // player
    V3 pos = { CAMP.x + 2.6f, 0, CAMP.z - 3.2f };
    float yaw = 1.44f, pitch = 0.0f, dyaw = 1.44f, dpitch = 0.0f;
    V3 vel = { 0, 0, 0 };
    int level = 0; bool crouch = false, sprint = false, ramp = false;
    float eyeH = 1.65f, stamina = 1, bobPh = 0, stepAcc = 0, speedNow = 0;
    float battery = 100; bool flash = true, nv = false;
    V3 flashD = { 0, 0, 1 }, flashP = { 0, 0, 0 };
    float indoors = 0, fogNow = 0.03f;
    // camera result
    V3 eye = { 0, 1.65f, 0 }, camF = { 0, 0, 1 }, camR = { -1, 0, 0 }, camU = { 0, 1, 0 };
    // input
    bool keys[256] = {}, pressed[256] = {}; float mdx = 0, mdy = 0;
    // monster + events
    Ent e = {};
    float evT = 22, sightT = 34, fear = 0, scare = 0, shake = 0, blackout = 0, glow = 1, glowT = 1, fade = 1, fadeT = 1, loopT = 0, deathT = 0, winT = 0, holdT = 0, enterT = -1, huntT = -1, quakeT = 0, staticT = 0, introT = 0;
    int holdTarget = -1; bool loopArmed = false, loopLine = false, sawIt = false, wasDown = false;
    std::string prompt, objective;
    std::vector<Sub> subs;
    int lampIdx[4] = { -1, -1, -1, -1 };
    int carLamp = -1;
    float nextStepT = 0;
    // for the screenshot tool
    bool frozenSim = false;
} g;

static void say(const char* s, float dur = 4.5f, bool note = false) { g.subs.push_back({ s, 0, dur, note }); }
static bool lvlMatch(int wl, int pl) { return wl == 2 || wl == pl; }
static float angdiff(float a, float b) { float d = a - b; while (d > PI) d -= 2 * PI; while (d < -PI) d += 2 * PI; return d; }

static void sfx3(int type, V3 src, float vol, float a = 0, float b = 0) {
    V3 d = src - g.eye; float dist = len(d);
    float att = 1.0f / (1.0f + dist * 0.10f);
    V3 h = norm({ d.x, 0, d.z });
    Au::play(type, vol * att, dot(h, g.camR), a, b);
}

// ------------------------------------------------------------ line of sight (walls and closed doors)
static bool segHits(float ax, float az, float bx, float bz, const Wall& w) {
    float dx = bx - ax, dz = bz - az, t0 = 0, t1 = 1;
    float p[4] = { -dx, dx, -dz, dz }, q[4] = { ax - w.x0, w.x1 - ax, az - w.z0, w.z1 - az };
    for (int i = 0; i < 4; i++) {
        if (fabsf(p[i]) < 1e-8f) { if (q[i] < 0) return false; }
        else { float r = q[i] / p[i]; if (p[i] < 0) { if (r > t1) return false; t0 = std::max(t0, r); } else { if (r < t0) return false; t1 = std::min(t1, r); } }
    }
    return t0 <= t1;
}
static bool hasLOS(float ax, float az, float bx, float bz, int level) {
    for (auto& w : walls) {
        if (!lvlMatch(w.lvl, level)) continue;
        float tw = std::min(w.x1 - w.x0, w.z1 - w.z0), lw = std::max(w.x1 - w.x0, w.z1 - w.z0);
        if (!(tw <= 0.36f && lw >= 0.8f)) continue;
        if (w.door >= 0 && doors[w.door].open >= 0.55f) continue;
        if (segHits(ax, az, bx, bz, w)) return false;
    }
    return true;
}

// ------------------------------------------------------------ player physics
static float targetFloor(float x, float z, int level, bool& ramp) {
    ramp = false;
    if (inMansion(x, z)) {
        if (fabsf(x) < STAIR_X && z >= STAIR_Z0 && z < STAIR_Z1) {
            ramp = true;
            int s = (int)floorf((z - STAIR_Z0) / ((STAIR_Z1 - STAIR_Z0) / STEPS));
            return (std::min(s, STEPS - 1) + 1) * (F1 / STEPS);
        }
        return level == 1 ? F1 : F0;
    }
    return floorYOut(x, z);
}
static void resolvePlayer(float rad) {
    for (int it = 0; it < 3; it++) {
        for (auto& w : walls) {
            if (!lvlMatch(w.lvl, g.level)) continue;
            if (w.door >= 0 && doors[w.door].open >= 0.55f) continue;
            float cx = clampf(g.pos.x, w.x0, w.x1), cz = clampf(g.pos.z, w.z0, w.z1);
            float dx = g.pos.x - cx, dz = g.pos.z - cz, d2 = dx * dx + dz * dz;
            if (d2 < rad * rad) {
                if (d2 > 1e-8f) { float d = sqrtf(d2), push = rad - d; g.pos.x += dx / d * push; g.pos.z += dz / d * push; }
                else {
                    float l = g.pos.x - w.x0, r = w.x1 - g.pos.x, t = g.pos.z - w.z0, b = w.z1 - g.pos.z, m = std::min(std::min(l, r), std::min(t, b));
                    if (m == l) g.pos.x = w.x0 - rad; else if (m == r) g.pos.x = w.x1 + rad; else if (m == t) g.pos.z = w.z0 - rad; else g.pos.z = w.z1 + rad;
                }
            }
        }
        if (g.level == 0 && !inMansion(g.pos.x, g.pos.z, -0.3f)) {
            int cx = (int)clampf(floorf((g.pos.x + WHALF) / CELL), 0, NC - 1), cz = (int)clampf(floorf((g.pos.z + WHALF) / CELL), 0, NC - 1);
            for (int j = std::max(cz - 1, 0); j <= std::min(cz + 1, NC - 1); j++) for (int i = std::max(cx - 1, 0); i <= std::min(cx + 1, NC - 1); i++)
                for (int ci : cellCircs[j * NC + i]) {
                    Circ& c = circs[ci];
                    float dx = g.pos.x - c.x, dz = g.pos.z - c.z, d2 = dx * dx + dz * dz, rr = rad + c.r;
                    if (d2 < rr * rr && d2 > 1e-8f) { float d = sqrtf(d2), push = rr - d; g.pos.x += dx / d * push; g.pos.z += dz / d * push; }
                }
        }
    }
}

// ------------------------------------------------------------ doors
static V3 doorCenter(const Door& d) { return d.hinge + d.u * (d.w * 0.5f) + V3{ 0, d.h * 0.5f, 0 }; }
static void slamDoor(int i, float vol = 1.0f) {
    if (i < 0 || i >= (int)doors.size()) return;
    doors[i].target = 0; doors[i].speed = 18;
    (void)vol;
}
static void updateDoors(float dt) {
    for (size_t i = 0; i < doors.size(); i++) {
        Door& d = doors[i];
        float before = d.open;
        d.open += (d.target - d.open) * std::min(1.0f, dt * d.speed);
        if (fabsf(d.target - d.open) < 0.004f) d.open = d.target;
        if (d.speed > 10 && before > 0.05f && d.open <= 0.05f) {            // slammed
            sfx3(Au::S_SLAM, doorCenter(d), 1.3f);
            d.open = 0; d.speed = 3; g.scare = std::max(g.scare, 0.35f);
        }
    }
}

// ------------------------------------------------------------ lamps
static bool lampInHouse(const Lamp& l) { return inMansion(l.p.x, l.p.z, 0.2f) && l.p.y < 7; }
static float lampIntensity(const Lamp& l) {
    float f = l.on;
    float fl = 1 - l.flick * (0.5f + 0.5f * vnoise(g.time * (l.kind == 1 ? 11.0f : 6.0f) + l.seed, l.seed));
    f *= fl;
    if (lampInHouse(l) && g.blackout > 0) f *= 0.55f * (vnoise(g.time * 16.0f, l.seed) > 0.62f ? 1.0f : 0.0f) * clampf(g.blackout - 1.0f, 0, 1);
    if (g.glow < 0.5f && lampInHouse(l) && l.kind == 0) f *= g.glow * 2.0f;
    return f;
}
static void selectLamps(FrameCB& cb) {
    struct S { float s; int i; };
    std::vector<S> v;
    for (size_t i = 0; i < lamps.size(); i++) {
        if (lampIntensity(lamps[i]) < 0.01f) continue;
        v.push_back({ len(lamps[i].p - g.eye) - lamps[i].r * 0.6f, (int)i });
    }
    std::sort(v.begin(), v.end(), [](const S& a, const S& b) { return a.s < b.s; });
    for (int k = 0; k < 8; k++) {
        if (k < (int)v.size()) {
            Lamp& l = lamps[v[k].i]; float it = lampIntensity(l);
            cb.pl[k][0] = l.p.x; cb.pl[k][1] = l.p.y; cb.pl[k][2] = l.p.z; cb.pl[k][3] = l.r;
            float k2 = l.kind == 1 ? 3.2f : (l.kind == 2 ? 3.0f : (l.kind == 3 ? 9.0f : 2.4f));
            cb.pc[k][0] = l.c.r * it * k2; cb.pc[k][1] = l.c.g * it * k2; cb.pc[k][2] = l.c.b * it * k2; cb.pc[k][3] = 0;
        } else { cb.pl[k][0] = cb.pl[k][1] = cb.pl[k][2] = 0; cb.pl[k][3] = 0.0f; cb.pc[k][0] = cb.pc[k][1] = cb.pc[k][2] = cb.pc[k][3] = 0; }
    }
}

// ------------------------------------------------------------ reset
static void gameReset() {
    EnterCriticalSection(&Au::cs); Au::shots.clear(); LeaveCriticalSection(&Au::cs);
    g.stage = 0; g.sigils = 0; g.time = g.time; g.playT = 0;
    g.pos = { CAMP.x + 2.6f, 0, CAMP.z - 3.2f }; g.yaw = g.dyaw = 1.44f; g.pitch = g.dpitch = 0; g.vel = { 0, 0, 0 };
    g.level = 0; g.crouch = false; g.eyeH = 1.65f; g.stamina = 1; g.battery = 100; g.flash = true; g.nv = false;
    g.flashD = { cosf(1.44f), 0, sinf(1.44f) };
    g.e = {}; g.e.mode = MD_HIDDEN;
    g.evT = 22; g.sightT = 34; g.fear = g.scare = g.shake = g.blackout = 0; g.glow = g.glowT = 1; g.fade = 0; g.fadeT = 1;
    g.loopT = 0; g.deathT = g.winT = g.holdT = 0; g.enterT = g.huntT = -1; g.quakeT = g.staticT = 0; g.introT = 0; g.holdTarget = -1;
    g.loopLine = false; g.sawIt = false; g.subs.clear(); g.prompt.clear();
    for (auto& p : picks) p.got = false;
    static const float openInit[9] = { 0, 0.9f, 0, 0.9f, 0, 0.0f, 0.9f, 0, 0.9f };
    for (size_t i = 0; i < doors.size(); i++) { doors[i].open = doors[i].target = i < 9 ? openInit[i] : 0; doors[i].locked = false; doors[i].speed = 3; }
    for (auto& l : lamps) l.on = 1;
    for (int i = 0; i < 4; i++) if (g.lampIdx[i] >= 0) lamps[g.lampIdx[i]].on = 1;
    if (g.carLamp >= 0) { lamps[g.carLamp].on = 0; lamps[g.carLamp + 1].on = 0; }
    g.state = ST_PLAY;
    g.objective = "FIND THE LIGHT IN THE WOODS";
}

static void gameInit() {
    for (auto& p : picks) if (p.type == 0) {
        addLamp(p.p, C(255, 40, 20), 5.5f, 0.25f, 2);
        g.lampIdx[p.id] = (int)lamps.size() - 1;
    }
    g.carLamp = (int)lamps.size();
    addLamp({ CAR.x - 0.6f, 0.75f, CAR.z + 2.4f }, C(255, 245, 220), 24, 0.0f, 3);
    addLamp({ CAR.x + 0.6f, 0.75f, CAR.z + 2.4f }, C(255, 245, 220), 24, 0.0f, 3);
    lamps[g.carLamp].on = 0; lamps[g.carLamp + 1].on = 0;
}

// ------------------------------------------------------------ the entity
static V3 entHead() { return g.e.pos + V3{ 0, 2.2f, 0 }; }
static float entFloor(V3 p, int lvl) { return inMansion(p.x, p.z) ? (lvl == 1 ? F1 : F0) : floorYOut(p.x, p.z); }

static bool entityLit() {
    Ent& e = g.e;
    V3 to = entHead() - g.eye; float d = len(to);
    if (d < 0.2f) return true;
    V3 dir = to * (1 / d);
    if (dot(dir, g.camF) < 0.5f) return false;
    if (g.level != e.level && inMansion(g.pos.x, g.pos.z)) return false;
    if (!hasLOS(g.pos.x, g.pos.z, e.pos.x, e.pos.z, g.level)) return false;
    bool lit = (g.flash && g.battery > 0 && dot(dir, g.flashD) > 0.905f && d < 26) || d < 2.2f || (g.nv && d < 16);
    if (!lit) for (auto& l : lamps) if (l.kind != 3 && lampIntensity(l) > 0.3f && len(l.p - e.pos) < l.r * 0.7f) { lit = true; break; }
    return lit;
}

static void entPose() {
    Ent& e = g.e;
    e.poseA = frr(-0.7f, 0.7f); e.poseB = frand() < 0.5f ? frr(0.0f, 1.0f) : 0.0f; e.poseC = frand() < 0.4f ? frr(0.0f, 1.0f) : 0.0f;
}
static void entSpawn(V3 p, int lvl, int mode) {
    Ent& e = g.e;
    e.pos = p; e.pos.y = entFloor(p, lvl); e.level = lvl; e.mode = mode; e.alpha = 1; e.seenT = e.unseenT = 0; e.ttl = 0; e.frozen = true;
    e.blinkT = 3; entPose();
}
static float huntSpeed() { return g.stage >= 2 ? 4.1f : 1.5f + 0.55f * g.sigils; }
static float blinkInterval() { return g.stage >= 2 ? 1.8f : 7.0f - 1.3f * g.sigils; }

static void entBlink() {
    Ent& e = g.e;
    struct C2 { float d; int i; };
    std::vector<C2> c;
    int pl = g.level;
    for (size_t i = 0; i < nodes.size(); i++) {
        Node& n = nodes[i];
        if (n.lvl != pl) continue;
        float d = hypotf(n.x - g.pos.x, n.z - g.pos.z);
        if (d < 3.5f || d > 40) continue;
        V3 dir = norm({ n.x - g.pos.x, 0, n.z - g.pos.z });
        bool visible = dot(dir, norm({ g.camF.x, 0, g.camF.z })) > 0.3f && hasLOS(g.pos.x, g.pos.z, n.x, n.z, pl);
        if (visible) continue;
        if (hypotf(n.x - e.pos.x, n.z - e.pos.z) < 2.0f && e.level == n.lvl) continue;
        c.push_back({ d, (int)i });
    }
    if (c.empty()) return;
    std::sort(c.begin(), c.end(), [](const C2& a, const C2& b) { return a.d < b.d; });
    int pick = c[std::min((size_t)(rnd() % 2), c.size() - 1)].i;
    e.pos = { nodes[pick].x, entFloor({ nodes[pick].x, 0, nodes[pick].z }, nodes[pick].lvl), nodes[pick].z };
    e.level = nodes[pick].lvl; e.alpha = 1; entPose();
    sfx3(Au::S_THUD, e.pos, 0.5f);
}

static void killPlayer() {
    if (g.state != ST_PLAY) return;
    g.state = ST_DEAD; g.deathT = 0; g.staticT = 3;
    Au::play(Au::S_SCREAM, 1.2f, 0); Au::play(Au::S_STATIC, 0.8f, 0);
}

static void updateEntity(float dt) {
    Ent& e = g.e;
    if (e.mode == MD_HIDDEN) return;
    V3 toP = g.pos - e.pos; toP.y = 0; float dist = len(toP);
    if (dist > 0.01f) e.yaw = atan2f(toP.x, toP.z);
    bool seen = entityLit();
    bool wasFrozen = e.frozen; e.frozen = seen;
    if (seen && !wasFrozen) { entPose(); if (!g.sawIt) { g.sawIt = true; } }
    if (seen) { e.seenT += dt; e.unseenT = 0; g.scare = std::max(g.scare, 0.35f); } else { e.unseenT += dt; }

    if (e.mode == MD_SIGHT) {
        e.ttl += dt;
        if (e.seenT > 2.4f || dist < 8.0f) {                       // it was looked at too long: gone
            e.mode = MD_HIDDEN; Au::play(Au::S_STATIC, 0.35f, 0); Au::play(Au::S_STING, 0.6f, 0); g.scare = 1; g.staticT = 0.5f; return;
        }
        if (e.unseenT > 6.0f || e.ttl > 40) { e.mode = MD_HIDDEN; return; }
        return;
    }
    // hunting
    bool sameLvl = e.level == g.level || !inMansion(g.pos.x, g.pos.z);
    bool los = sameLvl && hasLOS(e.pos.x, e.pos.z, g.pos.x, g.pos.z, g.level);
    if (!seen) {
        if (los) {
            V3 d = toP * (1 / std::max(dist, 0.01f));
            float sp = huntSpeed();
            e.pos = e.pos + d * (sp * dt);
            e.anim += dt * sp * 2.4f;
            e.pos.y = entFloor(e.pos, e.level);
            if (dist < 0.95f) { killPlayer(); return; }
        } else {
            e.blinkT -= dt;
            if (e.blinkT <= 0 && e.unseenT > 1.2f) { entBlink(); e.blinkT = blinkInterval() * frr(0.7f, 1.2f); }
        }
    }
    if (dist < 0.95f && !seen) killPlayer();
}

// ------------------------------------------------------------ events
static int randomDoorNear(bool openOnly) {
    std::vector<int> c;
    for (size_t i = 1; i < doors.size(); i++) {
        if (doors[i].lvl != g.level) continue;
        V3 dc = doorCenter(doors[i]);
        if (len(dc - g.eye) < 2.0f) continue;
        if (openOnly && doors[i].open < 0.4f) continue;
        c.push_back((int)i);
    }
    return c.empty() ? -1 : c[rnd() % c.size()];
}
static V3 randomAround(float dmin, float dmax) {
    float a = frr(0, 2 * PI), d = frr(dmin, dmax);
    return { g.eye.x + cosf(a) * d, g.eye.y, g.eye.z + sinf(a) * d };
}
static void breakSeal(int id) {
    g.sigils++;
    Au::play(Au::S_BELL, 0.9f, 0); Au::play(Au::S_STING, 0.8f, 0);
    g.scare = 1; g.shake = 0.5f; g.staticT = 0.6f;
    if (g.lampIdx[id] >= 0) lamps[g.lampIdx[id]].on = 0;
    if (g.sigils == 1) {
        say("It's cold. Something just... exhaled.", 4.5f);
        g.blackout = 3.0f; slamDoor(randomDoorNear(true));
        g.huntT = 4.0f;
    } else if (g.sigils == 2) {
        say("It's in the house with me. It only moves when I look away.", 5.0f);
        for (int i = 0; i < 2; i++) slamDoor(randomDoorNear(true));
        g.blackout = 4.0f;
    } else if (g.sigils == 3) {
        say("Keep the light on it. Keep the light on it...", 5.0f);
        g.blackout = 6.0f; Au::play(Au::S_SCREAM, 0.3f, -0.6f);
        for (int i = 0; i < 3; i++) slamDoor(randomDoorNear(true));
    } else {                                                        // all four: the house lets go
        g.stage = 2; g.objective = "RUN. THE CAR IS SOUTH.";
        g.quakeT = 4.0f; g.shake = 1.0f; g.blackout = 0;
        doors[0].locked = false; doors[0].target = 1; doors[0].speed = 10;
        g.glowT = 1; g.nv = false;
        lamps[g.carLamp].on = 1; lamps[g.carLamp + 1].on = 1;
        Au::play(Au::S_SLAM, 1.4f, 0); Au::play(Au::S_SCREAM, 0.6f, 0);
        say("The front door... it's open. RUN.", 5.0f);
        g.battery = std::max(g.battery, 45.0f);
    }
}

static void enterHouse() {
    g.stage = 1; g.enterT = 0; g.glowT = 0;
    doors[0].target = 0; doors[0].speed = 16; doors[0].locked = true;
    g.objective = "BREAK THE FOUR SEALS";
    g.huntT = 26.0f;
}

static void ambientEvent() {
    float r = frand();
    bool indoors = g.indoors > 0.5f;
    if (!indoors) {
        V3 src = randomAround(10, 28);
        if (r < 0.40f) sfx3(Au::S_SNAP, src, 1.2f);
        else if (r < 0.68f) sfx3(Au::S_WHISPER, src, 1.0f);
        else if (r < 0.88f) { for (int i = 0; i < 4; i++) sfx3(Au::S_FOOT_LEAF, src, 0.8f); }
        else sfx3(Au::S_KNOCK, src, 0.7f);
    } else {
        V3 src = randomAround(5, 14);
        if (r < 0.22f) sfx3(Au::S_KNOCK, src, 1.0f);
        else if (r < 0.42f) sfx3(Au::S_WHISPER, src, 1.2f);
        else if (r < 0.56f) { int d = randomDoorNear(false); if (d >= 0) { doors[d].target = doors[d].target > 0.5f ? 0.0f : 0.8f; doors[d].speed = 1.2f; sfx3(Au::S_CREAK, doorCenter(doors[d]), 1.0f); } }
        else if (r < 0.72f) sfx3(Au::S_THUD, src, 1.2f);
        else if (r < 0.82f) g.blackout = 1.8f;
        else if (r < 0.92f) { int d = randomDoorNear(true); if (d >= 0) slamDoor(d); }
        else Au::play(Au::S_CHIME, 0.6f, 0);
    }
}

static void trySighting() {
    // somewhere ahead among the trees, visible in the torch beam
    if (g.e.mode != MD_HIDDEN) return;
    float ang = atan2f(g.camF.z, g.camF.x) + frr(-0.55f, 0.55f), d = frr(17, 25);
    V3 p = { g.pos.x + cosf(ang) * d, 0, g.pos.z + sinf(ang) * d };
    if (inMansion(p.x, p.z, 3)) return;
    entSpawn(p, 0, MD_SIGHT);
    g.e.yaw = atan2f(g.pos.x - p.x, g.pos.z - p.z);
}

static void updateInteract(float dt) {
    g.prompt.clear();
    int best = -1; bool bestDoor = false; float bestScore = 0;
    for (size_t i = 0; i < picks.size(); i++) {
        Pick& p = picks[i]; if (p.got || p.lvl != g.level) continue;
        V3 d = p.p - g.eye; float L = len(d); if (L > 2.4f) continue;
        float c = dot(d * (1 / L), g.camF); if (c < 0.9f) continue;
        float s = c - L * 0.1f; if (best < 0 || s > bestScore) { best = (int)i; bestScore = s; bestDoor = false; }
    }
    for (size_t i = 0; i < doors.size(); i++) {
        Door& d = doors[i]; if (d.lvl != g.level && !(i == 0)) continue;
        // aim at the nearest point on the leaf, at eye height if the eye is within the door's height
        float ang = d.open * 1.75f; V3 dir = d.u * cosf(ang) + d.v * sinf(ang);
        float tt = clampf(dot(g.eye - d.hinge, dir) / d.w, 0, 1);
        V3 tp = d.hinge + dir * (d.w * tt); tp.y = clampf(g.eye.y, d.hinge.y + 0.1f, d.hinge.y + d.h);
        V3 dd = tp - g.eye; float L = len(dd); if (L > 2.5f) continue;
        float c = L < 0.05f ? 1.0f : dot(dd * (1 / L), g.camF); if (c < 0.72f) continue;
        float s = c - L * 0.1f - 0.05f; if (best < 0 || s > bestScore) { best = (int)i; bestScore = s; bestDoor = true; }
    }
    bool down = g.keys['E'], edge = g.pressed['E'];
    if (best < 0) { g.holdT = std::max(0.0f, g.holdT - dt * 2); g.holdTarget = -1; return; }
    if (bestDoor) {
        Door& d = doors[best];
        g.prompt = d.locked ? "LOCKED" : (d.target > 0.5f ? "[E] CLOSE" : "[E] OPEN");
        if (edge) {
            if (d.locked) { sfx3(Au::S_RATTLE, doorCenter(d), 1.0f); if (g.stage == 1 && best == 0) say("It won't open. It won't OPEN.", 3.5f); }
            else { d.target = d.target > 0.5f ? 0.0f : 1.0f; d.speed = 3; sfx3(Au::S_CREAK, doorCenter(d), 0.9f, 110.0f + (rnd() % 60), d.target > 0.5f ? 1.2f : 0.9f); }
        }
        g.holdT = std::max(0.0f, g.holdT - dt * 2);
        return;
    }
    Pick& p = picks[best];
    if (p.type == 0) {
        g.prompt = "[HOLD E] BREAK THE SEAL";
        if (down) { if (g.holdTarget != best) { g.holdTarget = best; g.holdT = 0; } g.holdT += dt; g.scare = std::max(g.scare, 0.25f); if (g.holdT >= 1.7f) { p.got = true; g.holdT = 0; breakSeal(p.id); } }
        else g.holdT = std::max(0.0f, g.holdT - dt * 2);
    } else if (p.type == 1) {
        g.prompt = "[E] TAKE BATTERIES";
        if (edge) { p.got = true; g.battery = std::min(100.0f, g.battery + 45); Au::play(Au::S_PAPER, 0.7f, 0); say("Batteries. Thank God.", 2.5f); }
    } else {
        g.prompt = "[E] READ";
        if (edge) {
            p.got = true; Au::play(Au::S_PAPER, 0.9f, 0);
            static const char* notes[3] = {
                "1887. Hollowmere buried his wife and son under the cellar, then sealed the house with four marks. 'It will let no one leave until the debt is paid.'",
                "Hikers came in '71. Again in '82. Their camera was found on the porch, still recording. Nobody found the hikers.",
                "If you hear it behind you, DON'T TURN AROUND... no. Turn around. It only moves when you don't look. Keep the light on it. Break the marks. Then RUN." };
            say(notes[p.id], 9.0f, true);
        }
    }
}

// ------------------------------------------------------------ main update
static void gameUpdate(float dt) {
    g.time += dt;
    if (g.state == ST_PAUSE || g.state == ST_MENU) return;
    if (g.frozenSim) return;

    // fades and timers
    g.fade += (g.fadeT - g.fade) * std::min(1.0f, dt * 2.2f);
    g.glow += (g.glowT - g.glow) * std::min(1.0f, dt * 0.6f);
    g.blackout = std::max(0.0f, g.blackout - dt); g.shake = std::max(0.0f, g.shake - dt * 0.35f); g.scare = std::max(0.0f, g.scare - dt * 0.35f);
    g.quakeT = std::max(0.0f, g.quakeT - dt); g.staticT = std::max(0.0f, g.staticT - dt);
    if (!g.subs.empty()) { g.subs[0].t += dt; if (g.subs[0].t > g.subs[0].dur) g.subs.erase(g.subs.begin()); }

    if (g.state == ST_DEAD) {
        g.deathT += dt;
        V3 to = entHead() - g.eye; float ty = atan2f(to.z, to.x), tp = atan2f(to.y, hypotf(to.x, to.z));
        g.yaw += angdiff(ty, g.yaw) * std::min(1.0f, dt * 7); g.pitch += (tp - g.pitch) * std::min(1.0f, dt * 7);
        g.fear = 1; g.shake = 0.8f;
        if (g.deathT > 1.6f) g.fadeT = 0;
        Au::tension = 0.9f; Au::heart = 150; Au::panic = 1;
        return;
    }
    if (g.state == ST_WIN) {
        g.winT += dt; if (g.winT > 3.5f) g.fadeT = 0;
        Au::tension = 0; Au::heart = 0; Au::panic = 0;
        return;
    }
    g.playT += dt;

    // ---- intro lines
    g.introT += dt;
    if (g.stage == 0) {
        static const float at[4] = { 1.5f, 7.0f, 13.0f, 20.0f };
        static const char* line[4] = { "October 29th, 11:47 PM. Day three. Still no trail.", "Jess went to look for signal and never came back.", "The compass just spins. The trees all look the same.", "...that light, through the trees. A house? Somebody might be home." };
        static int shown = 0;
        if (g.introT < 0.1f) shown = 0;
        if (shown < 4 && g.introT > at[shown]) say(line[shown++], 5.0f);
    }

    // ---- player look and movement
    if (g.keys['W'] || g.keys['A'] || g.keys['S'] || g.keys['D'] || true) {
        g.yaw += g.mdx * 0.0022f; g.pitch = clampf(g.pitch - g.mdy * 0.0022f, -1.45f, 1.45f);
    }
    g.mdx = g.mdy = 0;
    float fwd = (g.keys['W'] ? 1.0f : 0.0f) - (g.keys['S'] ? 1.0f : 0.0f), str = (g.keys['D'] ? 1.0f : 0.0f) - (g.keys['A'] ? 1.0f : 0.0f);
    bool crouchKey = g.keys['C'] || g.keys[VK_CONTROL];
    g.crouch = crouchKey;
    bool wantSprint = g.keys[VK_SHIFT] && fwd > 0 && !g.crouch && g.stamina > 0.05f;
    if (g.stamina < 0.02f) g.sprint = false;
    g.sprint = wantSprint;
    float spd = g.crouch ? 1.35f : (g.sprint ? 4.5f : 2.7f);
    V3 f = { cosf(g.yaw), 0, sinf(g.yaw) }, r = { -sinf(g.yaw), 0, cosf(g.yaw) };
    V3 wish = f * fwd + r * str;
    if (len(wish) > 1) wish = norm(wish);
    wish = wish * spd * (fwd < 0 ? 0.75f : 1.0f);
    g.vel = lerp3(g.vel, wish, std::min(1.0f, dt * 9.0f));
    V3 old = g.pos;
    g.pos.x += g.vel.x * dt; g.pos.z += g.vel.z * dt;
    bool rampNow;
    targetFloor(g.pos.x, g.pos.z, g.level, rampNow);
    // level change on the stairs
    {
        float ty = targetFloor(g.pos.x, g.pos.z, g.level, rampNow);
        if (rampNow) { if (ty > 3.3f) g.level = 1; else if (ty < 0.3f) g.level = 0; }
    }
    resolvePlayer(0.32f);
    float ty = targetFloor(g.pos.x, g.pos.z, g.level, g.ramp);
    if (g.ramp) { if (ty > 3.3f) g.level = 1; else if (ty < 0.3f) g.level = 0; }
    g.pos.y += (ty - g.pos.y) * std::min(1.0f, dt * (g.ramp ? 16.0f : 12.0f));
    float moved = hypotf(g.pos.x - old.x, g.pos.z - old.z);
    g.speedNow = moved / std::max(dt, 1e-4f);
    float eyeT = g.crouch ? 1.05f : 1.65f; g.eyeH += (eyeT - g.eyeH) * std::min(1.0f, dt * 8.0f);
    // stamina
    if (g.sprint && g.speedNow > 1.5f) g.stamina = std::max(0.0f, g.stamina - dt / 6.5f); else g.stamina = std::min(1.0f, g.stamina + dt / (g.sprint ? 100.0f : 4.5f));
    if (g.stamina <= 0.0f) g.sprint = false;
    // footsteps and head bob
    float stride = g.sprint ? 1.25f : 1.75f;
    g.stepAcc += moved; g.bobPh += moved * (PI / stride);
    if (g.stepAcc >= stride && g.speedNow > 0.5f) {
        g.stepAcc = 0;
        bool wood = inMansion(g.pos.x, g.pos.z, 0.2f) || inPorch(g.pos.x, g.pos.z);
        float vol = (g.crouch ? 0.35f : (g.sprint ? 1.0f : 0.65f));
        Au::play(wood ? Au::S_FOOT_WOOD : Au::S_FOOT_LEAF, vol, (rnd() & 1) ? 0.15f : -0.15f);
    }
    g.indoors += ((inMansion(g.pos.x, g.pos.z, -0.2f) ? 1.0f : 0.0f) - g.indoors) * std::min(1.0f, dt * 3.0f);

    // ---- toggles
    if (g.pressed['F']) { g.flash = !g.flash; Au::play(Au::S_CLICK, 0.6f, 0.2f); }
    if (g.pressed['N']) { g.nv = !g.nv; Au::play(Au::S_CLICK, 0.6f, -0.2f); }
    if (g.flash && g.battery > 0) g.battery = std::max(0.0f, g.battery - dt * 100.0f / 420.0f);
    if (g.nv && g.battery > 0) g.battery = std::max(0.0f, g.battery - dt * 100.0f / 600.0f);
    if (g.battery <= 0 && g.nv) g.nv = false;

    updateInteract(dt);
    updateDoors(dt);

    // ---- story triggers
    if (g.stage == 0) {
        if (inMansion(g.pos.x, g.pos.z, -0.6f) && g.pos.z > -6.8f) { enterHouse(); }
        float rr = hypotf(g.pos.x, g.pos.z);
        if (rr > 106 && g.fadeT > 0.5f && g.loopT <= 0) { g.loopT = 1; g.fadeT = 0; }
        if (g.loopT > 0) {
            if (g.fade < 0.06f) {                                    // black: put the player back among the trees
                float a = atan2f(g.pos.z, g.pos.x);
                float nr = 62.0f;
                g.pos.x = -cosf(a) * nr + frr(-6, 6); g.pos.z = -sinf(a) * nr + frr(-6, 6);
                g.pos.y = groundH(g.pos.x, g.pos.z);
                g.yaw += PI * 0.0f; g.dyaw = g.yaw;
                g.loopT = 0; g.fadeT = 1;
                Au::play(Au::S_WHISPER, 0.9f, -0.5f); Au::play(Au::S_WHISPER, 0.7f, 0.6f);
                if (!g.loopLine) { g.loopLine = true; say("That stick figure... I've already passed it. Twice. The woods are looping.", 5.5f); }
            }
        }
    } else if (g.stage == 1) {
        g.enterT += dt;
        if (g.enterT > 0.0f && g.enterT - dt <= 0.0f) { Au::play(Au::S_SLAM, 1.5f, 0); g.scare = 1; g.shake = 0.7f; g.blackout = 1.5f; }
        if (g.enterT > 2.2f && g.enterT - dt <= 2.2f) say("The door... it locked itself behind me.", 4.0f);
        if (g.enterT > 8.0f && g.enterT - dt <= 8.0f) say("Carved marks on the walls. Four of them. They're holding this place shut.", 5.5f);
        if (g.huntT > 0) { g.huntT -= dt; if (g.huntT <= 0 && g.e.mode == MD_HIDDEN) {
            struct { float d; int i; } best = { -1, -1 };
            for (size_t i = 0; i < nodes.size(); i++) { if (nodes[i].lvl > 1 || nodes[i].z < -9) continue; float d = hypotf(nodes[i].x - g.pos.x, nodes[i].z - g.pos.z); bool vis = hasLOS(g.pos.x, g.pos.z, nodes[i].x, nodes[i].z, g.level) && nodes[i].lvl == g.level;
                if (d > 9 && !vis && (best.i < 0 || d < best.d)) { best.d = d; best.i = (int)i; } }
            if (best.i >= 0) { entSpawn({ nodes[best.i].x, 0, nodes[best.i].z }, nodes[best.i].lvl, MD_HUNT); g.e.blinkT = 4; }
            else g.huntT = 3;
            Au::play(Au::S_STING, 0.7f, 0);
        } }
        if (g.e.mode == MD_HIDDEN && g.huntT <= 0 && g.enterT > 30) g.huntT = 6;
    } else if (g.stage == 2) {
        if (g.e.mode == MD_HIDDEN || g.e.mode == MD_SIGHT) {
            g.e.mode = MD_HUNT;
            if (g.e.pos.x == 0 && g.e.pos.z == 0) entSpawn({ 0, 0, 0.0f }, 0, MD_HUNT);
        }
        float dc = hypotf(g.pos.x - CAR.x, g.pos.z - CAR.z);
        if (dc < 4.5f) { g.state = ST_WIN; g.winT = 0; Au::play(Au::S_ENGINE, 0.9f, 0); }
    }

    // ---- ambient scares and outdoor sightings
    g.evT -= dt;
    if (g.evT <= 0) { ambientEvent(); g.evT = frr(g.stage ? 9.0f : 13.0f, g.stage ? 20.0f : 28.0f); }
    if (g.stage == 0 && g.introT > 28) {
        g.sightT -= dt;
        if (g.sightT <= 0) { trySighting(); g.sightT = frr(40, 75); if (!g.sawIt) g.sightT = 12; }
    }

    updateEntity(dt);

    // ---- fear and the sound bed
    float prox = 0;
    if (g.e.mode != MD_HIDDEN) { float d = hypotf(g.e.pos.x - g.pos.x, g.e.pos.z - g.pos.z); prox = powf(clampf(1 - d / 15.0f, 0, 1), 1.4f); if (g.e.level != g.level && inMansion(g.pos.x, g.pos.z)) prox *= 0.4f; }
    float fearT = std::max(prox, g.scare * 0.8f);
    g.fear += (fearT - g.fear) * std::min(1.0f, dt * 2.5f);
    Au::tension = clampf((g.stage == 0 ? 0.05f : 0.22f) + g.fear * 0.7f, 0, 1);
    Au::heart = g.fear > 0.25f ? 62 + g.fear * 105 : 0;
    Au::panic = g.fear;
    Au::inside = g.indoors;
    Au::wind = 0.7f;
    Au::quiet = g.crouch ? 0.0f : 0.0f;
    float fogT = lerpf(0.032f, 0.013f, g.indoors);
    g.fogNow += (fogT - g.fogNow) * std::min(1.0f, dt * 2);
    for (auto& k : g.pressed) k = false;
}

// ------------------------------------------------------------ camera
static void gameCamera(float dt) {
    float k = 1 - expf(-dt * 24);
    g.dyaw += (g.yaw - g.dyaw) * k; g.dpitch += (g.pitch - g.dpitch) * k;
    float t = g.time;
    float amp = 0.0012f + g.fear * 0.006f + g.shake * 0.02f + (g.quakeT > 0 ? 0.02f : 0);
    float jy = (vnoise(t * 1.7f, 3.1f) - 0.5f) * 0.014f + (vnoise(t * 9.0f, 7.0f) - 0.5f) * amp;
    float jp = (vnoise(t * 1.3f, 9.4f) - 0.5f) * 0.012f + (vnoise(t * 8.0f, 1.0f) - 0.5f) * amp;
    float bobAmt = clampf(g.speedNow / 2.7f, 0, 1.7f);
    float bobY = sinf(g.bobPh * 2) * 0.03f * bobAmt;
    float rollB = cosf(g.bobPh) * 0.012f * bobAmt + (vnoise(t * 1.1f, 5.5f) - 0.5f) * 0.02f + (vnoise(t * 7.0f, 2.2f) - 0.5f) * amp * 1.2f;
    float pv = clampf(g.dpitch + jp + sinf(g.bobPh * 2) * 0.006f * bobAmt, -1.5f, 1.5f), yw = g.dyaw + jy + cosf(g.bobPh) * 0.004f * bobAmt;
    g.eye = { g.pos.x, g.pos.y + g.eyeH + bobY, g.pos.z };
    g.camF = { cosf(pv) * cosf(yw), sinf(pv), cosf(pv) * sinf(yw) };
    V3 s = norm({ -g.camF.z, 0, g.camF.x }), u = cross(s, g.camF);
    g.camR = s * cosf(rollB) + u * sinf(rollB); g.camU = u * cosf(rollB) - s * sinf(rollB);
    float fk = 1 - expf(-dt * 8.0f);
    g.flashD = norm(lerp3(g.flashD, g.camF, fk));
    g.flashP = g.eye + g.camR * 0.2f + g.camU * -0.15f + g.camF * 0.25f;
}

// ------------------------------------------------------------ dynamic geometry: doors, pickups, the entity, headlights
static void drawEntity(const Ent& e) {
    Col skin = C(168, 164, 156), dark = C(14, 12, 12);
    float al = e.alpha;
    float t = g.time, ph = e.anim;
    float moving = (e.mode == MD_HUNT && !e.frozen) ? 1.0f : 0.0f;
    float sw = sinf(floorf(ph * 5.0f) / 5.0f * 2.0f) * 0.32f * moving;          // stepped, stuttering gait
    float sway = sinf(t * 1.3f) * 0.02f;
    xfSet(e.pos.x, e.pos.y, e.pos.z, e.yaw);
    // legs
    for (int s = -1; s <= 1; s += 2) {
        float z = sw * s;
        V3 p[4] = { { s * 0.11f, 1.08f, 0 }, { s * 0.12f, 0.58f, 0.08f + z * 0.5f }, { s * 0.1f, 0.12f, z }, { s * 0.1f, 0.03f, z + 0.12f } };
        float r[4] = { 0.065f, 0.05f, 0.032f, 0.03f };
        tube(p, r, 4, 6, skin, MAT_SKIN, al);
    }
    // spine and ribcage
    V3 sp[4] = { { 0, 1.0f, 0 }, { 0, 1.35f, 0.04f }, { 0, 1.72f, 0.09f }, { 0, 1.92f, 0.14f } };
    float sr[4] = { 0.13f, 0.17f, 0.2f, 0.12f };
    tube(sp, sr, 4, 8, skin, MAT_SKIN, al);
    for (int rb = 0; rb < 4; rb++) { float y = 1.5f + rb * 0.1f; ellipsoid({ 0, y, 0.1f + rb * 0.01f }, { 0.18f, 0.012f, 0.1f }, mixc(skin, dark, 0.35f), MAT_SKIN, al, 3, 8); }
    // arms: very long
    for (int s = -1; s <= 1; s += 2) {
        float raise = s < 0 ? e.poseB : e.poseC;
        float swing = sinf(t * 0.9f + s) * 0.03f + sw * -s * 0.5f;
        V3 sh = { s * 0.25f, 1.88f, 0.12f };
        V3 el = lerp3({ s * 0.3f, 1.32f, 0.14f + swing }, { s * 0.38f, 2.0f, 0.4f }, raise);
        V3 ha = lerp3({ s * 0.31f, 0.7f + sway, 0.18f + swing }, { s * 0.3f, 2.35f, 0.75f }, raise);
        V3 ap[3] = { sh, el, ha }; float ar[3] = { 0.045f, 0.035f, 0.025f };
        tube(ap, ar, 3, 6, skin, MAT_SKIN, al);
        for (int fng = 0; fng < 4; fng++) {                                     // long fingers
            V3 dir = norm(lerp3({ (fng - 1.5f) * 0.25f, -1, 0.15f }, { (fng - 1.5f) * 0.35f, 0.6f, 0.9f }, raise));
            tube2(ha, ha + dir * 0.3f, 0.011f, 0.006f, 4, skin, MAT_SKIN, al);
        }
    }
    // head and neck
    float tilt = e.poseA;
    V3 neck = { 0, 1.98f, 0.16f }, head = neck + V3{ sinf(tilt) * 0.16f, cosf(tilt) * 0.28f, 0.1f };
    tube2(neck, head, 0.045f, 0.04f, 6, skin, MAT_SKIN, al);
    ellipsoid(head + V3{ sinf(tilt) * 0.03f, 0.12f, 0.02f }, { 0.105f, 0.2f, 0.115f }, skin, MAT_SKIN, al, 8, 10);
    V3 hc = head + V3{ sinf(tilt) * 0.03f, 0.12f, 0.02f };
    ellipsoid(hc + V3{ -0.045f, 0.03f, 0.1f }, { 0.026f, 0.042f, 0.02f }, dark, MAT_PLAIN, 1, 4, 6);
    ellipsoid(hc + V3{ 0.045f, 0.03f, 0.1f }, { 0.026f, 0.042f, 0.02f }, dark, MAT_PLAIN, 1, 4, 6);
    ellipsoid(hc + V3{ 0, -0.1f, 0.105f }, { 0.016f, 0.075f, 0.016f }, dark, MAT_PLAIN, 1, 4, 6);
    // rags
    if (al > 0.6f) for (int i = 0; i < 6; i++) {
        float a = i * PI / 3; float sx = cosf(a) * 0.16f, sz = sinf(a) * 0.12f + 0.05f;
        quadAuto({ sx - 0.05f, 1.05f, sz }, { sx + 0.05f, 1.05f, sz }, { sx + 0.07f + sway, 0.45f, sz + 0.02f }, { sx - 0.07f, 0.4f, sz + 0.01f }, C(22, 19, 17), MAT_FABRIC);
    }
    xfId();
}

static void buildDynamic(Mesh& m) {
    m.v.clear(); M = &m; xfId();
    // doors
    for (size_t i = 0; i < doors.size(); i++) {
        Door& d = doors[i];
        float ang = d.open * 1.75f;
        V3 dir = d.u * cosf(ang) + d.v * sinf(ang);
        V3 nrm = norm(cross(V3{ 0, 1, 0 }, dir));
        V3 o = d.hinge - nrm * (d.thick * 0.5f);
        Col c = i == 0 ? C(30, 22, 16) : d.col;
        boxAxes(o, dir * d.w, { 0, d.h, 0 }, nrm * d.thick, c, MAT_WOOD, 0.8f);
        boxAxes(o + dir * (d.w * 0.08f) + V3{ 0, d.h * 0.58f, 0 } + nrm * (-0.0f), dir * (d.w * 0.84f), { 0, d.h * 0.3f, 0 }, nrm * (d.thick + 0.01f), mixc(c, C(0, 0, 0), 0.4f), MAT_WOOD, 0.8f);
        boxAxes(o + dir * (d.w - 0.12f) + V3{ 0, 1.0f, 0 } - nrm * 0.03f, dir * 0.06f, { 0, 0.06f, 0 }, nrm * (d.thick + 0.07f), C(110, 90, 40), MAT_METAL);
    }
    // pickups
    float t = g.time;
    for (auto& p : picks) {
        if (p.got) continue;
        if (p.type == 0) {
            float bob = sinf(t * 1.6f + p.id) * 0.03f, spin = t * 0.8f + p.id;
            xfSet(p.p.x, p.p.y + bob, p.p.z, spin);
            ellipsoid({ 0, 0, 0 }, { 0.014f, 0.24f, 0.24f }, C(60, 40, 26), MAT_WOOD, 0.8f, 6, 12);
            quadv({ 0.02f, -0.22f, -0.22f }, { 0.02f, -0.22f, 0.22f }, { 0.02f, 0.22f, 0.22f }, { 0.02f, 0.22f, -0.22f }, { 1, 0, 0 }, C(255, 30, 18), MAT_SIGIL, 0.5f, -1, -1, 1, 1);
            quadv({ -0.02f, -0.22f, 0.22f }, { -0.02f, -0.22f, -0.22f }, { -0.02f, 0.22f, -0.22f }, { -0.02f, 0.22f, 0.22f }, { -1, 0, 0 }, C(255, 30, 18), MAT_SIGIL, 0.5f, -1, -1, 1, 1);
            xfId();
        } else if (p.type == 1) {
            xfSet(p.p.x, p.p.y, p.p.z, p.id * 1.3f);
            tube2({ -0.05f, 0.0f, 0 }, { 0.05f, 0.0f, 0 }, 0.016f, 0.016f, 8, C(170, 170, 160), MAT_METAL);
            tube2({ 0.05f, 0.0f, 0 }, { 0.06f, 0.0f, 0 }, 0.008f, 0.008f, 6, C(190, 130, 60), MAT_METAL);
            tube2({ -0.05f, 0.0f, 0.04f }, { 0.05f, 0.0f, 0.04f }, 0.016f, 0.016f, 8, C(170, 170, 160), MAT_METAL);
            ellipsoid({ 0, 0.03f, 0.02f }, { 0.012f, 0.012f, 0.012f }, C(60, 255, 90), MAT_EMIT, 0.0f, 3, 5);
            xfId();
        } else {
            xfSet(p.p.x, p.p.y, p.p.z, p.id * 0.9f + 0.4f);
            box({ -0.11f, 0, -0.15f }, { 0.11f, 0.004f, 0.15f }, C(205, 194, 160), MAT_FABRIC);
            xfId();
        }
    }
    // headlights
    if (g.stage >= 2) {
        for (int s = -1; s <= 1; s += 2) ellipsoid({ CAR.x + s * 0.6f, 0.78f, CAR.z + 2.12f }, { 0.14f, 0.1f, 0.05f }, C(255, 250, 235), MAT_LIGHT, 1, 5, 8);
    }
    // the entity
    if (g.e.mode != MD_HIDDEN && g.e.alpha > 0.02f) drawEntity(g.e);
    M = nullptr;
}
