/*
 * ============================================================================
 *  HEATER BUS BRIDGE  -  Arduino Nano / Uno  -  v1.2  (Oct 23 2026)
 * ============================================================================
 *  The Nano is the 5V "LCD" on the heater's blue wire. It does the timing-
 *  critical bus work and talks to a master (ESP32, PC, or Serial Monitor)
 *  over plain text at 115200.
 *
 *  v1.2 (smarter decoding, from alxkalb's protocol notes inverted to our bits)
 *   - Status low byte decoded properly: 0x3F cooling, 0xBA..0xBF running
 *     (level = 0xC0 - low), 0xE0+ standby where low = 0xFF - supply volts
 *     (9FF3 = 12 V, same number the OEM display shows). Volts are only sent
 *     while idle, so they're reported with their age (status fields 12, 13).
 *   - PHASE name (field 14) from the fan/pump/glow flags:
 *       running: START, FAN, PREHEAT, IGNITION, RUN
 *       cooling: SHUTDOWN, CLEARING, PURGE, COOL      idle: IDLE
 *   - FAULT: tentative E-code (field 15) = ((~low) & 0x3F) - 1. E-08 (empty
 *     tank / no fuel) matches alxkalb's capture. -1 when not faulted.
 *   - ON now also works from FAULT (the OEM display clears a fault that way).
 *   - A queued command that can't be sent in 8 s is dropped: "ERR command expired".
 *   - Unknown low bytes are UNK (logged with the word) instead of guessed.
 *
 *  v1.1 (after the first shed run with the ESP32 page)
 *   - CHAMBER POLL. The OEM display's OK button never goes on the wire: pressing
 *     OK just makes the display start asking the heater for page 0x50. So in
 *     piggyback the Nano now asks for 0x50 itself every CHAMBER_POLL_S seconds
 *     while the heater is RUN or COOL, in the same idle gap commands use.
 *     Skipped automatically while the display is already on its chamber page.
 *     "CHAMBER n" sets the interval (0 = off); reported as status field 11.
 *     NOT YET TESTED against the OEM display - if its screen flickers or flashes
 *     an error every few seconds, turn it off (button on the web page).
 *   - FIX: never more than one injected frame per display cycle, and never right
 *     after the reply to our own frame. v1.0's LEVEL n could overshoot by one
 *     level and drop a second frame on top of the display's next heartbeat.
 *   - FIX: TX no longer blocks interrupts, so a command arriving from the ESP32
 *     mid-frame isn't dropped. Pulse jitter is microseconds, far inside the
 *     heater's tolerance (4 vs 8 ms).
 *   - Pin is set LOW before it becomes an output, so it never drives HIGH.
 *   - Listens 2.5 s after power-up before deciding standalone, so it never
 *     fires a blind heartbeat into a display that's plugged in.
 *   - FAULT state for 0x7xxx words (kellerman / alxkalb), needs 2 in a row.
 *   - "EVT old->new" on every state change, and "REQ xx -> yyyy" the first time
 *     the display sends a byte we haven't seen (OEM buttons / pages).
 *   - Guards: ON only from OFF, OFF only from RUN, UP refused at 6, DOWN at 1.
 *   - Chamber raw reported as -1 when older than 15 s.
 *
 *  PROTOCOL (bench-proven Oct 23 2026, nano_test_v22..v24)
 *    frame: 30 ms LOW start, then bits of HIGH gap + LOW pulse = 12.05 ms,
 *           LOW 8 ms = 1, LOW 4 ms = 0. Open drain only (pull low or let go).
 *    display -> heater 8 bits, heater -> display 16 bits, about once a second.
 *    0x59 heartbeat, 0x50 chamber page.
 *    commands = 0x59 with bits flipped, for one frame:
 *      POWER 0x5D   DOWN 0x5B   UP 0x5C (all three confirmed)
 *    status word  9 | flags | low byte
 *      flags (active LOW): bit3 fan  bit2 pump  bit1 ?  bit0 glow
 *      low byte: 3F cooling   BA..BF running, level = C0 - low
 *                E0..FF idle, supply volts = FF - low
 *    chamber word 0xB + 12-bit raw count (falls as it heats; ESP32 converts)
 *    fault words  7 | flags | low byte, E-code = ((~low) & 3F) - 1 (tentative)
 *
 *  WIRING  heater plug BLUE -> D2,  BLACK -> GND.  RED: leave alone.
 *          Nano 5V/GND from the ESP32. Nano TX -> ESP32 GPIO16, Nano RX <- GPIO17.
 *          Pull the jumper off Nano RX while flashing the Nano over USB.
 *
 *  SERIAL (115200, newline terminated, any case)
 *    in:  ON | OFF | UP | DOWN | LEVEL n | CHAMBER n | STATUS | PING | RAW ON|OFF
 *    out: S,<state>,<level>,<run>,<pump>,<glow>,<word>,<mode>,<chamber_raw>,<age_s>,<poll_s>,
 *           <volts>,<volts_age_s>,<phase>,<ecode>
 *           state = OFF|RUN|COOL|FAULT|UNK   mode = PB (piggyback) | SA (standalone)
 *           volts 0 / age 999 until the heater has been seen idle
 *         ACK ... | ERR ... | EVT old->new | REQ xx -> yyyy | MODE PB|SA | PONG
 *         R,LCD,xx / R,HTR,yyyy (R,HTR* = reply to our frame) when RAW ON
 * ============================================================================
 */

#include "bridge_types.h"

// ---------------- CONFIG ----------------
#define CHAMBER_POLL_S   5     // piggyback: ask for chamber every N s while RUN/COOL (0 = never)
// ----------------------------------------

#define DATA_PIN   2
#define LED_PIN    13

// ---- RX timing (what we accept) ----
#define BIT_0_MIN   2500
#define BIT_0_MAX   5500
#define BIT_1_MIN   6500
#define BIT_1_MAX   9500
#define FRAME_GAP   12000UL

// ---- TX timing ----
#define RF_START_LOW_US   30000UL
#define RF_BIT_PERIOD_US  12050UL
#define RF_LONG_US         8000UL
#define RF_SHORT_US        4000UL

// ---- bytes ----
#define B_HEARTBEAT 0x59
#define B_CHAMBER   0x50
#define M_POWER     0x04   // 0x5D  bench-proven
#define M_DOWN      0x02   // 0x5B  seen from the OEM display
#define M_UP        0x05   // 0x5C  old capture, confirm

// ---- timing ----
#define BOOT_LISTEN_MS     2500UL   // listen for an OEM display before heartbeating
#define LCD_TIMEOUT_MS     3000UL   // no display frame this long -> standalone
#define OWN_REPLY_MS        600UL   // stop waiting for the reply to our frame
#define CHAMBER_STALE_MS  15000UL
#define STATUS_MIN_MS       900UL
#define STATUS_MAX_MS      1500UL
#define MAX_LEVEL_STEPS       8
#define CMD_EXPIRE_MS      8000UL   // drop a command that never found a slot

// ============================================================================
// RX capture: pin-change ISR records edge-to-edge widths
// ============================================================================
#define MAX_PULSES 64
volatile unsigned long lastEdgeTime = 0;
volatile unsigned long pulseWidths[MAX_PULSES];
volatile uint8_t pulseIndex = 0;
volatile bool frameReady = false;

void rxISR() {
    unsigned long now = micros();
    unsigned long width = now - lastEdgeTime;
    lastEdgeTime = now;
    if (width < 1000) return;
    if (width > FRAME_GAP) { if (pulseIndex > 0) frameReady = true; return; }
    if (pulseIndex < MAX_PULSES && !frameReady) pulseWidths[pulseIndex++] = width;
}

void enableRX() {
    pinMode(DATA_PIN, INPUT_PULLUP);
    delayMicroseconds(100);
    noInterrupts();
    pulseIndex = 0; frameReady = false;
    lastEdgeTime = micros();
    interrupts();
    attachInterrupt(digitalPinToInterrupt(DATA_PIN), rxISR, CHANGE);
}
void disableRX() { detachInterrupt(digitalPinToInterrupt(DATA_PIN)); }

// ============================================================================
// TX: open drain. Write LOW first, then make it an output, so the pin never
// drives the bus HIGH.
// ============================================================================
static inline void busLow()     { digitalWrite(DATA_PIN, LOW); pinMode(DATA_PIN, OUTPUT); }
static inline void busRelease() { pinMode(DATA_PIN, INPUT_PULLUP); }
static void longDelay(unsigned long us) { while (us > 10000) { delayMicroseconds(10000); us -= 10000; } delayMicroseconds(us); }

unsigned long lastTxMs = 0;
bool ownReplyPending = false;       // we just sent a frame; the next heater reply is ours

void sendFrame(uint8_t b) {
    disableRX();
    busRelease(); delayMicroseconds(2000);
    busLow();     longDelay(RF_START_LOW_US);
    for (int8_t i = 7; i >= 0; i--) {
        unsigned long lowUs = ((b >> i) & 1) ? RF_LONG_US : RF_SHORT_US;
        busRelease(); delayMicroseconds(RF_BIT_PERIOD_US - lowUs);
        busLow();     delayMicroseconds(lowUs);
    }
    busRelease();
    lastTxMs = millis();
    ownReplyPending = true;
    enableRX();
}

// ============================================================================
// Decoded state
// ============================================================================
struct {
    HState   state = ST_UNK;
    uint8_t  level = 0;
    bool     run = false, pump = false, glow = false;
    uint16_t word = 0;
    int      chamberRaw = -1;
    unsigned long chamberMs = 0;
    unsigned long lastReplyMs = 0;
    uint8_t  volts = 0;            // supply volts, only sent while idle
    unsigned long voltsMs = 0;
    int8_t   ecode = -1;           // tentative E-number while faulted
} H;

bool     piggyback = false;
unsigned long lastLcdMs = 0;
bool     rawEcho = false;
uint8_t  cmdMask = 0;              // pending command, XOR'd onto 0x59
unsigned long cmdQueuedMs = 0;
int8_t   targetLevel = -1;
uint8_t  levelSteps = 0;
uint8_t  chamberPollS = CHAMBER_POLL_S;
unsigned long lastChamberReqMs = 0;
unsigned long lastHbMs = 0;
bool     nextIsChamber = false;
unsigned long lastStatusMs = 0;
uint8_t  seenReq[32];              // one bit per display byte seen since boot
int16_t  reqToReport = -1;

const char* stateName(HState s) {
    switch (s) {
        case ST_OFF:   return "OFF";
        case ST_RUN:   return "RUN";
        case ST_COOL:  return "COOL";
        case ST_FAULT: return "FAULT";
        default:       return "UNK";
    }
}

void printHex2(uint8_t b) { if (b < 0x10) Serial.print('0'); Serial.print(b, HEX); }

void queueCmd(uint8_t m) { cmdMask = m; cmdQueuedMs = millis(); }

// What the heater is doing inside the state, from the fan/pump/glow flags.
const __FlashStringHelper* phaseName() {
    bool f = H.run, p = H.pump, g = H.glow;
    switch (H.state) {
        case ST_OFF:   return F("IDLE");
        case ST_FAULT: return F("FAULT");
        case ST_RUN:
            if (!f && !p && !g) return F("START");
            if ( f && !p && !g) return F("FAN");
            if ( f && !p &&  g) return F("PREHEAT");
            if ( p &&  g)       return F("IGNITION");
            return F("RUN");
        case ST_COOL:
            if (p) return F("SHUTDOWN");
            if (g) return F("CLEARING");
            if (f) return F("PURGE");
            return F("COOL");
        default:       return F("UNK");
    }
}

// Pulses arrive as (HIGH gap, LOW pulse) pairs; the real bit is the LOW pulse
// (odd index). Returns 8 or 16 for a good frame, 0 for junk.
uint8_t decodeFrame(const unsigned long *w, uint8_t cnt, uint16_t &word) {
    uint8_t bits[MAX_PULSES]; uint8_t n = 0;
    for (uint8_t i = 0; i < cnt && n < MAX_PULSES; i++) {
        unsigned long x = w[i];
        if      (x >= BIT_0_MIN && x <= BIT_0_MAX) bits[n++] = 0;
        else if (x >= BIT_1_MIN && x <= BIT_1_MAX) bits[n++] = 1;
    }
    uint8_t real = n / 2;
    if (real != 8 && real != 16) return 0;
    word = 0;
    for (uint8_t i = 0; i < real; i++) word = (word << 1) | bits[2 * i + 1];
    return real;
}

// FAULT / UNK need two words in a row so one bad frame can't flash an alarm.
bool setState(HState ns, uint16_t w) {
    static HState oddState = ST_OFF;   // ST_OFF = none pending
    static uint8_t oddCount = 0;
    if (ns == ST_FAULT || ns == ST_UNK) {
        if (oddState != ns) { oddState = ns; oddCount = 0; }
        if (++oddCount < 2) return false;
    } else { oddState = ST_OFF; oddCount = 0; }
    if (ns != H.state) {
        Serial.print(F("EVT ")); Serial.print(stateName(H.state));
        Serial.print(F("->"));   Serial.print(stateName(ns));
        if (ns == ST_FAULT || ns == ST_UNK) { Serial.print(F(" word ")); Serial.print(w, HEX); }
        Serial.println();
        H.state = ns;
    }
    return true;
}

void handleHeaterWord(uint16_t w) {
    H.lastReplyMs = millis();
    uint8_t top = w >> 12;
    if (top == 0xB) { H.chamberRaw = w & 0x0FFF; H.chamberMs = millis(); return; }

    uint8_t flags = (w >> 8) & 0x0F;
    uint8_t lo    =  w       & 0xFF;
    HState ns = ST_UNK;
    if (top == 0x9) {
        if      (lo >= 0xE0)               ns = ST_OFF;
        else if (lo >= 0xBA && lo <= 0xBF) ns = ST_RUN;
        else if ((lo & 0xF0) == 0x30)      ns = ST_COOL;
    } else if (top == 0x7) ns = ST_FAULT;

    if (!setState(ns, w)) return;
    H.word = w;
    H.run   = !(flags & 0x08);
    H.pump  = !(flags & 0x04);
    H.glow  = !(flags & 0x01);
    H.level = (ns == ST_RUN) ? (0xC0 - lo) : 0;
    if (ns == ST_OFF) { H.volts = 0xFF - lo; H.voltsMs = millis(); }
    if (ns == ST_FAULT) H.ecode = (int8_t)(((uint8_t)~lo) & 0x3F) - 1;
    else                H.ecode = -1;
    if (ns == ST_UNK) H.run = H.pump = H.glow = false;
}

void printStatus() {
    int cr = (H.chamberMs && millis() - H.chamberMs < CHAMBER_STALE_MS) ? H.chamberRaw : -1;
    Serial.print(F("S,")); Serial.print(stateName(H.state));
    Serial.print(','); Serial.print(H.level);
    Serial.print(','); Serial.print(H.run  ? 1 : 0);
    Serial.print(','); Serial.print(H.pump ? 1 : 0);
    Serial.print(','); Serial.print(H.glow ? 1 : 0);
    Serial.print(','); Serial.print(H.word, HEX);
    Serial.print(','); Serial.print(piggyback ? F("PB") : F("SA"));
    Serial.print(','); Serial.print(cr);
    Serial.print(','); Serial.print(H.lastReplyMs ? (millis() - H.lastReplyMs) / 1000 : 999);
    Serial.print(','); Serial.print(chamberPollS);
    Serial.print(','); Serial.print(H.volts);
    Serial.print(','); Serial.print(H.voltsMs ? (millis() - H.voltsMs) / 1000 : 999);
    Serial.print(','); Serial.print(phaseName());
    Serial.print(','); Serial.println(H.ecode);
    lastStatusMs = millis();
}

// ============================================================================
// Scheduling
// ============================================================================
bool chamberDue() {
    if (chamberPollS == 0) return false;
    if (H.state != ST_RUN && H.state != ST_COOL) return false;
    unsigned long iv = (unsigned long)chamberPollS * 1000UL;
    if (H.chamberMs && millis() - H.chamberMs < iv) return false;          // fresh (display may be on chamber page)
    if (lastChamberReqMs && millis() - lastChamberReqMs < iv) return false;
    return true;
}

// Piggyback: called right after the heater answers the DISPLAY. The bus is now
// quiet for ~650 ms before the display's next heartbeat; one frame fits.
void piggybackSlot() {
    if (cmdMask) { uint8_t b = B_HEARTBEAT ^ cmdMask; cmdMask = 0; sendFrame(b); return; }
    if (chamberDue()) { lastChamberReqMs = millis(); sendFrame(B_CHAMBER); }
}

// Standalone: we are the display. Heartbeat once a second, alternating with the
// chamber request; a pending command replaces one heartbeat.
void standaloneTick() {
    if (millis() < BOOT_LISTEN_MS) return;
    if (ownReplyPending) return;
    if (millis() - lastHbMs < 1000) return;
    lastHbMs = millis();
    uint8_t b;
    if (cmdMask) { b = B_HEARTBEAT ^ cmdMask; cmdMask = 0; nextIsChamber = false; }
    else         { b = nextIsChamber ? B_CHAMBER : B_HEARTBEAT; nextIsChamber = !nextIsChamber; }
    sendFrame(b);
}

// LEVEL n: one step per display cycle, and only after the reply to the last
// step has shown the new level.
void stepTowardTarget() {
    if (targetLevel < 0 || cmdMask || ownReplyPending) return;
    if (H.state != ST_RUN) { targetLevel = -1; Serial.println(F("ERR LEVEL stopped, not running")); return; }
    if (H.level == targetLevel) {
        targetLevel = -1; Serial.print(F("ACK LEVEL done ")); Serial.println(H.level); return;
    }
    if (++levelSteps > MAX_LEVEL_STEPS) { targetLevel = -1; Serial.println(F("ERR LEVEL gave up")); return; }
    queueCmd((H.level < targetLevel) ? M_UP : M_DOWN);
}

void onLcdFrame(uint8_t b) {
    lastLcdMs = millis();
    if (rawEcho) { Serial.print(F("R,LCD,")); printHex2(b); Serial.println(); }
    if (!(seenReq[b >> 3] & (1 << (b & 7)))) {
        seenReq[b >> 3] |= (1 << (b & 7));
        reqToReport = b;                      // reported with the heater's answer
    }
}

void onHeaterFrame(uint16_t w) {
    bool ours = ownReplyPending;
    ownReplyPending = false;
    handleHeaterWord(w);
    if (rawEcho) { Serial.print(ours ? F("R,HTR*,") : F("R,HTR,")); Serial.println(w, HEX); }
    if (reqToReport >= 0 && !ours) {
        Serial.print(F("REQ ")); printHex2((uint8_t)reqToReport);
        Serial.print(F(" -> ")); Serial.println(w, HEX);
        reqToReport = -1;
    }
    digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    if (millis() - lastStatusMs >= STATUS_MIN_MS) printStatus();   // bus is quiet now
    if (piggyback && !ours) piggybackSlot();
}

// ============================================================================
// Commands from the ESP32 / Serial Monitor
// ============================================================================
void handleCommand(char *c) {
    for (char *p = c; *p; p++) *p = toupper(*p);

    if (!strcmp(c, "PING"))         { Serial.println(F("PONG")); }
    else if (!strcmp(c, "STATUS"))  { printStatus(); }
    else if (!strcmp(c, "RAW ON"))  { rawEcho = true;  Serial.println(F("ACK RAW ON")); }
    else if (!strcmp(c, "RAW OFF")) { rawEcho = false; Serial.println(F("ACK RAW OFF")); }
    else if (!strcmp(c, "ON")) {
        if (H.state != ST_OFF && H.state != ST_FAULT) { Serial.print(F("ERR ON refused, heater is ")); Serial.println(stateName(H.state)); }
        else { targetLevel = -1; queueCmd(M_POWER); Serial.println(H.state == ST_FAULT ? F("ACK ON (clear fault)") : F("ACK ON")); }
    }
    else if (!strcmp(c, "OFF")) {
        if (H.state != ST_RUN) { Serial.print(F("ERR OFF refused, heater is ")); Serial.println(stateName(H.state)); }
        else { targetLevel = -1; queueCmd(M_POWER); Serial.println(F("ACK OFF")); }   // OFF beats any pending step
    }
    else if (!strcmp(c, "UP") || !strcmp(c, "DOWN")) {
        bool up = (c[0] == 'U');
        if (H.state != ST_RUN)              Serial.println(F("ERR not running"));
        else if (cmdMask == M_POWER)        Serial.println(F("ERR busy"));
        else if (up && H.level >= 6)        Serial.println(F("ERR already level 6"));
        else if (!up && H.level <= 1)       Serial.println(F("ERR already level 1"));
        else { targetLevel = -1; queueCmd(up ? M_UP : M_DOWN); Serial.println(up ? F("ACK UP") : F("ACK DOWN")); }
    }
    else if (!strncmp(c, "LEVEL ", 6)) {
        int n = atoi(c + 6);
        if (n < 1 || n > 6)                 Serial.println(F("ERR level 1-6"));
        else if (H.state != ST_RUN)         Serial.println(F("ERR not running"));
        else if (cmdMask == M_POWER)        Serial.println(F("ERR busy"));
        else { targetLevel = n; levelSteps = 0; Serial.print(F("ACK LEVEL ")); Serial.println(n); }
    }
    else if (!strncmp(c, "CHAMBER ", 8)) {
        int n = atoi(c + 8);
        if (n < 0 || n > 60) Serial.println(F("ERR chamber 0-60"));
        else { chamberPollS = n; Serial.print(F("ACK CHAMBER ")); Serial.println(n); }
    }
    else Serial.println(F("ERR unknown"));
}

char cmdBuf[24]; uint8_t cmdLen = 0;

void pollSerial() {
    while (Serial.available()) {
        char ch = Serial.read();
        if (ch == '\n' || ch == '\r') {
            if (cmdLen) { cmdBuf[cmdLen] = 0; handleCommand(cmdBuf); cmdLen = 0; }
        } else if (cmdLen < sizeof(cmdBuf) - 1) cmdBuf[cmdLen++] = ch;
    }
}

// ============================================================================
void setup() {
    pinMode(LED_PIN, OUTPUT);
    Serial.begin(115200);
    memset(seenReq, 0, sizeof(seenReq));
    seenReq[B_HEARTBEAT >> 3] |= 1 << (B_HEARTBEAT & 7);   // known, don't report
    seenReq[B_CHAMBER >> 3]   |= 1 << (B_CHAMBER & 7);
    enableRX();
    Serial.println(F("HEATER BRIDGE v1.2 ready"));
}

void loop() {
    // A lone reply has no trailing edge: call the frame done after 24 ms of quiet.
    if (!frameReady && pulseIndex > 0) {
        noInterrupts(); unsigned long idle = micros() - lastEdgeTime; interrupts();
        if (idle > FRAME_GAP * 2) frameReady = true;
    }

    if (frameReady) {
        unsigned long w[MAX_PULSES]; uint8_t cnt;
        noInterrupts();
        cnt = pulseIndex;
        for (uint8_t i = 0; i < cnt; i++) w[i] = pulseWidths[i];
        pulseIndex = 0; frameReady = false;
        interrupts();
        uint16_t word;
        uint8_t nb = decodeFrame(w, cnt, word);
        if (nb == 8)       onLcdFrame((uint8_t)word);
        else if (nb == 16) onHeaterFrame(word);
    }

    if (ownReplyPending && millis() - lastTxMs > OWN_REPLY_MS) ownReplyPending = false;
    if (cmdMask && millis() - cmdQueuedMs > CMD_EXPIRE_MS) {
        cmdMask = 0; targetLevel = -1;
        Serial.println(F("ERR command expired"));
    }

    bool pbNow = lastLcdMs && (millis() - lastLcdMs < LCD_TIMEOUT_MS);
    if (pbNow != piggyback) {
        piggyback = pbNow;
        Serial.println(piggyback ? F("MODE PB") : F("MODE SA"));
    }

    if (!piggyback) standaloneTick();
    stepTowardTarget();
    pollSerial();

    if (millis() - lastStatusMs >= STATUS_MAX_MS) printStatus();   // heater silent: keep the link alive
}
