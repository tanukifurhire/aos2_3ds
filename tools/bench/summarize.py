"""Turn a benchmark log (sdmc:/aos2_prof.txt of a bench run) into markdown tables.
usage: python tools/bench/summarize.py <log> [--windows]
Per scenario: section avg/max us, cpu percentiles, frames over budget, render counters, pools, memory."""
import re
import sys

NAMES = {3: 'Sora', 4: 'Alte', 5: 'Tsih', 6: 'Mira', 7: 'Sham', 8: 'Nath', 9: 'Star Breaker', 10: 'Suguri', 11: 'Saki',
         12: 'Iru', 13: 'Nanako', 14: 'Kae', 15: 'Kyoko', 16: 'Hime', 17: 'Sumika', 18: 'Sumika 2nd'}
FLAGS = {0: '-', 1: 'no stage', 2: 'no HUD', 4: 'no submit', 8: 'half-size sprites', 16: 'no draw list', 32: 'one blend'}


def kv(line):
    d = {}
    for m in re.finditer(r'(\S+) (\d+(?:\.\d+)?)/(\d+(?:\.\d+)?)', line):
        d[m.group(1)] = (float(m.group(2)), float(m.group(3)))
    for m in re.finditer(r'(\S+) (-?\d+)(?= |$)', line):
        d.setdefault(m.group(1), float(m.group(2)))
    return d


def parse(path):
    sc = []
    cur = None
    for line in open(path, encoding='utf-8', errors='replace'):
        line = line.rstrip()
        m = re.match(r'bench scenario (\d+): (\d+) vs (\d+) stage (\d+) frames (\d+) hyper (\d+) difficulty (\d+)(?: flags (\d+))?', line)
        if m:
            cur = dict(p1=int(m[2]), p2=int(m[3]), stage=int(m[4]), frames=int(m[5]), flags=int(m[8] or 0))
            sc.append(cur)
        elif line.startswith('match ') and cur is not None:
            m = re.search(r'linear \d+ KB \(free (\d+) KB\), vram free (\d+) KB.*sprite textures (\d+) KB linear \+ (\d+) KB VRAM', line)
            if m:
                cur['lin_free'], cur['vram_free'], cur['spr_lin'], cur['spr_vram'] = map(int, m.groups())
        elif line.startswith('summary frames') and cur is not None and 's' not in cur:
            cur['s'] = kv(line.split(':', 1)[1])
        elif line.startswith('summary2') and cur is not None and 's2' not in cur:
            cur['s2'] = kv(line)
    return [s for s in sc if 's' in s]


def main():
    sc = parse(sys.argv[1])
    print('| scenario | stage | removed | tick | drawlist | stageDraw | 2d submit | hud | panel | frameEnd | cpu avg | cpu P95 / P99 / max | frames > 16.7 ms cpu | wall frame avg / max | bgm thread |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for s in sc:
        a, b = s['s'], s['s2']
        n = s['frames']
        f = lambda k: f"{a[k][0]:.0f} / {a[k][1]:.0f}"
        print(f"| {NAMES.get(s['p1'])} vs {NAMES.get(s['p2'])} | {s['stage']} | {FLAGS.get(s['flags'], s['flags'])} | {f('tick')} | {f('drawlist')} | "
              f"{f('stageDraw')} | {f('2d')} | {f('hud')} | {f('panel')} | {f('frameEnd')} | **{a['cpu'][0]:.0f}** | "
              f"{b.get('cpuP95', 0):.0f} / {b.get('cpuP99', 0):.0f} / {a['cpu'][1]:.0f} | {b.get('cpuOver16.7', 0):.0f} ({100 * b.get('cpuOver16.7', 0) / n:.1f}%) | "
              f"{a['frame'][0]:.0f} / {a['frame'][1]:.0f} | {f('bgm')} |")
    print()
    print('| scenario | stage | removed | sprite quads avg/max | blend flushes | tex switches | sprite px avg/max (additive) | stage draws / verts | cmdbuf % | dl max | effect / enemy / missile / beam / laser / particle max | linear free (sprites lin) | VRAM free |')
    print('|---|---|---|---|---|---|---|---|---|---|---|---|---|')
    for s in sc:
        b = s['s2']
        g = lambda k: f"{b[k][0]:.0f} / {b[k][1]:.0f}"
        print(f"| {NAMES.get(s['p1'])} vs {NAMES.get(s['p2'])} | {s['stage']} | {FLAGS.get(s['flags'], s['flags'])} | {g('quads')} | {g('blendsw')} | {g('texsw')} | "
              f"{b['px'][0] / 1000:.0f}k / {b['px'][1] / 1000:.0f}k ({b['pxAdd'][0] / 1000:.0f}k / {b['pxAdd'][1] / 1000:.0f}k) | "
              f"{b['stgDraws'][0]:.0f} / {b['stgVerts'][0]:.0f} | {g('cmdbuf%')} | {b.get('dcmdmax', 0):.0f} | "
              f"{b.get('effectmax', 0):.0f} / {b.get('enemymax', 0):.0f} / {b.get('missilemax', 0):.0f} / {b.get('beammax', 0):.0f} / {b.get('lasermax', 0):.0f} / {b.get('particlemax', 0):.0f} | "
              f"{s.get('lin_free', 0)} KB ({s.get('spr_lin', 0)} KB) | {b.get('vramfree', 0):.0f} KB |")


main()
