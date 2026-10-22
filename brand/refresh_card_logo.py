"""Re-embeds the current lockups into the project card header (run after build_logo.py)."""
import re

CARD = "Z:/!CCHGEU/ВКР/samplit-card.html"


def inline(fn, cls):
    x = open("logo/" + fn, encoding="utf-8").read()
    x = re.sub(r"<title>.*?</title>\n?", "", x)
    x = re.sub(r' width="[^"]*" height="[^"]*"', "", x, count=1)
    return x.replace("<svg ", f'<svg class="{cls}" ', 1).strip()


s = open(CARD, encoding="utf-8").read()
head = '<h1 class="brand" aria-label="Samplit">'
i0 = s.index(head) + len(head)
i1 = s.index("</h1>", i0)
s = s[:i0] + inline("lockup-h-color.svg", "sw-l") + inline("lockup-h-on-dark.svg", "sw-d") + s[i1:]
open(CARD, "w", encoding="utf-8").write(s)
print("card header refreshed")
