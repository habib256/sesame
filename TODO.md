# TODO — travaux ouverts

Uniquement l'ouvert. Le réalisé est dans [`CHANGELOG.md`](CHANGELOG.md)
(historique) et [`docs/IMPLEMENTED.md`](docs/IMPLEMENTED.md) (état par puce).
Les phases 1-5 viennent de l'audit d'architecture du 2026-08-27 (constats
vérifiés dans le code, références `fichier:ligne`). Les entrées `[R]`
viennent de [`docs/RECHERCHE-EMULATEURS.md`](docs/RECHERCHE-EMULATEURS.md).

## Phase 1 — Bugs confirmés

Fait (2026-08-27) — voir [`CHANGELOG.md`](CHANGELOG.md).

## Phase 2 — CI & durcissement de la suite (~1 jour)

Tout est déjà prêt (ROMs générées, étalons commités, headless sans GLFW,
ZEXALL à SHA épinglé) ; corriger d'abord le hang `compare_ppm` (Phase 1).

- [ ] Workflow GitHub Actions Linux : build gcc + clang `-Werror` puis
      `run_selftests.py` (~2 s) ; job ASan/UBSan sur la même suite ;
      build macOS (compilation du GUI — rien ne le garantit aujourd'hui) ;
      nightly : ZEXALL complet + `tools/build_wasm.sh`. Le premier run
      Linux validera que les étalons PPM sont bit-exacts hors Apple Silicon
      (seul vrai risque de portabilité du cœur)
- [ ] CMake : `enable_testing()` + `add_test` sur `run_selftests.py`,
      `option(SESAME_SANITIZE)` (-fsanitize=address,undefined), flags par
      `CXX_COMPILER_ID` (MSVC ≠ `-Wall;-Wextra`), `-Wshadow`, `install()`,
      `stdc++fs` conditionnel (GCC 8)
- [ ] `run_selftests.py` : `timeout=` + `try/except TimeoutExpired` sur
      TOUS les subprocess (générateurs et `compare_ppm` compris) ;
      assertions positives `OK 1`…`OK 5` (aujourd'hui seule l'absence de
      « FAIL » est vérifiée : une ROM qui plante après le test 2 passe) ;
      `--rewind-check` sur `vdptest.sms` (animée — sur l'image figée de
      `selftest.sms`, un rewind no-op passe) ; `tempfile` au lieu de `/tmp`
      en dur ; assertion `len(results) == EXPECTED`
- [ ] `run_zexall.py` : `assert ok_count == 79` (compté mais jamais
      comparé) + `timeout` ; `fetch_zexall.py` : vérifier le SHA des ROM
      déjà en place (le commentaire le promet, le code ne le fait pas)

## Phase 3 — Couverture de test (2-3 jours, après la Phase 2)

Zéro check aujourd'hui sur : audio (PSG + YM2413), Game Gear, save-states
fichier, Config, PAL, Light Phaser, GUI. Par rentabilité décroissante :

- [ ] Check WAV avec checksum dans la suite (`--wav` existe déjà) —
      première couverture du PSG et du YM2413 ; un `--check` en diff sur
      `make_blip_table.py` vs le `.inc` commité
- [ ] Aller-retour save-state fichier en deux invocations du headless
      (`--state-save`/`--state-load` ne sont jamais exercés par la suite)
- [ ] ROM de test Game Gear : CRAM à latch pair/impair, ports 0x00-0x06,
      recadrage 160×144 — le sous-système le plus riche en pièges. Au
      passage : table de registres dans `smsasm.py` (collapser les ~30
      méthodes par (opcode, registre) en 5) et garde-fou de débordement
      dans `Asm.db()` (l'affectation de tranche redimensionne le bytearray
      silencieusement)
- [ ] Micro-tests C++ (`doctest.h`, un seul header) : `Config` (qui réécrit
      le fichier utilisateur !), `Cartridge::load`, `StateIO`, et
      l'aller-retour `smsasm.py` ↔ `Z80Disasm.cpp` (valide l'assembleur et
      le désassembleur l'un par l'autre)
- [ ] Brancher `--shot-at`/`--exit-at` du GUI dans la suite (créés pour ça,
      jamais utilisés — le GUI a zéro test)
- [ ] Light Phaser : recommiter une ROM de validation (celle des « trois
      visées » n'est pas dans le dépôt — la validation n'est pas rejouable)
- [ ] Check PAL (séquence VCounter 313 lignes) dans la ROM VDP

## Phase 4 — Refactors structurels (par impact)

- [ ] Extraire `Machine::stepInstruction()` du corps de `runFrame`
      (`Machine.cpp:85-116`) — débloque débogueur pas-à-pas, tests
      d'instruction et le serveur MCP (le meilleur ratio valeur/effort du
      projet)
- [ ] Backend mémoire pour `StateIO` (~30 lignes) remplaçant
      `open_memstream`/`fmemopen` POSIX (`Machine.cpp:216-237`) — le cœur
      (et le rewind) devient 100 % C++ standard, Windows atteignable
- [ ] Couche hôte partagée (`src/host/`) : `looksLikeBios`/résolution BIOS
      (3 copies), table d'options déclarative + `--help` généré (le GUI
      n'en a pas et avale toute option inconnue comme chemin de ROM),
      `Vdp::visibleRect(Model)` (recadrage GG copié 3×), pompe audio,
      `FramePacer` (cadence en dur `5992` dans le wasm). Objectif :
      `main()` GUI < 100 lignes (aujourd'hui ~690)
- [ ] Verrouiller les invariants : `= delete` sur les copies de
      `Machine`/`Bus`/`Z80` (la copie implicite produit une machine
      incohérente — `Z80` garde une référence sur le Bus d'origine) ;
      `reset()` dans le constructeur de `Machine` (sinon PSG à volume max) ;
      tables OPL en `constexpr` (globales mutables non thread-safe) ;
      surcharges const/non-const dans `StateIO` + checksum d'état ;
      `Machine::loadRom` acceptant un `Model` forcé (supprime le correctif
      `forceGg` copié 3×)
- [ ] Purger les 4 TODO mensongers (décrivent comme absent de l'implémenté) :
      `Vdp.cpp:16`, `Io.cpp:94`, `Ym2413.hpp:19`, `Ym2413.cpp:220`
- [ ] Divers relevés à l'audit : mutex dans le callback CoreAudio (anneau
      SPSC lock-free — 1 producteur, 1 consommateur), backoff dans la
      boucle d'erreur ALSA (spin 100 % CPU si périphérique débranché),
      destructeurs GL à remplacer par un `shutdown()` explicite avant
      `glfwTerminate`, accents cassés dans le menu kiosk
      (`KioskMenu.cpp:211`, masque `& 0x7F`), config écrite dans le CWD
      (viser `$XDG_CONFIG_HOME` / `Application Support`)

## Phase 5 — Moyen terme

- [ ] [R] Serveur MCP (à la Gearsystem) : exposer registres Z80/VDP,
      mémoire, points d'arrêt et pas-à-pas à des assistants IA via STDIO —
      base : `sesame-headless` + `stepInstruction()` (Phase 4). Le chantier
      le plus aligné avec la vocation pédagogique
- [ ] Cible wasm dans CMake (`emcmake`) partageant la liste de sources
      (aujourd'hui dupliquée dans `build_wasm.sh` — un `.cpp` ajouté au
      cœur casse silencieusement le build web) ; `AudioWorklet` à la place
      du `ScriptProcessor` déprécié ; réutiliser `Io::Button` au lieu des
      bits recopiés en dur dans `index.html`
- [ ] Valider le backend ALSA sur une vraie machine Linux (écrit non testé)
- [ ] Bouton Reset console et lecture-arrière 0x3E côté SMS1
- [ ] Rewind par LIGNE (débogueur)
- [ ] [R] Code/Data Logger (tracer code exécuté vs données dans la ROM,
      inspiration Mesen 2/Nexen)
- [ ] Port Windows si désiré (après le backend mémoire `StateIO` il reste :
      headers GL sous MSVC/MinGW, backend audio WASAPI)
- [ ] Décision de fond sur le rendu : l'immediate mode OpenGL est en sursis
      sur macOS — le jour venu, viser SDL2/SDL3 plutôt qu'une migration
      Metal directe

## Non-objectifs (décisions actées, ne pas rouvrir sans raison)

- Pas de scheduler générique : la boucle actuelle, une fois le bug
  `lineCycles` corrigé, est précise à l'instruction près — suffisant
- Pas de gros framework de test : `doctest.h` (un header) couvre le besoin
- Pas de Mega Drive : le seul geste utile est de ne pas coupler davantage
  `Z80` à la classe concrète `Bus` si on y retouche
- [R] Lunettes SegaScope 3-D (stéréoscopie ; seul MAME le gère) — noté
  pour mémoire, non prioritaire
