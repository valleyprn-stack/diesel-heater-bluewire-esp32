# Bus simulator

Runs the real Nano bridge code (`firmware/heater_bridge_nano`) on a PC against a simulated OEM display and heater, with microsecond timing, and reports collisions and unanswered frames. Handy before changing any of the bridge's scheduling.

```
g++ -std=c++17 -O1 -w -I. -o bus_sim bus_sim.cpp
./bus_sim A
```

| Scenario | What it does |
|----------|--------------|
| A | OEM display on, ON / LEVEL 5 / LEVEL 2 / UP / DOWN / OFF / ON |
| B | display sitting on its chamber page |
| C | no display (standalone) |
| D | chamber poll off |
| E | display unplugged mid-run |
| F | start, run out of fuel (E-08 fault), clear with ON |
| G | heater stops answering, queued command expires |
| H | full start and stop, every status line printed (phases, volts) |

The heater model is simplified (phase timing is shortened) - it checks the bus timing and the bridge's decoding, not the heater.
