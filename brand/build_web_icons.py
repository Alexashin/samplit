"""Web / PWA icon set from the main icon (A). Run after build_logo.py, from the brand/ folder.

Needs a PNG renderer script (render_png.py from the logo-design skill): pass its path in the
SAMPLIT_RENDER_PNG environment variable or as the first argument."""
import json
import os
import shutil
import subprocess
import sys

RENDER = (sys.argv[1] if len(sys.argv) > 1 else os.environ.get("SAMPLIT_RENDER_PNG")
          or os.path.expanduser("~/.claude/skills/logo-design/scripts/render_png.py"))
if not os.path.isfile(RENDER):
    sys.exit(f"render_png.py not found: {RENDER}\nPass the path as an argument or set SAMPLIT_RENDER_PNG.")
OUT = "logo/web"
os.makedirs(OUT, exist_ok=True)


def render(src, out, size):
    subprocess.run([sys.executable, RENDER, f"logo/{src}", "-o", f"{OUT}/{out}", "--size", str(size)],
                   check=True, stdout=subprocess.DEVNULL)


shutil.copy("logo/icon-a-small.svg", f"{OUT}/favicon.svg")
subprocess.run([sys.executable, RENDER, "logo/icon-a-small.svg", "--ico", f"{OUT}/favicon.ico", "--ico-sizes", "16", "32", "48"],
               check=True, stdout=subprocess.DEVNULL)
for n in (16, 32, 48):
    render("icon-a-small.svg", f"favicon-{n}.png", n)
render("icon-a-square.svg", "apple-touch-icon.png", 180)
render("icon-a.svg", "icon-192.png", 192)
render("icon-a.svg", "icon-512.png", 512)
render("icon-a-square.svg", "maskable-512.png", 512)
render("icon-a-mac.svg", "icon-mac-1024.png", 1024)

json.dump({
    "name": "Samplit", "short_name": "Samplit", "theme_color": "#314bff", "background_color": "#eef0f4",
    "icons": [
        {"src": "icon-192.png", "sizes": "192x192", "type": "image/png"},
        {"src": "icon-512.png", "sizes": "512x512", "type": "image/png"},
        {"src": "maskable-512.png", "sizes": "512x512", "type": "image/png", "purpose": "maskable"},
    ],
}, open(f"{OUT}/site.webmanifest", "w", encoding="utf-8"), indent=2)

open(f"{OUT}/head-snippet.html", "w", encoding="utf-8").write(
    '<link rel="icon" href="/favicon.ico" sizes="48x48">\n'
    '<link rel="icon" href="/favicon.svg" type="image/svg+xml">\n'
    '<link rel="apple-touch-icon" href="/apple-touch-icon.png">\n'
    '<link rel="manifest" href="/site.webmanifest">\n'
    '<meta name="theme-color" content="#314bff">\n')
print(sorted(os.listdir(OUT)))
