#!/usr/bin/env python3
# Copyright (C) 2026 VERHILLE Arnaud
# SPDX-License-Identifier: GPL-3.0-or-later
# Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
# (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

# =============================================================================
#  fetch_zexall.py — télécharge les ROM ZEXDOC/ZEXALL (port SMS de Maxim Zhao,
#  sortie sur la console SDSC) dans roms/. Œuvre tierce sous GPLv2, non
#  vendorée dans ce dépôt : on la récupère à la demande depuis les
#  releases GitHub, avec vérification SHA-256.
#
#  Usage : python3 tools/fetch_zexall.py
#  Sortie : roms/zexdoc.sms et roms/zexall.sms (no-op si déjà présents
#  et conformes).
# =============================================================================
import hashlib
import io
import pathlib
import sys
import urllib.request
import zipfile

VERSION = "0.21"
URL = (f"https://github.com/maxim-zhao/zexall-sms/releases/download/"
       f"v{VERSION}/ZEXALL-SMS-{VERSION}.zip")
# SHA-256 du zip de la release v0.21 (vérifié lors de l'intégration).
ZIP_SHA256 = "704e39402516fba832b923395c7579b617896e1f00d2f036696342ab4b5fde7c"

ROMS = ("zexdoc.sms", "zexall.sms")


def main() -> int:
    roms_dir = pathlib.Path(__file__).resolve().parent.parent / "roms"
    roms_dir.mkdir(exist_ok=True)

    if all((roms_dir / r).is_file() for r in ROMS):
        print(f"fetch_zexall: ROMs already present in {roms_dir}")
        return 0

    print(f"fetch_zexall: downloading {URL}")
    try:
        with urllib.request.urlopen(URL, timeout=60) as resp:
            data = resp.read()
    except OSError as e:
        print(f"fetch_zexall: download failed: {e}", file=sys.stderr)
        return 1

    digest = hashlib.sha256(data).hexdigest()
    if digest != ZIP_SHA256:
        print(f"fetch_zexall: SHA-256 mismatch: got {digest}", file=sys.stderr)
        return 1

    with zipfile.ZipFile(io.BytesIO(data)) as z:
        for rom in ROMS:
            with z.open(rom) as src:
                (roms_dir / rom).write_bytes(src.read())
            print(f"fetch_zexall: wrote {roms_dir / rom}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
