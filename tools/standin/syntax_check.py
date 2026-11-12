#!/usr/bin/env python3
"""Compile (at -O0, objects discarded) every shadPS4 src/ translation unit from a
compile_commands.json. A real compile is used instead of -fsyntax-only because
zig's driver needs an output file and because inline-asm errors only appear at
code generation.

Usage: syntax_check.py <build_dir> <out_dir> [jobs]
Writes <out_dir>/results.tsv (status, seconds, file) and one .log per failing file.
"""
import json, os, shlex, subprocess, sys, time, hashlib
from concurrent.futures import ThreadPoolExecutor

build, out = sys.argv[1], sys.argv[2]
jobs = int(sys.argv[3]) if len(sys.argv) > 3 else 2
os.makedirs(out, exist_ok=True)
cmds = json.load(open(os.path.join(build, "compile_commands.json")))
units = [c for c in cmds if "/core/shadPS4/src/" in c["file"]]
done = set()
res_path = os.path.join(out, "results.tsv")
if os.path.exists(res_path):
    done = {l.rstrip("\n").split("\t")[2] for l in open(res_path)}

def run(c):
    f = c["file"]
    if f in done:
        return
    args = shlex.split(c["command"])
    cleaned, skip = [], False
    for a in args:
        if skip:
            skip = False
            continue
        if a in ("-o", "-MF", "-MT"):
            skip = True
            continue
        if a == "-MD" or a.startswith("-O"):
            continue
        cleaned.append(a)
    obj = os.path.join(out, "obj_" + hashlib.sha1(f.encode()).hexdigest()[:12] + ".o")
    cleaned += ["-O0", "-o", obj, "-ferror-limit=0", "-Wno-everything"]
    t = time.time()
    p = subprocess.run(cleaned, cwd=c["directory"], capture_output=True, text=True)
    dt = time.time() - t
    status = "OK" if p.returncode == 0 else "FAIL"
    if os.path.exists(obj):
        os.remove(obj)
    if status == "FAIL":
        name = hashlib.sha1(f.encode()).hexdigest()[:10] + "_" + os.path.basename(f) + ".log"
        open(os.path.join(out, name), "w").write(f + "\n" + p.stderr)
    with open(res_path, "a") as r:
        r.write(f"{status}\t{dt:.1f}\t{f}\n")

print(len(units), "units,", len(done), "already done", flush=True)
with ThreadPoolExecutor(jobs) as ex:
    list(ex.map(run, units))
