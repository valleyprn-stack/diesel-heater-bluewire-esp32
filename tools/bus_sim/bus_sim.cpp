#include "Arduino.h"
#include <vector>
#include <queue>
#include <algorithm>
uint64_t simUs = 0;
SerialMock Serial;
#include "../../firmware/heater_bridge_nano/heater_bridge_nano.ino"

enum { NANO = 0, LCDS = 1, HTR = 2 };
static const char *srcName[] = {"NANO", "LCD ", "HTR "};
static void (*g_isr)() = nullptr; static bool g_isrOn = false;
static int nanoMode = INPUT_PULLUP, nanoVal = HIGH, ledVal = 0;
static int lowCount[3] = {0, 0, 0}; static bool busPrev = true;
struct Ev { uint64_t t; int src, d; bool operator>(const Ev &o) const { return t > o.t; } };
static std::priority_queue<Ev, std::vector<Ev>, std::greater<Ev>> evq;
struct Frame { int src; uint64_t s, e; uint32_t val; int bits; };
static std::vector<Frame> frames;
struct Cmd { uint64_t t; std::string s; };
static std::vector<Cmd> cmds; static size_t cmdIdx = 0;
static bool quietStatus = true; static uint64_t lastStatusPrint = 0;
struct Reply { uint64_t reqEnd; uint8_t req; uint16_t word; };
static std::vector<Reply> replies;

void SerialMock::emit() {
    size_t p;
    while ((p = out.find('\n')) != std::string::npos) {
        std::string line = out.substr(0, p); out.erase(0, p + 1);
        if (line.rfind("S,", 0) == 0 && quietStatus) { if (simUs - lastStatusPrint < 5000000) continue; lastStatusPrint = simUs; }
        printf("%8.3f  %s\n", simUs / 1e6, line.c_str());
    }
}
bool busLevel() { return lowCount[0] + lowCount[1] + lowCount[2] == 0; }
void heaterSeeEdge(uint64_t t, bool level);
void onBusChange() { bool lv = busLevel(); if (lv != busPrev) { busPrev = lv; heaterSeeEdge(simUs, lv); if (g_isrOn && g_isr) g_isr(); } }
void advanceTo(uint64_t t) {
    for (;;) {
        uint64_t nextEv = evq.empty() ? UINT64_MAX : evq.top().t;
        uint64_t nextCmd = cmdIdx < cmds.size() ? cmds[cmdIdx].t : UINT64_MAX;
        uint64_t n = std::min(nextEv, nextCmd);
        if (n > t) break;
        if (nextCmd <= nextEv) { simUs = nextCmd; Serial.in += cmds[cmdIdx].s + "\n"; printf("%8.3f  >> %s\n", simUs / 1e6, cmds[cmdIdx].s.c_str()); cmdIdx++; }
        else { Ev e = evq.top(); evq.pop(); simUs = e.t; lowCount[e.src] += e.d; onBusChange(); }
    }
    simUs = t;
}
unsigned long micros() { return (unsigned long)simUs; }
unsigned long millis() { return (unsigned long)(simUs / 1000); }
void delayMicroseconds(unsigned int us) { advanceTo(simUs + us); }
void noInterrupts() {} void interrupts() {}
void attachInterrupt(int, void (*f)(), int) { g_isr = f; g_isrOn = true; }
void detachInterrupt(int) { g_isrOn = false; }
static uint64_t nLowStart = 0, nFrameStart = 0; static int nLows = 0; static uint32_t nVal = 0;
void nanoUpdate() {
    int want = (nanoMode == OUTPUT && nanoVal == LOW) ? 1 : 0;
    if (want == lowCount[NANO]) return;
    if (want) nLowStart = simUs;
    else {
        uint64_t d = simUs - nLowStart;
        if (d >= 25000) { nLows = 0; nFrameStart = nLowStart; nVal = 0; }
        else { nVal = (nVal << 1) | (d > 6000 ? 1 : 0); if (++nLows == 8) frames.push_back({NANO, nFrameStart, simUs, nVal, 8}); }
    }
    lowCount[NANO] = want; onBusChange();
}
void pinMode(int pin, int mode) { if (pin == 2) { nanoMode = mode; if (mode == INPUT_PULLUP) nanoVal = HIGH; nanoUpdate(); } }
void digitalWrite(int pin, int val) { if (pin == 2) { nanoVal = val; nanoUpdate(); } else ledVal = val; }
int digitalRead(int pin) { return pin == 13 ? ledVal : busLevel(); }

void scheduleFrame(int src, uint64_t t0, uint32_t val, int bits) {
    uint64_t t = t0;
    evq.push({t, src, +1}); t += 30000; evq.push({t, src, -1});
    for (int i = bits - 1; i >= 0; i--) {
        uint64_t low = ((val >> i) & 1) ? 8000 : 4000;
        t += 12050 - low; evq.push({t, src, +1}); t += low; evq.push({t, src, -1});
    }
    frames.push_back({src, t0, t, val, bits});
}
// ---- heater model (v1.2: start/stop phases, standby volts, fault) ----
// state 0 idle, 1 running, 2 cooling, 3 fault
struct { int state = 0; int level = 3; uint64_t since = 0, coolUntil = 0, txS = 0, txE = 0; bool mute = false; int volts = 12; } htr;
uint64_t faultAt = UINT64_MAX;
uint16_t status(uint64_t now) {
    uint64_t dt = now - htr.since;
    if (htr.state == 1) {
        uint8_t lo = 0xC0 - htr.level, fl;
        if (dt < 2000000) fl = 0xF; else if (dt < 4000000) fl = 0x7; else if (dt < 8000000) fl = 0x6;
        else if (dt < 12000000) fl = 0x2; else fl = 0x3;
        return 0x9000 | (fl << 8) | lo;
    }
    if (htr.state == 2) {
        uint8_t fl = dt < 3000000 ? 0x3 : dt < 5000000 ? 0x6 : 0x7;
        return 0x903F | (fl << 8);
    }
    if (htr.state == 3) {   // E-08 sequence, inverted alxkalb capture
        if (dt < 2000000) return 0x7336; if (dt < 4000000) return 0x7636; if (dt < 8000000) return 0x7736; return 0x7FF6;
    }
    return 0x9F00 | (0xFF - htr.volts);
}
void heaterGotRequest(uint8_t b, uint64_t tEnd) {
    if (htr.mute) return;
    if (htr.state == 2 && tEnd > htr.coolUntil) { htr.state = 0; htr.since = tEnd; }
    if (htr.state == 1 && tEnd > faultAt) { htr.state = 3; htr.since = tEnd; faultAt = UINT64_MAX; }
    uint16_t r;
    if (b == 0x50) r = 0xB000 | (htr.state == 1 ? 3150 : 3880);
    else {
        if (b == 0x5D) {
            if (htr.state == 0 || htr.state == 3) { htr.state = 1; htr.since = tEnd; }
            else if (htr.state == 1) { htr.state = 2; htr.since = tEnd; htr.coolUntil = tEnd + 10000000; }
        }
        if (b == 0x5C && htr.state == 1 && htr.level < 6) htr.level++;
        if (b == 0x5B && htr.state == 1 && htr.level > 1) htr.level--;
        r = status(tEnd);
    }
    htr.txS = tEnd + 2000; htr.txE = htr.txS + 30000 + 16 * 12050;
    scheduleFrame(HTR, htr.txS, r, 16);
    replies.push_back({tEnd, b, r});
}
struct { bool in = false; int n = 0; uint8_t v = 0; uint64_t lastT = 0; } hr;
void heaterSeeEdge(uint64_t t, bool level) {
    if (htr.txE && t >= htr.txS && t <= htr.txE) { hr.lastT = t; hr.in = false; return; }
    uint64_t d = t - hr.lastT; hr.lastT = t;
    if (level) {
        if (d >= 25000 && d <= 35000) { hr.in = true; hr.n = 0; hr.v = 0; return; }
        if (!hr.in) return;
        if (d >= 3000 && d <= 5000) { hr.v <<= 1; hr.n++; }
        else if (d >= 7000 && d <= 9000) { hr.v = (hr.v << 1) | 1; hr.n++; }
        else { hr.in = false; return; }
        if (hr.n == 8) { hr.in = false; heaterGotRequest(hr.v, t); }
    } else if (hr.in && (d < 3000 || d > 9000)) hr.in = false;
}
int main(int argc, char **argv) {
    char sc = argc > 1 ? argv[1][0] : 'A';
    uint64_t lcdOn = 0, lcdOff = UINT64_MAX; bool chamberPage = false;
    if (sc == 'B') chamberPage = true;
    if (sc == 'C') lcdOn = UINT64_MAX;
    if (sc == 'E') lcdOff = 20000000;
    // OEM display: one frame per second, fixed phase, does NOT check the bus
    bool alt = false;
    for (uint64_t t = 300000; t < 69000000; t += 1000000) {
        if (t < lcdOn || t >= lcdOff) continue;
        uint8_t b = 0x59; if (chamberPage) { b = alt ? 0x50 : 0x59; alt = !alt; }
        scheduleFrame(LCDS, t, b, 8);
    }
    if (sc == 'D') cmds.push_back({1000000, "CHAMBER 0"});
    if (sc == 'F') {   // start, run out of fuel at 20 s, clear with ON at 40 s
        faultAt = 20000000; quietStatus = false;
        cmds.push_back({6000000, "ON"}); cmds.push_back({30000000, "OFF"}); cmds.push_back({40000000, "ON"});
        cmds.push_back({55000000, "STATUS"});
    } else if (sc == 'G') {   // heater stops answering; a queued ON must expire
        cmds.push_back({12000000, "ON"});
    } else if (sc == 'H') {   // full start + shutdown, print every status line for phases
        quietStatus = false; htr.volts = 13;
        cmds.push_back({3000000, "ON"}); cmds.push_back({22000000, "OFF"});
    } else {
    cmds.push_back({6000000, "ON"});
    cmds.push_back({14000000, "LEVEL 5"});
    cmds.push_back({24000000, "LEVEL 2"});
    cmds.push_back({32000000, "UP"});
    cmds.push_back({38000000, "DOWN"});
    cmds.push_back({44000000, "OFF"});
    cmds.push_back({46000000, "ON"});
    }
    setup();
    while (simUs < 70000000) { if (sc == 'G' && simUs > 10000000) htr.mute = true; loop(); advanceTo(simUs + 50); }
    // ---- checks ----
    std::sort(frames.begin(), frames.end(), [](const Frame &a, const Frame &b) { return a.s < b.s; });
    int coll = 0; uint64_t minGap = UINT64_MAX;
    for (size_t i = 0; i + 1 < frames.size(); i++) {
        if (frames[i + 1].s < frames[i].e) { coll++; printf("COLLISION %s %.3f-%.3f with %s %.3f\n", srcName[frames[i].src], frames[i].s / 1e6, frames[i].e / 1e6, srcName[frames[i + 1].src], frames[i + 1].s / 1e6); }
        else minGap = std::min(minGap, frames[i + 1].s - frames[i].e);
    }
    int lcdN = 0, lcdNoReply = 0, nanoN = 0, nano50 = 0, nanoCmd = 0, nanoNoReply = 0;
    for (auto &f : frames) {
        bool got = false; for (auto &r : replies) if (r.reqEnd == f.e) got = true;
        if (f.src == LCDS) { lcdN++; if (!got) lcdNoReply++; }
        if (f.src == NANO) { nanoN++; if (f.val == 0x50) nano50++; else if (f.val != 0x59) nanoCmd++; if (!got) nanoNoReply++; }
    }
    // quiet time left before each display frame (margin after our injects)
    uint64_t minLcdMargin = UINT64_MAX;
    for (size_t i = 1; i < frames.size(); i++) if (frames[i].src == LCDS && frames[i].s > frames[i-1].e) minLcdMargin = std::min(minLcdMargin, frames[i].s - frames[i-1].e);
    printf("\nmargin before display's next heartbeat (worst case): %.0f ms\n", minLcdMargin / 1000.0);
    printf("\nSCENARIO %c: display frames %d (unanswered %d) | nano frames %d (chamber %d, commands %d, unanswered %d) | collisions %d | tightest gap %.1f ms\n",
           sc, lcdN, lcdNoReply, nanoN, nano50, nanoCmd, nanoNoReply, coll, minGap / 1000.0);
    printf("heater final: %s level %d\n", htr.state == 0 ? "OFF" : htr.state == 1 ? "RUN" : htr.state == 2 ? "COOL" : "FAULT", htr.level);
    return 0;
}
