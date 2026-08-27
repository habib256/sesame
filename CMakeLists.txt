# =============================================================================
#  Sesame — Émulateur Sega Master System pédagogique
#  Cible : macOS Silicon / Linux x86_64. C++17.
#
#  Architecture : un cœur SANS GUI (sesame_core) partagé par les frontends :
#    - sesame            : fenêtré (GLFW3 + OpenGL immediate mode)
#    - sesame-headless   : sans interface, traces déterministes + captures PPM
#
#  Le Bus EST le plan mémoire : il route read8/write8 et in/out vers les puces.
#
#  Copyright (C) 2026 VERHILLE Arnaud
#  SPDX-License-Identifier: GPL-3.0-or-later — voir le fichier LICENSE.
# =============================================================================
cmake_minimum_required(VERSION 3.20)

project(Sesame
    VERSION 0.1.0
    DESCRIPTION "Emulateur Sega Master System oriente transparence materielle"
    LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

option(SESAME_WARN_STRICT "Compilation stricte (-Wall -Wextra)" ON)

set(SESAME_STRICT_OPTS "")
if(SESAME_WARN_STRICT)
    set(SESAME_STRICT_OPTS "-Wall;-Wextra")
endif()

# =============================================================================
#  Bibliothèque cœur — AUCUNE dépendance GUI.
# =============================================================================
add_library(sesame_core STATIC
    src/core/Bus.cpp
    src/core/Machine.cpp
    src/core/Z80.cpp
    src/core/Z80Disasm.cpp
    src/core/Vdp.cpp
    src/core/Psg.cpp
    src/core/Ym2413.cpp
    src/core/Io.cpp
    src/core/Cartridge.cpp
)
target_include_directories(sesame_core PUBLIC src)
target_compile_options(sesame_core PRIVATE ${SESAME_STRICT_OPTS})

# =============================================================================
#  Frontend headless — l'outil de validation n°1 (traces, PPM, WAV).
# =============================================================================
add_executable(sesame-headless src/headless/main_headless.cpp)
target_link_libraries(sesame-headless PRIVATE sesame_core)
target_compile_options(sesame-headless PRIVATE ${SESAME_STRICT_OPTS})

# =============================================================================
#  Frontend fenêtré — GLFW3 + OpenGL (immediate mode). Optionnel : ne se
#  construit que si GLFW est trouvé (brew install glfw / pacman -S glfw).
# =============================================================================
find_package(glfw3 QUIET)
if(glfw3_FOUND)
    add_executable(sesame
        src/gui/main.cpp
        src/gui/AudioOut.cpp
        src/gui/Config.cpp
        src/gui/CrtEffectStack.cpp
        src/gui/KioskMenu.cpp
        src/gui/OpenGLShader.cpp
    )
    target_link_libraries(sesame PRIVATE sesame_core glfw)
    if(APPLE)
        # AudioToolbox : sortie audio temps réel (AudioQueue) — voir AudioOut.
        target_link_libraries(sesame PRIVATE "-framework OpenGL"
                                             "-framework AudioToolbox")
        # OpenGL immediate mode est déprécié par Apple mais fonctionne toujours.
        target_compile_definitions(sesame PRIVATE GL_SILENCE_DEPRECATION)
    else()
        find_package(OpenGL REQUIRED)
        target_link_libraries(sesame PRIVATE OpenGL::GL)
        # ALSA : sortie audio temps réel sur Linux (stub muet si absent).
        find_package(ALSA QUIET)
        if(ALSA_FOUND)
            find_package(Threads REQUIRED)
            target_link_libraries(sesame PRIVATE ALSA::ALSA Threads::Threads)
            target_compile_definitions(sesame PRIVATE SESAME_HAVE_ALSA)
        else()
            message(STATUS "Sesame: ALSA introuvable, frontend GUI muet")
        endif()
    endif()
    target_compile_options(sesame PRIVATE ${SESAME_STRICT_OPTS})
else()
    message(STATUS "Sesame: GLFW3 introuvable, frontend fenêtré non construit")
endif()
