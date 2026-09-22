#!/usr/bin/env python3
"""Render frames exported by the production UiView host tests into a standalone preview.

PSU_UI_SNAPSHOTS=/tmp/r4850-ui python3 tools/test_host.py
python3 tools/render_ui.py /tmp/r4850-ui docs/ui-preview.html

Run a mega2560 build first to install the pinned Adafruit GFX bitmap font.
"""
import argparse
import json
import re
from pathlib import Path

root = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("frames", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
source = (root / ".pio/libdeps/mega2560/Adafruit GFX Library/glcdfont.c").read_text()
source = re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)
body = source.split("font[] PROGMEM = {", 1)[1].split("}", 1)[0]
font = [int(n, 16) for n in re.findall(r"0x[0-9a-fA-F]+", body)]
assert len(font) == 1280
frames = {}
for name in ["groups", "groups-page2", "chargers", "details", "config", "confirmation"]:
    lines = (args.frames / (name + ".txt")).read_text().splitlines()
    assert len(lines) == 16 and all(len(line) == 26 for line in lines[:15]) and len(lines[15]) == 390
    frames[name] = {"rows": lines[:15], "styles": lines[15]}

html = """<!doctype html>
<!-- Bitmap font license:
__FONT_LICENSE__
-->
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Parallel PSU controller — implemented UI</title>
<link rel="icon" href="data:image/svg+xml,%3Csvg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 32 32'%3E%3Crect width='32' height='32' fill='%23213d2b'/%3E%3Cpath d='M6 9h20M6 16h20M6 23h20' stroke='%238cdbb2' stroke-width='3'/%3E%3C/svg%3E">
<style>
:root{--bg:#f2f5f1;--surface:#fff;--border:#c8d5ca;--text:#192b22;--text-dim:#52665a;--accent:#176843;--warn:#95561a}
@media(prefers-color-scheme:dark){:root{--bg:#101a16;--surface:#192b22;--border:#4c6253;--text:#eaf4ed;--text-dim:#afc0b4;--accent:#8cdbb2;--warn:#f6bd73}}
*{box-sizing:border-box}body{background:var(--bg);color:var(--text);font:16px/1.6 'DejaVu Sans',sans-serif;margin:0}main{max-width:850px;margin:auto;padding:34px 24px}h1{font-size:30px;line-height:1.2;letter-spacing:-.8px;margin:0 0 20px}p{max-width:70ch}nav{display:flex;flex-wrap:wrap;gap:8px;margin:22px 0}button{padding:10px 14px;font:inherit;border:1px solid var(--border);background:var(--surface);color:var(--text);cursor:pointer}button[aria-pressed=true]{background:var(--accent);color:var(--bg)}button:focus-visible,a:focus-visible{outline:3px solid var(--warn);outline-offset:3px}figure{margin:0}canvas{display:block;width:640px;max-width:100%;height:auto;image-rendering:pixelated;border:8px solid #26392d}figcaption{color:var(--text-dim);font-size:14px;margin-top:12px}a{color:var(--accent)}pre{overflow:auto;font:13px/1.4 'DejaVu Sans Mono',monospace;background:var(--surface);padding:16px}details{margin-top:28px}small{color:var(--text-dim)}
</style></head><body><main>
<h1>Parallel PSU controller · local UI</h1>
<p>These frames come from the implemented layout exercised by host tests. The display is 320×240 pixels; the preview is enlarged. Example readings are simulated, not measurements from hardware.</p>
<nav aria-label="Preview screen"></nav>
<figure><canvas width="320" height="240" role="img" aria-label="Firmware display preview"></canvas>
<figcaption>Three group columns, two chargers per detail page, shared V/A/W header. Selection uses a coloured background; errors also have text. Requested current is the authorized profile; PENDING includes missing acknowledgements.</figcaption></figure>
<p><strong>Controls:</strong> turn to select, click to open/edit/apply, hold to configure a group or return. A flashing value indicates editing; hold cancels the uncommitted edit. This preview switches between captured frames and does not simulate commands.</p>
<p><a href="LOCAL_UI.md">Wiring, controls, commissioning and failure behavior</a></p>
<details><summary>Accessible text of the selected frame</summary><pre id="text"></pre></details>
<p><small>Generated with tools/render_ui.py. Glyphs: pinned Adafruit GFX classic bitmap font (see the library's BSD license and attribution). Layout and RGB565 colours match the firmware.</small></p>
</main><script>
const frames=__FRAMES__, font=__FONT__;
const palette=[0xefbd,0xadf5,0x6f2f,0xfdcd,0xfa69,0x8ef6];
function rgb(v){return 'rgb('+[(v>>11)*255/31,((v>>5)&63)*255/63,(v&31)*255/31].map(Math.round).join(',')+')'}
const c=document.querySelector('canvas'),ctx=c.getContext('2d');
const labels={'groups':'Groups','groups-page2':'Groups · page 2','chargers':'Chargers','details':'Error details','config':'Group config','confirmation':'Missing member'};
function show(name){const frame=frames[name];ctx.fillStyle=rgb(0x10c2);ctx.fillRect(0,0,320,240);
for(let r=0;r<15;r++)for(let col=0;col<26;col++){const style=parseInt(frame.styles[r*26+col],16),x=4+col*12,y=r*16,ch=frame.rows[r].charCodeAt(col);ctx.fillStyle=rgb(style&8?0x21e5:0x10c2);ctx.fillRect(x,y,12,16);ctx.fillStyle=rgb(palette[style&7]||palette[0]);for(let gx=0;gx<5;gx++)for(let gy=0;gy<8;gy++)if(font[ch*5+gx]&(1<<gy))ctx.fillRect(x+gx*2,y+gy*2,2,2)}
document.querySelectorAll('button').forEach(b=>b.setAttribute('aria-pressed',String(b.dataset.name===name)));c.setAttribute('aria-label',labels[name]+': '+frame.rows.join('; '));document.getElementById('text').textContent=frame.rows.join('\\n')}
for(const name of Object.keys(frames)){const b=document.createElement('button');b.textContent=labels[name];b.dataset.name=name;b.type='button';b.addEventListener('click',()=>show(name));document.querySelector('nav').appendChild(b)}show('groups');
</script></body></html>
"""
license_text = (root / ".pio/libdeps/mega2560/Adafruit GFX Library/license.txt").read_text()
args.output.write_text(html.replace("__FRAMES__", json.dumps(frames)).replace("__FONT__", json.dumps(font)).replace("__FONT_LICENSE__", license_text))
print(args.output)
