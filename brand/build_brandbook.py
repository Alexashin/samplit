"""Assembles brandbook.html (локальная копия не хранится в репозитории, публикуется в артефакт) from tokens.json, contrast.json and logo/*.svg."""
import json, re, math
from coloraide import Color

T = json.load(open("tokens.json", encoding="utf-8"))
CR = json.load(open("contrast.json", encoding="utf-8"))
tokens_css = open("tokens.css", encoding="utf-8").read()


def svg_inline(path, cls="", label=None):
    s = open(path, encoding="utf-8").read()
    s = re.sub(r"<title>.*?</title>\n?", "", s)
    s = s.replace('role="img"', 'role="img"' if label else 'aria-hidden="true"')
    if not label:
        s = re.sub(r' aria-label="[^"]*"', "", s)
    s = re.sub(r' width="[^"]*" height="[^"]*"', "", s, count=1)
    if cls:
        s = s.replace("<svg ", f'<svg class="{cls}" ', 1)
    return s


def oklch_label(hexv):
    c = Color(hexv).convert("oklch")
    return f"{c['lightness']:.2f} {c['chroma']:.2f} {c['hue']:.0f}"


def ramp(name):
    out = []
    for step, hx in T["ramps"][name].items():
        dark_text = int(step) <= 400
        out.append(
            f'<li style="background:{hx};color:{"#0e141f" if dark_text else "#fff"}">'
            f'<b>{step}</b><span>{hx}</span><span>{oklch_label(hx)}</span></li>')
    return "\n".join(out)


role_notes = {
    "bg": "фон окна", "surface": "панели, карточки", "pad": "пустая клавиша, дорожка, неактивное",
    "line": "разделители", "text": "основной текст", "text-muted": "подписи, второстепенное",
    "accent": "заливка главного действия, звучащая клавиша и микрофон", "accent-hover": "наведение на акцент",
    "on-accent": "текст на акценте", "accent-soft": "фон плашки с акцентом", "accent-text": "акцентный текст и ссылки",
}
rows = []
for k, note in role_notes.items():
    rows.append(
        f'<tr><td><code>--{k}</code></td><td><i class="sw" style="background:{T["light"][k]}"></i><code>{T["light"][k]}</code></td>'
        f'<td><i class="sw" style="background:{T["dark"][k]}"></i><code>{T["dark"][k]}</code></td><td>{note}</td></tr>')
token_rows = "\n".join(rows)

pair_names = {
    ("text", "bg"): "текст на фоне", ("text-muted", "bg"): "подписи на фоне",
    ("on-accent", "accent"): "текст на акценте", ("accent-text", "bg"): "акцентный текст на фоне",
    ("accent", "bg"): "акцентная заливка к фону", ("ok-text", "ok-soft"): "статус «готово»",
    ("warn-text", "warn-soft"): "статус «проверить»", ("crit-text", "crit-soft"): "статус «ошибка»",
}
ct = {}
for theme, fg, bg, v in CR:
    ct[(theme, fg, bg)] = v
crow = []
for (fg, bg), label in pair_names.items():
    l, d = ct[("light", fg, bg)], ct[("dark", fg, bg)]
    need = "3" if fg == "accent" else "4.5"
    crow.append(f'<tr><td>{label}</td><td class="n">{l}</td><td class="n">{d}</td><td class="n">{need}</td></tr>')
contrast_rows = "\n".join(crow)

L = lambda n: svg_inline(f"logo/{n}.svg")
LA = lambda n, c="": svg_inline(f"logo/{n}.svg", c, label=True)

# dark scope block for always-dark surfaces (plugin mockup)
dark_scope = ".dark-scope{" + "".join(f"--{k}:{v};" for k, v in T["dark"].items()) + "color-scheme:dark}"

def split_bg(y0=0.34, y1=0.70, g=0.025, w=1000, h=400):
    """Two tinted regions separated by the mark's cut: flat, drop, flat."""
    a, b, gg = y0 * h, y1 * h, g * h
    top = f"M0 0H{w}V{b - gg:g}C{w * .414:g} {b - gg:g} {w * .586:g} {a - gg:g} 0 {a - gg:g}Z"
    bot = f"M0 {a + gg:g}C{w * .586:g} {a + gg:g} {w * .414:g} {b + gg:g} {w} {b + gg:g}V{h}H0Z"
    return (f'<svg class="split-bg" viewBox="0 0 {w} {h}" preserveAspectRatio="none" aria-hidden="true">'
            f'<path class="t" d="{top}"/><path class="b" d="{bot}"/></svg>')


def cubic_split(p, t):
    l = lambda a, b: tuple(a[i] + (b[i] - a[i]) * t for i in (0, 1))
    a, b, c, d = p
    ab, bc, cd = l(a, b), l(b, c), l(c, d)
    abc, bcd = l(ab, bc), l(bc, cd)
    m = l(abc, bcd)
    return (a, ab, abc, m), (m, bcd, cd, d)


def pd(c):
    return f"M{c[0][0]:.1f} {c[0][1]:.1f}C{c[1][0]:.1f} {c[1][1]:.1f} {c[2][0]:.1f} {c[2][1]:.1f} {c[3][0]:.1f} {c[3][1]:.1f}"


STK = 'fill="none" stroke-width="3" stroke-linecap="round"'
SM = ((0, 6.5), (60, 6.5), (60, 23.5), (120, 23.5))  # the cut at its own proportions: flat, drop, flat


def rule_step(cl, cr, gap=False):
    if gap:
        a, _ = cubic_split(SM, 0.44)
        _, b = cubic_split(SM, 0.56)
        paths = f'<path d="{pd(a)}" stroke="{cl}" {STK}/><path d="{pd(b)}" stroke="{cr}" {STK}/>'
    else:
        paths = f'<path d="{pd(SM)}" stroke="{cl}" {STK}/>'
    return (f'<div class="rule-step" aria-hidden="true"><i class="rl" style="background:{cl}"></i>'
            f'<svg viewBox="0 0 120 30" width="120" height="30">{paths}</svg><i class="rr" style="background:{cr}"></i></div>')


div_a = rule_step("var(--accent)", "var(--accent)")
div_b = rule_step("var(--accent)", "var(--text)", gap=True)
div_c = (f'<svg viewBox="0 0 960 48" preserveAspectRatio="none" aria-hidden="true">'
         f'<path d="M0 24C160 2 320 2 480 24S800 46 960 24" stroke="var(--accent)" {STK} vector-effect="non-scaling-stroke"/></svg>')

# deterministic waveform bars
bars = []
for i in range(64):
    env = math.exp(-i / 22)
    h = 6 + 52 * env * (0.55 + 0.45 * abs(math.sin(i * 1.7) * math.cos(i * 0.6)))
    bars.append(f'<rect x="{i * 6}" y="{32 - h / 2:.1f}" width="3.4" height="{h:.1f}" rx="1.7"/>')
wave = f'<svg viewBox="0 0 384 64" class="wave" aria-hidden="true">{"".join(bars)}</svg>'

# mockup: drum keys (note, label) and the microphones of one snare hit (name, out, level %, enabled in the DAW)
keys = [("A-1", "KICK"), ("C#0", "RIM"), ("D0", "SNARE"), ("F#0", "GHOST"), ("C1", "CRASH"), ("G#1", "CHINA"),
        ("C2", "HAT"), ("E4", "PEDAL"), ("C5", "RIDE"), ("C6", "TOM 16"), ("E6", "TOM 14"), ("F#6", "TOM 10")]
lit_keys = {2, 6}
keys_html = "\n".join(
    f'<div class="key{" lit" if i in lit_keys else ""}"><span>{n}</span><b>{lb}</b></div>' for i, (n, lb) in enumerate(keys))

mics = [("KICK IN", 1, 12, True), ("KICK OUT", 2, 10, True), ("SN TOP", 3, 92, True), ("SN BOT", 4, 60, True),
        ("HAT", 5, 22, True), ("OVER 1", 6, 48, True), ("OVER 2", 7, 46, False), ("ROOM MID", 8, 34, False)]
mics_html = "\n".join(
    f'<div class="mic"><b>{nm}</b><i class="lvl"><u style="width:{lv}%"></u></i>'
    f'<span class="tag{"" if on else " off"}">{"OUT " + str(o) if on else "→ MAIN"}</span></div>'
    for nm, o, lv, on in mics)

html = f'''<title>Samplit Brandbook</title>
<meta name="claude:viewport-fit-sides" content="cover">
<link rel="preconnect" href="https://fonts.googleapis.com">
<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Geologica:wght@400;500;700;800&family=JetBrains+Mono:wght@400;600&display=swap">
<style>
/* Layout: one long column of numbered-by-topic sections; wide specimens sit in their own bands */
{tokens_css}
:root {{
  --f-display: "Geologica", "Segoe UI", system-ui, sans-serif;
  --f-mono: "JetBrains Mono", ui-monospace, Consolas, monospace;
  --r-pad: 24%;
}}
{dark_scope}
:root {{ --paper: #eef0f4; --ink: #0e141f; --accent-fixed: #314bff; --crit-fixed: #b4232d; --show-l: block; --show-d: none; }}
@media (prefers-color-scheme: dark) {{ :root:not([data-theme="light"]) {{ --show-l: none; --show-d: block; }} }}
:root[data-theme="dark"] {{ --show-l: none; --show-d: block; }}
.sw-l {{ display: var(--show-l) !important; }}
.sw-d {{ display: var(--show-d) !important; }}
* {{ box-sizing: border-box; }}
html {{ -webkit-font-smoothing: antialiased; -moz-osx-font-smoothing: grayscale; }}
body {{ margin: 0; background: var(--bg); color: var(--text); font: 400 1rem/1.6 var(--f-display); }}
body::before {{ content: ""; position: fixed; inset: 0 0 auto; height: env(safe-area-inset-top, 0px); background: var(--surface); z-index: 10; }}
.wrap {{ max-width: 1040px; margin: 0 auto; padding-left: max(16px, env(safe-area-inset-left, 0px)); padding-right: max(16px, env(safe-area-inset-right, 0px)); }}
header.top {{ background: var(--surface); border-bottom: 1px solid var(--line); position: sticky; top: env(safe-area-inset-top, 0px); z-index: 5; }}
header.top .wrap {{ display: flex; align-items: center; gap: 24px; padding-block: 12px; flex-wrap: wrap; }}
header.top svg {{ height: 28px; width: auto; }}
header.top nav {{ display: flex; gap: 4px; margin-left: auto; overflow-x: auto; scrollbar-width: none; }}
header.top nav a {{ color: var(--text-muted); text-decoration: none; font-size: .875rem; padding: 6px 10px; border-radius: 8px; white-space: nowrap; }}
header.top nav a:hover {{ background: var(--pad); color: var(--text); }}
a:focus-visible, button:focus-visible {{ outline: 2px solid var(--focus); outline-offset: 2px; }}
h1, h2, h3 {{ text-wrap: balance; margin: 0; }}
h1 {{ font: 800 clamp(2.2rem, 6vw, 3.6rem)/1.05 var(--f-display); letter-spacing: -.02em; }}
h2 {{ font: 700 1.75rem/1.2 var(--f-display); letter-spacing: -.015em; }}
h3 {{ font: 600 1.0625rem/1.3 var(--f-display); }}
p {{ margin: 0; max-width: 68ch; text-wrap: pretty; }}
code, .mono {{ font-family: var(--f-mono); font-size: .86em; }}
.eyebrow {{ font: 600 .75rem/1 var(--f-mono); letter-spacing: .06em; text-transform: uppercase; color: var(--text-muted); }}
.muted {{ color: var(--text-muted); }}
section {{ padding-block: 56px 8px; scroll-margin-top: 64px; }}
section > .eyebrow {{ display: block; margin-bottom: 10px; }}
section > h2 + p, section > h2 + .lead {{ margin-top: 12px; }}
.lead {{ font-size: 1.125rem; max-width: 62ch; }}
.stack {{ display: grid; gap: 24px; margin-top: 28px; }}
.cols {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }}
.panel {{ background: var(--surface); border: 1px solid var(--line); border-radius: 16px; padding: 24px; min-width: 0; }}
.panel.flush {{ padding: 0; overflow: hidden; display: flex; flex-direction: column; }}
.panel.flush .stage {{ flex: 1; }}
.stage {{ display: grid; place-items: center; padding: 40px 24px; min-height: 180px; }}
.stage.dark {{ background: var(--ink); }}
.stage.light {{ background: var(--paper); }}
.lightbox {{ background: var(--paper); color: #4a505c; border-radius: 12px; padding: 20px; }}
.lightbox .hint, .lightbox figcaption {{ color: #4a505c; }}
.stage.accent {{ background: var(--accent); }}
.cap {{ display: flex; justify-content: space-between; gap: 12px; padding: 12px 16px; border-top: 1px solid var(--line); font-size: .8125rem; color: var(--text-muted); }}

/* hero */
.hero {{ padding-block: 64px 24px; display: grid; gap: 28px; }}
.hero .mark {{ width: clamp(104px, 18vw, 168px); }}
.hero .mark svg {{ width: 100%; height: auto; }}
.hero .lead {{ max-width: 56ch; }}
.facts {{ display: flex; flex-wrap: wrap; gap: 8px; }}
.fact {{ font: 600 .75rem/1 var(--f-mono); padding: 7px 10px; border-radius: 99px; background: var(--pad); color: var(--text); }}
.fact.lit {{ background: var(--accent-soft); color: var(--accent-text); }}

/* logo */
.lockup-h svg {{ width: min(100%, 440px); height: auto; display: block; }}
.lockup-v svg {{ width: min(100%, 220px); height: auto; display: block; }}
.grid6 {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(150px, 1fr)); gap: 12px; }}
.tile {{ display: grid; gap: 10px; }}
.tile .box {{ display: grid; place-items: center; height: 120px; border-radius: 12px; border: 1px solid var(--line); background: var(--surface); }}
.tile .box svg {{ height: 56px; width: auto; max-width: 80%; }}
.tile .box.dk {{ background: var(--ink); border-color: var(--ink); }}
.tile .box.lt {{ background: var(--paper); border-color: var(--paper); }}
.tile .box.ac {{ background: var(--accent-fixed); border-color: var(--accent-fixed); }}
.tile .box.lg svg {{ height: 28px; }}
.tile small {{ font-size: .8125rem; color: var(--text-muted); }}
.clear svg {{ width: min(100%, 320px); height: auto; }}
.sizes {{ display: flex; align-items: flex-end; gap: 28px; flex-wrap: wrap; }}
.sizes figure {{ margin: 0; display: grid; gap: 8px; justify-items: start; }}
.sizes span svg {{ display: block; width: 100%; height: auto; }}
.sizes figcaption {{ font: 400 .75rem/1.2 var(--f-mono); color: var(--text-muted); }}

.icons {{ display: flex; gap: 24px; flex-wrap: wrap; align-items: flex-end; }}
.icons figure {{ margin: 0; display: grid; gap: 8px; justify-items: start; }}
.icons figcaption {{ font-size: .8125rem; color: var(--text-muted); }}
.icons .ic {{ width: 128px; }}
.icons .ic svg {{ display: block; width: 100%; height: auto; }}
.icons.small {{ gap: 14px 18px; align-items: flex-end; }}
.icons .s64 {{ width: 64px; }} .icons .s32 {{ width: 32px; }} .icons .s16 {{ width: 16px; }}

/* split backgrounds: the cut of the mark reused as a surface */
.split-bg {{ position: absolute; inset: 0; width: 100%; height: 100%; z-index: -1; display: block; }}
.split-bg .t {{ fill: var(--accent-soft); }}
.split-bg .b {{ fill: var(--pad); }}
.demo {{ position: relative; isolation: isolate; border-radius: 16px; overflow: hidden; border: 1px solid var(--line); min-height: 220px; display: grid; place-items: center; text-align: center; padding: 32px 24px; }}
.demo > div {{ max-width: 34ch; display: grid; gap: 8px; justify-items: center; }}
.demo b {{ font-weight: 700; }}
.opt {{ display: grid; gap: 8px; }}
.sdiv {{ margin-bottom: 32px; }}
.sdiv .rule-step svg {{ width: 120px; height: 30px; }}
.rule-step {{ display: grid; grid-template-columns: minmax(0, 1fr) 120px minmax(0, 1fr); align-items: start; height: 30px; width: 100%; }}
.rule-step i {{ display: block; height: 3px; border-radius: 2px; margin-top: 5px; }}
.rule-step i.rr {{ margin-top: 22px; }}
.opt .rule-step svg {{ width: 120px; height: 30px; }}
.icons.small.lightbox {{ background: #a6abb4; }}
.icons.small figcaption {{ color: #0e141f; }}
.opt svg {{ width: 100%; height: 48px; display: block; }}
.opt small {{ color: var(--text-muted); font-size: .8125rem; }}

/* color */
.ramp {{ list-style: none; margin: 0; padding: 0; display: grid; grid-template-columns: repeat(11, minmax(0, 1fr)); border-radius: 12px; overflow: hidden; }}
.ramp li {{ display: grid; gap: 2px; padding: 12px 8px 10px; min-height: 96px; align-content: end; font: 400 .6875rem/1.3 var(--f-mono); }}
.ramp li b {{ font-weight: 600; font-size: .8125rem; }}
.ramp li span {{ overflow-wrap: anywhere; opacity: .92; }}
@media (max-width: 720px) {{ .ramp {{ grid-template-columns: repeat(2, 1fr); }} .ramp li {{ min-height: 64px; grid-auto-flow: column; justify-content: space-between; align-content: center; }} }}
.share {{ display: flex; height: 44px; border-radius: 10px; overflow: hidden; border: 1px solid var(--line); }}
.share i {{ display: block; }}
.tbl {{ overflow-x: auto; border: 1px solid var(--line); border-radius: 12px; background: var(--surface); }}
table {{ border-collapse: collapse; width: 100%; font-size: .9rem; }}
th, td {{ text-align: left; padding: 10px 14px; border-bottom: 1px solid var(--line); vertical-align: middle; }}
th {{ font: 600 .75rem/1.2 var(--f-mono); letter-spacing: .05em; text-transform: uppercase; color: var(--text-muted); background: var(--bg); }}
tr:last-child td {{ border-bottom: 0; }}
td.n {{ font-variant-numeric: tabular-nums; font-family: var(--f-mono); }}
.sw {{ display: inline-block; width: 16px; height: 16px; border-radius: 5px; border: 1px solid var(--line); vertical-align: -3px; margin-right: 8px; }}

/* type */
.spec {{ display: grid; gap: 0; }}
.spec > div {{ display: grid; grid-template-columns: 150px 1fr; gap: 16px; align-items: baseline; padding: 14px 0; border-bottom: 1px solid var(--line); }}
.spec > div:last-child {{ border-bottom: 0; }}
.spec .k {{ font: 400 .75rem/1.4 var(--f-mono); color: var(--text-muted); }}
.spec .s {{ min-width: 0; overflow-wrap: anywhere; }}
@media (max-width: 640px) {{ .spec > div {{ grid-template-columns: 1fr; gap: 4px; }} }}
.big {{ font: 800 clamp(3rem, 12vw, 6.5rem)/1 var(--f-display); letter-spacing: -.03em; }}
.glyphs {{ font: 500 1.25rem/1.5 var(--f-display); overflow-wrap: anywhere; }}

/* shape */
.radii {{ display: flex; gap: 20px; flex-wrap: wrap; align-items: flex-end; }}
.radii figure {{ margin: 0; display: grid; gap: 8px; justify-items: start; font: 400 .75rem/1.3 var(--f-mono); color: var(--text-muted); }}
.radii i {{ display: block; background: var(--accent); }}
.cut {{ height: 64px; width: 100%; display: block; }}

/* plugin mockup */
.plug {{ background: var(--bg); color: var(--text); border-radius: 16px; border: 1px solid var(--line); padding: 16px; display: grid; gap: 16px; }}
.plug-top {{ display: flex; gap: 12px; align-items: center; flex-wrap: wrap; }}
.plug-top svg {{ height: 24px; width: auto; }}
.search {{ flex: 1 1 220px; min-width: 0; background: var(--surface); border: 1px solid var(--line); border-radius: 10px; padding: 9px 12px; font-size: .9rem; display: flex; gap: 8px; align-items: center; }}
.search span {{ color: var(--text-muted); }}
.plug-body {{ display: grid; grid-template-columns: minmax(0, 1fr) minmax(0, 1fr); gap: 16px; }}
@media (max-width: 720px) {{ .plug-body {{ grid-template-columns: 1fr; }} }}
.list {{ background: var(--surface); border-radius: 12px; padding: 6px; display: grid; gap: 2px; align-content: start; }}
.row {{ display: grid; grid-template-columns: 1fr auto; gap: 8px; align-items: center; padding: 8px 10px; border-radius: 8px; font-size: .875rem; min-width: 0; }}
.row b {{ font-weight: 500; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; }}
.row.sel {{ background: var(--accent-soft); }}
.row .tags {{ display: flex; gap: 4px; }}
.tag {{ font: 600 .6875rem/1 var(--f-mono); padding: 4px 6px; border-radius: 99px; background: var(--pad); color: var(--text-muted); }}
.row.sel .tag {{ background: var(--accent); color: var(--on-accent); }}
.wave {{ width: 100%; height: 56px; fill: var(--text-muted); }}
.kbd {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(56px, 1fr)); gap: 8px; }}
.key {{ aspect-ratio: 1 / 1.1; border-radius: 12px; background: var(--pad); display: grid; align-content: space-between; padding: 10px 8px; min-width: 0; }}
.key span {{ font: 600 .6875rem/1 var(--f-mono); color: var(--text-muted); }}
.key b {{ font: 700 .75rem/1.1 var(--f-display); overflow-wrap: anywhere; }}
.key.lit {{ background: var(--accent); color: var(--on-accent); }}
.key.lit span {{ color: var(--on-accent); opacity: .85; }}
.seg {{ display: inline-flex; border: 1px solid var(--line); border-radius: 10px; padding: 3px; gap: 2px; font-size: .8125rem; background: var(--surface); }}
.seg span {{ padding: 6px 10px; border-radius: 7px; color: var(--text-muted); white-space: nowrap; }}
.seg span.on {{ background: var(--accent-soft); color: var(--accent-text); font-weight: 600; }}
.mixer {{ background: var(--surface); border-radius: 12px; padding: 6px; display: grid; gap: 2px; align-content: start; }}
.mic {{ display: grid; grid-template-columns: minmax(64px, 88px) 1fr auto; gap: 12px; align-items: center; padding: 8px 10px; border-radius: 8px; }}
.mic b {{ font: 600 .75rem/1 var(--f-mono); letter-spacing: .04em; }}
.lvl {{ display: block; height: 6px; border-radius: 3px; background: var(--pad); overflow: hidden; }}
.lvl u {{ display: block; height: 100%; background: var(--accent); }}
.tag.off {{ background: var(--warn-soft); color: var(--warn-text); }}
.alert {{ display: flex; gap: 10px; align-items: baseline; padding: 10px 12px; border-radius: 10px; background: var(--warn-soft); color: var(--warn-text); font-size: .8125rem; line-height: 1.45; }}
.alert b {{ font: 700 .8125rem/1 var(--f-mono); }}
.hint {{ font: 400 .75rem/1.4 var(--f-mono); color: var(--text-muted); }}

ul.plain {{ margin: 0; padding-left: 1.1em; display: grid; gap: 8px; max-width: 68ch; }}
.rule {{ display: grid; gap: 6px; }}
.rule h3 {{ display: flex; gap: 10px; align-items: baseline; }}
.pillok {{ font: 600 .6875rem/1 var(--f-mono); padding: 4px 7px; border-radius: 99px; background: var(--accent-soft); color: var(--accent-text); }}
.files code {{ white-space: nowrap; }}
footer {{ padding-block: 56px 80px; color: var(--text-muted); font-size: .875rem; }}
</style>

<header class="top"><div class="wrap">
  <a href="#top" aria-label="Samplit" style="display:flex">{LA("lockup-h-color","sw-l")}{LA("lockup-h-on-dark","sw-d")}</a>
  <nav aria-label="Разделы">
    <a href="#logo">Логотип</a><a href="#color">Цвет</a><a href="#type">Шрифты</a><a href="#shape">Форма</a><a href="#ui">Интерфейс</a><a href="#voice">Голос</a><a href="#files">Файлы</a>
  </nav>
</div></header>

<main class="wrap" id="top">

<div class="hero">
  <div class="mark">{LA("mark-color","sw-l")}{LA("mark-on-dark","sw-d")}</div>
  <h1>Один удар, <br>разные выходы</h1>
  <p class="lead">Samplit собирается из двух слов: sample и split. Знак показывает это буквально: пэд, разрезанный волной. Верхняя половина горит, нижняя ждёт. Так же работает плагин: один удар барабана записан на несколько микрофонов, и каждый микрофон уходит на свою дорожку.</p>
  <div class="facts"><span class="fact lit">v1.0 · брендбук</span><span class="fact">офлайн</span><span class="fact">Main + 16 выходов</span><span class="fact">VST3 · AU · Standalone</span><span class="fact">Geologica + JetBrains Mono</span></div>
</div>

<!-- ======== ЛОГОТИП ======== -->
<section id="logo">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Логотип</span>
  <h2>Знак и надпись</h2>
  <p class="lead">Знак состоит из пэда и разреза. Разрез идёт плавной S-кривой, которая даёт и «split», и намёк на букву S. В надписи точка над <b>i</b> заменена маленьким пэдом.</p>

  <div class="stack">
    <div class="cols">
      <div class="panel flush"><div class="stage light lockup-h">{LA("lockup-h-color")}</div><div class="cap"><span>Основная версия</span><span class="mono">lockup-h-color.svg</span></div></div>
      <div class="panel flush"><div class="stage dark lockup-h">{LA("lockup-h-on-dark")}</div><div class="cap"><span>На тёмном</span><span class="mono">lockup-h-on-dark.svg</span></div></div>
    </div>

    <div class="cols">
      <div class="panel flush"><div class="stage light lockup-v">{LA("lockup-v-color")}</div><div class="cap"><span>Вертикальная версия</span><span class="mono">lockup-v-color.svg</span></div></div>
      <div class="panel">
        <h3>Построение</h3>
        <p class="muted" style="margin-top:8px">Тайл 200×200, радиус углов 48 (24% стороны). Разрез: кубическая S-кривая от левой грани к правой, зазор 20. Высота тайла равна высоте буквы <b>l</b>, нижний край стоит на базовой линии. Зазор между знаком и надписью: 20% высоты тайла.</p>
        <div class="clear lightbox" style="margin-top:20px;display:grid;place-items:center">
          <svg viewBox="-22 -22 300 300" role="img" aria-label="Охранное поле знака равно четверти его высоты">
            <rect x="-22" y="-22" width="300" height="300" rx="14" fill="none" stroke="#ced1d8" stroke-dasharray="4 4"/>
            <rect x="28" y="28" width="200" height="200" rx="2" fill="rgba(49,75,255,.08)" stroke="#314bff" stroke-dasharray="3 3"/>
            <g transform="translate(0 0)">{L("mark-color").split(chr(10), 1)[1].rsplit("</svg>", 1)[0]}</g>
            <text x="128" y="14" text-anchor="middle" font-family="var(--f-mono)" font-size="11" fill="#4a505c">x = ¼ высоты</text>
          </svg>
        </div>
        <p class="muted" style="margin-top:12px">Охранное поле: x = четверть высоты знака со всех сторон.</p>
      </div>
    </div>

    <div class="panel">
      <h3>Версии</h3>
      <div class="grid6" style="margin-top:16px">
        <div class="tile"><div class="box lt">{LA("mark-color")}</div><small>Цветная</small></div>
        <div class="tile"><div class="box lt">{LA("mark-black")}</div><small>Чёрная, один цвет</small></div>
        <div class="tile"><div class="box dk">{LA("mark-white")}</div><small>Белая, на тёмном</small></div>
        <div class="tile"><div class="box dk">{LA("mark-on-dark")}</div><small>Для тёмной темы</small></div>
        <div class="tile"><div class="box ac">{LA("mark-white")}</div><small>Белая на акценте</small></div>
      </div>
      <p class="muted" style="margin-top:16px">В обратных версиях (светлое на тёмном и на акценте) зазор шире, 15% против 10%: светлые формы растекаются и съедают разрез, поэтому его режем глубже. Так S читается одинаково и на светлом, и на тёмном.</p>
    </div>

    <div class="panel">
      <div style="display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap"><h3>Иконка приложения</h3><span class="mono muted">A основная · B универсальная</span></div>
      <p class="muted" style="margin-top:8px">Тайл знака сам становится иконкой, без вложенной плашки. Разрез остаётся виден за счёт светлой (или тёмной) подложки в зазоре. A основная: плагин, установщик, панель задач. B универсальная: подходит там, где графитовая половина теряется на тёмном фоне. C запасная, для тёмных окружений.</p>
      <div class="icons" style="margin-top:20px">
        <figure><div class="ic">{LA("icon-a")}</div><figcaption><b>A</b> · основная</figcaption></figure>
        <figure><div class="ic">{LA("icon-b")}</div><figcaption><b>B</b> · универсальная</figcaption></figure>
        <figure><div class="ic">{LA("icon-c")}</div><figcaption><b>C</b> · запасная, обратная</figcaption></figure>
      </div>
      <div class="icons small lightbox" style="margin-top:20px">
        <figure><div class="ic s64">{L("icon-a")}</div><figcaption>A · 64</figcaption></figure><figure><div class="ic s32">{L("icon-a-small")}</div><figcaption>32</figcaption></figure><figure><div class="ic s16">{L("icon-a-small")}</div><figcaption>16</figcaption></figure>
        <figure><div class="ic s64">{L("icon-b")}</div><figcaption>B · 64</figcaption></figure><figure><div class="ic s32">{L("icon-b-small")}</div><figcaption>32</figcaption></figure><figure><div class="ic s16">{L("icon-b-small")}</div><figcaption>16</figcaption></figure>
        <figure><div class="ic s64">{L("icon-c")}</div><figcaption>C · 64</figcaption></figure><figure><div class="ic s32">{L("icon-c")}</div><figcaption>32</figcaption></figure><figure><div class="ic s16">{L("icon-c")}</div><figcaption>16</figcaption></figure>
      </div>
    </div>

    <div class="cols">
      <div class="panel">
        <h3>Минимальные размеры</h3>
        <p class="muted" style="margin-top:8px">Меньше 32 px берём версию «small»: зазор шире, поэтому разрез читается до 16 px.</p>
        <div class="sizes lightbox" style="margin-top:20px">
          <figure><span style="display:block;width:64px">{L("mark-color")}</span><figcaption>64 px</figcaption></figure>
          <figure><span style="display:block;width:32px">{L("mark-color")}</span><figcaption>32 px</figcaption></figure>
          <figure><span style="display:block;width:24px">{L("mark-small")}</span><figcaption>24 px · small</figcaption></figure>
          <figure><span style="display:block;width:16px">{L("mark-small")}</span><figcaption>16 px · small</figcaption></figure>
          <figure><span style="display:block;width:120px">{L("lockup-h-color")}</span><figcaption>надпись ≥ 120 px</figcaption></figure>
        </div>
      </div>
    </div>
  </div>
</section>

<!-- ======== ЦВЕТ ======== -->
<section id="color">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Цвет</span>
  <h2>Графит, бумага и один горящий цвет</h2>
  <p class="lead">Основа спокойная, холодная. Акцент один, ультрамарин: его нет ни у одной из DAW, в которых живёт плагин. Он включается там, где что-то «горит»: звучащая клавиша, главное действие, выбранный пресет.</p>
  <div class="stack">
    <div class="panel">
      <div style="display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;margin-bottom:12px"><h3>Акцент · ультрамарин</h3><span class="mono muted">oklch, оттенок 268</span></div>
      <ol class="ramp" style="padding:0">{ramp("accent")}</ol>
      <div style="display:flex;justify-content:space-between;gap:12px;flex-wrap:wrap;margin:28px 0 12px"><h3>Нейтраль · графит</h3><span class="mono muted">oklch, оттенок 265</span></div>
      <ol class="ramp" style="padding:0">{ramp("neutral")}</ol>
    </div>

    <div class="cols">
      <div class="panel">
        <h3>Пропорции</h3>
        <p class="muted" style="margin-top:8px">Акцента мало. Чем реже он встречается, тем сильнее работает.</p>
        <div class="share" style="margin-top:16px" role="img" aria-label="Нейтраль 70%, графит 22%, акцент 8%">
          <i style="width:70%;background:var(--paper)"></i><i style="width:22%;background:var(--ink)"></i><i style="width:8%;background:var(--accent-fixed)"></i>
        </div>
        <div style="display:flex;justify-content:space-between;margin-top:8px" class="mono muted"><span>70% бумага</span><span>22% графит</span><span>8% акцент</span></div>
        <p style="margin-top:20px"><b>Один цвет, одно значение.</b> Ультрамарин значит «активно» или «главное». Для статусов отдельные цвета, они не пересекаются с акцентом по оттенку: зелёный, янтарный, красный.</p>
      </div>
      <div class="panel">
        <h3>Контраст, измеренный</h3>
        <div class="tbl" style="margin-top:12px"><table>
          <thead><tr><th>Пара</th><th>Светлая</th><th>Тёмная</th><th>Нужно</th></tr></thead>
          <tbody>{contrast_rows}</tbody>
        </table></div>
        <p class="hint" style="margin-top:10px">Рассчитано по WCAG 2.1 из значений токенов. Граница поля ввода должна давать ≥ 3:1: для неё берите ступень 500 нейтрали, а не <code>--line</code>.</p>
      </div>
    </div>

    <div>
      <h3 style="margin-bottom:12px">Токены</h3>
      <div class="tbl"><table>
        <thead><tr><th>Роль</th><th>Светлая</th><th>Тёмная</th><th>Где</th></tr></thead>
        <tbody>{token_rows}</tbody>
      </table></div>
      <p class="hint" style="margin-top:10px">В коде используем только роли (<code>--accent</code>), ступени (<code>accent-600</code>) остаются внутри палитры. Файлы: <code>tokens.css</code>, <code>tokens.json</code>.</p>
    </div>
  </div>
</section>

<!-- ======== ШРИФТЫ ======== -->
<section id="type">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Шрифты</span>
  <h2>Geologica и JetBrains Mono</h2>
  <p class="lead">Geologica: широкие округлые формы, родня скруглённому пэду, есть кириллица и вариативный вес. Она делает и надпись, и заголовки, и текст интерфейса. JetBrains Mono для данных: номера выходов, теги, ноты, BPM, единицы.</p>
  <div class="stack">
    <div class="panel">
      <div class="big">Аа Samplit</div>
      <p class="glyphs" style="margin-top:12px">АБВГДЕЁЖЗИЙКЛМНОПРСТУФХЦЧШЩЪЫЬЭЮЯ abcdefghijklmnopqrstuvwxyz 0123456789 → ♪ ·</p>
      <p class="mono muted" style="margin-top:12px">Geologica 400 · 500 · 700 · 800 (Google Fonts, OFL). JetBrains Mono 400 · 600 (OFL).</p>
    </div>
    <div class="panel">
      <div class="spec">
        <div><span class="k">Display · 800 · 1.05</span><span class="s" style="font:800 2.5rem/1.05 var(--f-display);letter-spacing:-.02em">Выберите пресет</span></div>
        <div><span class="k">H2 · 700 · 1.2 · −0.015em</span><span class="s" style="font:700 1.75rem/1.2 var(--f-display);letter-spacing:-.015em">Мульти-аут в DAW</span></div>
        <div><span class="k">H3 · 600 · 1.3</span><span class="s" style="font:600 1.0625rem/1.3 var(--f-display)">Микрофоны и выходы</span></div>
        <div><span class="k">Текст · 400 · 1.6 · 16px</span><span class="s">Каждый микрофон удара звучит на своём выходе, а в Main идёт общий микс.</span></div>
        <div><span class="k">Мелкий · 400 · 1.5 · 14px</span><span class="s" style="font-size:.875rem">Пакет загружается в фоне, DAW не подвисает.</span></div>
        <div><span class="k">Данные · mono 600 · 12px</span><span class="s mono" style="font-weight:600;font-size:.75rem;letter-spacing:.06em;text-transform:uppercase">OUT 3 · SN TOP · D0 · 120–127</span></div>
      </div>
    </div>
    <ul class="plain">
      <li>Размеры только из шкалы: 12 · 14 · 16 · 17 · 28 · 40+. Заголовки идут по убыванию, ни один не мельче текста.</li>
      <li>Межстрочный интервал безразмерный. Подписи и теги короче одной строки, поэтому там 1.2–1.3, всё, что переносится, не ниже 1.4.</li>
      <li>Длина строки в тексте до 68 символов. Заголовки с <code>text-wrap: balance</code>, абзацы с <code>pretty</code>.</li>
      <li>Цифры, которые меняются (время, velocity, число ударов), набираем с <code>tabular-nums</code>.</li>
      <li>В плагине минимальный размер 11 px, только для моно-меток. Основной текст интерфейса 13–14 px.</li>
    </ul>
  </div>
</section>

<!-- ======== ФОРМА ======== -->
<section id="shape">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Форма</span>
  <h2>Всё скруглено как пэд</h2>
  <p class="lead">Пэд определяет форму всего интерфейса: радиус около четверти стороны. Разрез-волна встречается там, где нужно что-то разделить или показать «один в два».</p>
  <div class="stack">
    <div class="cols">
      <div class="panel">
        <h3>Радиусы</h3>
        <div class="radii" style="margin-top:20px">
          <figure><i style="width:96px;height:96px;border-radius:24%"></i>пэд · 24%</figure>
          <figure><i style="width:96px;height:48px;border-radius:12px"></i>панель · 12</figure>
          <figure><i style="width:96px;height:32px;border-radius:8px"></i>кнопка · 8</figure>
          <figure><i style="width:56px;height:24px;border-radius:99px"></i>тег · полный</figure>
        </div>
        <p class="muted" style="margin-top:20px">Вложенный элемент: внешний радиус = внутренний + отступ. Больше 24 px отступа считаем отдельной поверхностью со своим радиусом.</p>
      </div>
      <div class="panel">
        <h3>Поверхности и сетка</h3>
        <ul class="plain" style="margin-top:12px">
          <li>Шаг сетки 4 px, отступы 8 · 12 · 16 · 24 · 32.</li>
          <li>Глубину даёт слой цвета (<code>bg</code> → <code>surface</code> → <code>pad</code>), а не рамка. Рамка остаётся на разделителях и полях ввода.</li>
          <li>Тени только у плавающего: меню, поповеры. Две полупрозрачные тени вместо одной тёмной.</li>
          <li>Иконки в одном наборе, линия 1.5 px, цвет через <code>currentColor</code>.</li>
        </ul>
      </div>
    </div>
    <div class="panel">
      <h3>Мотив разреза: разделитель</h3>
      <p class="muted" style="margin-top:8px">Тонкая линия между секциями и в пустых состояниях. Это разрез из знака в его пропорциях: ровно, спад, ровно. Слева горит акцент, справа графит, между половинками зазор, как в знаке. Ступень фиксированного размера: растянутый на всю ширину разрез превращается в почти прямую линию.</p>
      <div class="opt" style="margin-top:20px">{div_b}</div>
      <p class="hint" style="margin-top:12px">Этот разделитель стоит между секциями этой страницы.</p>
    </div>
    <div class="panel">
      <h3>Мотив разреза: фон</h3>
      <p class="muted" style="margin-top:8px">Тот же разрез как поверхность: две тонировки, между ними полоска фона. Применяем сдержанно: пустые состояния, заставки, редкие широкие полосы. Не на первом экране и не в рабочих местах плагина (клавиатура, микшер микрофонов), где фон остаётся ровным.</p>
      <div class="cols" style="margin-top:20px">
        <div class="demo">{split_bg(0.30, 0.66, 0.02, 600, 300)}<div><b>Пресет не выбран</b><span class="muted">Выберите пресет в списке. Клавиши заиграют, когда пакет загрузится.</span></div></div>
        <div class="demo">{split_bg(0.62, 0.38, 0.02, 600, 300)}<div><b>Пакет «Тарелки» не установлен</b><span class="muted">Положите файл пакета в папку Packs и перезапустите проект. Остальные барабаны уже играют.</span></div></div>
      </div>
    </div>
  </div>
</section>

<!-- ======== ИНТЕРФЕЙС ======== -->
<section id="ui">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Интерфейс</span>
  <h2>Плагин живёт в тёмной теме</h2>
  <p class="lead">DAW почти всегда тёмные, поэтому плагин по умолчанию тёмный, светлая тема опциональна. Эскиз ниже показывает, как работает система: пользователь выбирает пресет, играет по клавишам и видит, какие микрофоны звучат и на какие выходы они уходят. Горит то, что звучит или выбрано.</p>
  <div class="stack">
    <div class="dark-scope plug" role="img" aria-label="Эскиз окна плагина: выбор пресета, клавиатура с названиями барабанов, список пресетов и микшер микрофонов с выходами">
      <div class="plug-top">{L("lockup-h-on-dark")}
        <div class="search"><span>⌕</span><b style="font-weight:500">Studio600 · Ahead Live</b></div>
        <div class="seg" aria-label="Выбор удара"><span>Как в Kontakt</span><span class="on">Живой</span></div>
      </div>
      <div class="kbd">{keys_html}</div>
      <div class="plug-body">
        <div style="display:grid;gap:12px;align-content:start">
          <div class="list">
            <div class="row sel"><b>Studio600 · Ahead Live</b><span class="tags"><span class="tag">base</span><span class="tag">cymbals</span></span></div>
            <div class="row"><b>Studio600 · Ahead Low</b><span class="tags"><span class="tag">base</span></span></div>
            <div class="row"><b>Studio600 · Ambasador Hi</b><span class="tags"><span class="tag">base</span><span class="tag">large</span></span></div>
          </div>
          <div class="list" style="padding:12px">{wave}<div class="hint" style="margin-top:6px">SN TOP · 0.82 s · 44.1 kHz · пик −3.4 dB</div></div>
        </div>
        <div style="display:grid;gap:10px;align-content:start">
          <div class="mixer">{mics_html}</div>
          <div class="alert" role="note"><b>!</b><span>OUT 7 и OUT 8 выключены в DAW: OVER 2 и ROOM MID играют в Main.</span></div>
          <div class="hint">Микрофон N → OUT N. Один удар играет всеми своими микрофонами сразу.</div>
        </div>
      </div>
    </div>
    <div class="cols">
      <div class="rule"><h3><span class="pillok">1</span> Горит только активное</h3><p class="muted">Заливка акцентом у звучащих клавиш, выбранного пресета и одного главного действия на экране. Остальное нейтральное.</p></div>
      <div class="rule"><h3><span class="pillok">2</span> Состояние видно без цвета</h3><p class="muted">Выход микрофона подписан (OUT 3), выключенный в DAW помечен текстом «→ MAIN» и пояснением внизу, выбранный пресет отличается плашкой. Цвет не единственный носитель смысла.</p></div>
      <div class="rule"><h3><span class="pillok">3</span> Часто нажимаемое не анимируем</h3><p class="muted">Клавиши и строки отвечают мгновенно. Анимация ≤ 150 мс и только для прозрачности и цвета.</p></div>
    </div>
  </div>
</section>

<!-- ======== ГОЛОС ======== -->
<section id="voice">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Голос</span>
  <h2>Коротко и по делу</h2>
  <p class="lead">Говорим как знающий коллега: прямо, без пафоса. Называем вещи так, как их называют барабанщик и звукорежиссёр.</p>
  <div class="stack">
    <div class="cols">
      <div class="panel"><h3>Да</h3><ul class="plain" style="margin-top:12px"><li>«Выбрать пресет», «Всё в Main», «Микрофоны по выходам».</li><li>«Загружено 6 из 8 микрофонов. Играть можно.»</li><li>«OUT 7 выключен в DAW. OVER 2 играет в Main.»</li></ul></div>
      <div class="panel"><h3>Нет</h3><ul class="plain" style="margin-top:12px"><li>«Произвести инициализацию мультиканальной конфигурации».</li><li>«Ой! Что-то пошло не так».</li><li>«Нейросеть магическим образом подберёт идеальный звук».</li></ul></div>
    </div>
    <ul class="plain">
      <li>Кнопка называет действие глаголом, после нажатия сообщение говорит о результате.</li>
      <li>Ошибка объясняет, что случилось и что делать. Без извинений.</li>
      <li>Термины оставляем как есть: kick, snare, overhead, room, velocity, choke.</li>
      <li>Словарь продукта: пресет, удар, микрофон, выход. «Сэмпл» только про отдельный файл.</li>
      <li>Единицы и числа с неразрывным пробелом: 1,84&nbsp;с, 44,1&nbsp;кГц, −1,2&nbsp;дБ.</li>
    </ul>
  </div>
</section>

<!-- ======== ФАЙЛЫ ======== -->
<section id="files">
  <div class="sdiv">{div_b}</div>
  <span class="eyebrow">Файлы</span>
  <h2>Что где лежит</h2>
  <div class="stack">
    <div class="tbl files"><table>
      <thead><tr><th>Файл</th><th>Зачем</th></tr></thead>
      <tbody>
        <tr><td><code>brand/logo/mark-*.svg</code></td><td>Знак: цветной, чёрный, белый, для тёмной темы, small</td></tr>
        <tr><td><code>brand/logo/lockup-h-*.svg · lockup-v-*.svg</code></td><td>Знак с надписью, горизонтальный и вертикальный</td></tr>
        <tr><td><code>brand/logo/icon-a*.svg · icon-b*.svg · icon-c.svg</code></td><td>Иконка приложения: A основная, B универсальная, C запасная. Суффиксы: small (мелкие размеры), square (iOS и maskable), mac (сетка macOS 1024)</td></tr>
        <tr><td><code>brand/logo/web/</code></td><td>favicon.ico и SVG, PNG-иконки, apple-touch, maskable, webmanifest, сниппет для head</td></tr>
        <tr><td><code>brand/logo/web/</code></td><td>favicon.ico, PNG-иконки, webmanifest</td></tr>
        <tr><td><code>brand/tokens.css · tokens.json</code></td><td>Цвета-роли для веба и для переноса в JUCE</td></tr>
        <tr><td><code>brand/build_*.py</code></td><td>Скрипты, которыми всё это собрано: поменяли палитру, пересобрали</td></tr>
      </tbody>
    </table></div>
    <p class="muted">Шрифты Geologica и JetBrains Mono распространяются по лицензии SIL OFL: их можно встраивать в плагин и в записку, указав лицензию. Надпись в логотипе переведена в контуры, шрифт для неё не нужен. Юридическую проверку названия и знака мы не проводили.</p>
  </div>
</section>

<footer>Samplit · брендбук v1.0 · ВКР Алексашина А.А., ВГТУ</footer>
</main>
'''
open("brandbook.html", "w", encoding="utf-8").write(html)
print(len(html))
