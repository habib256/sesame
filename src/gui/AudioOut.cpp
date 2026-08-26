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

void AudioOut::push(const s16* smp, int n) {
    std::lock_guard<std::mutex> lock(mtx);
    for (int i = 0; i < n; ++i) {
        if (count == kRingSize) {
            // Trop-plein (l'émulation va un poil plus vite que 44100 Hz réels) :
            // on écrase le plus vieux pour garder la latence bornée.
            ringR = (ringR + 1) % kRingSize;
            --count;
        }
        ring[ringW] = smp[i];
        ringW = (ringW + 1) % kRingSize;
        ++count;
    }
}

void AudioOut::pull(s16* out, int max) {
    std::lock_guard<std::mutex> lock(mtx);
    int n = (count < max) ? count : max;
    for (int i = 0; i < n; ++i) {
        out[i] = ring[ringR];
        ringR = (ringR + 1) % kRingSize;
    }
    count -= n;
    if (n < max)
        std::memset(out + n, 0, static_cast<size_t>(max - n) * sizeof(s16));
}

#ifdef __APPLE__

namespace {
constexpr int kBufferSamples = 512;  // ~11,6 ms par tampon à 44100 Hz
constexpr int kBufferCount   = 4;
}  // namespace

void AudioOut::queueCallback(void* user, AudioQueueRef q, AudioQueueBufferRef buf) {
    auto* self = static_cast<AudioOut*>(user);
    self->pull(static_cast<s16*>(buf->mAudioData), kBufferSamples);
    buf->mAudioDataByteSize = kBufferSamples * sizeof(s16);
    AudioQueueEnqueueBuffer(q, buf, 0, nullptr);
}

bool AudioOut::start() {
    AudioStreamBasicDescription desc{};
    desc.mSampleRate       = Psg::kSampleRate;
    desc.mFormatID         = kAudioFormatLinearPCM;
    desc.mFormatFlags      = kLinearPCMFormatFlagIsSignedInteger |
                             kLinearPCMFormatFlagIsPacked;
    desc.mBitsPerChannel   = 16;
    desc.mChannelsPerFrame = 1;
    desc.mBytesPerFrame    = 2;
    desc.mFramesPerPacket  = 1;
    desc.mBytesPerPacket   = 2;

    if (AudioQueueNewOutput(&desc, queueCallback, this, nullptr, nullptr, 0,
                            &queue) != noErr) {
        queue = nullptr;
        return false;
    }

    // Tampons pré-remplis de silence puis mis en file : le callback prend le
    // relais dès que la file tourne.
    for (int i = 0; i < kBufferCount; ++i) {
        AudioQueueBufferRef buf = nullptr;
        if (AudioQueueAllocateBuffer(queue, kBufferSamples * sizeof(s16), &buf)
                != noErr) {
            stop();
            return false;
        }
        std::memset(buf->mAudioData, 0, kBufferSamples * sizeof(s16));
        buf->mAudioDataByteSize = kBufferSamples * sizeof(s16);
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
