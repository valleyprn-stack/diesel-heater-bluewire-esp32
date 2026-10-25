# The "blue wire" protocol

This describes what we decoded on the 3-wire link between a cheap Chinese diesel air heater and its simple LCD controller (the kind with the red / blue / black cable). It was worked out on one heater with a logic analyzer and an Arduino, then confirmed by controlling the heater with it for real. Your heater may differ. Treat anything marked *tentative* as exactly that.

These heaters do **not** all speak the same language. Some use a 4800-baud UART, some (like the Afterburner-style controllers) use another serial scheme. This one is a slow pulse-width code on a single wire. If your logic analyzer shows 4 ms and 8 ms low pulses about 12 ms apart, you're in the right place.

## Wiring

| Wire  | What it is |
|-------|------------|
| Blue  | Data. One wire, both directions, idles HIGH at 5 V. |
| Black | Ground. |
| Red   | Supply for the display. Not used here. Meter it before you touch it. |

The data line is **open drain**: whoever is talking pulls it LOW, and the heater/display pull-ups bring it back HIGH. Never drive it HIGH. On an Arduino that means `digitalWrite(pin, LOW); pinMode(pin, OUTPUT);` to pull low and `pinMode(pin, INPUT_PULLUP)` to let go.

## Frame format

```
start      bit        bit        bit
____      ___    __   ___    __
    |    |   |  |  | |   |  |
    |____|   |__|  |_|   |__|  ...
    30 ms   gap+LOW  gap+LOW
```

- A frame starts with the line held **LOW for 30 ms**.
- Then each bit is a **12.05 ms slot**: the line goes HIGH (gap), then LOW for the rest of the slot.
- **LOW for 8 ms = 1, LOW for 4 ms = 0** (so the HIGH gap is about 4 ms for a 1 and 8 ms for a 0).
- Most significant bit first.
- Display to heater: **8 bits**, about once a second. Heater answers with **16 bits** straight after.
- A display frame takes about 126 ms, a heater reply about 223 ms.

Receive window we use: 2.5-5.5 ms LOW = 0, 6.5-9.5 ms LOW = 1, any edge gap over 12 ms ends the frame.

Bit convention note: the values here match [kellerman/vevor_pwm](https://github.com/kellerman/vevor_pwm). The notes in [alxkalb/vevor-8kw-diesel-heater-esphome](https://github.com/alxkalb/vevor-8kw-diesel-heater-esphome) read the pulses the other way round, so every value there is bit-inverted compared to this page (their `0x8CC9` is our `0x7336`).

## What the display sends

| Byte | Meaning |
|------|---------|
| `0x59` | Heartbeat / "give me your status". Sent about once a second. |
| `0x50` | "Give me the chamber temperature". The OEM display only sends this while it's on its chamber screen. Its OK button never goes on the wire - it just switches the display to asking for `0x50`. |

**Commands are a heartbeat with some bits flipped, for one frame:**

| Command | Byte | (= `0x59` XOR) |
|---------|------|----------------|
| Power (on / off / clear fault) | `0x5D` | `0x04` |
| Level down | `0x5B` | `0x02` |
| Level up | `0x5C` | `0x05` |

All three confirmed on a real heater.

## What the heater answers

### Status word `0x9???` (answer to `0x59`)

```
 1001  F F F F  LLLL LLLL
 '9'   flags    low byte
```

**Flags nibble, active LOW (0 = on):**

| Bit | Meaning |
|-----|---------|
| 3 | Fan running |
| 2 | Fuel pump ticking |
| 1 | Unknown (always 1 so far) |
| 0 | Glow plug on |

**Low byte:**

| Value | State | Extra |
|-------|-------|-------|
| `0xE0`-`0xFF` | Standby | **supply volts = `0xFF - low`** (`0x9FF3` = 12 V, matches the OEM display) |
| `0xBA`-`0xBF` | Running | **level = `0xC0 - low`** (`0xBF` = level 1 ... `0xBA` = level 6) |
| `0x3F` | Cooling down / shutting down | |

The supply voltage is only reported in standby, in whole volts.

**Phases** come from the flags. These are the words we saw going through a start and a stop:

| Word | Flags on | Phase |
|------|----------|-------|
| `9FBD` | none | Start requested |
| `97xx` | fan | Fan spinning up |
| `96xx` | fan + glow | Preheat (glow plug heating) |
| `92xx` | fan + pump + glow | Ignition |
| `93xx` | fan + pump | Running (lit) |
| `9B3F` / `933F` | pump (+ fan) | Shutdown, last fuel burning off |
| `963F` | fan + glow | Clearing (glow plug burns off leftovers) |
| `973F` | fan | Purge / cool-down |
| `9FFx` | none | Standby again |

On our heater, from pressing ON to the glow plug switching off with the pump running ("lit") took **125 s**, twice in a row. A full stop from RUN to standby took about **4 minutes**.

### Chamber word `0xB???` (answer to `0x50`)

`0xB` + a **12-bit raw count** that **falls as the chamber heats**. Calibrated against the OEM display's own chamber readout:

| Raw | 3859 | 3722 | 3254 | 3144 | 3118 | 3110 | 3100 |
|-----|------|------|------|------|------|------|------|
| °C  | 34   | 50   | 114  | 160  | 180  | 190  | 200  |

Straight lines between the points work well. Outside 34-200 °C we extrapolate and mark the reading as approximate.

The heater did not answer the chamber request during the CLEARING phase right after a stop (the OEM display won't show chamber temperature then either).

### Fault word `0x7???`

Same flags nibble as the status word. **E-code (tentative) = `((~low) & 0x3F) - 1`.**

The only fault sequence we have is alxkalb's empty-tank / no-ignition capture, converted to our bit convention: `7336` → `7636` → `7736` → `7FF6`, which works out to **E-08** with the formula. Per those same notes, pressing POWER while faulted clears the fault and restarts, like the OEM display does. We haven't had a fault on our heater to confirm either yet.

## Talking alongside the OEM display ("piggyback")

You can leave the original display plugged in and still send commands:

1. Listen. The display sends a byte, the heater answers.
2. About **25 ms after the heater's answer to the display**, send **one** frame of your own (a command or a `0x50`).
3. The heater answers you. Don't send again until the display's next cycle.
4. Never send right after the heater answered *your* frame, and never more than one extra frame per display cycle. The display keeps its 1-second rhythm and doesn't notice.

The display shows the new level when you send UP/DOWN this way, and starts / stops normally with POWER. With no display plugged in, just send `0x59` once a second yourself (we alternate it with `0x50` to get chamber temperature).

The bus simulator in `tools/bus_sim` replays these timings and checks for collisions if you change the scheduling.

## Still unknown

- Flags bit 1.
- The full E-code table (only E-08 is backed by a capture, and that one is borrowed).
- Whether the red wire is battery voltage or a regulated supply on every model.
- What other display bytes do. The bridge prints `REQ xx -> yyyy` the first time it sees a new byte, so walking through the OEM menus with it attached is an easy way to find them.
