// =============================================================================
//  Cartridge.cpp — chargement des ROM .sms et mapper Sega standard.
//
//  Rappels matériels (voir CLAUDE.md et SMS Power!) :
//   - Le premier Ko (0x0000-0x03FF) n'est JAMAIS paginé : il pointe toujours
//     sur le début physique de la ROM, quel que soit pageReg[0].
//   - Trois fenêtres de 16 Ko : 0x0000-0x3FFF, 0x4000-0x7FFF, 0x8000-0xBFFF,
//     pilotées par les registres écrits à 0xFFFD/E/F (relayés par le Bus).
//   - 0xFFFC contrôle la RAM de sauvegarde : bit 3 = RAM mappée sur
//     0x8000-0xBFFF, bit 2 = choix de la banque (0 ou 1).
// =============================================================================
#include "Cartridge.hpp"
#include <fstream>

bool Cartridge::load(const std::string& path) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        return false;

    std::streamoff size = f.tellg();
    if (size <= 0)
        return false;

    // Certains vieux dumpers ajoutaient un en-tête parasite de 512 octets :
    // on le détecte (taille = multiple de 16 Ko + 512) et on le saute.
    std::streamoff skip = 0;
    if (size % 0x4000 == 512) {
        skip = 512;
        size -= 512;
    }
    if (size <= 0)
        return false;

    rom.resize(static_cast<size_t>(size));
    f.seekg(skip, std::ios::beg);
    if (!f.read(reinterpret_cast<char*>(rom.data()), size))
        return false;

    // Une ROM plus petite qu'une page (16 Ko) est traitée comme une page
    // complète : on la complète par miroir d'elle-même.
    const size_t original = rom.size();
    while (rom.size() < 0x4000)
        rom.push_back(rom[rom.size() % original]);
    // Idem si la taille n'est pas un multiple exact de 16 Ko.
    while (rom.size() % 0x4000 != 0)
        rom.push_back(rom[rom.size() % original]);

    // Nombre de pages arrondi à la puissance de 2 supérieure : le masque de
    // page devient un simple ET binaire. On complète physiquement la ROM par
    // miroir jusqu'à cette taille pour que tout accès masqué reste valide.
    size_t pages = rom.size() / 0x4000;
    size_t pow2 = 1;
    while (pow2 < pages)
        pow2 <<= 1;
    while (rom.size() < pow2 * 0x4000)
        rom.push_back(rom[rom.size() % original]);
    romMask = static_cast<int>(pow2 - 1);

    // Persistance de la RAM de sauvegarde : « <rom sans extension>.sav » à
    // côté de la ROM. Un .sav existant est rechargé (jeu commencé sur pile).
    savePath.clear();
    saveRamDirty = false;
    if (savEnabled) {
        savePath = path;
        const auto slash = savePath.find_last_of("/\\");
        const auto dot   = savePath.find_last_of('.');
        if (dot != std::string::npos &&
            (slash == std::string::npos || dot > slash))
            savePath.resize(dot);
        savePath += ".sav";

        std::ifstream sav(savePath, std::ios::binary);
        if (sav)
            sav.read(reinterpret_cast<char*>(cartRam[0]), sizeof(cartRam));
    }

    reset();
    return true;
}

void Cartridge::persistSaveRam() {
    if (!saveRamDirty || savePath.empty())
        return;
    std::ofstream sav(savePath, std::ios::binary | std::ios::trunc);
    if (!sav)
        return;  // disque plein / dossier en lecture seule : on réessaiera
    sav.write(reinterpret_cast<const char*>(cartRam[0]), sizeof(cartRam));
    if (sav)
        saveRamDirty = false;
}

void Cartridge::reset() {
    // État de mise sous tension du mapper : fenêtres 0/1/2, RAM désactivée.
    pageReg[0] = 0;
    pageReg[1] = 1;
    pageReg[2] = 2;
    ramControl = 0;
}

u8 Cartridge::read(u16 addr) {
    if (rom.empty())
        return 0xFF;  // pas de cartouche : bus flottant

    // 0x0000-0x03FF : TOUJOURS le début physique de la ROM, jamais paginé
    // (c'est là que vivent les vecteurs d'interruption du Z80).
    if (addr < 0x0400)
        return rom[addr];

    // 0x0400-0x3FFF : reste de la fenêtre 0.
    if (addr < 0x4000)
        return rom[(static_cast<size_t>(pageReg[0] & romMask) << 14) | (addr & 0x3FFF)];

    // 0x4000-0x7FFF : fenêtre 1.
    if (addr < 0x8000)
        return rom[(static_cast<size_t>(pageReg[1] & romMask) << 14) | (addr & 0x3FFF)];

    // 0x8000-0xBFFF : RAM de sauvegarde si activée (0xFFFC bit 3),
    // sinon fenêtre 2. Le bit 2 choisit la banque de RAM.
    if (ramControl & 0x08)
        return cartRam[(ramControl >> 2) & 1][addr & 0x3FFF];
    return rom[(static_cast<size_t>(pageReg[2] & romMask) << 14) | (addr & 0x3FFF)];
}

void Cartridge::write(u16 addr, u8 v) {
    // Seule la fenêtre 0x8000-0xBFFF est inscriptible, et uniquement quand la
    // RAM cartouche y est mappée ; toute autre écriture est ignorée (ROM).
    if (addr >= 0x8000 && addr < 0xC000 && (ramControl & 0x08)) {
        cartRam[(ramControl >> 2) & 1][addr & 0x3FFF] = v;
        saveRamDirty = true;
    }
}

void Cartridge::writeMapper(u16 addr, u8 v) {
    // Registres du mapper, « sous » le miroir RAM (le Bus écrit les deux).
    switch (addr) {
    case 0xFFFC: ramControl = v; break;  // contrôle RAM de sauvegarde
    case 0xFFFD: pageReg[0] = v; break;  // fenêtre 0x0000-0x3FFF
    case 0xFFFE: pageReg[1] = v; break;  // fenêtre 0x4000-0x7FFF
    case 0xFFFF: pageReg[2] = v; break;  // fenêtre 0x8000-0xBFFF
    default: break;
    }
}
