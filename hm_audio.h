// HOLLOWMERE - fully procedural audio (WinMM waveOut, no assets).
#pragma once
#include "hm_core.h"
#include <mmsystem.h>

namespace Au {
enum Snd { S_FOOT_LEAF = 1, S_FOOT_WOOD, S_CREAK, S_SLAM, S_WHISPER, S_SNAP, S_STING, S_BELL, S_KNOCK, S_BREATH,
           S_SCREAM, S_CLICK, S_RATTLE, S_STATIC, S_ENGINE, S_THUD, S_CHIME, S_PAPER };

static const int SR = 44100;
struct Shot { int type; float t, dur, vol, gl, gr, a, b; float s1, s2, s3, s4; uint32_t seed; };

static CRITICAL_SECTION cs;
static std::vector<Shot> shots;
// layer controls, written by the game thread
static volatile float wind = 0.6f, inside = 0, tension = 0, heart = 0, panic = 0, master = 1, quiet = 0;

static HWAVEOUT hwo;
static WAVEHDR hdr[4];
static short bufs[4][2 * 1024];
static HANDLE ev, th;
static volatile bool run = false;

static uint32_t ns = 2463534242u;
static inline float wn() { ns ^= ns << 13; ns ^= ns >> 17; ns ^= ns << 5; return (ns & 0xFFFF) / 32768.0f - 1.0f; }

static void play(int type, float vol, float pan = 0, float a = 0, float b = 0) {
    Shot s = {};
    s.type = type; s.vol = vol; s.a = a; s.b = b;
    float ang = (clampf(pan, -1, 1) + 1) * PI / 4;
    s.gl = cosf(ang); s.gr = sinf(ang);
    s.seed = (uint32_t)GetTickCount() * 2654435761u + type * 977u;
    static const float dur[] = { 0, 0.18f, 0.25f, 1.1f, 0.9f, 1.4f, 0.12f, 2.6f, 3.0f, 1.2f, 0.8f, 1.9f, 0.05f, 0.9f, 0.5f, 5.0f, 0.4f, 3.2f, 0.25f };
    s.dur = dur[type];
    if (type == S_CREAK && b > 0) s.dur = b;
    EnterCriticalSection(&cs);
    if (shots.size() < 48) shots.push_back(s);
    LeaveCriticalSection(&cs);
}

// one-shot synthesis; returns mono sample
static float shotSample(Shot& s) {
    float t = s.t, u = t / s.dur, x = 0;
    switch (s.type) {
    case S_FOOT_LEAF: {
        float n = wn();
        float env = expf(-t * 26.0f);
        float crack = (wn() > 0.82f) ? wn() : 0;
        x = (n * 0.55f + crack * 1.2f) * env;
        s.s1 += (x - s.s1) * 0.35f; x = x - s.s1 * 0.6f; break; }
    case S_FOOT_WOOD: {
        float f = 95 - t * 160;
        x = sinf(2 * PI * f * t) * expf(-t * 22) * 0.9f + wn() * expf(-t * 70) * 0.25f;
        break; }
    case S_CREAK: {
        float f = lerpf(s.a > 0 ? s.a : 140, (s.a > 0 ? s.a : 140) * 1.9f, u) * (1 + 0.07f * sinf(2 * PI * 7.3f * t));
        s.s1 += f / SR; s.s1 -= floorf(s.s1);
        float saw = 2 * s.s1 - 1;
        float k = 0.06f + 0.12f * (0.5f + 0.5f * sinf(t * 19));
        s.s2 += (saw - s.s2) * k;
        x = (s.s2 * 0.9f + wn() * 0.06f) * powf(sinf(PI * u), 0.6f); break; }
    case S_SLAM: {
        float n = wn(); s.s1 += (n - s.s1) * 0.12f;
        x = s.s1 * 2.4f * expf(-t * 8) + sinf(2 * PI * 46 * t) * expf(-t * 9) * 1.2f + n * expf(-t * 90) * 0.7f; break; }
    case S_WHISPER: {
        float n = wn();
        float c1 = 520 + 300 * sinf(u * 6 + (s.seed & 255) * 0.1f), c2 = 1700 + 700 * sinf(u * 9 + 1.3f);
        float f1 = 2 * sinf(PI * c1 / SR), f2 = 2 * sinf(PI * c2 / SR), q = 0.08f;
        s.s1 += f1 * s.s2; float hp = n - s.s1 - q * s.s2; s.s2 += f1 * hp;
        s.s3 += f2 * s.s4; float hp2 = n - s.s3 - q * s.s4; s.s4 += f2 * hp2;
        float syl = 0.35f + 0.65f * fabsf(sinf(u * 14 + (s.seed & 63)));
        x = (s.s2 * 0.5f + s.s4 * 0.9f) * powf(sinf(PI * u), 1.4f) * syl * 0.9f; break; }
    case S_SNAP:
        x = wn() * expf(-t * 260) * 0.9f + sinf(2 * PI * 1450 * t) * expf(-t * 110) * 0.4f + sinf(2 * PI * 620 * t) * expf(-t * 50) * 0.3f; break;
    case S_STING: {
        float env = (1 - expf(-t * 60)) * expf(-u * 3.2f);
        float v = sinf(2 * PI * 233 * t) + sinf(2 * PI * 247 * t) + 0.8f * sinf(2 * PI * 349 * t) + 0.7f * sinf(2 * PI * 370 * t) + 0.5f * sinf(2 * PI * 1480 * t * (1 + 0.05f * u));
        x = v * 0.22f * env + wn() * expf(-t * 12) * 0.5f; break; }
    case S_BELL: {
        float f = 174.6f;
        x = (sinf(2 * PI * f * t) * expf(-t * 1.4f) + 0.55f * sinf(2 * PI * f * 2.76f * t) * expf(-t * 2.8f) + 0.3f * sinf(2 * PI * f * 5.4f * t) * expf(-t * 5)) * 0.5f; break; }
    case S_KNOCK: {
        x = 0;
        static const float at[3] = { 0, 0.36f, 0.62f };
        for (int k = 0; k < 3; k++) { float tt = t - at[k]; if (tt > 0) x += (sinf(2 * PI * 88 * tt) * expf(-tt * 22) + wn() * expf(-tt * 120) * 0.35f) * (k == 1 ? 0.8f : 1.0f); }
        break; }
    case S_BREATH: {
        float n = wn(); s.s1 += (n - s.s1) * 0.25f; s.s2 += (s.s1 - s.s2) * 0.25f;
        x = (s.s1 - s.s2) * 2.2f * powf(sinf(PI * u), 1.5f); break; }
    case S_SCREAM: {
        float f = lerpf(520, 1350, powf(u, 0.6f)) * (1 + 0.04f * sinf(2 * PI * 9 * t)) + wn() * 40;
        s.s1 += f / SR; s.s1 -= floorf(s.s1);
        float v = (2 * s.s1 - 1) * 0.6f + sinf(2 * PI * f * 1.5f * t) * 0.3f + wn() * 0.4f;
        x = tanhf(v * 3.0f) * 0.5f * (1 - expf(-t * 40)) * (1 - sstep(0.7f, 1.0f, u)); break; }
    case S_CLICK:
        x = (wn() * 0.6f + sinf(2 * PI * 2400 * t)) * expf(-t * 600) * 0.8f; break;
    case S_RATTLE: {
        float ph = fmodf(t * 14.0f, 1.0f);
        x = (wn() * expf(-ph * 30) * 0.8f + sinf(2 * PI * 110 * t) * 0.2f * expf(-ph * 12)) * (1 - u); break; }
    case S_STATIC:
        x = wn() * ((fmodf(t * 37.0f, 1.0f) < 0.6f) ? 0.8f : 0.3f) * (1 - u); break;
    case S_ENGINE: {
        float f = 44 + 38 * powf(u, 0.7f);
        s.s1 += f / SR; s.s1 -= floorf(s.s1);
        x = ((2 * s.s1 - 1) * 0.5f + sinf(2 * PI * f * 2 * t) * 0.3f + wn() * 0.08f) * sinf(PI * u) * 0.6f; break; }
    case S_THUD:
        x = sinf(2 * PI * (62 - 20 * t) * t) * expf(-t * 11) * 1.2f + wn() * expf(-t * 40) * 0.2f; break;
    case S_CHIME:
        x = (sinf(2 * PI * 196 * t) + 0.5f * sinf(2 * PI * 392.7f * t) * expf(-t * 1.2f) + 0.25f * sinf(2 * PI * 590 * t)) * expf(-t * 1.05f) * 0.45f; break;
    case S_PAPER: {
        float n = wn(); s.s1 += (n - s.s1) * 0.5f;
        x = (n - s.s1) * 0.9f * powf(sinf(PI * u), 0.5f) * (0.5f + 0.5f * wn()); break; }
    }
    return x * s.vol;
}

// reverb (three combs) and layer state
static float cmb[3][2400]; static int cmbI[3] = { 0, 0, 0 };
static const int cmbN[3] = { 1117, 1559, 2113 };
static float wlp1, wlp2, wlp3, phCr, phDr1, phDr2, phDr3, phHb, hum, lfoW, tFl, gate1, gate2;
static float sm_wind, sm_in, sm_ten, sm_hb, sm_pan, sm_quiet;
static double clk = 0;

static void renderBlock(short* out, int frames) {
    EnterCriticalSection(&cs);
    std::vector<Shot> local;
    local.swap(shots);
    LeaveCriticalSection(&cs);
    static std::vector<Shot> active;
    for (auto& s : local) active.push_back(s);
    float tw = wind, ti = inside, tt2 = tension, th2 = heart, tp = panic, tq = quiet, mst = master;
    for (int f = 0; f < frames; f++) {
        float k = 0.0004f;
        sm_wind += (tw - sm_wind) * k; sm_in += (ti - sm_in) * k; sm_ten += (tt2 - sm_ten) * k * 2;
        sm_hb += (th2 - sm_hb) * k * 4; sm_pan += (tp - sm_pan) * k * 3; sm_quiet += (tq - sm_quiet) * k * 4;
        clk += 1.0 / SR; float t = (float)clk;

        // wind: two cascaded low-passes over noise, slowly gusting
        float n = wn();
        lfoW = 0.55f + 0.45f * sinf(2 * PI * 0.061f * t + 1.0f) * sinf(2 * PI * 0.023f * t);
        wlp1 += (n - wlp1) * (0.010f + 0.012f * lfoW); wlp2 += (wlp1 - wlp2) * 0.04f; wlp3 += (wlp2 - wlp3) * 0.2f;
        float w = wlp2 * 11.0f * sm_wind * lfoW * (1 - sm_in * 0.78f);
        w += (wlp1 - wlp2) * 14.0f * sm_wind * lfoW * 0.35f * (1 - sm_in * 0.8f);

        // crickets: two bursty chirpers
        float cr = 0;
        gate1 = (fmodf(t * 1.7f, 1.0f) < 0.55f) ? 1.0f : 0.0f; gate2 = (fmodf(t * 1.13f + 0.4f, 1.0f) < 0.4f) ? 1.0f : 0.0f;
        float p1 = fmodf(t * 41.0f, 1.0f) < 0.45f ? 1.0f : 0.0f, p2 = fmodf(t * 36.0f, 1.0f) < 0.5f ? 1.0f : 0.0f;
        cr = sinf(2 * PI * 4350 * t) * gate1 * p1 + 0.8f * sinf(2 * PI * 3820 * t) * gate2 * p2;
        cr *= 0.010f * (1 - sm_in * 0.9f) * (1 - sm_ten * 0.85f);

        // dread drone: detuned low pair + tritone, a shimmer that appears when tension is high
        phDr1 += 55.0f / SR; phDr2 += 58.7f / SR; phDr3 += 77.8f / SR; phCr += 2930.0f / SR;
        phDr1 -= floorf(phDr1); phDr2 -= floorf(phDr2); phDr3 -= floorf(phDr3); phCr -= floorf(phCr);
        float dr = (sinf(2 * PI * phDr1) + sinf(2 * PI * phDr2) + 0.6f * sinf(2 * PI * phDr3) + 0.5f * (2 * phDr1 - 1) * 0.3f) * 0.07f * sm_ten;
        dr += sinf(2 * PI * phCr) * (0.5f + 0.5f * sinf(2 * PI * 5.1f * t)) * 0.012f * sm_ten * sm_ten;

        // room tone, indoors
        hum += (n - hum) * 0.002f;
        float room = (hum * 5.0f + sinf(2 * PI * 50 * t) * 0.012f) * sm_in * 0.5f;

        // heartbeat
        float hb = 0;
        if (sm_hb > 20) {
            phHb += sm_hb / 60.0f / SR; phHb -= floorf(phHb);
            float tb = phHb * 60.0f / sm_hb;
            hb = sinf(2 * PI * 52 * tb) * expf(-tb * 26) + ((tb > 0.26f) ? 0.7f * sinf(2 * PI * 46 * (tb - 0.26f)) * expf(-(tb - 0.26f) * 28) : 0);
            hb *= 0.55f * clampf(0.35f + sm_pan, 0, 1);
        }

        float m = w + cr + dr + room + hb;
        float L = m, R = m;
        // spread the wind a little
        L += wlp3 * 0.5f * sm_wind * (1 - sm_in * 0.8f); R -= wlp3 * 0.5f * sm_wind * (1 - sm_in * 0.8f);

        for (size_t i = 0; i < active.size();) {
            Shot& s = active[i];
            float x = shotSample(s);
            L += x * s.gl; R += x * s.gr;
            s.t += 1.0f / SR;
            if (s.t >= s.dur) { active[i] = active.back(); active.pop_back(); } else i++;
        }
        // reverb: stronger indoors
        float wet = 0.10f + 0.28f * sm_in;
        float mono = (L + R) * 0.5f, rv = 0;
        for (int c = 0; c < 3; c++) {
            float d = cmb[c][cmbI[c]];
            cmb[c][cmbI[c]] = mono + d * (0.62f + 0.12f * sm_in);
            if (++cmbI[c] >= cmbN[c]) cmbI[c] = 0;
            rv += d;
        }
        L += rv * wet * 0.5f; R += rv * wet * 0.5f * 0.9f;
        float g = mst * (1 - sm_quiet * 0.6f) * 0.8f;
        L *= g; R *= g;
        L = L / (1 + fabsf(L)); R = R / (1 + fabsf(R));       // soft clip
        out[f * 2] = (short)(clampf(L, -1, 1) * 30000); out[f * 2 + 1] = (short)(clampf(R, -1, 1) * 30000);
    }
}

static DWORD WINAPI thread(LPVOID) {
    while (run) {
        for (int k = 0; k < 4; k++) {
            if (hdr[k].dwFlags & WHDR_DONE) {
                renderBlock(bufs[k], 1024);
                waveOutWrite(hwo, &hdr[k], sizeof(WAVEHDR));
            }
        }
        WaitForSingleObject(ev, 40);
    }
    return 0;
}

static bool init() {
    InitializeCriticalSection(&cs);
    WAVEFORMATEX fmt = {};
    fmt.wFormatTag = WAVE_FORMAT_PCM; fmt.nChannels = 2; fmt.nSamplesPerSec = SR; fmt.wBitsPerSample = 16;
    fmt.nBlockAlign = 4; fmt.nAvgBytesPerSec = SR * 4;
    ev = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (waveOutOpen(&hwo, WAVE_MAPPER, &fmt, (DWORD_PTR)ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) return false;
    for (int k = 0; k < 4; k++) {
        memset(&hdr[k], 0, sizeof(WAVEHDR));
        hdr[k].lpData = (LPSTR)bufs[k]; hdr[k].dwBufferLength = sizeof(bufs[k]);
        waveOutPrepareHeader(hwo, &hdr[k], sizeof(WAVEHDR));
        hdr[k].dwFlags |= WHDR_DONE;
    }
    run = true;
    th = CreateThread(nullptr, 0, thread, nullptr, 0, nullptr);
    return true;
}
static void shutdown() {
    run = false;
    if (th) { WaitForSingleObject(th, 500); CloseHandle(th); }
    if (hwo) { waveOutReset(hwo); for (int k = 0; k < 4; k++) waveOutUnprepareHeader(hwo, &hdr[k], sizeof(WAVEHDR)); waveOutClose(hwo); }
}
}   // namespace Au
