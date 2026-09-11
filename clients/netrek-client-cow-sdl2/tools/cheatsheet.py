#!/usr/bin/env python3
"""Build the printable Netrek COM command reference.

The key list is parsed out of the client's own help_message[] table, so the
sheet cannot drift from what the game actually does. Pass a .netrekrc and the
sheet is rewritten in that player's remapped keys instead of the defaults
(netrekrc keymap pairs are "newkey defaultkey", applied additively).

  tools/cheatsheet.py                       -> default keys, build/cheatsheet.pdf
  tools/cheatsheet.py --rc ~/.netrekrc      -> your layout
  tools/cheatsheet.py --rc ~/.netrekrc --title "Dave's layout" -o build/mine.pdf

Credentials in the .netrekrc (name, password, login) are never read.
"""
import argparse, html, pathlib, re, shutil, subprocess, sys

HERE = pathlib.Path(__file__).resolve().parent.parent

# Which section each default key belongs to. Keys absent here land in "Other".
SECTIONS = [
    ("Speed & course",   "0)!@%#<>k"),
    ("Weapons",          "ptfdD"),
    ("Defense",          "][usc{}R"),
    ("Tractor & pressor","Ty_^$"),
    ("Planets & armies", "bzxoer*"),
    ("Info & locking",   "iIl;NVB"),
    ("Windows",          "LPSh?wU/+,.\\`~M "),
    ("Messages & macros","mEFX:"),
    ("Session",          "Qq=&-|"),
]
MAC_KEYS = [("\u2318F", "Fullscreen on/off")]
RC_SAMPLE = """name:       yourname
password:   yourpassword
login:      yourlogin
server:     sturgeon.elevateconsulting.dev
port:       2592
tryShort:   off
agriCAPS:   on
showArmy:   on
autoSetWar: 1
autoQuit:   600
macroKey:   TAB
keymap:     qs
buttonmap:  1t2p3k
"""
MOUSE = [("Left button", "Fire photon torpedo"),
         ("Middle button", "Fire phaser"),
         ("Right button", "Set course")]

RETITLE = {"&": "Reread .netrekrc"}   # upstream help still says .xtrekrc

def collapse_digits(entries, aliases):
    """0-9 are ten identical "Set speed" rows in the client's own help; one row says it.
    A digit the player has remapped keeps its own row, or the remap would vanish."""
    if len([e for e in entries if e[0].isdigit()]) < 10:
        return entries
    out, done = [], False
    for k, d in entries:
        if k.isdigit() and k not in aliases:
            if not done:
                out.append(("0-9", "Set speed to that warp"))
                done = True
        elif k.isdigit():
            out.append((k, f"Set speed {int(k)}"))
        else:
            out.append((k, d))
    return out

def parse_help(src: pathlib.Path):
    """(key, description) in the order the client lists them."""
    body = re.search(r"char\s+\*help_message\[\]\s*=\s*\{(.*?)\n\s*0\s*\n\};",
                     src.read_text(), re.S)
    if not body:
        sys.exit(f"could not find help_message[] in {src}")
    out = []
    for raw in re.findall(r'"((?:[^"\\]|\\.)*)"', body.group(1)):
        entry = raw.replace('\\\\', '\\').replace('\\"', '"')
        key, _, desc = entry.partition("  ")          # two spaces separate them
        key, desc = key.strip(), desc.strip()
        if not key:                                    # the "(space)" entry
            key, desc = " ", desc.replace("(space) ", "")
        out.append((key, RETITLE.get(key, desc)))
    return out

def parse_rc(path: pathlib.Path):
    """keymap aliases {default_key: [new_keys]} and the buttonmap, from a .netrekrc."""
    aliases, buttons = {}, {}
    for line in path.read_text(errors="replace").splitlines():
        line = line.strip()
        if line.startswith("#") or ":" not in line:
            continue
        opt, _, val = line.partition(":")
        opt, val = opt.strip().lower(), val.strip()
        if opt == "keymap":
            for i in range(0, len(val) - 1, 2):
                aliases.setdefault(val[i + 1], []).append(val[i])
        elif opt == "buttonmap":
            for i in range(0, len(val) - 1, 2):
                buttons[val[i]] = val[i + 1]
    return aliases, buttons

def key_label(k):
    return {" ": "space", "\\": "\\"}.get(k, k)

def build_html(entries, aliases, buttons, title, subtitle):
    seen, sections = set(), []
    for name, keys in SECTIONS:
        rows = [(k, d) for k, d in entries if k[0] in keys]
        seen.update(k[0] for k, _ in rows)
        if rows:
            sections.append((name, rows))
    leftovers = [(k, d) for k, d in entries if k[0] not in seen]
    if leftovers:
        sections.append(("Other", leftovers))

    def chips(k):
        # A remapped key replaces the default in the printed sheet; the default
        # still works, so it is kept as a dimmed second chip.
        mapped = aliases.get(k[0], [])
        first = [f'<kbd>{html.escape(key_label(m))}{html.escape(k[1:])}</kbd>' for m in mapped]
        default = f'<kbd class="d">{html.escape(key_label(k[0]))}{html.escape(k[1:])}</kbd>'
        return "".join(first) + default if first else default

    body = []
    for name, rows in sections:
        body.append(f'<section><h2>{html.escape(name)}</h2><table>')
        for k, d in rows:
            body.append(f'<tr><td class="k">{chips(k)}</td><td>{html.escape(d)}</td></tr>')
        body.append("</table></section>")

    mouse = list(MOUSE)
    if buttons:
        inv = {}
        for default_key, new_keys in aliases.items():
            for nk in new_keys:
                inv[nk] = default_key
        by_key = {k[0]: d for k, d in entries}
        mouse = []
        for btn in sorted(buttons):
            bound = buttons[btn]
            action = by_key.get(inv.get(bound, bound), "unbound")
            names = {"1": "Left button", "2": "Middle button", "3": "Right button"}
            mouse.append((names.get(btn, f"Button {btn}"), action))
    body.append('<section><h2>Mouse</h2><table class="wide">')
    for label, action in mouse:
        body.append(f'<tr><td class="k"><kbd class="d">{html.escape(label)}</kbd></td>'
                    f'<td>{html.escape(action)}</td></tr>')
    body.append("</table></section>")

    body.append('<section><h2>Mac client</h2><table>')
    for k, d in MAC_KEYS:
        body.append(f'<tr><td class="k"><kbd class="d">{html.escape(k)}</kbd></td>'
                    f'<td>{html.escape(d)}</td></tr>')
    body.append("</table></section>")

    body.append('<section class="rc"><h2>Your ~/.netrekrc</h2>'
                f'<pre>{html.escape(RC_SAMPLE)}</pre>'
                '<p>One option per line. <b>keymap</b> takes pairs of '
                '<i>newkey defaultkey</i> and <b>buttonmap</b> pairs of <i>button key</i>; '
                'a remapped key is added, not moved, so the default keeps working. Press '
                '<kbd class="d">&amp;</kbd> in game to reread the file without restarting. '
                'Every option: netrek.elevateconsulting.dev/netrekrc.html</p></section>')

    note = ("Grey keys are the client defaults, which keep working alongside your remaps."
            if aliases else "Netrek COM \u00b7 native SDL2 client for macOS")
    return TEMPLATE.format(title=html.escape(title), subtitle=html.escape(subtitle),
                           body="\n".join(body), note=html.escape(note))

TEMPLATE = """<!doctype html><meta charset="utf-8"><title>{title}</title>
<style>
  @page {{ size: letter; margin: 12mm 10mm; }}
  * {{ box-sizing: border-box; }}
  body {{ margin:0; font:10.5px/1.35 -apple-system,BlinkMacSystemFont,"Helvetica Neue",Arial,sans-serif;
         color:#11181c; }}
  header {{ border-bottom:2px solid #11181c; padding-bottom:6px; margin-bottom:10px;
           display:flex; align-items:baseline; justify-content:space-between; gap:12px; }}
  h1 {{ font-size:19px; margin:0; letter-spacing:-.2px; }}
  .sub {{ font-size:10px; color:#5b6770; text-align:right; }}
  .cols {{ column-count:3; column-gap:9mm; }}
  section {{ break-inside:avoid; margin:0 0 9px; }}
  h2 {{ font-size:9.5px; text-transform:uppercase; letter-spacing:.1em; color:#0a6b3d;
       margin:0 0 3px; padding-bottom:2px; border-bottom:1px solid #d7dde1; }}
  table {{ width:100%; border-collapse:collapse; }}
  td {{ padding:1.1px 0; vertical-align:top; }}
  td.k {{ width:52px; white-space:nowrap; }}
  table.wide td.k {{ width:74px; }}
  .rc pre {{ margin:0 0 4px; padding:5px 6px; background:#f6f8f9; border:1px solid #e2e8eb;
            border-radius:4px; font:8.2px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;
            white-space:pre-wrap; }}
  .rc p {{ margin:0; font-size:9px; color:#5b6770; }}
  td+td {{ color:#2c3a42; }}
  kbd {{ display:inline-block; min-width:13px; padding:0 3px; margin-right:2px;
        border:1px solid #b9c3c9; border-bottom-width:2px; border-radius:3px;
        background:#fff; font:600 9.5px/1.5 ui-monospace,SFMono-Regular,Menlo,monospace;
        text-align:center; }}
  kbd.d {{ color:#6b7880; border-color:#dde3e7; background:#f6f8f9; font-weight:400; }}
  footer {{ margin-top:8px; padding-top:5px; border-top:1px solid #d7dde1;
           font-size:9px; color:#5b6770; display:flex; justify-content:space-between; }}
</style>
<header><h1>{title}</h1><div class="sub">{subtitle}</div></header>
<div class="cols">
{body}
</div>
<footer><span>netrek.elevateconsulting.dev</span><span>{note}</span></footer>
"""

def find_chrome():
    for c in ["/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
              "/Applications/Chromium.app/Contents/MacOS/Chromium",
              shutil.which("chromium"), shutil.which("google-chrome")]:
        if c and pathlib.Path(c).exists():
            return c
    sys.exit("no Chrome/Chromium found to render the PDF")

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--rc", type=pathlib.Path, help="a .netrekrc whose keymap to apply")
    ap.add_argument("-o", "--out", type=pathlib.Path,
                    default=HERE / "build/netrek-com-cheatsheet.pdf")
    ap.add_argument("--title", default="Netrek COM — Command Reference")
    ap.add_argument("--subtitle", default="default keys · sturgeon.elevateconsulting.dev")
    a = ap.parse_args()
    a.out = a.out.resolve()

    aliases, buttons = parse_rc(a.rc) if a.rc else ({}, {})
    entries = collapse_digits(parse_help(HERE.parent / "netrek-client-cow/helpwin.c"), aliases)
    page = build_html(entries, aliases, buttons, a.title, a.subtitle)

    a.out.parent.mkdir(parents=True, exist_ok=True)
    src = a.out.with_suffix(".html")
    src.write_text(page)
    subprocess.run([find_chrome(), "--headless", "--disable-gpu", "--no-pdf-header-footer",
                    f"--print-to-pdf={a.out}", src.as_uri()],
                   check=True, capture_output=True)
    print(f"{len(entries)} commands -> {a.out}")

if __name__ == "__main__":
    main()
