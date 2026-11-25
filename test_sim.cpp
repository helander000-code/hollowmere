// Headless gameplay test: walks the scripted route through the real game logic (no graphics).
#define _CRT_SECURE_NO_WARNINGS
#include "hm_game.h"

static void step(float dt = 1 / 120.0f) { gameUpdate(dt); gameCamera(dt); }
static void run(float sec) { for (int i = 0; i < (int)(sec * 120); i++) step(); }
static void look(float yaw, float pitch = 0) { g.yaw = g.dyaw = yaw; g.pitch = g.dpitch = pitch; gameCamera(1 / 120.0f); }
static void pr(const char* tag) { printf("%-28s pos=(%.2f, %.2f, %.2f) level=%d stage=%d state=%d ramp=%d\n", tag, g.pos.x, g.pos.y, g.pos.z, g.level, g.stage, g.state, g.ramp); }
static void walkTo(float x, float z, float maxSec = 12) {
    float t = 0;
    while (t < maxSec) {
        float dx = x - g.pos.x, dz = z - g.pos.z; if (hypotf(dx, dz) < 0.25f) break;
        look(atan2f(dz, dx));
        g.keys['W'] = true; step(); t += 1 / 120.0f;
    }
    g.keys['W'] = false; run(0.2f);
}
static void press(char k) { g.pressed[k] = true; g.keys[k] = true; step(); g.keys[k] = false; }

int main() {
    InitializeCriticalSection(&Au::cs);
    buildWorld(); gameInit(); gameReset();
    printf("world: %zu walls, %zu circles, %zu doors, %zu pickups, %zu nodes\n", walls.size(), circs.size(), doors.size(), picks.size(), nodes.size());
    g.introT = -1000;
    g.pos = { 0, 0, -16 }; g.pos.y = floorYOut(0, -16);
    // 1. walk into the closed front door
    walkTo(0, -5, 6); pr("blocked by closed door?");
    // 2. open it and go in
    look(PI / 2); g.pos.z = std::min(g.pos.z, -8.7f); look(PI / 2); run(0.1f);
    press('E'); run(1.2f); printf("door0 open=%.2f target=%.2f prompt='%s'\n", doors[0].open, doors[0].target, g.prompt.c_str());
    walkTo(0, -3, 8); pr("inside the foyer");
    run(2.5f); printf("front door locked=%d open=%.2f objective='%s'\n", doors[0].locked, doors[0].open, g.objective.c_str());
    // 3. stairs
    walkTo(0, -1.2f, 6); pr("at stair foot");
    g.keys['W'] = true; look(PI / 2);
    for (int i = 0; i < 120 * 8; i++) { step(); if (i % 120 == 0) pr("  climbing"); }
    g.keys['W'] = false;
    pr("top of stairs");
    walkTo(0, 7, 4); pr("landing");
    // 4. into the master bedroom (door x=-4, z=-4 upstairs): go around the stair opening
    walkTo(-2.6f, 7, 6); walkTo(-2.6f, -4, 10); pr("upstairs west strip");
    look(PI); g.pos.x = -3.2f; look(PI); run(0.1f);
    press('E'); run(1.0f); printf("door5 open=%.2f\n", doors[5].open);
    walkTo(-9, -4, 8); pr("in master bedroom");
    // 5. wall collision: try to walk through the west wall
    look(PI); g.keys['W'] = true; run(3); g.keys['W'] = false; pr("pushed into west wall");
    // 6. sigil pick
    look(PI / 2 + 0.0f); { Pick& p = picks[2]; V3 d = p.p - g.eye; look(atan2f(d.z, d.x), atan2f(d.y, hypotf(d.x, d.z))); printf("sigil dist=%.2f prompt='%s'\n", len(d), g.prompt.c_str()); }
    g.keys['E'] = true; run(2.2f); g.keys['E'] = false; printf("sigils=%d stage=%d prompt='%s'\n", g.sigils, g.stage, g.prompt.c_str());
    // 7. entity behaviour: stand still in the dark, then with the torch on it
    g.sigils = 1; g.stage = 1; g.flash = false; g.battery = 80;
    float t = 0; int blinks = 0; V3 last = g.e.pos;
    for (; t < 90 && g.state == ST_PLAY; t += 1 / 120.0f) {
        step();
        if (g.e.mode == MD_HUNT && len(g.e.pos - last) > 3.0f) blinks++;
        last = g.e.pos;
    }
    printf("after %.1fs in the dark: state=%s mode=%d blinks=%d dist=%.2f\n", t, g.state == ST_DEAD ? "DEAD" : "alive", g.e.mode, blinks, hypotf(g.e.pos.x - g.pos.x, g.e.pos.z - g.pos.z));
    // 8. "keep the light on it": a lit, watched entity must not move; once the torch goes off it must
    gameReset(); g.introT = -1000; g.stage = 1; g.sigils = 2;
    g.pos = { 0, F0, -5 }; look(PI / 2); g.flash = true; g.battery = 100;
    entSpawn({ 0, 0, 2.0f }, 0, MD_HUNT); g.e.blinkT = 100;
    V3 p0 = g.e.pos; run(4.0f);
    printf("lit + watched for 4s: moved %.2f m (expect ~0), frozen=%d\n", len(g.e.pos - p0), g.e.frozen);
    g.flash = false; p0 = g.e.pos; run(1.5f);
    printf("torch off for 1.5s:   moved %.2f m (expect ~%.1f), frozen=%d\n", len(g.e.pos - p0), huntSpeed() * 1.5f, g.e.frozen);
    g.flash = true; look(-PI / 2); p0 = g.e.pos; run(1.0f);
    printf("turned away, torch on: moved %.2f m (expect ~%.1f)\n", len(g.e.pos - p0), huntSpeed());
    // 9. stage 0: the woods loop
    gameReset(); g.introT = -1000; g.pos = { 100, 0, 40 }; g.pos.y = groundH(100, 40); look(0);
    g.keys['W'] = true; float t0 = 0; V3 start = g.pos; bool looped = false;
    for (; t0 < 20; t0 += 1 / 120.0f) { step(); if (hypotf(g.pos.x, g.pos.z) < 90) { looped = true; break; } }
    g.keys['W'] = false;
    printf("woods loop: started r=%.0f, looped back=%d after %.1fs, now r=%.0f\n", hypotf(start.x, start.z), looped, t0, hypotf(g.pos.x, g.pos.z));
    // 10. finish: break all four seals, then reach the car
    gameReset(); g.introT = -1000; g.stage = 1; doors[0].locked = true;
    for (int i = 0; i < 4; i++) { picks[i].got = true; breakSeal(i); }
    printf("seals broken: stage=%d front door locked=%d open target=%.0f\n", g.stage, doors[0].locked, doors[0].target);
    g.pos = { 0, 0, CAR.z + 20 }; g.pos.y = groundH(g.pos.x, g.pos.z); g.e.mode = MD_HIDDEN;
    walkTo(CAR.x, CAR.z + 2, 15); run(0.3f);
    printf("reached the car: state=%d (3 = win)\n", g.state);
    return 0;
}
