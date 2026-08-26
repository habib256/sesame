#!/usr/bin/env python3
# =============================================================================
#  run_zexall.py — validation du cœur Z80 par l'exerciseur ZEXDOC/ZEXALL
#  (port SMS de Maxim Zhao, sortie SDSC). Lance sesame-headless avec --sdsc,
#  capture la console et vérifie que chaque test se termine par « OK » et que
#  la bannière de fin apparaît.
#
#  Usage : python3 tools/run_zexall.py [zexdoc|zexall|all]   (défaut : zexdoc)
#  Prérequis : python3 tools/fetch_zexall.py (téléchargement des ROM),
#  build/sesame-headless compilé.
#
#  Durée indicative (Apple Silicon) : ZEXDOC ~30 s, ZEXALL ~1 min.
# =============================================================================
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADLESS = ROOT / "build" / "sesame-headless"

# Le headless s'arrête dès la bannière de fin (--exit-sdsc) ; ce plafond ne
# sert que de garde-fou si l'exerciseur ne termine jamais (~2 h de temps
# émulé NTSC).
FRAMES = 1500000


def run_one(rom: pathlib.Path) -> bool:
    print(f"=== {rom.name} ===")
    proc = subprocess.run(
        [str(HEADLESS), str(rom), "--frames", str(FRAMES),
         "--exit-sdsc", "Tests complete"],
        capture_output=True, text=True)
    out = proc.stdout

    ok_count = sum(1 for line in out.splitlines() if line.endswith(" OK"))
    # Une CRC fausse est signalée par des lignes « expected/found » (et le
    # test ne se termine pas par OK).
    errors = [l for l in out.splitlines()
              if "expected" in l or "ERROR" in l.upper()]
    complete = "Tests complete" in out

    for line in errors:
        print(f"  [FAIL] {line.strip()}")
    print(f"  tests OK : {ok_count}")
    if not complete:
        print("  [FAIL] completion banner not found "
              "(crash, boucle infinie ou FRAMES trop bas ?)")
        # Aide au diagnostic : dernière ligne émise.
        tail = out.strip().splitlines()
        if tail:
            print(f"  last output: {tail[-1]}")
    passed = complete and not errors
    print(f"  => {'PASS' if passed else 'FAIL'}")
    return passed


def main() -> int:
    which = sys.argv[1] if len(sys.argv) > 1 else "zexdoc"
    names = {"zexdoc": ["zexdoc.sms"], "zexall": ["zexall.sms"],
             "all": ["zexdoc.sms", "zexall.sms"]}.get(which)
    if names is None:
        print(f"usage: {sys.argv[0]} [zexdoc|zexall|all]", file=sys.stderr)
        return 2
    if not HEADLESS.is_file():
        print("error: build/sesame-headless not found (build first)",
              file=sys.stderr)
        return 1

    ok = True
    for name in names:
        rom = ROOT / "roms" / name
        if not rom.is_file():
            print(f"error: {rom} missing — run tools/fetch_zexall.py first",
                  file=sys.stderr)
            return 1
        ok &= run_one(rom)

    print("SUCCESS: Z80 core passes" if ok else "FAILURE: see above")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
