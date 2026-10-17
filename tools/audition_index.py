#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Writes <dir>/index.html from <dir>/summary.json (made by build/host/audition <dir>): one table per machine with an
audio player, name, category, the loudness / DC figures (failing ones marked), a 1-5 rating kept in localStorage and an
"export ratings" button that downloads JSON {machine: {preset: rating}}.  Opens from file://, no server.
  usage: tools/audition_index.py [DIR]      (default build/audition)"""
import html
import json
import os
import sys

MACHINE_NOTE = {
    "DRIFT": "DRIFT is mono (one voice, by decision): the 4-note chord on its KEYS / STRINGS / ATMOS presets sounds as a single note.",
    "ACID": "ACID plays the factory ACID pattern (accents and slides) for every preset.",
}
FLAG_TEXT = {"PEAK": "peak over -1 dBFS", "RMS_LOW": "RMS under -24 dBFS", "RMS_HIGH": "RMS over -10 dBFS",
             "DC": "DC over 1 %", "SILENT": "silent"}

CSS = """
:root{color-scheme:dark;--bg:#0b0c0e;--panel:#14161a;--line:#2a2d35;--fg:#dfe3ea;--dim:#8b92a0;--bad:#ff6b6b;--ok:#4cff91;--acc:#f5a623}
body{background:var(--bg);color:var(--fg);font:14px/1.4 -apple-system,Helvetica,Arial,sans-serif;margin:0;padding:16px 16px 64px}
h1{font-size:20px;margin:0 0 4px}h2{font-size:16px;margin:28px 0 4px;color:var(--acc)}
p{color:var(--dim);margin:4px 0 8px}
table{border-collapse:collapse;width:100%;background:var(--panel)}
th,td{padding:5px 8px;border-bottom:1px solid var(--line);text-align:left;vertical-align:middle}
th{color:var(--dim);font-weight:600;font-size:12px;position:sticky;top:0;background:var(--panel)}
td.num{font-variant-numeric:tabular-nums;white-space:nowrap}
tr.fail td.name{color:var(--bad)}
.bad{color:var(--bad)}.flag{display:inline-block;border:1px solid var(--bad);color:var(--bad);border-radius:3px;padding:0 4px;margin-right:4px;font-size:11px}
audio{height:30px;width:260px;max-width:100%}
select{background:var(--bg);color:var(--fg);border:1px solid var(--line);padding:3px}
button{background:var(--acc);color:#111;border:0;border-radius:4px;padding:7px 12px;font-weight:600;cursor:pointer}
.bar{position:fixed;left:0;right:0;bottom:0;background:var(--panel);border-top:1px solid var(--line);padding:8px 16px;display:flex;gap:12px;align-items:center}
@media (max-width:700px){table{display:block;overflow-x:auto}}
"""

JS = """
const KEY='fm1four.audition.ratings.v1';
let ratings={};
try{ratings=JSON.parse(localStorage.getItem(KEY)||'{}')||{};}catch(e){ratings={};}
function save(){try{localStorage.setItem(KEY,JSON.stringify(ratings));}catch(e){}}
function count(){const n=Object.values(ratings).reduce((a,m)=>a+Object.keys(m).length,0);
  document.getElementById('count').textContent=n+' rated';}
document.querySelectorAll('select.rate').forEach(s=>{
  const m=s.dataset.machine,p=s.dataset.preset;
  if(ratings[m]&&ratings[m][p])s.value=String(ratings[m][p]);
  s.addEventListener('change',()=>{
    if(!ratings[m])ratings[m]={};
    if(s.value==='')delete ratings[m][p];else ratings[m][p]=Number(s.value);
    if(!Object.keys(ratings[m]).length)delete ratings[m];
    save();count();});
});
document.getElementById('export').addEventListener('click',()=>{
  const blob=new Blob([JSON.stringify(ratings,null,2)],{type:'application/json'});
  const a=document.createElement('a');a.href=URL.createObjectURL(blob);a.download='fm1four-ratings.json';
  document.body.appendChild(a);a.click();a.remove();setTimeout(()=>URL.revokeObjectURL(a.href),1000);});
document.addEventListener('play',e=>{document.querySelectorAll('audio').forEach(a=>{if(a!==e.target)a.pause();});},true);
count();
"""


def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "build/audition"
    with open(os.path.join(d, "summary.json")) as f:
        data = json.load(f)
    lim = data["limits"]
    machines = []
    for p in data["presets"]:
        if p["machine"] not in machines:
            machines.append(p["machine"])
    out = ["<!doctype html><html lang=en><head><meta charset=utf-8><meta name=viewport content='width=device-width,initial-scale=1'>",
           "<title>VOID FOUR audition</title><style>%s</style></head><body>" % CSS,
           "<h1>VOID FOUR preset audition</h1>",
           "<p>%d presets, %d s each, %d Hz stereo, rendered through the device's mix path. Checks: peak at most %g dBFS, "
           "RMS (sounding part) %g..%g dBFS, DC at most %g %%, not silent. Failing figures are marked; ratings are kept in this browser.</p>"
           % (len(data["presets"]), data["seconds"], data["sample_rate"], lim["peak_db_max"], lim["rms_db_min"],
              lim["rms_db_max"], lim["dc_pct_max"])]
    for m in machines:
        rows = [p for p in data["presets"] if p["machine"] == m]
        nf = sum(1 for p in rows if p["fail"])
        out.append("<h2>%s</h2><p>%d presets, %d failing the checks. %s</p>" % (html.escape(m), len(rows), nf,
                                                                              html.escape(MACHINE_NOTE.get(m, ""))))
        out.append("<table><tr><th>#<th>Name<th>Category<th>Played<th>Audio<th>Peak dBFS<th>RMS dBFS<th>DC %<th>Checks<th>Rating</tr>")
        for p in rows:
            fl = set(p["fail"])
            def cell(v, bad, fmt):
                return "<td class='num%s'>%s" % (" bad" if bad else "", fmt % v)
            out.append("<tr class='%s'><td>%d<td class=name>%s<td>%s<td>%s<td><audio controls preload=none src=\"%s\"></audio>%s%s%s<td>%s<td>"
                       "<select class=rate data-machine=\"%s\" data-preset=\"%s\"><option value=''>-<option>1<option>2<option>3<option>4<option>5</select></tr>"
                       % ("fail" if fl else "ok", p["index"], html.escape(p["name"]), html.escape(p["category"]),
                          html.escape(p["phrase"]), html.escape(p["file"], quote=True),
                          cell(p["peak_db"], "PEAK" in fl, "%.1f"),
                          cell(p["rms_db"], bool(fl & {"RMS_LOW", "RMS_HIGH", "SILENT"}), "%.1f"),
                          cell(p["dc_pct"], "DC" in fl, "%.3f"),
                          "".join("<span class=flag>%s</span>" % html.escape(FLAG_TEXT.get(x, x)) for x in p["fail"])
                          or "<span style='color:var(--ok)'>ok</span>",
                          html.escape(m, quote=True), html.escape(p["name"], quote=True)))
        out.append("</table>")
    out.append("<div class=bar><button id=export>Export ratings</button><span id=count></span></div>")
    out.append("<script>%s</script></body></html>" % JS)
    path = os.path.join(d, "index.html")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print("wrote", path)


if __name__ == "__main__":
    main()
