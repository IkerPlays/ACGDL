"""Lector de macros GDR (v1 MessagePack y v2 binario) para analisis. Solo lectura."""
import struct


class R:
    def __init__(self, b):
        self.b = b
        self.i = 0

    def u8(self):
        v = self.b[self.i]
        self.i += 1
        return v

    def varint(self):
        out = 0
        for k in range(10):
            byte = self.u8()
            out |= (byte & 0x7F) << (7 * k)
            if not byte & 0x80:
                return out
        raise ValueError('varint')

    def string(self):
        n = self.varint()
        s = self.b[self.i:self.i + n].decode('utf-8', 'replace')
        self.i += n
        return s

    def f32(self):
        v = struct.unpack('>f', self.b[self.i:self.i + 4])[0]
        self.i += 4
        return v

    def f64(self):
        v = struct.unpack('>d', self.b[self.i:self.i + 8])[0]
        self.i += 8
        return v

    def skip(self, n):
        self.i += n

    def empty(self):
        return self.i >= len(self.b)


def parse_gdr2(data):
    r = R(data)
    assert data[:3] == b'GDR'
    r.skip(3)
    version = r.varint()
    assert version == 2, version
    input_tag = r.string()
    author = r.string()
    r.string()
    duration = r.f32()
    r.varint()
    framerate = r.f64()
    r.varint()
    r.varint()
    r.u8()
    platformer = r.u8() != 0
    bot = r.string()
    r.varint()
    level_id = r.varint()
    level_name = r.string()
    ext_len = r.varint()
    ext = data[r.i:r.i + ext_len]
    frames = []
    if bot == 'xdBot' and ext:
        e = R(ext)
        n = e.varint()
        for _ in range(n):
            e.varint()
            p = (e.f32(), e.f32(), e.f32())
            e.skip(1)
            e.f32(); e.f32(); e.f32()
            e.skip(1)
            frames.append(p)
    r.skip(ext_len)
    deaths = []
    dc = r.varint()
    p = 0
    for _ in range(dc):
        p += r.varint()
        deaths.append(p)
    input_count = r.varint()
    p1 = r.varint()
    inputs = []
    frame = 0
    remaining = p1
    in_p2 = False
    while not r.empty():
        packed = r.varint()
        if platformer:
            delta, button, down = packed >> 3, (packed >> 1) & 3, packed & 1 == 1
        else:
            delta, button, down = packed >> 1, 1, packed & 1 == 1
        frame += delta
        if not in_p2 and remaining == 0:
            in_p2 = True
            frame = delta
        inputs.append({'frame': frame, 'button': button, 'down': down, 'p2': in_p2})
        if not in_p2:
            remaining -= 1
            if remaining == 0 and p1 > 0:
                frame = 0
        if input_tag:
            r.skip(r.varint())
    inputs.sort(key=lambda x: x['frame'])
    return {'author': author, 'bot': bot, 'level': level_name, 'level_id': level_id, 'duration': duration,
            'framerate': framerate, 'platformer': platformer, 'inputs': inputs, 'frames': frames, 'deaths': deaths}


def parse_gdr1(data):
    import msgpack
    d = msgpack.unpackb(data, raw=False, strict_map_key=False)
    inputs = []
    for i in d.get('inputs', []):
        inputs.append({'frame': i.get('frame', 0), 'button': i.get('btn', i.get('button', 1)),
                       'down': bool(i.get('down')), 'p2': bool(i.get('2p', False))})
    frames = []
    for f in d.get('frameFixes', []) or []:
        p1 = f.get('p1') or {}
        frames.append((f.get('frame'), p1.get('x'), p1.get('y'), p1.get('r')))
    bot = d.get('bot') or {}
    lvl = d.get('level') or {}
    return {'author': d.get('author'), 'bot': bot.get('name') if isinstance(bot, dict) else str(bot),
            'level': lvl.get('name') if isinstance(lvl, dict) else None, 'duration': d.get('duration'),
            'framerate': d.get('framerate', 240.0), 'inputs': sorted(inputs, key=lambda x: x['frame']),
            'frames1': frames}


def load(path):
    data = open(path, 'rb').read()
    if data[:3] == b'GDR':
        return parse_gdr2(data)
    return parse_gdr1(data)
