#pragma once
// =============================================================================
//  AudioOut — sortie audio temps réel du frontend fenêtré.
//  Consomme les échantillons s16 mono 44100 Hz du PSG via un anneau interne
//  protégé par mutex : la boucle d'émulation pousse (push), le callback du
//  backend tire à son rythme (silence en cas de sous-alimentation ; en cas de
//  trop-plein — rattrapage après un décrochage, dérive d'horloges — les plus
//  vieux échantillons sont écrasés pour garder la latence bornée).
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

    // Pousse n échantillons dans l'anneau (appelé par la boucle d'émulation).
    void push(const s16* smp, int n);

private:
    static constexpr int kRingSize = 8192;   // ~185 ms de marge

    std::mutex mtx;
    s16 ring[kRingSize]{};
    int ringR = 0, ringW = 0, count = 0;

    // Tire jusqu'à max échantillons, complète par du silence (pour le callback).
    void pull(s16* out, int max);

#ifdef __APPLE__
    static void queueCallback(void* user, AudioQueueRef q, AudioQueueBufferRef buf);
    AudioQueueRef queue = nullptr;
#endif
};
