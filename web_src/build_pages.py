#!/usr/bin/env python3
"""Turn web_src/*.html + core.js into the PROGMEM .h tabs of the ESP32 sketch.
Usage: python3 build_pages.py <sketch_dir>"""
import os, sys

SRC = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(SRC, '..', 'firmware', 'heater_web_esp32')
PAGES = [('core.js', 'web_core.h', 'CORE_JS'), ('burner.html', 'web_burner.h', 'PAGE_BURNER'),
         ('lcd.html', 'web_lcd.h', 'PAGE_LCD'), ('dash.html', 'web_dash.h', 'PAGE_DASH')]
DELIM = 'PG'

def squeeze(text):
    # strip indentation and blank lines; keep everything else as written
    return '\n'.join(l.strip() for l in text.splitlines() if l.strip()) + '\n'

total = 0
for src, dst, name in PAGES:
    body = squeeze(open(os.path.join(SRC, src), encoding='utf-8').read())
    assert ')' + DELIM + '"' not in body, src
    total += len(body.encode('utf-8'))
    with open(os.path.join(OUT, dst), 'w', encoding='utf-8', newline='\n') as f:
        f.write('// Generated from web_src/%s by build_pages.py - edit the source, not this file.\n' % src)
        f.write('#pragma once\n#include <pgmspace.h>\n')
        f.write('const char %s[] PROGMEM = R"%s(%s)%s";\n' % (name, DELIM, body, DELIM))
    print('%-12s -> %-13s %6d bytes' % (src, dst, len(body.encode('utf-8'))))
print('total %d bytes' % total)
