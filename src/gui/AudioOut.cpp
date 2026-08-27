// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  AudioOut.cpp — anneau producteur/consommateur + backend CoreAudio.
//
//  Côté macOS : une AudioQueue de 4 tampons de 512 échantillons (~11,6 ms
//  chacun) ; le callback tourne sur un thread CoreAudio et vient tirer dans
//  l'anneau. La latence totale reste ainsi sous ~50 ms.
//  TODO(v2) : backend ALSA/PipeWire pour Linux (le stub actuel y est muet).
// =============================================================================
#include "AudioOut.hpp"

#include "core/Psg.hpp"

#include <cstring>

void AudioOut::push(const s16* frames, int n) {
    std::lock_guard<std::mutex> lock(mtx);
    for (int i = 0; i < n; ++i) {
        if (count == kRingFrames) {
            // Trop-plein (l'émulation va un poil plus vite que 44100 Hz réels) :
            // on écrase le plus vieux pour garder la latence bornée.
            ringR = (ringR + 1) % kRingFrames;
            --count;
        }
        ring[ringW * 2]     = frames[i * 2];
        ring[ringW * 2 + 1] = frames[i * 2 + 1];
        ringW = (ringW + 1) % kRingFrames;
        ++count;
    }
}

void AudioOut::pull(s16* out, int max) {
    std::lock_guard<std::mutex> lock(mtx);
    int n = (count < max) ? count : max;
    for (int i = 0; i < n; ++i) {
        out[i * 2]     = ring[ringR * 2];
        out[i * 2 + 1] = ring[ringR * 2 + 1];
        ringR = (ringR + 1) % kRingFrames;
    }
    count -= n;
    if (n < max)
        std::memset(out + n * 2, 0,
                    static_cast<size_t>(max - n) * 2 * sizeof(s16));
}

#ifdef __APPLE__

namespace {
constexpr int kBufferFrames = 512;   // ~11,6 ms par tampon à 44100 Hz
constexpr int kBufferCount  = 4;
constexpr int kBufferBytes  = kBufferFrames * 2 * sizeof(s16);  // stéréo
}  // namespace

void AudioOut::queueCallback(void* user, AudioQueueRef q, AudioQueueBufferRef buf) {
    auto* self = static_cast<AudioOut*>(user);
    self->pull(static_cast<s16*>(buf->mAudioData), kBufferFrames);
    buf->mAudioDataByteSize = kBufferBytes;
    AudioQueueEnqueueBuffer(q, buf, 0, nullptr);
}

bool AudioOut::start() {
    AudioStreamBasicDescription desc{};
    desc.mSampleRate       = Psg::kSampleRate;
    desc.mFormatID         = kAudioFormatLinearPCM;
    desc.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger |
                             kLinearPCMFormatFlagIsPacked;
    desc.mBitsPerChannel   = 16;
    desc.mChannelsPerFrame = 2;
    desc.mBytesPerFrame    = 4;
    desc.mFramesPerPacket  = 1;
    desc.mBytesPerPacket   = 4;

    if (AudioQueueNewOutput(&desc, queueCallback, this, nullptr, nullptr, 0,
                            &queue) != noErr) {
        queue = nullptr;
        return false;
    }

    // Tampons pré-remplis de silence puis mis en file : le callback prend le
    // relais dès que la file tourne.
    for (int i = 0; i < kBufferCount; ++i) {
        AudioQueueBufferRef buf = nullptr;
        if (AudioQueueAllocateBuffer(queue, kBufferBytes, &buf) != noErr) {
            stop();
            return false;
        }
        std::memset(buf->mAudioData, 0, kBufferBytes);
        buf->mAudioDataByteSize = kBufferBytes;
        AudioQueueEnqueueBuffer(queue, buf, 0, nullptr);
    }

    if (AudioQueueStart(queue, nullptr) != noErr) {
        stop();
        return false;
    }
    return true;
}

void AudioOut::stop() {
    if (!queue)
        return;
    AudioQueueStop(queue, true);      // synchrone : le callback ne tourne plus
    AudioQueueDispose(queue, true);
    queue = nullptr;
}

#else  // stub muet (Linux : TODO ALSA/PipeWire)

bool AudioOut::start() { return false; }
void AudioOut::stop() {}

#endif
