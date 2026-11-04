#!/usr/bin/env python3
"""Gera os bitmaps e o icone (installer.ico) do instalador (NSIS MUI2) a partir de assets/logo/kai.png.

Só biblioteca padrão (sem PIL). Saída: welcome.bmp (164x314) e header.bmp
(150x57), 24 bits, ao lado deste script. Uso: python3 gen-installer-art.py
"""
import os, struct, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
LOGO = os.path.join(HERE, "..", "..", "assets", "logo", "kai.png")

ACCENT = (0x6E, 0x7B, 0xFF)   # accent_color do tema kai-dark
DARK = (0x0D, 0x0E, 0x12)     # bg do tema kai-dark
WHITE = (0xFF, 0xFF, 0xFF)
TINT = (0xEC, 0xEE, 0xFF)


def read_png(path):
    data = open(path, "rb").read()
    pos, idat, w, h, ctype = 8, b"", 0, 0, 0
    while pos < len(data):
        n, typ = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if typ == b"IHDR":
            w, h, depth, ctype, _, _, inter = struct.unpack(">IIBBBBB", body)
            assert depth == 8 and inter == 0 and ctype in (2, 6), "PNG nao suportado"
        elif typ == b"IDAT":
            idat += body
        pos += 12 + n
    bpp = 4 if ctype == 6 else 3
    raw, stride = zlib.decompress(idat), w * bpp
    rows, prev, i = [], bytearray(stride), 0
    for _ in range(h):
        f, line = raw[i], bytearray(raw[i + 1:i + 1 + stride])
        i += 1 + stride
        for x in range(stride):
            a = line[x - bpp] if x >= bpp else 0
            b = prev[x]
            c = prev[x - bpp] if x >= bpp else 0
            if f == 1: line[x] = (line[x] + a) & 255
            elif f == 2: line[x] = (line[x] + b) & 255
            elif f == 3: line[x] = (line[x] + ((a + b) >> 1)) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pr = a if pa <= pb and pa <= pc else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        rows.append(line)
        prev = line
    px = [[(r[x * bpp], r[x * bpp + 1], r[x * bpp + 2], r[x * bpp + 3] if bpp == 4 else 255)
           for x in range(w)] for r in rows]
    return w, h, px


def scale(px, w, h, nw, nh):
    """Reducao por media de caixa (qualidade boa p/ reduzir)."""
    out = []
    for y in range(nh):
        y0, y1 = y * h // nh, max(y * h // nh + 1, (y + 1) * h // nh)
        row = []
        for x in range(nw):
            x0, x1 = x * w // nw, max(x * w // nw + 1, (x + 1) * w // nw)
            acc, n = [0, 0, 0, 0], 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    r, g, b, a = px[yy][xx]
                    acc[0] += r * a; acc[1] += g * a; acc[2] += b * a; acc[3] += a
                    n += 1
            a = acc[3]
            row.append((acc[0] // a, acc[1] // a, acc[2] // a, a // n) if a else (0, 0, 0, 0))
        out.append(row)
    return out


def canvas(w, h, fn):
    return [[fn(x, y) for x in range(w)] for y in range(h)]


def blend(img, logo, ox, oy):
    for y, row in enumerate(logo):
        for x, (r, g, b, a) in enumerate(row):
            yy, xx = oy + y, ox + x
            if 0 <= yy < len(img) and 0 <= xx < len(img[0]) and a:
                br, bg, bb = img[yy][xx]
                k = a / 255
                img[yy][xx] = (round(r * k + br * (1 - k)), round(g * k + bg * (1 - k)), round(b * k + bb * (1 - k)))


def mix(c1, c2, t):
    return tuple(round(a + (b - a) * t) for a, b in zip(c1, c2))


def write_bmp(path, img):
    h, w = len(img), len(img[0])
    pad = (4 - w * 3 % 4) % 4
    body = b"".join(
        b"".join(bytes((b, g, r)) for r, g, b in row) + b"\0" * pad for row in reversed(img))
    hdr = b"BM" + struct.pack("<IHHI", 54 + len(body), 0, 0, 54)
    hdr += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 24, 0, len(body), 2835, 2835, 0, 0)
    open(path, "wb").write(hdr + body)


def palettize(small, ncolors):
    """Paleta por popularidade (cores a 4 bits/canal); indice 0 = transparente."""
    from collections import Counter
    cnt = Counter()
    for row in small:
        for r, g, b, a in row:
            if a >= 128:
                cnt[(r >> 4, g >> 4, b >> 4)] += 1
    pal = [(0, 0, 0)] + [((r << 4) | r, (g << 4) | g, (b << 4) | b)
                         for (r, g, b), _ in cnt.most_common(ncolors - 1)]
    pal += [(0, 0, 0)] * (ncolors - len(pal))

    def nearest(c):
        return min(range(1, len(pal)), key=lambda i: sum((x - y) ** 2 for x, y in zip(c, pal[i])))
    idx = [[0 if a < 128 else nearest((r, g, b)) for r, g, b, a in row] for row in small]
    return pal, idx


def ico_image(small, n, bpp):
    """Imagem de icone como DIB (32 bits BGRA, ou paleta 4/8 bits) + mascara AND."""
    maskrow = ((n + 31) // 32) * 4
    mask = b""
    for row in reversed(small):
        bits = bytearray(maskrow)
        for x, px in enumerate(row):
            if px[3] < 128:
                bits[x // 8] |= 0x80 >> (x % 8)
        mask += bytes(bits)
    if bpp == 32:
        xor = b"".join(b"".join(bytes((b, g, r, a)) for r, g, b, a in row) for row in reversed(small))
        pal = b""
        used = 0
    else:
        colors, idx = palettize(small, 1 << bpp)
        pal = b"".join(bytes((b, g, r, 0)) for r, g, b in colors)
        used = 1 << bpp
        xor = b""
        for row in reversed(idx):
            line = bytearray(((n * bpp + 31) // 32) * 4)
            for x, v in enumerate(row):
                if bpp == 8:
                    line[x] = v
                else:
                    line[x // 2] |= v << (4 if x % 2 == 0 else 0)
            xor += bytes(line)
    hdr = struct.pack("<IiiHHIIiiII", 40, n, n * 2, 1, bpp, 0, len(xor) + len(mask), 0, 0, used, 0)
    return hdr + pal + xor + mask


def write_ico(path, logo, lw, lh):
    """ICO no formato que o makensis aceita: mesmas (tamanho, bpp) do icone padrao dele."""
    side = max(lw, lh)
    sq = [[(0, 0, 0, 0)] * side for _ in range(side)]
    for y in range(lh):
        for x in range(lw):
            sq[(side - lh) // 2 + y][(side - lw) // 2 + x] = logo[y][x]
    layout = [(16, 4), (16, 8), (32, 4), (32, 8), (48, 8), (16, 32), (32, 32)]
    cache, entries = {}, []
    for n, bpp in layout:
        small = cache.setdefault(n, scale(sq, side, side, n, n))
        entries.append((n, bpp, ico_image(small, n, bpp)))
    out = struct.pack("<HHH", 0, 1, len(entries))
    off = 6 + 16 * len(entries)
    for n, bpp, d in entries:
        out += struct.pack("<BBBBHHII", n, n, 1 << bpp if bpp < 8 else 0, 0, 1, bpp, len(d), off)
        off += len(d)
    open(path, "wb").write(out + b"".join(d for _, _, d in entries))


def main():
    lw, lh, logo = read_png(LOGO)
    write_ico(os.path.join(HERE, "installer.ico"), logo, lw, lh)

    # Painel lateral das paginas de boas-vindas/conclusao.
    W, H = 164, 314
    img = canvas(W, H, lambda x, y: mix(WHITE, TINT, y / (H - 58)) if y < H - 58
                 else (ACCENT if y < H - 54 else DARK))
    tw = 112
    th = tw * lh // lw
    blend(img, scale(logo, lw, lh, tw, th), (W - tw) // 2, 70)
    write_bmp(os.path.join(HERE, "welcome.bmp"), img)

    # Faixa do cabecalho das demais paginas.
    W, H = 150, 57
    img = canvas(W, H, lambda x, y: ACCENT if y >= H - 2 else WHITE)
    th = 44
    tw = th * lw // lh
    blend(img, scale(logo, lw, lh, tw, th), W - tw - 14, 5)
    write_bmp(os.path.join(HERE, "header.bmp"), img)


main()
