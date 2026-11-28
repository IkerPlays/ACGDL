#!/usr/bin/env python3
"""Analiza un .gmd (GDShare): modos de juego, orbes, pads, velocidades...

Uso: python3 tools/analyze_gmd.py nivel.gmd [otro.gmd ...]
Sirve para ver que mecanicas usa un nivel antes de probar el bot.
"""
import base64
import collections
import re
import sys
import zlib

NAMES = {
    '12': 'cubo', '13': 'nave', '47': 'bola', '111': 'ufo', '660': 'wave',
    '745': 'robot', '1331': 'spider', '1933': 'swing',
    '10': 'gravedad normal', '11': 'gravedad invertida', '99': 'mini', '101': 'tamano normal',
    '286': 'dual', '287': 'solo', '45': 'mirror on', '46': 'mirror off',
    '200': 'vel 0.7x', '201': 'vel 1x', '202': 'vel 2x', '203': 'vel 3x', '1334': 'vel 4x',
    '36': 'orbe amarillo', '84': 'orbe azul', '141': 'orbe rosa', '1022': 'orbe verde',
    '1333': 'orbe rojo', '1330': 'orbe negro', '1704': 'orbe dash verde', '1751': 'orbe dash rosa',
    '35': 'pad amarillo', '67': 'pad azul', '140': 'pad rosa', '1332': 'pad rojo',
}
MODES = {'12', '13', '47', '111', '660', '745', '1331', '1933'}


def load(path):
    text = open(path, 'rb').read().decode('latin1')
    m = re.search(r'<k>k4</k><s>([^<]*)</s>', text)
    if not m:
        raise SystemExit(f'{path}: no tiene datos de nivel (k4). Exportalo de nuevo desde GDShare.')
    s = m.group(1)
    data = zlib.decompress(base64.urlsafe_b64decode(s + '=' * (-len(s) % 4)), 15 + 32).decode('latin1')
    objs = []
    for raw in data.split(';')[1:]:
        if not raw:
            continue
        kv = raw.split(',')
        objs.append({kv[i]: kv[i + 1] for i in range(0, len(kv) - 1, 2)})
    return objs


def main():
    for path in sys.argv[1:]:
        objs = load(path)
        ids = collections.Counter(o.get('1') for o in objs)
        xs = [float(o['2']) for o in objs if '2' in o]
        print(f'===== {path}: {len(objs)} objetos, longitud {int(max(xs))} unidades')
        for k, n in sorted(ids.items(), key=lambda kv: -kv[1]):
            if k in NAMES:
                print(f'  {NAMES[k]:<22} {n}')
        modes = sorted((float(o['2']), NAMES[o['1']]) for o in objs if o.get('1') in MODES)
        seq = []
        for x, m in modes:
            if not seq or seq[-1][1] != m:
                seq.append((int(x), m))
        print('  modos:', ' > '.join(f'{m}@{x}' for x, m in seq))


if __name__ == '__main__':
    main()
