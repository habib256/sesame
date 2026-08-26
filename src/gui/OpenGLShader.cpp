// Copyright (C) 2026 VERHILLE Arnaud
// SPDX-License-Identifier: GPL-3.0-or-later
// Ce fichier fait partie de Sesame, distribué sous licence GNU GPL v3
// (ou ultérieure), SANS AUCUNE GARANTIE : voir le fichier LICENSE.

// =============================================================================
//  OpenGLShader.cpp — compilation/link de shader GLSL (porté de NeoST).
//
//  On a besoin des points d'entrée GL 2.0+ (glCreateShader, glCompileShader…)
//  absents du <GL/gl.h> 1.1 de Linux/Windows. Stratégie :
//    * macOS — <OpenGL/gl3.h> les déclare directement.
//    * Linux / Windows — typedefs PFN via <GL/glext.h> + résolution paresseuse
//      par glfwGetProcAddress (GLFW est déjà lié partout).
//  Autonome : évite de tirer GLEW/GLAD pour une poignée de points d'entrée.
// =============================================================================
#include "OpenGLShader.hpp"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if defined(__APPLE__)
#  ifndef GL_SILENCE_DEPRECATION
#  define GL_SILENCE_DEPRECATION 1
#  endif
#  include <OpenGL/gl3.h>
#else
#  include <GL/gl.h>
#  include <GL/glext.h>
#  include <GLFW/glfw3.h>

namespace {
PFNGLCREATESHADERPROC      glCreateShader_      = nullptr;
PFNGLSHADERSOURCEPROC      glShaderSource_      = nullptr;
PFNGLCOMPILESHADERPROC     glCompileShader_     = nullptr;
PFNGLGETSHADERIVPROC       glGetShaderiv_       = nullptr;
PFNGLGETSHADERINFOLOGPROC  glGetShaderInfoLog_  = nullptr;
PFNGLDELETESHADERPROC      glDeleteShader_      = nullptr;
PFNGLCREATEPROGRAMPROC     glCreateProgram_     = nullptr;
PFNGLATTACHSHADERPROC      glAttachShader_      = nullptr;
PFNGLLINKPROGRAMPROC       glLinkProgram_       = nullptr;
PFNGLGETPROGRAMIVPROC      glGetProgramiv_      = nullptr;
PFNGLGETPROGRAMINFOLOGPROC glGetProgramInfoLog_ = nullptr;
PFNGLDELETEPROGRAMPROC     glDeleteProgram_     = nullptr;
PFNGLBINDATTRIBLOCATIONPROC glBindAttribLocation_ = nullptr;
bool entryPointsLoaded_ = false;

bool loadEntryPoints()
{
    if (entryPointsLoaded_) return true;
    auto get = [](const char* name) {
        return reinterpret_cast<void*>(glfwGetProcAddress(name));
    };
    glCreateShader_      = reinterpret_cast<PFNGLCREATESHADERPROC>     (get("glCreateShader"));
    glShaderSource_      = reinterpret_cast<PFNGLSHADERSOURCEPROC>     (get("glShaderSource"));
    glCompileShader_     = reinterpret_cast<PFNGLCOMPILESHADERPROC>    (get("glCompileShader"));
    glGetShaderiv_       = reinterpret_cast<PFNGLGETSHADERIVPROC>      (get("glGetShaderiv"));
    glGetShaderInfoLog_  = reinterpret_cast<PFNGLGETSHADERINFOLOGPROC> (get("glGetShaderInfoLog"));
    glDeleteShader_      = reinterpret_cast<PFNGLDELETESHADERPROC>     (get("glDeleteShader"));
    glCreateProgram_     = reinterpret_cast<PFNGLCREATEPROGRAMPROC>    (get("glCreateProgram"));
    glAttachShader_      = reinterpret_cast<PFNGLATTACHSHADERPROC>     (get("glAttachShader"));
    glLinkProgram_       = reinterpret_cast<PFNGLLINKPROGRAMPROC>      (get("glLinkProgram"));
    glGetProgramiv_      = reinterpret_cast<PFNGLGETPROGRAMIVPROC>     (get("glGetProgramiv"));
    glGetProgramInfoLog_ = reinterpret_cast<PFNGLGETPROGRAMINFOLOGPROC>(get("glGetProgramInfoLog"));
    glDeleteProgram_     = reinterpret_cast<PFNGLDELETEPROGRAMPROC>    (get("glDeleteProgram"));
    glBindAttribLocation_ = reinterpret_cast<PFNGLBINDATTRIBLOCATIONPROC>(get("glBindAttribLocation"));
    entryPointsLoaded_ =
        glCreateShader_ && glShaderSource_ && glCompileShader_ &&
        glGetShaderiv_ && glGetShaderInfoLog_ && glDeleteShader_ &&
        glCreateProgram_ && glAttachShader_ && glLinkProgram_ &&
        glGetProgramiv_ && glGetProgramInfoLog_ && glDeleteProgram_ &&
        glBindAttribLocation_;
    return entryPointsLoaded_;
}
} // namespace

#  define glCreateShader      glCreateShader_
#  define glShaderSource      glShaderSource_
#  define glCompileShader     glCompileShader_
#  define glGetShaderiv       glGetShaderiv_
#  define glGetShaderInfoLog  glGetShaderInfoLog_
#  define glDeleteShader      glDeleteShader_
#  define glCreateProgram     glCreateProgram_
#  define glAttachShader      glAttachShader_
#  define glLinkProgram       glLinkProgram_
#  define glGetProgramiv      glGetProgramiv_
#  define glGetProgramInfoLog glGetProgramInfoLog_
#  define glDeleteProgram     glDeleteProgram_
#  define glBindAttribLocation glBindAttribLocation_
#endif

#if defined(__APPLE__)
namespace { bool loadEntryPoints() { return true; } }
#endif

namespace {

// Un préambule GLSL candidat : ligne #version + lignes de precision.
struct GlslDialect {
    const char* version;
    const char* precision;
};

// Dialectes à essayer, le plus riche d'abord. Le corps des shaders fournit un
// chemin de compatibilité GLSL 1.20 pour le contexte OpenGL 2.1 imposé par le
// rendu immediate mode. Certaines piles plafonnent à 1.40 (V3D des Raspberry
// Pi sous Mesa) — d'où le repli en cascade plutôt qu'une version codée en dur.
std::vector<GlslDialect> glslDialects()
{
    const char* kEsPrecision = "precision highp float;\nprecision highp int;\n";

    const char* sl = reinterpret_cast<const char*>(
        glGetString(GL_SHADING_LANGUAGE_VERSION));

    // Contexte GLES natif (Pi en mode KMS/GLES, Wayland…).
    if (sl && std::strstr(sl, "ES ") != nullptr)
        return { { "#version 300 es\n", kEsPrecision } };

    // Desktop : « 1.40 » ou « 4.60 NVIDIA ». Sans chaîne exploitable, on tente
    // toute la cascade (le pire cas coûte 2 compilations ratées, au démarrage).
    int major = 0, minor = 0;
    if (sl) std::sscanf(sl, "%d.%d", &major, &minor);
    if (minor < 10) minor *= 10;           // « 4.6 » ≡ « 4.60 »
    const int ver = major * 100 + minor;   // 1.40 -> 140

    std::vector<GlslDialect> out;
    if (ver == 0 || ver >= 150) out.push_back({ "#version 150\n", "\n" });
    if (ver == 0 || ver >= 140) out.push_back({ "#version 140\n", "\n" });
    if (ver == 0 || ver >= 130) out.push_back({ "#version 130\n", "\n" });
    if (ver == 0 || ver >= 120) out.push_back({ "#version 120\n", "\n" });
    if (out.empty()) out.push_back({ "#version 120\n", "\n" });  // dernier recours
    return out;
}

// `quiet` : n'écrit rien sur stderr — utilisé pour les tentatives de repli,
// dont l'échec est normal et ne doit pas alarmer l'utilisateur.
unsigned int compileOne(unsigned int kind,
                        const char* versionLine,
                        const char* precisionLine,
                        const char* body,
                        std::string* errorOut,
                        bool quiet)
{
    unsigned int sh = glCreateShader(kind);
    if (!sh) {
        if (errorOut) *errorOut = "glCreateShader returned 0";
        return 0;
    }
    const char* parts[3] = { versionLine, precisionLine, body };
    glShaderSource(sh, 3, parts, nullptr);
    glCompileShader(sh);
    int ok = 0;
    glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetShaderInfoLog(sh, sizeof(log) - 1, &len, log);
        std::string msg = "shader compile failed: ";
        msg.append(log, len);
        if (errorOut) *errorOut = msg;
        if (!quiet) std::fprintf(stderr, "[CRT] %s\n", msg.c_str());
        glDeleteShader(sh);
        return 0;
    }
    return sh;
}

} // namespace

unsigned int compileShaderProgram(const char* vertexBody,
                                  const char* fragmentBody,
                                  std::string* errorOut)
{
    if (!loadEntryPoints()) {
        if (errorOut) *errorOut = "GL 3.x entry points unavailable";
        std::fprintf(stderr, "[CRT] GL 3.x entry points unavailable — "
                             "CRT effects disabled\n");
        return 0;
    }

    // Essaie chaque dialecte jusqu'à ce que les DEUX shaders compilent. Un
    // pilote peut annoncer une version et la refuser dans ce contexte : seule
    // la compilation réelle tranche.
    const std::vector<GlslDialect> dialects = glslDialects();
    const char* versionLine = nullptr;
    unsigned int vs = 0, fs = 0;
    for (std::size_t i = 0; i < dialects.size(); ++i) {
        const bool last = (i + 1 == dialects.size());
        vs = compileOne(GL_VERTEX_SHADER, dialects[i].version,
                        dialects[i].precision, vertexBody, errorOut, !last);
        if (vs) {
            fs = compileOne(GL_FRAGMENT_SHADER, dialects[i].version,
                            dialects[i].precision, fragmentBody, errorOut, !last);
            if (fs) {
                versionLine = dialects[i].version;
                break;
            }
            glDeleteShader(vs);
            vs = 0;
        }
    }
    if (!versionLine) return 0;   // errorOut porte l'échec du dernier essai
    // Un dialecte a fini par passer : effacer l'erreur des tentatives ratées.
    if (errorOut) errorOut->clear();
    // Trace systématique : dit quel dialecte a réellement été accepté et ce
    // que le pilote annonçait (précieux sur une machine à problème).
    {
        const char* sl =
            reinterpret_cast<const char*>(glGetString(GL_SHADING_LANGUAGE_VERSION));
        std::string chosen(versionLine + std::strlen("#version "));
        while (!chosen.empty() && chosen.back() == '\n') chosen.pop_back();
        std::fprintf(stderr, "[CRT] GLSL %s (driver: %s)\n",
                     chosen.c_str(), sl ? sl : "?");
    }

    unsigned int prog = glCreateProgram();
    if (!prog) {
        glDeleteShader(vs);
        glDeleteShader(fs);
        if (errorOut) *errorOut = "glCreateProgram returned 0";
        return 0;
    }
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    // Épingle l'attribut position du quad plein écran sur l'emplacement 0
    // avant le link (l'appelant code glVertexAttribPointer(0, ...)).
    glBindAttribLocation(prog, 0, "aPos");
    glLinkProgram(prog);
    int ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    glDeleteShader(vs);
    glDeleteShader(fs);
    if (!ok) {
        char log[2048] = {0};
        int len = 0;
        glGetProgramInfoLog(prog, sizeof(log) - 1, &len, log);
        std::string msg = "shader link failed: ";
        msg.append(log, len);
        if (errorOut) *errorOut = msg;
        std::fprintf(stderr, "[CRT] %s\n", msg.c_str());
        glDeleteProgram(prog);
        return 0;
    }
    return prog;
}
