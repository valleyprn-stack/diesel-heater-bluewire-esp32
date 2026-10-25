/*
 * ============================================================================
 *  HEATER WEB CONTROLLER  -  ESP32 WROOM  -  v1.5  (Oct 24 2026)
 * ============================================================================
 *  Talks to the Nano bus bridge over Serial2 and serves a phone page.
 *  Pair with heater_bridge_nano (bridge v1.2) (v1.1 still works - you just don't get
 *  idle volts, the exact phase names, or fault clearing).
 *
 *  v1.5
 *   - WIRING CHANGE: the Nano link now comes in on GPIO4 (was GPIO16). This
 *     board's RGB LED sits on GPIO16, so the Nano's data was lighting it up at
 *     random. Move the yellow divider wire from 16 to 4.
 *   - LIGHT SHOW on the RGB LED (led_show.h tab): rainbow at power-up, blue
 *     radar ping while looking for WiFi, dim green heartbeat when idle (purple
 *     on the hotspot), red double blink if the Nano goes quiet, amber pulse if
 *     the heater does, then the heater itself - fan spin-up, glow plug throb,
 *     ignition sparks, a flame that grows with the level, embers, cooling
 *     orange-to-blue, red fault flashes, alert flashes, white blip on commands.
 *     LED_MAX below sets the brightness (0 = LED off).
 *
 *  v1.4.2
 *   - FIX: page stuck on "connecting..." after a power-up. When the Nano and
 *     ESP32 start together the Nano's first few bytes can be noise; those raw
 *     bytes went into the event log and made the status data unreadable for
 *     the phone. Now only clean, known Nano lines are logged (junk is counted
 *     and shown on the Serial Monitor as "[nano junk]"), status lines with a
 *     bad state are ignored, and the log text is scrubbed before it's sent.
 *     /api/status has a "junk" count - it should stay at 0-3 (boot noise only).
 *
 *  v1.4.1
 *   - FIX: false "flame-out?" alert about 50 s after lighting on the 2nd+ run of
 *     the day. The 2-minute chamber buffer was emptied at each start but its
 *     write position wasn't, so the check compared against the LAST run's hot
 *     readings (first shed test Oct 24: "dropped 117 C" right after light-up).
 *     Pages, Nano and everything else unchanged.
 *
 *  v1.4
 *   - THREE PAGE STYLES, picked from the dropdown at the top of any page:
 *       Burner (the v1.3 cutaway), LCD (OEM-style 7-segment panel with round
 *       buttons), Dash (gauge with needle, warning lamps, LED level bar).
 *     The choice is saved on the ESP32. Direct links: /burner /lcd /dash.
 *     All three share one script (/core.js) so they behave the same.
 *   - PHASES from bridge v1.2: START, FAN UP, PREHEAT, IGNITION, RUN,
 *     SHUTDOWN, CLEARING, PURGE, COOLING - shown on every style.
 *   - IDLE VOLTS (the 12 V the OEM display shows) with low / high warning.
 *   - FAULT CODE (tentative E-number) and START clears a fault (bridge v1.2).
 *   - STATS: this run, last light-up time, today, total hours, number of starts.
 *     Total / starts / today are saved, so they survive power cuts. "Today"
 *     uses internet time (Pacific); on the hotspot it shows "since boot".
 *   - GRAPH: chamber temperature for the last hour (every 15 s), with a
 *     running / cooling / fault strip under it. Touch it to read a point.
 *   - ALERTS (banner at the top): fault, slow ignition, flame-out, low battery.
 *   - Log lines carry the clock time once internet time is in.
 *   - OTA upload still on (Tools > Port > "heater").
 *
 *  Earlier: v1.3 burner page, v1.2 C/F + chamber poll button, v1.1 chamber
 *  temperature table (see docs/PROTOCOL.md).
 *
 *  WIRING (same as v1.0)
 *    GPIO4 (RX2) <- 10k/20k divider <- Nano TX     GPIO17 (TX2) -> Nano RX
 *    GPIO16 = the board's own RGB LED (leave nothing plugged into 16)
 *    5V -> Nano 5V               GND <-> Nano GND
 *
 *  SETUP
 *    1. Put your WiFi name and password in WIFI_SSID / WIFI_PASS below.
 *    2. Upload by USB, or over WiFi: Tools > Port > network port "heater".
 *    3. Phone: the IP the Serial Monitor prints (115200), or http://heater.local
 *       If WiFi fails it makes hotspot "HeaterCtl" / heater123 / http://192.168.4.1
 *
 *  The web pages live in the web_*.h tabs next to this file.
 *  Libraries: all built into the ESP32 core. Nothing to install.
 * ============================================================================
 */

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ArduinoOTA.h>
#include <Preferences.h>
#include <time.h>
#include <math.h>
#include "web_core.h"
#include "web_burner.h"
#include "web_lcd.h"
#include "web_dash.h"
#include "led_show.h"

// ---------------- CONFIG ----------------
const char* WIFI_SSID = "YOUR_WIFI_NAME";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
const char* HOSTNAME  = "heater";
const char* AP_SSID   = "HeaterCtl";
const char* AP_PASS   = "heater123";
const char* TZ_INFO   = "PST8PDT,M3.2.0,M11.1.0";   // Pacific, for "today" and log times
#define NANO_RX_PIN 4        // was 16 - GPIO16 is the board's RGB LED
#define NANO_TX_PIN 17
#define RGB_PIN     16       // onboard RGB LED
#define LED_MAX     160      // LED brightness cap 0-255 (0 = LED off)
// ----------------------------------------

// ---------------- ALERT TUNING ----------------
#define IGN_PHASE_MAX_S    180   // still in IGNITION (glow + fuel) this long -> warn
#define IGN_PUMP_S         240   // fuel pump on this long...
#define IGN_RISE_C          20   // ...and chamber hasn't risen this much -> warn
#define FLAMEOUT_DROP_C     40   // chamber falls this much inside 2 min while running -> alert
#define LEVEL_DOWN_QUIET_S 180   // ignore drops this long after turning the level down
#define VOLTS_WARN          11
#define VOLTS_BAD           10
#define VOLTS_HIGH          15
// ----------------------------------------------

// ---------------- CHAMBER CALIBRATION ----------------
// raw count from the heater (falls as it heats) -> degrees C, as the OEM display shows it.
// Keep the list ordered from cold (high raw) to hot (low raw).
struct CalPt { int raw; float c; };
const CalPt CAL[] = {
    {3859,  34}, {3722,  50}, {3254, 114}, {3144, 160},
    {3118, 180}, {3110, 190}, {3100, 200},
};
const int NCAL = sizeof(CAL) / sizeof(CAL[0]);

float chamberC(int raw, bool &approx) {
    approx = (raw > CAL[0].raw) || (raw < CAL[NCAL - 1].raw);
    int i = 0;
    if (raw <= CAL[NCAL - 1].raw) {
        i = NCAL - 2;
    } else {
        while (i < NCAL - 2 && raw < CAL[i + 1].raw) i++;
    }
    float r0 = CAL[i].raw, r1 = CAL[i + 1].raw;
    float c = CAL[i].c + (raw - r0) * (CAL[i + 1].c - CAL[i].c) / (r1 - r0);
    if (c < -40) c = -40;
    if (c > 350) c = 350;
    return c;
}
// ------------------------------------------------------

WebServer server(80);
HardwareSerial &nano = Serial2;
Preferences prefs;
bool unitF = false;               // page shows F instead of C (saved)
int  pollPref = -1;               // chamber poll seconds the user picked (-1 = Nano's default)
String theme = "burner";          // saved page style
unsigned long lastPollFixMs = 0;

// ---- last status line from the bridge ----
struct {
    String state = "UNK", word = "0000", mode = "--", phase = "UNK";
    int level = 0, run = 0, pump = 0, glow = 0, chamber = -1, age = 999, poll = -1;
    int volts = 0, vage = 999, ecode = -1, bridge = 0;   // bridge 11 or 12
    unsigned long lastLineMs = 0;
} S;

uint32_t junkLines = 0;          // garbled lines from the Nano (boot noise is normal; a climbing count is not)

bool linkStale()    { return S.lastLineMs == 0 || millis() - S.lastLineMs > 5000; }
bool heaterLive()   { return !linkStale() && S.age <= 5; }
bool chamberFresh() { return heaterLive() && S.chamber >= 0; }
float chamberNow()  { bool ap; return chamberFresh() ? chamberC(S.chamber, ap) : NAN; }

// ---- clock ----
bool clockOk() { return time(nullptr) > 1700000000; }
String stamp() {
    if (!clockOk()) return String(millis() / 1000) + "s";
    time_t n = time(nullptr); struct tm t; localtime_r(&n, &t);
    char b[12]; strftime(b, sizeof b, "%H:%M:%S", &t); return String(b);
}
int dayKey() {
    if (!clockOk()) return -1;
    time_t n = time(nullptr); struct tm t; localtime_r(&n, &t);
    return (t.tm_year + 1900) * 1000 + t.tm_yday;
}

#define LOG_N 20
String eventLog[LOG_N]; uint8_t logHead = 0;
void addLog(const String &s) {
    eventLog[logHead] = stamp() + " " + s;
    logHead = (logHead + 1) % LOG_N;
}

// ---- stats (saved in NVS) ----
uint32_t totRunS = 0, starts = 0, todayRunS = 0;
int todayKey = -1, lastLightS = -1;
bool statsDirty = false; unsigned long lastSaveMs = 0;
unsigned long runStartMs = 0;     // 0 = not running
bool lightPending = false;

void saveStats() {
    prefs.putUInt("totRun", totRunS);
    prefs.putUInt("starts", starts);
    prefs.putUInt("todRun", todayRunS);
    prefs.putInt("todKey", todayKey);
    prefs.putInt("light", lastLightS);
    statsDirty = false; lastSaveMs = millis();
}

// ---- alert watchers ----
unsigned long pumpSinceMs = 0, ignSinceMs = 0, lastLevelDownMs = 0, flameMs = 0;
float pumpStartC = NAN;
String flameTxt;
String alertTxt, alertLvl;
#define FO_N 12                    // 12 x 10 s = 2 min of chamber samples while burning
float foBuf[FO_N]; uint8_t foCount = 0, foHead = 0; unsigned long foLastMs = 0;

// ---- chamber history: 240 x 15 s = 1 hour ----
#define HIST_N 240
#define HIST_MS 15000UL
int16_t histC[HIST_N]; char histS[HIST_N]; uint16_t histHead = 0; unsigned long lastHistMs = 0;

// Phase from the flags, for a v1.1 bridge that doesn't send one (same rules as bridge v1.2).
String guessPhase() {
    bool f = S.run, p = S.pump, g = S.glow;
    if (S.state == "OFF")   return "IDLE";
    if (S.state == "FAULT") return "FAULT";
    if (S.state == "RUN") {
        if (!f && !p && !g) return "START";
        if ( f && !p && !g) return "FAN";
        if ( f && !p &&  g) return "PREHEAT";
        if ( p &&  g)       return "IGNITION";
        return "RUN";
    }
    if (S.state == "COOL") {
        if (p) return "SHUTDOWN";
        if (g) return "CLEARING";
        if (f) return "PURGE";
        return "COOL";
    }
    return "UNK";
}

void raiseFlame(const String &t) {
    if (flameMs) return;           // once per run
    flameMs = millis(); if (!flameMs) flameMs = 1;
    flameTxt = t;
    addLog("ALERT flame-out?");
}

// Called after every status line: starts, light-up time, watchers.
void onUpdate(const String &prev, const String &prevPhase, int prevLevel) {
    unsigned long now = millis();
    if (S.state != prev) {
        if (S.state == "RUN") {
            runStartMs = now ? now : 1;
            if (prev == "OFF" || prev == "FAULT") {
                starts++; lightPending = true; saveStats();
            } else lightPending = false;          // ESP32 booted mid-run: start time unknown
            flameMs = 0; foCount = 0; foHead = 0; pumpSinceMs = 0; ignSinceMs = 0; pumpStartC = NAN;
        }
        if (prev == "RUN") { runStartMs = 0; lightPending = false; saveStats(); }
    }
    if (S.state != "RUN") { pumpSinceMs = 0; ignSinceMs = 0; foCount = 0; foHead = 0; return; }

    if (lightPending && S.phase == "RUN") {
        lastLightS = (now - runStartMs) / 1000; lightPending = false;
        addLog("EVT lit after " + String(lastLightS) + "s");
        saveStats();
    }
    if (prevPhase == "RUN" && (S.phase == "PREHEAT" || S.phase == "IGNITION"))
        raiseFlame("The glow plug came back on while it was running - the heater is re-lighting. The flame may have gone out (fuel?).");
    if (prevLevel > 0 && S.level < prevLevel) lastLevelDownMs = now;

    if (S.phase == "IGNITION") { if (!ignSinceMs) ignSinceMs = now ? now : 1; } else ignSinceMs = 0;
    if (S.pump) {
        if (!pumpSinceMs) { pumpSinceMs = now ? now : 1; pumpStartC = chamberNow(); }
        else if (isnan(pumpStartC)) pumpStartC = chamberNow();
    } else { pumpSinceMs = 0; pumpStartC = NAN; }
}

// S,<state>,<level>,<run>,<pump>,<glow>,<word>,<mode>,<chamber_raw>,<age_s>[,<poll_s>[,<volts>,<vage>,<phase>,<ecode>]]
void parseStatus(const String &l) {
    String f[16]; int n = 0, from = 0;
    while (n < 16) {
        int c = l.indexOf(',', from);
        if (c < 0) { f[n++] = l.substring(from); break; }
        f[n++] = l.substring(from, c);
        from = c + 1;
    }
    if (n < 10) return;
    if (f[1] != "OFF" && f[1] != "RUN" && f[1] != "COOL" && f[1] != "FAULT" && f[1] != "UNK") { junkLines++; return; }
    String prev = S.state, prevPhase = S.phase; int prevLevel = S.level;
    S.state   = f[1];
    S.level   = f[2].toInt();
    S.run     = f[3].toInt();
    S.pump    = f[4].toInt();
    S.glow    = f[5].toInt();
    S.word    = f[6];
    S.mode    = f[7];
    S.chamber = f[8].toInt();
    S.age     = f[9].toInt();
    S.poll    = (n >= 11) ? f[10].toInt() : -1;     // -1 = v1.0 bridge, no poll field
    if (n >= 15) {
        S.volts = f[11].toInt(); S.vage = f[12].toInt();
        S.phase = f[13]; S.ecode = f[14].toInt(); S.bridge = 12;
    } else {
        S.volts = 0; S.vage = 999; S.phase = guessPhase(); S.ecode = -1; S.bridge = 11;
    }
    S.lastLineMs = millis();
    onUpdate(prev, prevPhase, prevLevel);

    // Nano restarted with its default poll? Put the user's choice back.
    if (pollPref >= 0 && S.poll >= 0 && S.poll != pollPref && millis() - lastPollFixMs > 5000) {
        lastPollFixMs = millis();
        nano.println("CHAMBER " + String(pollPref));
    }
}

String lineBuf;
bool lineBad = false;

bool knownLine(const String &l) {
    return l.startsWith("EVT ") || l.startsWith("ACK") || l.startsWith("ERR") || l.startsWith("REQ ") ||
           l.startsWith("MODE ") || l.startsWith("PONG") || l.startsWith("HEATER BRIDGE");
}

void pollNano() {
    while (nano.available()) {
        char c = nano.read();
        if (c == '\n' || c == '\r') {
            if (lineBuf.length() || lineBad) {
                if (lineBad)                        { junkLines++; Serial.println("[nano junk] " + lineBuf); }
                else if (lineBuf.startsWith("S,"))  parseStatus(lineBuf);
                else if (lineBuf.startsWith("R,"))  Serial.println("[nano] " + lineBuf);   // raw frames: monitor only
                else if (knownLine(lineBuf))        { Serial.println("[nano] " + lineBuf); addLog(lineBuf); }
                else                                { junkLines++; Serial.println("[nano junk] " + lineBuf); }
            }
            lineBuf = ""; lineBad = false;
        } else if (c < 0x20 || c > 0x7E) lineBad = true;     // the Nano only ever sends plain text
        else if (lineBuf.length() < 120) lineBuf += c;
    }
}

// ---------------- once a second: stats, alerts, history ----------------
void tickStats() {
    static unsigned long lastMs = 0, acc = 0;
    unsigned long now = millis();
    if (!lastMs) lastMs = now;
    if (S.state == "RUN" && heaterLive()) acc += now - lastMs;
    lastMs = now;
    while (acc >= 1000) { acc -= 1000; totRunS++; todayRunS++; statsDirty = true; }
    int k = dayKey();
    if (k >= 0 && k != todayKey) {
        if (todayKey != -1) todayRunS = 0;     // new day (or first clock since a different day)
        todayKey = k; saveStats();
    }
    if (statsDirty && now - lastSaveMs > 300000UL) saveStats();   // every 5 min while running
}

String two(int n) { return n < 10 ? "0" + String(n) : String(n); }

String faultHint(int e) {
    if (e == 8) return " E-08 is the code seen when a heater ran out of fuel / couldn't light.";
    return " Check the heater manual for this code.";
}

void tickAlerts() {
    unsigned long now = millis();
    float c = chamberNow();

    // flame-out by temperature: sample every 10 s while burning
    if (S.state == "RUN" && S.phase == "RUN" && !isnan(c)) {
        if (now - foLastMs >= 10000) {
            foLastMs = now;
            foBuf[foHead] = c; foHead = (foHead + 1) % FO_N; if (foCount < FO_N) foCount++;
            if (foCount >= 6) {
                float mx = -1000; for (uint8_t i = 0; i < foCount; i++) mx = max(mx, foBuf[i]);
                bool quiet = lastLevelDownMs && now - lastLevelDownMs < LEVEL_DOWN_QUIET_S * 1000UL;
                if (mx - c >= FLAMEOUT_DROP_C && !quiet)
                    raiseFlame("Chamber dropped " + String((int)(mx - c)) + "\xC2\xB0" "C in under 2 min while running - the flame may have gone out.");
            }
        }
    } else if (S.state != "RUN") { foCount = 0; foHead = 0; }

    String txt, lvl, tag;
    bool live = heaterLive();
    if (live && S.state == "FAULT") {
        lvl = "bad"; tag = "fault";
        txt = "Heater stopped on a fault";
        if (S.ecode >= 0) { txt += " E-" + two(S.ecode) + " (tentative)."; txt += faultHint(S.ecode); tag += " E-" + two(S.ecode); }
        else txt += ".";
        txt += S.bridge >= 12 ? " Tap START to clear it and restart." : " Clear it on the heater's own display.";
    } else if (flameMs && S.state == "RUN" && now - flameMs < 600000UL) {
        lvl = "bad"; tag = "flame-out?"; txt = flameTxt;
    } else if (live && S.state == "RUN" && ignSinceMs && now - ignSinceMs > IGN_PHASE_MAX_S * 1000UL) {
        lvl = "warn"; tag = "slow ignition";
        txt = "Still trying to light after " + String((now - ignSinceMs) / 60000) + " min (glow plug + fuel on). Check the fuel supply.";
    } else if (live && S.state == "RUN" && pumpSinceMs && now - pumpSinceMs > IGN_PUMP_S * 1000UL &&
               !isnan(c) && !isnan(pumpStartC) && c < pumpStartC + IGN_RISE_C) {
        lvl = "warn"; tag = "not warming";
        txt = "Fuel pump on " + String((now - pumpSinceMs) / 60000) + " min but the chamber has only warmed " +
              String((int)(c - pumpStartC)) + "\xC2\xB0" "C. It may not have lit.";
    } else if (live && S.state == "OFF" && S.volts > 0 && S.volts <= VOLTS_WARN) {
        lvl = S.volts <= VOLTS_BAD ? "bad" : "warn"; tag = "low battery";
        txt = "Supply is " + String(S.volts) + " V at idle - the battery is getting low.";
    } else if (live && S.state == "OFF" && S.volts >= VOLTS_HIGH) {
        lvl = "warn"; tag = "high volts";
        txt = "Supply is " + String(S.volts) + " V at idle - check the charger.";
    }
    static String lastTag;
    if (tag != lastTag) {                  // log each new alert once, not every time its minutes tick
        if (tag.length() && tag != "flame-out?") addLog("ALERT " + tag);   // flame-out logs when raised
        lastTag = tag;
    }
    alertTxt = txt; alertLvl = lvl;
}

void tickHistory() {
    if (millis() - lastHistMs < HIST_MS) return;
    lastHistMs = millis();
    float c = chamberNow();
    histC[histHead] = isnan(c) ? INT16_MIN : (int16_t)lroundf(c);
    char s = '-';
    if (heaterLive()) s = S.state == "RUN" ? 'R' : S.state == "COOL" ? 'C' : S.state == "FAULT" ? 'F' : S.state == "OFF" ? 'O' : '-';
    histS[histHead] = s;
    histHead = (histHead + 1) % HIST_N;
}

// ---------------- RGB LED ----------------
uint32_t ledBootMs = 0, ledFlashMs = 0, lastLedMs = 0;
uint8_t ledR = 1, ledG = 1, ledB = 1;

void ledTick(bool force = false) {
    uint32_t now = millis();
    if (!force && now - lastLedMs < 20) return;          // 50 frames a second
    lastLedMs = now;
    uint8_t r = 0, g = 0, b = 0;
    if (LED_MAX > 0) {
        LedIn in;
        in.wifi   = (WiFi.getMode() == WIFI_AP) ? 2 : (WiFi.status() == WL_CONNECTED ? 1 : 0);
        in.link   = !linkStale();
        in.heater = heaterLive();
        in.st     = S.state == "OFF" ? 'O' : S.state == "RUN" ? 'R' : S.state == "COOL" ? 'C' : S.state == "FAULT" ? 'F' : 'U';
        in.phase  = ledPhaseFromName(S.phase.c_str());
        in.level  = S.level;
        in.chamberC = chamberNow();
        in.alert  = alertLvl == "bad" ? 2 : (alertLvl == "warn" ? 1 : 0);
        in.flashMs = ledFlashMs;
        in.bootMs  = ledBootMs;
        ledToBytes(ledCompute(in, now), LED_MAX, r, g, b);
    }
    if (r != ledR || g != ledG || b != ledB) { rgbLedWrite(RGB_PIN, r, g, b); ledR = r; ledG = g; ledB = b; }
}

// ---------------- WEB ----------------
void sendPage(const String &t) {
    if      (t == "lcd")  server.send_P(200, "text/html", PAGE_LCD);
    else if (t == "dash") server.send_P(200, "text/html", PAGE_DASH);
    else                  server.send_P(200, "text/html", PAGE_BURNER);
}
void handleRoot() { sendPage(theme); }
void handleCore() {
    server.sendHeader("Cache-Control", "max-age=86400");
    server.send_P(200, "application/javascript", CORE_JS);
}

String jsonEsc(const String &in) {        // keep JSON valid no matter what ends up in a string
    String s; s.reserve(in.length());
    for (size_t i = 0; i < in.length(); i++) {
        char c = in[i];
        if (c == '\\') s += '/';
        else if (c == '"') s += '\'';
        else if ((uint8_t)c < 0x20) s += ' ';
        else s += c;                          // bytes 0x80+ are UTF-8 (the degree sign) - keep
    }
    return s;
}

void handleStatus() {
    bool stale = linkStale();
    String lg;
    for (uint8_t i = 0; i < LOG_N; i++) {
        const String &e = eventLog[(logHead + LOG_N - 1 - i) % LOG_N];
        if (e.length()) { lg += jsonEsc(e); lg += "\\n"; }
    }
    String j; j.reserve(3200);
    j += "{\"fw\":14,\"state\":\""; j += jsonEsc(S.state); j += "\"";
    j += ",\"phase\":\"";  j += jsonEsc(S.phase); j += "\"";
    j += ",\"level\":";    j += S.level;
    j += ",\"run\":";      j += S.run;
    j += ",\"pump\":";     j += S.pump;
    j += ",\"glow\":";     j += S.glow;
    j += ",\"chamber\":";  j += S.chamber;
    j += ",\"chamberC\":";
    if (S.chamber >= 0) {
        bool ap; float c = chamberC(S.chamber, ap);
        j += String(c, 1);
        j += ",\"approx\":"; j += ap ? "true" : "false";
    } else {
        j += "null,\"approx\":false";
    }
    j += ",\"volts\":";    j += S.volts;
    j += ",\"vage\":";     j += S.vage;
    j += ",\"ecode\":";    j += S.ecode;
    j += ",\"bridge\":";   j += S.bridge;
    j += ",\"junk\":";     j += junkLines;
    j += ",\"unit\":\"";   j += unitF ? "F" : "C"; j += "\"";
    j += ",\"theme\":\"";  j += theme; j += "\"";
    j += ",\"poll\":";     j += S.poll;
    j += ",\"age\":";      j += S.age;
    j += ",\"stale\":";    j += stale ? "true" : "false";
    j += ",\"word\":\"";   j += jsonEsc(S.word); j += "\"";
    j += ",\"mode\":\"";   j += jsonEsc(S.mode); j += "\"";
    j += ",\"alert\":\"";  j += jsonEsc(alertTxt); j += "\"";
    j += ",\"alertLvl\":\""; j += alertLvl; j += "\"";
    j += ",\"stats\":{\"today\":"; j += todayRunS;
    j += ",\"total\":";    j += totRunS;
    j += ",\"starts\":";   j += starts;
    j += ",\"light\":";    j += lastLightS;
    j += ",\"runFor\":";   j += (runStartMs && S.state == "RUN") ? (long)((millis() - runStartMs) / 1000) : -1L;
    j += ",\"clock\":";    j += clockOk() ? "true" : "false";
    j += "}";
    j += ",\"ip\":\"";     j += (WiFi.getMode() == WIFI_AP ? WiFi.softAPIP().toString() : WiFi.localIP().toString()); j += "\"";
    j += ",\"log\":\"";    j += lg; j += "\"}";
    server.send(200, "application/json", j);
}

void handleHistory() {
    String j; j.reserve(HIST_N * 5 + 320);
    j += "{\"step\":15,\"c\":[";
    for (uint16_t i = 0; i < HIST_N; i++) {
        int16_t v = histC[(histHead + i) % HIST_N];
        if (i) j += ',';
        if (v == INT16_MIN) j += "null"; else j += v;
    }
    j += "],\"s\":\"";
    for (uint16_t i = 0; i < HIST_N; i++) j += histS[(histHead + i) % HIST_N];
    j += "\"}";
    server.send(200, "application/json", j);
}

void handleCmd() {
    String c = server.arg("c"); c.toLowerCase();
    String out;
    if      (c == "on")    out = "ON";
    else if (c == "off")   out = "OFF";
    else if (c == "up")    out = "UP";
    else if (c == "down")  out = "DOWN";
    else if (c == "level") {
        int n = server.arg("n").toInt();
        if (n < 1 || n > 6) { server.send(400, "text/plain", "level 1-6"); return; }
        out = "LEVEL " + String(n);
    }
    else if (c == "chamber") {
        int n = server.arg("n").toInt();
        if (n < 0 || n > 60) { server.send(400, "text/plain", "chamber 0-60"); return; }
        out = "CHAMBER " + String(n);
        pollPref = n;
        prefs.putInt("poll", n);
    }
    else { server.send(400, "text/plain", "unknown"); return; }
    nano.println(out);
    addLog("> " + out);
    ledFlashMs = millis(); if (!ledFlashMs) ledFlashMs = 1;
    server.send(200, "text/plain", "sent " + out);
}

void handleUnit() {
    String u = server.arg("u"); u.toUpperCase();
    if (u != "C" && u != "F") { server.send(400, "text/plain", "u=C or u=F"); return; }
    unitF = (u == "F");
    prefs.putBool("unitF", unitF);
    server.send(200, "text/plain", unitF ? "F" : "C");
}

void handleTheme() {
    String t = server.arg("t"); t.toLowerCase();
    if (t != "burner" && t != "lcd" && t != "dash") { server.send(400, "text/plain", "t=burner|lcd|dash"); return; }
    theme = t;
    prefs.putString("theme", theme);
    server.send(200, "text/plain", theme);
}

// ---------------- SETUP ----------------
void setup() {
    Serial.begin(115200);
    nano.begin(115200, SERIAL_8N1, NANO_RX_PIN, NANO_TX_PIN);
    delay(200);
    Serial.println("\nHEATER WEB v1.5");
    ledBootMs = millis();
    ledTick(true);

    for (uint16_t i = 0; i < HIST_N; i++) { histC[i] = INT16_MIN; histS[i] = '-'; }

    prefs.begin("heater", false);
    unitF      = prefs.getBool("unitF", false);
    pollPref   = prefs.getInt("poll", -1);
    theme      = prefs.getString("theme", "burner");
    totRunS    = prefs.getUInt("totRun", 0);
    starts     = prefs.getUInt("starts", 0);
    todayRunS  = prefs.getUInt("todRun", 0);
    todayKey   = prefs.getInt("todKey", -1);
    lastLightS = prefs.getInt("light", -1);

    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASS);
    Serial.print("Joining "); Serial.print(WIFI_SSID);
    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
        for (int i = 0; i < 25; i++) { delay(20); ledTick(); }       // keep the LED animating
        Serial.print('.');
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(); Serial.print("IP: "); Serial.println(WiFi.localIP());
        configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov");
        addLog("WiFi " + WiFi.localIP().toString());
    } else {
        Serial.println("\nWiFi failed - starting hotspot");
        WiFi.mode(WIFI_AP);
        WiFi.softAP(AP_SSID, AP_PASS);
        Serial.print("AP IP: "); Serial.println(WiFi.softAPIP());
        addLog("AP mode " + WiFi.softAPIP().toString());
    }
    if (MDNS.begin(HOSTNAME)) Serial.println("mDNS: http://heater.local");

    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();

    server.on("/", handleRoot);
    server.on("/burner", []() { sendPage("burner"); });
    server.on("/lcd",    []() { sendPage("lcd"); });
    server.on("/dash",   []() { sendPage("dash"); });
    server.on("/core.js", handleCore);
    server.on("/api/status", handleStatus);
    server.on("/api/history", handleHistory);
    server.on("/api/cmd", handleCmd);
    server.on("/api/unit", handleUnit);
    server.on("/api/theme", handleTheme);
    server.begin();

    nano.println("PING");
    if (pollPref >= 0) nano.println("CHAMBER " + String(pollPref));
}

void loop() {
    server.handleClient();
    ArduinoOTA.handle();
    pollNano();
    ledTick();
    static unsigned long lastTick = 0;
    if (millis() - lastTick >= 1000) {
        lastTick = millis();
        tickStats();
        tickAlerts();
    }
    tickHistory();
}
