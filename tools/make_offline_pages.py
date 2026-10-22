"""Собирает офлайн-копии страниц проекта (карточка, брендбук, обзор рынка) для показа без интернета.

Шрифты и mermaid берутся из OUT/assets (скачиваются заранее), задачи карточки вшиваются снимком из JSON.

    python tools/make_offline_pages.py OUT_DIR TASKS_JSON_DIR
"""
import json
import re
import sys
from datetime import date
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
CARD = Path("Z:/!CCHGEU/ВКР/samplit-card.html")
PAGES = {
    "Карточка_проекта.html": CARD,
    "Брендбук.html": REPO / "brand" / "brandbook.html",
    "Обзор_рынка.html": REPO / "docs" / "research" / "market.html",
}

FONTS = [("Geologica", "geologica", (400, 500, 700, 800)), ("JetBrains Mono", "jetbrains-mono", (400, 600))]
RANGES = {
    "latin": "U+0000-00FF, U+0131, U+0152-0153, U+02BB-02BC, U+02C6, U+02DA, U+02DC, U+2000-206F, U+2074, U+20AC, U+2122, U+2191, U+2193, U+2212, U+2215, U+FEFF, U+FFFD",
    "cyrillic": "U+0301, U+0400-045F, U+0490-0491, U+04B0-04B1, U+2116",
}


def fonts_css():
    rules = []
    for family, slug, weights in FONTS:
        for w in weights:
            for subset, rng in RANGES.items():
                rules.append(f"@font-face {{ font-family: '{family}'; font-style: normal; font-weight: {w}; font-display: swap; "
                             f"src: url('fonts/{slug}-{subset}-{w}.woff2') format('woff2'); unicode-range: {rng}; }}")
    return "\n".join(rules)


def skeleton(title_html_body):
    return ('<!doctype html>\n<html lang="ru">\n<head>\n<meta charset="utf-8">\n'
            '<meta name="viewport" content="width=device-width, initial-scale=1">\n'
            '<link rel="stylesheet" href="assets/fonts.css">\n'
            # То же, что платформа артефактов добавляет к каждой странице
            '<style>body{margin:0}img{max-width:100%}[hidden]{display:none!important}</style>\n'
            '</head>\n<body>\n' + title_html_body + "\n</body>\n</html>\n")


def strip_remote_fonts(html):
    html = re.sub(r'<link rel="preconnect"[^>]*>\n?', "", html)
    return re.sub(r'<link rel="stylesheet" href="https://fonts\.googleapis\.com[^"]*">\n?', "", html)


def db_shim(tasks):
    # Подмена базы артефакта: задачи из снимка, правки живут только до перезагрузки страницы
    return """<script>
(function () {
  const docs = new Map(%s.map(t => [t.id, t.data]));
  const listeners = [];
  const snap = () => ({ docs: [...docs].map(([id, data]) => ({ id, data: () => data })) });
  const notify = () => listeners.forEach(cb => cb(snap()));
  let seq = 0;
  const db = {
    collection: () => ({
      onSnapshot (cb) { listeners.push(cb); setTimeout(() => cb(snap()), 0); },
      add: async (data) => { docs.set("local" + (++seq), data); notify(); },
      doc: (id) => ({
        update: async (patch) => { docs.set(id, Object.assign({}, docs.get(id), patch)); notify(); },
        delete: async () => { docs.delete(id); notify(); }
      })
    })
  };
  window.claude = { use: async (name) => name === "db" ? db : name === "user" ? { can: async () => true } : null };
})();
</script>
""" % json.dumps(tasks, ensure_ascii=False)


def mermaid_init():
    return """<script src="assets/mermaid.min.js"></script>
<script>
  mermaid.initialize({ startOnLoad: true, securityLevel: "strict",
    theme: matchMedia("(prefers-color-scheme: dark)").matches ? "dark" : "neutral" });
  // Ссылка с якорем (#tasks) открывает вкладку, но браузер успевает прокрутить к ещё скрытому разделу
  addEventListener("load", () => scrollTo(0, 0));
</script>
"""


def banner(text):
    return ('<div style="position:sticky;top:0;z-index:20;background:#1b295a;color:#eef0f4;'
            'font:500 13px/1.4 Geologica,Segoe UI,sans-serif;padding:6px 16px;text-align:center">' + text + "</div>\n")


def main():
    out = Path(sys.argv[1])
    tasks_dir = Path(sys.argv[2])
    (out / "assets").mkdir(parents=True, exist_ok=True)
    (out / "assets" / "fonts.css").write_text(fonts_css(), encoding="utf-8")

    tasks = [{"id": p.stem, "data": json.loads(p.read_text(encoding="utf-8"))} for p in sorted(tasks_dir.glob("*.json"))]
    stamp = date.today().strftime("%d.%m.%Y")

    for name, src in PAGES.items():
        html = strip_remote_fonts(src.read_text(encoding="utf-8"))
        if src == CARD:
            html = banner(f"Офлайн-копия от {stamp}. Задачи сняты с карточки на этот день, отметки здесь не сохраняются") \
                   + db_shim(tasks) + html
            # Скрипт карточки ставит обработчики сразу, mermaid подключаем в конец
            html += mermaid_init()
        (out / name).write_text(skeleton(html), encoding="utf-8")
        print("wrote", name)


if __name__ == "__main__":
    main()
