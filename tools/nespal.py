"""nespal.py -- the NES's 64 colours (FCEUX's default palette) and their nearest Uzebox colour.

The Uzebox has 256 colours, BBGGGRRR: red and green 0-7, blue 0-3, each a linear step of the
DAC. nearest() picks by squared distance in a lightly perceptual RGB (green weighted most).
"""

NES_RGB = [
    0x747474, 0x24188C, 0x0000A8, 0x44009C, 0x8C0074, 0xA80010, 0xA40000, 0x7C0800,
    0x402C00, 0x004400, 0x005000, 0x003C14, 0x183C5C, 0x000000, 0x000000, 0x000000,
    0xBCBCBC, 0x0070EC, 0x2038EC, 0x8000F0, 0xBC00BC, 0xE40058, 0xD82800, 0xC84C0C,
    0x887000, 0x009400, 0x00A800, 0x009038, 0x008088, 0x000000, 0x000000, 0x000000,
    0xFCFCFC, 0x3CBCFC, 0x5C94FC, 0xCC88FC, 0xF478FC, 0xFC74B4, 0xFC7460, 0xFC9838,
    0xF0BC3C, 0x80D010, 0x4CDC48, 0x58F898, 0x00E8D8, 0x787878, 0x000000, 0x000000,
    0xFCFCFC, 0xA8E4FC, 0xC4D4FC, 0xD4C8FC, 0xFCC4FC, 0xFCC4D8, 0xFCBCB0, 0xFCD8A8,
    0xFCE4A0, 0xE0FCA0, 0xA8F0BC, 0xB0FCCC, 0x9CFCF0, 0xC4C4C4, 0x000000, 0x000000,
]


def uze_rgb(c):
    """A Uzebox colour byte as (r, g, b) 0-255."""
    r, g, b = c & 7, (c >> 3) & 7, (c >> 6) & 3
    return (r * 255 // 7, g * 255 // 7, b * 255 // 3)


def nearest(rgb):
    r0, g0, b0 = (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255
    best, bd = 0, None
    for c in range(256):
        r, g, b = uze_rgb(c)
        d = 3 * (r - r0) ** 2 + 4 * (g - g0) ** 2 + 2 * (b - b0) ** 2
        if bd is None or d < bd:
            best, bd = c, d
    return best


NES_TO_UZE = [nearest(c) for c in NES_RGB]

if __name__ == "__main__":
    for i in range(0, 64, 8):
        print(" ".join("%02x:%02X" % (i + k, NES_TO_UZE[i + k]) for k in range(8)))
