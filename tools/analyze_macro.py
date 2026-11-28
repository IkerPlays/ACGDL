#!/usr/bin/env python3
"""Estadisticas de un macro (GDR en JSON, como los de xdBot/Echo): como pulsa una persona.

Uso: python3 tools/analyze_macro.py macro.gdr [otro.gdr ...]

Calcula la duracion de las pulsaciones y de las pausas, cuantas pulsaciones por segundo,
y los percentiles. Sirve para ajustar las entradas humanas del bot.
"""
import json
import statistics
import sys


def load(path):
    with open(path, 'rb') as fh:
        raw = fh.read()
    try:
        return json.loads(raw.decode('utf-8'))
    except Exception:
        raise SystemExit(f'{path}: no es JSON (si es binario, exportalo como JSON desde el bot)')


def pct(v, p):
    if not v:
        return 0
    v = sorted(v)
    return v[min(len(v) - 1, int(len(v) * p / 100))]


def main():
    for path in sys.argv[1:]:
        data = load(path)
        fps = data.get('framerate') or data.get('fps') or 240
        inputs = data.get('inputs') or []
        presses, gaps = [], []
        down_at = None
        up_at = None
        for ev in inputs:
            if ev.get('2p') or ev.get('player2'):
                continue
            if ev.get('button', 1) not in (1, 'jump'):
                continue
            frame = ev.get('frame', 0)
            is_down = ev.get('down', ev.get('pressed', True))
            if is_down:
                if up_at is not None:
                    gaps.append((frame - up_at) / fps * 1000)
                down_at = frame
            elif down_at is not None:
                presses.append((frame - down_at) / fps * 1000)
                up_at = frame
                down_at = None
        total = (inputs[-1].get('frame', 0) / fps) if inputs else 0
        print(f'===== {path}: {len(presses)} pulsaciones, {total:.0f} s, {fps} fps')
        if presses:
            print(f'  pulsacion (ms): mediana {statistics.median(presses):.0f} | p10 {pct(presses,10):.0f} | p90 {pct(presses,90):.0f}')
        if gaps:
            print(f'  pausa (ms):     mediana {statistics.median(gaps):.0f} | p10 {pct(gaps,10):.0f} | p90 {pct(gaps,90):.0f}')
        if total:
            print(f'  pulsaciones por segundo: {len(presses) / total:.2f}')


if __name__ == '__main__':
    main()
