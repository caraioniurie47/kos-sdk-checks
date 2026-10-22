"""For each FINDINGS.md finding: the files its "Check:" paragraph names, and whether each exists (a script under
src/ or host/; an output under results/ or results/2.0.0.179/). "Ran" is derived from the output files being
present on disk, not from a hard-coded list of check names.
  py -I -X utf8 coverage-check.py <FINDINGS.md>
"""
import io
import os
import re
import sys

sys.stdout.reconfigure(encoding="utf-8")
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
LINK_RE = re.compile(r"\[`?([^\]`]+)`?\]\(([^)\s]+)\)")


def check_paragraph(section):
    lines = section.splitlines()
    out = []
    started = False
    for line in lines:
        if line.startswith("Check:"):
            started = True
        if started:
            out.append(line)
            if not line.strip() and out:
                break
    return "\n".join(out)


def main():
    path = sys.argv[1]
    text = io.open(path, encoding="utf-8").read()
    parts = re.split(r"^### ", text, flags=re.M)[1:]
    missing = 0
    external = 0
    for p in parts:
        title = p.splitlines()[0]
        fid = title.split(".")[0]
        if not re.match(r"^[BEUMP]\d+$", fid):
            continue
        para = check_paragraph(p)
        if not para:
            print(f"??  {fid:<4} {title[len(fid) + 2:][:45]:<45} no Check: paragraph")
            missing += 1
            continue
        if "github.com" in para and not re.search(r"\(src/|\(host/", para):
            print(f"OK  {fid:<4} {title[len(fid) + 2:][:45]:<45} external repository (not checked here)")
            external += 1
            continue
        scripts, outputs_main, outputs_20 = [], [], []
        for _, target in LINK_RE.findall(para):
            if target.startswith("src/") or target.startswith("host/"):
                scripts.append(target)
            elif target.startswith("results/2.0.0.179/"):
                outputs_20.append(target)
            elif target.startswith("results/"):
                outputs_main.append(target)
        missing_files = [t for t in scripts + outputs_main + outputs_20
                          if not os.path.exists(os.path.join(ROOT, t))]
        ran_20 = bool(outputs_20) and not any(t in missing_files for t in outputs_20)
        flag = "OK " if scripts and not missing_files else "?? "
        if flag == "?? ":
            missing += 1
        note = ""
        if not outputs_20 and outputs_main:
            note = "  NO 2.0.0.179 OUTPUT"
        if missing_files:
            note += "  MISSING: " + ", ".join(missing_files)
        print(f"{flag}{fid:<4} {title[len(fid) + 2:][:45]:<45} "
              f"scripts={','.join(scripts) or '-'} ran2.0={ran_20}{note}")
    print(f"findings with a problem: {missing} (plus {external} external, reported not failed)")
    sys.exit(1 if missing else 0)


if __name__ == "__main__":
    main()
