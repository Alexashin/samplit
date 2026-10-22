"""Samplit logo: split-pad mark + Geologica wordmark, all as outlined paths."""
import io, json, math, os
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.pens.boundsPen import BoundsPen
from fontTools.pens.recordingPen import DecomposingRecordingPen, RecordingPen
import uharfbuzz as hb

T = json.load(open("tokens.json"))
INK, ACC = T["ramps"]["neutral"]["950"], T["ramps"]["accent"]["600"]
ACC_D = T["ramps"]["accent"]["400"]
PAPER_D = T["ramps"]["neutral"]["100"]
ACC_800 = T["ramps"]["accent"]["800"]
S_REV = 15  # reversed versions: light shapes on dark bleed into the gap, so it is cut wider
OUT = "logo"
os.makedirs(OUT, exist_ok=True)

# ---------- mark ----------
X0, X1, R = 28, 228, 48  # tile 200x200, corner radius


def bez(p, t):
    u = 1 - t
    return tuple(u**3 * p[0][i] + 3 * u * u * t * p[1][i] + 3 * u * t * t * p[2][i] + t**3 * p[3][i] for i in (0, 1))


def split(p, t):
    def l(a, b):
        return tuple(a[i] + (b[i] - a[i]) * t for i in (0, 1))
    a, b, c, d = p
    ab, bc, cd = l(a, b), l(b, c), l(c, d)
    abc, bcd = l(ab, bc), l(bc, cd)
    m = l(abc, bcd)
    return (a, ab, abc, m), (m, bcd, cd, d)


def solve(f, lo, hi):
    for _ in range(80):
        m = (lo + hi) / 2
        if f(lo) * f(m) <= 0:
            hi = m
        else:
            lo = m
    return (lo + hi) / 2


def f2(v):
    return f"{v:.2f}".rstrip("0").rstrip(".")


def P(pt):
    return f"{f2(pt[0])} {f2(pt[1])}"


def mark_paths(s=10, a=76, b=180):
    """(top_d, bottom_d). s = half-gap. The cut is the original sigmoid through the tile."""
    def cut(d):
        return ((0, a + d), (150, a + d), (106, b + d), (256, b + d))
    ct, cb = cut(-s), cut(s)

    def tx(c, x):
        return solve(lambda t: bez(c, t)[0] - x, 0, 1)

    cx, cy = X0 + R, X0 + R
    t_a = solve(lambda t: math.hypot(bez(ct, t)[0] - cx, bez(ct, t)[1] - cy) - R, tx(ct, X0), tx(ct, cx))
    t_r = tx(ct, X1)
    seg_t = split(split(ct, t_r)[0], t_a / t_r)[1]
    q1 = seg_t[0]
    yr = seg_t[3][1]
    rev = (seg_t[3], seg_t[2], seg_t[1], seg_t[0])
    top = (f"M{P(q1)}A{R} {R} 0 0 1 {P((X0 + R, X0))}H{f2(X1 - R)}A{R} {R} 0 0 1 {P((X1, X0 + R))}"
           f"V{f2(yr)}C{P(rev[1])} {P(rev[2])} {P(rev[3])}Z")

    bx, by = X1 - R, X1 - R
    t_l = tx(cb, X0)
    t_b = solve(lambda t: math.hypot(bez(cb, t)[0] - bx, bez(cb, t)[1] - by) - R, tx(cb, bx), tx(cb, X1))
    seg_b = split(split(cb, t_b)[0], t_l / t_b)[1]
    q2 = seg_b[3]
    bot = (f"M{P(seg_b[0])}C{P(seg_b[1])} {P(seg_b[2])} {P(q2)}A{R} {R} 0 0 1 {P((X1 - R, X1))}"
           f"H{f2(X0 + R)}A{R} {R} 0 0 1 {P((X0, X1 - R))}Z")
    return top, bot


def svg(w, h, body, title, vb=None):
    vb = vb or f"0 0 {f2(w)} {f2(h)}"
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{vb}" width="{f2(w)}" height="{f2(h)}" '
            f'role="img" aria-label="{title}">\n<title>{title}</title>\n{body}\n</svg>\n')


def mark_svg(top_fill, bot_fill, s=10, title="Samplit", bg=None, rx=0):
    t, b = mark_paths(s)
    body = ""
    if bg:
        body += f'<rect width="256" height="256" rx="{rx}" fill="{bg}"/>\n'
    body += f'<path fill="{top_fill}" d="{t}"/>\n<path fill="{bot_fill}" d="{b}"/>'
    return svg(256, 256, body, title)


def write(name, txt):
    with open(f"{OUT}/{name}", "w", encoding="utf-8") as fh:
        fh.write(txt)


# ---------- wordmark ----------
tt = TTFont("type/fonts/geologica-latin-800.woff")
tt.flavor = None
buf = io.BytesIO()
tt.save(buf)
data = buf.getvalue()
upem = tt["head"].unitsPerEm
face = hb.Face(data)
font = hb.Font(face)
b_ = hb.Buffer()
b_.add_str("samplit")
b_.guess_segment_properties()
hb.shape(font, b_, {"kern": True})
gs = tt.getGlyphSet()
order = tt.getGlyphOrder()
TRACK = -0.015 * upem


def wordmark_path(k, ox, oy):
    """Outline path of the wordmark; k = scale, (ox, oy) = left end of the baseline. The i-dot becomes a pad."""
    pen = SVGPathPen(None, ntos=f2)
    x = 0
    dot = None
    for info, pos in zip(b_.glyph_infos, b_.glyph_positions):
        name = order[info.codepoint]
        tp = TransformPen(pen, (k, 0, 0, -k, ox + (x + pos.x_offset) * k, oy))
        if name == "i":
            rec = DecomposingRecordingPen(gs)
            gs[name].draw(rec)
            conts, cur = [], []
            for op, args in rec.value:
                cur.append((op, args))
                if op == "closePath":
                    conts.append(cur)
                    cur = []
            for c in conts:
                bp = BoundsPen(gs)
                rp = RecordingPen()
                rp.value = c
                rp.replay(bp)
                if bp.bounds[1] > 1000:
                    dot = (bp.bounds, x + pos.x_offset)
                else:
                    rp.replay(tp)
        else:
            gs[name].draw(tp)
        x += pos.x_advance + TRACK
    if dot:
        (dx0, dy0, dx1, dy1), gx = dot
        r = 0.24 * (dx1 - dx0) * k
        xa, xb = ox + (gx + dx0) * k, ox + (gx + dx1) * k
        ya, yb = oy - dy1 * k, oy - dy0 * k
        s = f2
        pen._commands.append(
            f"M{s(xa + r)} {s(ya)}H{s(xb - r)}Q{s(xb)} {s(ya)} {s(xb)} {s(ya + r)}V{s(yb - r)}"
            f"Q{s(xb)} {s(yb)} {s(xb - r)} {s(yb)}H{s(xa + r)}Q{s(xa)} {s(yb)} {s(xa)} {s(yb - r)}"
            f"V{s(ya + r)}Q{s(xa)} {s(ya)} {s(xa + r)} {s(ya)}Z")
    return pen.getCommands()


def word_bounds(k):
    bp = BoundsPen(gs)
    x = 0
    for info, pos in zip(b_.glyph_infos, b_.glyph_positions):
        name = order[info.codepoint]
        gs[name].draw(TransformPen(bp, (k, 0, 0, -k, (x + pos.x_offset) * k, 0)))
        x += pos.x_advance + TRACK
    return bp.bounds  # (xmin, ymin = top (negative), xmax, ymax = descender)


bp0 = BoundsPen(gs)
gs["l"].draw(bp0)
L_TOP = bp0.bounds[3]
TILE = 200
K = TILE / L_TOP
WB = word_bounds(K)


def boxed(x0, y0, x1, y1, body, title):
    """viewBox snapped outward to whole units so nothing clips."""
    x0, y0, x1, y1 = math.floor(x0), math.floor(y0), math.ceil(x1), math.ceil(y1)
    return svg(x1 - x0, y1 - y0, body, title, vb=f"{x0} {y0} {x1 - x0} {y1 - y0}")


def lockup(top_fill, bot_fill, word_fill, s=10, title="Samplit", stacked=False):
    t, b = mark_paths(s)
    if not stacked:
        gap = TILE * 0.20
        base_y = X1
        wx = X0 + TILE + gap - WB[0]
        wd = wordmark_path(K, wx, base_y)
        body = (f'<path fill="{top_fill}" d="{t}"/>\n<path fill="{bot_fill}" d="{b}"/>\n'
                f'<path fill="{word_fill}" d="{wd}"/>')
        return boxed(X0, min(X0, base_y + WB[1]), wx + WB[2], base_y + WB[3], body, title)
    k2 = K * 0.78
    wb2 = word_bounds(k2)
    ww = wb2[2] - wb2[0]
    W = max(TILE, ww)
    cx = X0 + W / 2
    mx = cx - (X0 + TILE / 2)
    wy = X1 + TILE * 0.22 - wb2[1]
    wd = wordmark_path(k2, cx - ww / 2 - wb2[0], wy)
    body = (f'<g transform="translate({f2(mx)} 0)"><path fill="{top_fill}" d="{t}"/>'
            f'<path fill="{bot_fill}" d="{b}"/></g>\n<path fill="{word_fill}" d="{wd}"/>')
    return boxed(X0, X0, X0 + W, wy + wb2[3], body, title)


# ---------- outputs ----------
write("mark-color.svg", mark_svg(ACC, INK))
write("mark-black.svg", mark_svg("#000", "#000"))
write("mark-white.svg", mark_svg("#fff", "#fff", s=S_REV))
write("mark-small.svg", mark_svg(ACC, INK, s=16))
write("mark-small-black.svg", mark_svg("#000", "#000", s=16))
write("mark-on-dark.svg", mark_svg(ACC_D, PAPER_D, s=S_REV))
write("lockup-h-color.svg", lockup(ACC, INK, INK))
write("lockup-h-black.svg", lockup("#000", "#000", "#000"))
write("lockup-h-white.svg", lockup("#fff", "#fff", "#fff", s=S_REV))
write("lockup-h-on-dark.svg", lockup(ACC_D, PAPER_D, PAPER_D, s=S_REV))
write("lockup-v-color.svg", lockup(ACC, INK, INK, stacked=True))
write("lockup-v-black.svg", lockup("#000", "#000", "#000", stacked=True))


def icon_svg(top, bot, base, s=12, title="Samplit app icon"):
    """Full-bleed icon: the mark's tile is the icon. `base` shows only through the cut."""
    t, b = mark_paths(s)
    sc = 256 / TILE
    body = (f'<g transform="scale({f2(sc)}) translate(-{X0} -{X0})">'
            f'<rect x="{X0 + 1}" y="{X0 + 1}" width="{TILE - 2}" height="{TILE - 2}" rx="{R - 1}" fill="{base}"/>'
            f'<path fill="{top}" d="{t}"/><path fill="{bot}" d="{b}"/></g>')
    return svg(256, 256, body, title)


def square_paths(s):
    """Unrounded halves of the full tile, for platforms that apply their own mask (iOS, maskable)."""
    def cut(d):
        return ((0, 76 + d), (150, 76 + d), (106, 180 + d), (256, 180 + d))

    def seg(c):
        t0 = solve(lambda t: bez(c, t)[0] - X0, 0, 1)
        t1 = solve(lambda t: bez(c, t)[0] - X1, 0, 1)
        return split(split(c, t1)[0], t0 / t1)[1]

    st, sb = seg(cut(-s)), seg(cut(s))
    top = (f"M{X0} {X0}H{X1}V{f2(st[3][1])}C{P(st[2])} {P(st[1])} {P(st[0])}Z")
    bot = (f"M{P(sb[0])}C{P(sb[1])} {P(sb[2])} {P(sb[3])}V{X1}H{X0}Z")
    return top, bot


def icon_square_svg(top, bot, base, s=12, title="Samplit app icon"):
    t, b = square_paths(s)
    sc = 256 / TILE
    body = (f'<g transform="scale({f2(sc)}) translate(-{X0} -{X0})">'
            f'<rect x="{X0}" y="{X0}" width="{TILE}" height="{TILE}" fill="{base}"/>'
            f'<path fill="{top}" d="{t}"/><path fill="{bot}" d="{b}"/></g>')
    return svg(256, 256, body, title)


def icon_mac_svg(top, bot, base, s=12, title="Samplit app icon"):
    """1024 canvas, 824 tile centred (the macOS icon grid)."""
    t, b = mark_paths(s)
    sc = 824 / TILE
    body = (f'<g transform="translate(100 100) scale({f2(sc)}) translate(-{X0} -{X0})">'
            f'<rect x="{X0 + 1}" y="{X0 + 1}" width="{TILE - 2}" height="{TILE - 2}" rx="{R - 1}" fill="{base}"/>'
            f'<path fill="{top}" d="{t}"/><path fill="{bot}" d="{b}"/></g>')
    return svg(1024, 1024, body, title)


# A is the main icon, B the universal alternative, C the reversed spare
write("icon-a.svg", icon_svg(ACC, INK, PAPER_D))
write("icon-a-small.svg", icon_svg(ACC, INK, PAPER_D, s=18))
write("icon-a-square.svg", icon_square_svg(ACC, INK, PAPER_D))
write("icon-a-mac.svg", icon_mac_svg(ACC, INK, PAPER_D))
write("icon-b.svg", icon_svg(ACC, ACC_800, PAPER_D))
write("icon-b-small.svg", icon_svg(ACC, ACC_800, PAPER_D, s=18))
write("icon-b-square.svg", icon_square_svg(ACC, ACC_800, PAPER_D))
write("icon-c.svg", icon_svg(ACC_D, PAPER_D, INK))
write("wordmark-black.svg", svg(WB[2] - WB[0], WB[3] - WB[1],
      f'<path d="{wordmark_path(K, -WB[0], -WB[1])}"/>', "Samplit"))
print("built", sorted(os.listdir(OUT)))
