#!/usr/bin/env python3
"""Estadisticas de como pulsa una persona en un macro GDR (v1 MessagePack o v2 binario).

Uso:
    python3 tools/analyze_macro.py macro.gdr2 [otro.gdr ...]
    python3 tools/analyze_macro.py macro.gdr2 --gmd nivel.gmd     # por modo de juego (necesita posiciones, p.ej. xdBot)

Requiere `pip install msgpack` para los .gdr (v1).
"""
import base64
import bisect
import collections
import os
import re
import statistics
import sys
import zlib

sys.path.insert(0, os.path.dirname(__file__))
import gdrlib  # noqa: E402


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p / 100))] if v else 0


def main_stream(m):
    p1 = [e for e in m['inputs'] if not e['p2']]
    p2 = [e for e in m['inputs'] if e['p2']]
    return p1 if len(p1) >= len(p2) else p2


def level_modes(gmd):
    t = open(gmd, 'rb').read().decode('latin1')
    s = re.search(r'<k>k4</k><s>([^<]*)</s>', t).group(1)
    d = zlib.decompress(base64.urlsafe_b64decode(s + '=' * (-len(s) % 4)), 15 + 32).decode('latin1')
    parts = d.split(';')
    hdr = parts[0].split(',')
    h = {hdr[i]: hdr[i + 1] for i in range(0, len(hdr) - 1, 2) if hdr[i].startswith('k')}
    start = {'0': 'cubo', '1': 'nave', '2': 'bola', '3': 'ufo', '4': 'wave', '5': 'robot', '6': 'spider', '7': 'swing'}.get(h.get('kA2', '0'), 'cubo')
    ids = {'12': 'cubo', '13': 'nave', '47': 'bola', '111': 'ufo', '660': 'wave', '745': 'robot', '1331': 'spider', '1933': 'swing'}
    ev = []
    for o in parts[1:]:
        if not o:
            continue
        kv = o.split(',')
        dd = {kv[i]: kv[i + 1] for i in range(0, len(kv) - 1, 2)}
        if dd.get('1') in ids:
            ev.append((float(dd['2']), ids[dd['1']]))
    ev.sort()
    return start, ev


def runs(stream, key=None):
    pr, gp, down, up = [], [], None, None
    for e in stream:
        k = key(e) if key else None
        if e['down']:
            if up is not None:
                gp.append(((e['frame'] - up) / 240 * 1000, k))
            down = e['frame']
        elif down is not None:
            pr.append(((e['frame'] - down) / 240 * 1000, k))
            up = e['frame']
            down = None
    return pr, gp


def show(label, pr, gp):
    if len(pr) < 5:
        return
    print(f"  {label:8} n={len(pr):4} | pulsacion ms: mediana={statistics.median(pr):4.0f} p10={pct(pr, 10):3.0f} p25={pct(pr, 25):3.0f} p75={pct(pr, 75):4.0f}"
          f" | pausa ms: mediana={statistics.median(gp) if gp else 0:4.0f} p10={pct(gp, 10):3.0f} p25={pct(gp, 25):3.0f}")


def main():
    args = sys.argv[1:]
    gmd = None
    if '--gmd' in args:
        i = args.index('--gmd')
        gmd = args[i + 1]
        args = args[:i] + args[i + 2:]
    for path in args:
        m = gdrlib.load(path)
        stream = main_stream(m)
        print(f"===== {os.path.basename(path)} | {m['bot']} | {m['level']} | {len(stream)} entradas")
        pr, gp = runs(stream)
        show('total', [x for x, _ in pr], [x for x, _ in gp])
        if gmd:
            frames = m.get('frames') or []
            fmap = {i + 1: (f[0], f[1]) for i, f in enumerate(frames)} if frames else \
                {fr: (x, y) for (fr, x, y, r) in m.get('frames1', []) if x is not None}
            if not fmap:
                print('  (el macro no trae posiciones: no se puede separar por modo)')
                continue
            start, ev = level_modes(gmd)
            xs = [e[0] for e in ev]

            def mode_of(e):
                pos = fmap.get(e['frame']) or fmap.get(e['frame'] + 1)
                if not pos:
                    return None
                i = bisect.bisect_right(xs, pos[0]) - 1
                return ev[i][1] if i >= 0 else start

            prm, gpm = runs(stream, mode_of)
            by = collections.defaultdict(lambda: ([], []))
            for x, k in prm:
                if k:
                    by[k][0].append(x)
            for x, k in gpm:
                if k:
                    by[k][1].append(x)
            for mode, (a, b) in sorted(by.items(), key=lambda kv: -len(kv[1][0])):
                show(mode, a, b)


if __name__ == '__main__':
    main()
