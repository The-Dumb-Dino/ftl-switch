#!/usr/bin/env python3
"""The game's app icon, from your own .ipa, as the 256x256 JPEG an .nro carries.

    tools/ipa_icon.py FTL.ipa icon.jpg [size]

Takes the largest AppIcon image in the app's asset catalog (Assets.car: the
1024x1024 App Store icon for FTL), which CoreUI stores as premultiplied BGRA
compressed with LZVN, decoded here in Python. Needs Pillow for scaling and the
JPEG. This made switch/icon.jpg (Subset Games allows using FTL's art).
"""
import struct
import sys
import zipfile


def lzvn_decode(src, size):
    """Apple's LZVN (as in lzfse's lzvn_decode_base.c), to exactly size bytes."""
    out = bytearray()
    i, d = 0, 0
    n = len(src)

    def lit(count):
        nonlocal i
        out.extend(src[i:i + count])
        i += count

    def match(length, dist):
        if dist <= 0 or dist > len(out):
            raise ValueError("lzvn: bad distance")
        start = len(out) - dist
        for k in range(length):
            out.append(out[start + k])

    while i < n and len(out) < size:
        op = src[i]
        if op >= 0xF0:  # match with the previous distance
            if op == 0xF0:
                m, i = src[i + 1] + 16, i + 2
            else:
                m, i = op & 0xF, i + 1
            match(m, d)
        elif op >= 0xE0:  # literals
            if op == 0xE0:
                l, i = src[i + 1] + 16, i + 2
            else:
                l, i = op & 0xF, i + 1
            lit(l)
        elif op >= 0xD0 or 0x70 <= op <= 0x7F:
            raise ValueError("lzvn: undefined opcode %#x" % op)
        elif 0xA0 <= op <= 0xBF:  # medium distance
            b1, b2 = src[i + 1], src[i + 2]
            l = (op >> 3) & 3
            m = (((op & 7) << 2) | (b1 & 3)) + 3
            i += 3
            lit(l)
            d = (b2 << 6) | (b1 >> 2)
            match(m, d)
        elif op & 7 == 7:  # large distance
            l, m = (op >> 6) & 3, ((op >> 3) & 7) + 3
            dist = src[i + 1] | (src[i + 2] << 8)
            i += 3
            lit(l)
            d = dist
            match(m, d)
        elif op & 7 == 6:
            if op < 0x40:  # end of stream, nop, undefined
                if op == 0x06:
                    break
                if op in (0x0E, 0x16):
                    i += 1
                    continue
                raise ValueError("lzvn: undefined opcode %#x" % op)
            l, m = (op >> 6) & 3, ((op >> 3) & 7) + 3  # previous distance
            i += 1
            lit(l)
            match(m, d)
        else:  # small distance
            l, m = (op >> 6) & 3, ((op >> 3) & 7) + 3
            dist = ((op & 7) << 8) | src[i + 1]
            i += 2
            lit(l)
            d = dist
            match(m, d)
    if len(out) != size:
        raise ValueError("lzvn: got %d bytes, expected %d" % (len(out), size))
    return bytes(out)


def renditions(car):
    """(name, width, height, offset of the pixel data) for each bitmap rendition."""
    i = car.find(b"ISTC")
    while i >= 0:
        w, h = struct.unpack_from("<II", car, i + 12)
        fmt = car[i + 24:i + 28]
        name = car[i + 40:i + 168].split(b"\0")[0].decode("utf-8", "replace")
        tlv = struct.unpack_from("<I", car, i + 168)[0]
        data = i + 184 + tlv
        if fmt == b"BGRA" and car[data:data + 4] == b"MLEC":
            yield name, w, h, data
        i = car.find(b"ISTC", i + 4)


def pixels(car, w, h, at):
    """Premultiplied BGRA, top row first."""
    _, compression, length = struct.unpack_from("<III", car, at + 4)
    if compression != 3:
        raise ValueError("compression %d isn't handled (only LZVN)" % compression)
    p = at + 16
    if car[p:p + 4] != b"KCBC":  # one stream
        return lzvn_decode(car[p:p + length], w * h * 4)
    out = bytearray()
    while car[p:p + 4] == b"KCBC":  # chunks of rows
        rows, clen = struct.unpack_from("<II", car, p + 12)
        out += lzvn_decode(car[p + 20:p + 20 + clen], rows * w * 4)
        p += 20 + clen
    if len(out) != w * h * 4:
        raise ValueError("rendition is %d bytes, expected %d" % (len(out), w * h * 4))
    return bytes(out)


def main():
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__.strip())
    ipa, dest = sys.argv[1], sys.argv[2]
    size = int(sys.argv[3]) if len(sys.argv) == 4 else 256
    with zipfile.ZipFile(ipa) as z:
        cars = [n for n in z.namelist()
                if n.startswith("Payload/") and n.endswith(".app/Assets.car") and "__MACOSX" not in n]
        if not cars:
            sys.exit("no Assets.car in " + ipa)
        car = z.read(cars[0])
    icons = [r for r in renditions(car) if r[1] == r[2]]
    if not icons:
        sys.exit("no square icon in the asset catalog")
    name, w, h, at = max(icons, key=lambda r: r[1])
    bgra = pixels(car, w, h, at)

    from PIL import Image
    img = Image.frombytes("RGBA", (w, h), bgra, "raw", "BGRa")  # BGRa: premultiplied
    img = img.resize((size, size), Image.LANCZOS)
    bg = Image.new("RGB", img.size, (0, 0, 0))
    bg.paste(img, mask=img.getchannel("A"))
    bg.save(dest, "JPEG", quality=92, optimize=True)  # baseline: hbmenu reads no progressive
    print("%s: %s (%dx%d) -> %dx%d" % (dest, name, w, h, size, size))


if __name__ == "__main__":
    main()
