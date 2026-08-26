#pragma once
// Types de base partagés par tout le cœur.
#include <cstdint>

using u8  = uint8_t;
using u16 = uint16_t;
using u32 = uint32_t;
using u64 = uint64_t;
using s8  = int8_t;
using s16 = int16_t;
using s32 = int32_t;
using s64 = int64_t;

// Norme vidéo de la console : conditionne l'horloge CPU/PSG, le nombre de
// lignes par trame du VDP et la séquence du VCounter.
enum class Region : u8 {
    Ntsc,  // Japon / Amérique du Nord : 262 lignes, ~59,92 Hz, CPU 3 579 545 Hz
    Pal,   // Europe : 313 lignes, ~49,70 Hz, CPU 3 546 893 Hz
};
