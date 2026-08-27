// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

#pragma once
// =============================================================================
//  AudioOut — sortie audio temps réel du frontend fenêtré.
//  Consomme les TRAMES STÉRÉO s16 entrelacées (G,D) 44100 Hz du PSG via un
//  anneau interne protégé par mutex : la boucle d'émulation pousse (push),
//  le callback du backend tire à son rythme (silence en cas de
//  sous-alimentation ; en cas de trop-plein — rattrapage après un
//  décrochage, dérive d'horloges — les plus vieilles trames sont écrasées
//  pour garder la latence bornée).
//  Backend : CoreAudio (AudioQueue) sur macOS ; ailleurs, stub muet
//  (start() retourne false, le frontend continue en silence).
// =============================================================================
#include "core/Types.hpp"

#include <mutex>

#ifdef __APPLE__
#include <AudioToolbox/AudioToolbox.h>
#endif

class AudioOut {
public:
    ~AudioOut() { stop(); }

    // Démarre le flux ; false si aucun backend disponible (frontend muet).
    bool start();
    void stop();

    // Pousse n trames stéréo (2 s16 chacune) dans l'anneau (appelé par la
    // boucle d'émulation).
    void push(const s16* frames, int n);

private:
    static constexpr int kRingFrames = 8192;   // ~185 ms de marge

    std::mutex mtx;
    s16 ring[kRingFrames * 2]{};               // entrelacé G,D
    int ringR = 0, ringW = 0, count = 0;       // en TRAMES

    // Tire jusqu'à max trames, complète par du silence (pour le callback).
    void pull(s16* out, int max);

#ifdef __APPLE__
    static void queueCallback(void* user, AudioQueueRef q, AudioQueueBufferRef buf);
    AudioQueueRef queue = nullptr;
#endif
};
