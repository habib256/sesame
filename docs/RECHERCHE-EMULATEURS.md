# Recherche — Écosystème des émulateurs SMS/Game Gear open-source

Synthèse d'une recherche externe (août 2026) sur les émulateurs Master
System / Game Gear, condensée et recentrée sur ce qui sert le développement
de Sesame. Source : rapport d'analyse fourni par Arnaud ; les affirmations
n'ont pas toutes été vérifiées sur pièce — vérifier avant de s'appuyer sur
un détail précis.

## Panorama des projets de référence

| Projet | Langage | Licence | Intérêt pour Sesame |
|--------|---------|---------|---------------------|
| **Genesis Plus GX** (Eke-Eke) | C/C++ | **Non commerciale** (source-available) | Référence de comportement (compat ~100 %, Blip Buffer, panning GG, EEPROM 93C46). **Licence incompatible GPLv3 : lecture seule, jamais de copie de code.** |
| **Gearsystem** (drhelius) | C++ | GPLv3 | Compatible licence. Zexall PASS, YM2413, mode 224 lignes SMS2, mappers coréens/Janggun. Serveur **MCP** embarqué + agent skills (debug/romhacking par IA). |
| **Mesen 2 / Nexen** (SourMesen) | C++ / C# | GPLv3 | Compatible licence. Outillage d'analyse : step-back par scanline, Code/Data Logger, undo/redo RAM/ROM, TAS (Piano Roll, Greenzone). |
| **ares** (Near †) | C++ | ISC (permissive) | Compatible licence. Code = documentation vivante du matériel, sans hacks. Bonne référence de lisibilité — philosophie proche de Sesame. |
| **jgenesis** (jsgroth) | Rust | MIT | Compatible licence. Rendu GPU natif, WebGPU/navigateur (pertinent pour notre TODO WebAssembly), overclock Z80, option « no sprite limit ». |
| **MAME** (`sms.cpp`, `sn76496.cpp`, `315_5124.cpp`) | C++ | GPLv2 / BSD-3 par fichier | Déjà notre référence de comportement (CLAUDE.md). Seul à gérer les lunettes SegaScope 3-D. Réutilisation de code : fichiers BSD-3 seulement. |
| Snepulator | C | MIT | Portabilité, light gun tactile, no sprite limit. |
| PicoDrive | C | Non commerciale | Vitesse sur ARM faible puissance ; précision sacrifiée. Peu pertinent. |
| Emulicious, MEKA, Kega Fusion, MasterGear | — | Fermés ou historiques | Emulicious : très précis mais fermé. MEKA (Omar Cornut) : code publié depuis, pionnier des débogueurs. |

## Rappels matériels confirmés par la recherche

- SMS et GG partagent l'essentiel (Z80A ~3,58 MHz NTSC / ~4 MHz PAL,
  VDP dérivé TMS9918, 16 Ko VRAM, 8 Ko RAM, PSG SN76489) — d'où la
  compatibilité croisée quasi totale des émulateurs.
- **Game Gear** : 160×144, palette 32 couleurs parmi **4096** (12 bits),
  mode compatibilité SMS (palette réduite) quand une cartouche SMS est
  détectée via adaptateur ; **panning stéréo** par registres de volume
  gauche/droite par canal PSG (port 0x06).
- Z80 : la précision moderne exige R, **MEMPTR/WZ** et les tests
  **Zexall** — *déjà fait dans Sesame (79/79 ZEXDOC et ZEXALL)*.
- YM2413 (OPLL) : 9 canaux FM, option japonaise ; le défi principal est le
  **rééchantillonnage sans aliasing** vers 44,1/48 kHz.
- Limite matérielle de 8 sprites/scanline (clignotement d'époque) — déjà
  émulée dans Sesame ; plusieurs émulateurs offrent une option pour
  l'abolir.

## Implications concrètes pour la feuille de route Sesame

Croisement établi avec le `TODO.md` de l'époque (2026-08). La plupart de ces
pistes ont depuis été réalisées — voir [`CHANGELOG.md`](../CHANGELOG.md) ;
restent ouverts les points 6 (serveur MCP), 7 (step-back par ligne,
Code/Data Logger) et 9 (SegaScope), repris dans le `TODO.md` actuel :

1. **YM2413 (TODO Son)** — implémentations GPLv3/MIT consultables et
   réutilisables : Gearsystem, jgenesis, MAME (`ym2413` si BSD-3). Prévoir
   le rééchantillonnage propre dès la conception (cf. point suivant).
2. **Qualité audio** — notre PSG sort en anneau 44,1 kHz naïf. La référence
   du domaine est la **band-limited synthesis « Blip Buffer » de Blargg**
   (utilisée par Genesis Plus GX). L'algorithme est documenté publiquement
   par Blargg (blip_buf existe en LGPL) — piste sérieuse pour améliorer le
   PSG et indispensable pour un futur YM2413 sans aliasing.
3. **Game Gear (TODO Machine)** — périmètre confirmé : fenêtre 160×144
   centrée dans la sortie VDP, CRAM 12 bits (2 octets/entrée), registres de
   panning stéréo, port Start (0x00), mode compat SMS. Références :
   Gearsystem et jgenesis (licences compatibles).
4. **Mode 224 lignes SMS2 (TODO VDP)** — géré par Gearsystem : bonne
   référence croisée avec la doc SMS Power! et MAME `315_5124.cpp`.
5. **Option pédagogique « no sprite limit »** — facile chez nous (le
   compteur d'overflow existe déjà dans `Vdp.cpp`) ; précédent chez
   jgenesis, Snepulator, Mesen 2. Bien la présenter comme une entorse au
   matériel (valeur pédagogique : montrer *pourquoi* ça clignotait).
6. **Serveur MCP à la Gearsystem** — l'idée la plus alignée avec la
   vocation pédagogique de Sesame : exposer registres Z80/VDP, mémoire,
   points d'arrêt et pas-à-pas via MCP (STDIO), pour que des assistants IA
   pilotent le débogage et le rom-hacking. Notre `sesame-headless`
   déterministe est une base naturelle.
7. **Outillage d'analyse (inspiration Mesen 2)** — step-back par scanline
   (exige des save-states déterministes → dépend du TODO save-states) et
   Code/Data Logger (tracer code vs données dans la ROM). Ambitieux mais
   très « pédagogique ».
8. **WebAssembly (TODO Frontends)** — jgenesis prouve la viabilité du
   navigateur (WebGPU). Notre cœur sans dépendance est déjà prêt pour
   Emscripten ; seul le frontend est à adapter.
9. **SegaScope 3-D** — anecdotique (MAME seul le gère) ; à noter comme
   curiosité, pas comme priorité.

## Règle de licence (à respecter strictement)

Sesame est **GPL-3.0-or-later**. Donc :

- **Copie/adaptation de code possible** : Gearsystem, Mesen 2/Nexen (GPLv3),
  ares (ISC), jgenesis, Snepulator (MIT), fichiers MAME sous BSD-3,
  blip_buf (LGPL) — avec attribution et mention dans les en-têtes.
- **Lecture seule, jamais de copie** : Genesis Plus GX et PicoDrive
  (clauses non commerciales, incompatibles GPL), Emulicious (fermé).
  S'en servir uniquement comme référence de *comportement*.
