# TODO — par sous-système

## CPU (Z80)
- [x] Valider avec ZEXDOC/ZEXALL (port SMS de Maxim Zhao via SDSC :
      `tools/fetch_zexall.py` puis `tools/run_zexall.py`, 2026-08)
- [x] MEMPTR/WZ exact pour BIT n,(HL) — validé par le test
      « bit n,r/(hl) » de ZEXALL (2026-08)

## VDP
- [ ] Modes 224/240 lignes (SMS2)
- [x] PAL (313 lignes, ~49,70 Hz) — cadence, horloge CPU/PSG 3 546 893 Hz,
      séquence VCounter (`--pal` dans les deux frontends, 2026-08)
- [ ] HCounter réel + latch sur front TH (light gun)
- [ ] Modes hérités TMS9918 (0-3) pour les jeux SG-1000/F-16
- [ ] Timing fin intra-ligne (mid-line scroll/CRAM effects)

## Son
- [x] Sortie audio temps réel dans le GUI (CoreAudio/AudioQueue, 2026-08)
- [ ] Backend audio Linux (ALSA/PipeWire) — stub muet actuellement
- [ ] YM2413 (FM japonais)

## Machine
- [ ] Save-states
- [x] BIOS optionnel + contrôle mémoire 0x3E réel (2026-08 ; reste : bouton
      Reset console et lecture-arrière 0x3E côté SMS1)
- [ ] Game Gear (fenêtre 160x144, palette 12 bits, ports Start)
- [ ] Mappers exotiques (Codemasters, Korean)
- [x] Sauvegarde RAM cartouche sur disque (.sav — GUI toujours, headless
      opt-in via --sav pour rester déterministe, 2026-08)

## Frontends
- [x] Manette USB (API gamepad GLFW, 2 pads + Start = Pause, 2026-08)
- [x] Redimensionnement + plein écran (touche F, 2026-08)
- [x] Filtre CRT (pile d'effets portée de NeoST/POM2 : baril, scanlines,
      shadow mask, persistance… — --crt / touche C, 2026-08)
- [x] Mode kiosk (--kiosk [--kiosk-monitor N] : plein écran exclusif,
      curseur masqué, CRT activé, 2026-08) + menu in-game (Start/F9 :
      liste de jeux, restart, desktop, quit — 2026-08)
- [ ] Menu kiosk : dossiers ROM additionnels + affectation des manettes
      (parité NeoST)
- [ ] Presets CRT configurables (--crt-preset / fichier de config)
- [ ] Build WebAssembly

## Outils
- [ ] Étalons d'images (comparaison PPM avec tolérance)
- [ ] ROM de test VDP dédiée (scroll, sprites, IRQ ligne)
