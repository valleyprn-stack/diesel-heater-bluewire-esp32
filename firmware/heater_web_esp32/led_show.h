// ============================================================================
//  LED SHOW - the board's RGB LED (GPIO16) acts out what the heater is doing.
//  Pure colour engine: no Arduino calls in here, so it can be previewed on a PC.
//
//  power-up ........ quick rainbow spin
//  WiFi searching .. blue radar ping, once a second
//  hotspot mode .... slow purple breathing
//  idle + WiFi OK .. dim green heartbeat (lub-dub)
//  no Nano data .... red double blink
//  heater silent ... slow amber pulse
//  starting / fan .. cool-white spin-up flutter
//  preheat ......... glow plug: red-orange throb, getting hotter
//  ignition ........ orange with yellow sparks
//  running ......... flickering flame, bigger and brighter with the level
//  shutdown ........ dying embers
//  clearing ........ glow plug throb again, dimmer
//  purge / cooling . breathing, orange when hot fading to blue as it cools
//  fault ........... urgent red double flash
//  alert overlay ... red (or amber) flashes every 4 s on top of the above
//  command sent .... short white flash
// ============================================================================
#pragma once
#include <stdint.h>
#include <math.h>

enum LedPhase : uint8_t { LP_IDLE, LP_START, LP_FAN, LP_PREHEAT, LP_IGNITION, LP_RUN,
                          LP_SHUTDOWN, LP_CLEARING, LP_PURGE, LP_COOL, LP_FAULT, LP_UNK };

struct LedIn {
    uint8_t  wifi = 0;          // 0 searching, 1 connected, 2 hotspot
    bool     link = false;      // status lines arriving from the Nano
    bool     heater = false;    // heater answering the bus
    char     st = 'U';          // 'O' off, 'R' run, 'C' cool, 'F' fault, 'U' unknown
    uint8_t  phase = LP_UNK;
    uint8_t  level = 3;         // 1..6
    float    chamberC = NAN;    // NAN = no reading
    uint8_t  alert = 0;         // 0 none, 1 warn, 2 bad
    uint32_t flashMs = 0;       // time of the last command (0 = none)
    uint32_t bootMs = 0;        // time the ESP32 started
};

struct LedRGB { float r, g, b; };   // 0..1, before brightness cap and gamma

static inline float lclamp(float x) { return x < 0 ? 0 : (x > 1 ? 1 : x); }
static inline LedRGB lmix(LedRGB a, LedRGB b, float t) { t = lclamp(t); return { a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t }; }
static inline LedRGB lscale(LedRGB c, float k) { return { c.r * k, c.g * k, c.b * k }; }
static inline float lwave(uint32_t ms, uint32_t period) { return 0.5f - 0.5f * cosf(6.2831853f * (float)(ms % period) / period); }  // 0..1..0
// sharp attack, exponential decay pulse starting at t0 (ms), returns 0..1
static inline float lpulse(int32_t t, float decayMs) { return t < 0 ? 0.f : expf(-(float)t / decayMs); }

static uint32_t ledRng = 0x9E3779B9u;
static inline float lrand() { ledRng ^= ledRng << 13; ledRng ^= ledRng >> 17; ledRng ^= ledRng << 5; return (ledRng & 0xFFFF) / 65535.f; }

// smoothed random flicker 0..1, advanced once per frame
static float ledFlick = 0.5f, ledFlickTarget = 0.5f;
static uint32_t ledFlickNext = 0;
static inline float lflicker(uint32_t ms, uint32_t stepMs, float smooth) {
    if ((int32_t)(ms - ledFlickNext) >= 0 || (int32_t)(ledFlickNext - ms) > (int32_t)(4 * stepMs)) {   // wrap-safe
        ledFlickTarget = lrand(); ledFlickNext = ms + stepMs / 2 + (uint32_t)(lrand() * stepMs);
    }
    ledFlick += (ledFlickTarget - ledFlick) * smooth;
    return ledFlick;
}

static LedRGB hue(float h) {          // h 0..1 -> full saturation colour
    h -= floorf(h); float x = h * 6; int i = (int)x; float f = x - i;
    switch (i % 6) { case 0: return {1, f, 0}; case 1: return {1 - f, 1, 0}; case 2: return {0, 1, f};
                     case 3: return {0, 1 - f, 1}; case 4: return {f, 0, 1}; default: return {1, 0, 1 - f}; }
}

// The whole show: what colour should the LED be at time ms?
LedRGB ledCompute(const LedIn &in, uint32_t ms) {
    const LedRGB OFF = {0, 0, 0};
    const LedRGB GREEN = {0.10f, 1.0f, 0.30f}, BLUE = {0.05f, 0.45f, 1.0f}, PURPLE = {0.65f, 0.10f, 1.0f};
    const LedRGB RED = {1.0f, 0.02f, 0.0f}, AMBER = {1.0f, 0.45f, 0.0f}, ORANGE = {1.0f, 0.32f, 0.0f};
    const LedRGB DEEPRED = {1.0f, 0.06f, 0.0f}, YELLOW = {1.0f, 0.75f, 0.15f}, COOLWHITE = {0.55f, 0.75f, 1.0f};
    LedRGB c = OFF;
    uint32_t up = ms - in.bootMs;

    if (up < 1200) {                                            // power-up rainbow spin
        return lscale(hue(up / 600.f), 0.55f * (1 - up / 1400.f));
    }

    bool heaterBusy = in.link && in.heater && (in.st == 'R' || in.st == 'C' || in.st == 'F');
    if (!heaterBusy) {
        if (in.wifi == 0) {                                     // searching: radar ping
            return lscale(lmix(BLUE, {0.0f, 0.85f, 1.0f}, 0.4f), 0.55f * lpulse((int32_t)(ms % 1000), 160));
        }
        if (!in.link) {                                         // no Nano: red double blink
            uint32_t t = ms % 2000;
            float k = (t < 120 || (t > 260 && t < 380)) ? 0.5f : 0.f;
            return lscale(RED, k);
        }
        if (!in.heater) {                                       // heater silent: amber pulse
            return lscale(AMBER, 0.08f + 0.30f * lwave(ms, 2600));
        }
        // idle: heartbeat (green on WiFi, purple on the hotspot)
        uint32_t t = ms % 1500;
        float k = 0.30f * lpulse((int32_t)t, 120) + 0.22f * lpulse((int32_t)t - 260, 150);
        c = lscale(in.wifi == 2 ? PURPLE : GREEN, k);
    } else {
        float lv = in.level < 1 ? 1 : (in.level > 6 ? 6 : in.level);
        switch (in.phase) {
        case LP_START: case LP_FAN: {                           // fan spinning up
            float k = 0.25f + 0.30f * lwave(ms, 260) * lclamp((ms % 4000) / 2000.f + 0.4f);
            c = lscale(COOLWHITE, k); break; }
        case LP_PREHEAT: {                                      // glow plug heating
            float heat = lwave(ms, 1800);
            c = lscale(lmix(DEEPRED, ORANGE, heat), 0.35f + 0.35f * heat); break; }
        case LP_IGNITION: {                                     // fuel + glow: sparks
            float f = lflicker(ms, 90, 0.35f);
            c = lscale(lmix(DEEPRED, ORANGE, f), 0.40f + 0.20f * f);
            if (lrand() < 0.06f) c = lscale(YELLOW, 0.85f);
            break; }
        case LP_RUN: {                                          // flame, sized by level
            float f = lflicker(ms, 110, 0.28f);
            float base = 0.40f + 0.08f * lv;
            c = lscale(lmix(ORANGE, YELLOW, 0.15f + 0.45f * f), base * (0.70f + 0.30f * f)); break; }
        case LP_SHUTDOWN: {                                     // embers dying
            float f = lflicker(ms, 160, 0.20f);
            c = lscale(lmix(DEEPRED, ORANGE, f * 0.6f), 0.25f + 0.15f * f); break; }
        case LP_CLEARING: {                                     // glow plug again, dimmer
            float heat = lwave(ms, 1500);
            c = lscale(lmix(DEEPRED, ORANGE, heat), 0.25f + 0.25f * heat); break; }
        case LP_PURGE: case LP_COOL: {                          // cooling: hot orange -> blue
            float hot = isnan(in.chamberC) ? 0.5f : lclamp((in.chamberC - 40.f) / 110.f);
            c = lscale(lmix(BLUE, ORANGE, hot), 0.15f + 0.30f * lwave(ms, 2400)); break; }
        case LP_FAULT: {                                        // urgent red
            uint32_t t = ms % 1000;
            c = lscale(RED, (t < 90 || (t > 200 && t < 290)) ? 0.85f : 0.04f); break; }
        default:
            if (in.st == 'F') { uint32_t t = ms % 1000; c = lscale(RED, (t < 90 || (t > 200 && t < 290)) ? 0.85f : 0.04f); }
            else c = lscale(COOLWHITE, 0.15f);
        }
    }

    // alert overlay: every 4 s, two red flashes (bad) or one amber flash (warn)
    if (in.alert && in.st != 'F') {
        uint32_t t = ms % 4000;
        if (in.alert == 2 && (t < 110 || (t > 220 && t < 330))) c = lscale(RED, 0.9f);
        if (in.alert == 1 && t < 160) c = lscale(AMBER, 0.8f);
    }
    // command feedback: short white flash
    if (in.flashMs && ms - in.flashMs < 180) c = lmix(c, {1, 1, 1}, 0.85f * (1 - (ms - in.flashMs) / 180.f));
    return c;
}

// 0..1 colour -> 0..maxLevel bytes. The gamma curve goes on the overall brightness only,
// so the colour mix (e.g. orange = full red + a third green) stays the same at any brightness.
static inline void ledToBytes(LedRGB c, uint8_t maxLevel, uint8_t &r, uint8_t &g, uint8_t &b) {
    float m = lclamp(fmaxf(c.r, fmaxf(c.g, c.b)));
    if (m <= 0.001f) { r = g = b = 0; return; }
    float k = maxLevel * powf(m, 2.2f) / m;
    r = (uint8_t)lroundf(lclamp(c.r) * k);
    g = (uint8_t)lroundf(lclamp(c.g) * k);
    b = (uint8_t)lroundf(lclamp(c.b) * k);
}

static inline uint8_t ledPhaseFromName(const char *p) {
    static const char *N[] = {"IDLE", "START", "FAN", "PREHEAT", "IGNITION", "RUN", "SHUTDOWN", "CLEARING", "PURGE", "COOL", "FAULT"};
    for (uint8_t i = 0; i < sizeof(N) / sizeof(N[0]); i++) { const char *a = N[i], *b = p; while (*a && *a == *b) { a++; b++; } if (!*a && !*b) return i; }
    return LP_UNK;
}
