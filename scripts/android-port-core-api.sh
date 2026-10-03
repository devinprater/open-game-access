#!/usr/bin/env bash
# android-port-core-api.sh — retarget the borrowed Android shell to our melonDS.
#
# The shell is rafaelvcaetano/melonDS-android, written against melonDS 2025. Our
# emulator is NPO-197/melonDS-lua, which is NEWER (2026) and refactored the
# renderer: Renderer3D was folded into a single unified Renderer, the concrete
# classes gained a "3D"/2D split, and the per-renderer setters became one
# RendererSettings struct. Args.h changed in six places.
#
# This edits the SHELL, never the emulator: the Lua-enabled core is the thing we
# actually want, and it is already proven on iOS.
#
# Measured before/after on the real trees (see scripts/android-jit-check.sh for
# the assertions that run in CI).
#
# Usage: scripts/android-port-core-api.sh <frontend-dir>
set -euo pipefail

FRONTEND="${1:?usage: android-port-core-api.sh <frontend-dir>}"
CPP="$FRONTEND/app/src/main/cpp"
[ -d "$CPP" ] || { echo "!! no frontend sources at $CPP" >&2; exit 1; }

say() { printf '== %s\n' "$*"; }

# ---------------------------------------------------------------------------
# 1. Args.h shape: what the shell expects vs what our core provides.
#
#    shell (2025)                    our core (2026)
#    ------------------------------------------------------------------
#    ARM9BIOS = <(bios_arm9_bin)>    <(FreeBIOSGetNtrArm9())
#    ARM7BIOS = <(bios_arm7_bin)>    <(FreeBIOSGetNtrArm7())
#    Renderer3D = SoftRenderer()     Renderer = nullptr
#    NANDImage (required)            NANDImage (optional)
#    FullBIOSBoot = false            (removed)
#
#    The shell's EmulatorArgsBuilder names bios_arm9_bin / bios_arm7_bin
#    directly, so those two call sites change; the struct defaults we simply do
#    not override.
# ---------------------------------------------------------------------------
say "retargeting the BIOS loaders to our core's free-BIOS API"
python3 - "$CPP/EmulatorArgsBuilder.cpp" <<'PY'
import sys, re
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
before = s

# FreeBIOSGetNtrArm9()/Arm7() return a 16 KiB array by value; ARM9BIOSImage is
# std::array<u8, 16 KiB>, so the copy constructs the same thing bios_arm*_bin did.
s = s.replace(
    "return std::make_unique<ARM9BIOSImage>(bios_arm9_bin);",
    "// Our core (NPO-197/melonDS-lua, 2026) has no bios_arm9_bin symbol: the\n"
    "        // free BIOS comes from FreeBIOSGetNtrArm9(). Same array, same size.\n"
    "        return std::make_unique<ARM9BIOSImage>(FreeBIOSGetNtrArm9());")
s = s.replace(
    "return std::make_unique<ARM7BIOSImage>(bios_arm7_bin);",
    "// Same as above: the free ARM7 BIOS, not the shell's bios_arm7_bin symbol.\n"
    "        return std::make_unique<ARM7BIOSImage>(FreeBIOSGetNtrArm7());")

# DSiArgs: our core's NANDImage is std::optional and FullBIOSBoot is gone, so the
# brace-init has to name the right members and drop the trailing bool.
old_dsi = """        DSiArgs _dsiArgs = DSiArgs {
            std::move(*ndsArgs),
            std::move(arm9ibios),
            std::move(arm7ibios),
            std::move(*nand),
            std::move(sdcard),
            false,
            true,
        };

        std::unique_ptr<DSiArgs> uniqueArgs = std::make_unique<DSiArgs>(std::move(_dsiArgs));
        return uniqueArgs;"""
new_dsi = """        // Our core's DSiArgs takes the base args by value, then the two DSi BIOS
        // images, an optional NAND, an optional SD card and DSPHLE. There is no
        // FullBIOSBoot member any more, so the trailing `false, true` pair the
        // shell used does not fit -- set DSPHLE explicitly instead.
        DSiArgs _dsiArgs = DSiArgs {
            std::move(*ndsArgs),
            std::move(arm9ibios),
            std::move(arm7ibios),
            std::move(*nand),
            std::move(sdcard),
            true,   // DSPHLE
        };

        std::unique_ptr<DSiArgs> uniqueArgs = std::make_unique<DSiArgs>(std::move(_dsiArgs));
        return uniqueArgs;"""
if old_dsi in s:
    s = s.replace(old_dsi, new_dsi, 1)

# The function must return optional<unique_ptr<NDSArgs>>; a unique_ptr<DSiArgs>
# needs the move into an optional of the base type.
s = s.replace(
    "        std::unique_ptr<DSiArgs> uniqueArgs = std::make_unique<DSiArgs>(std::move(_dsiArgs));\n        return uniqueArgs;",
    "        std::unique_ptr<DSiArgs> uniqueArgs = std::make_unique<DSiArgs>(std::move(_dsiArgs));\n"
    "        // unique_ptr<DSiArgs> -> optional<unique_ptr<NDSArgs>>: DSiArgs derives\n"
    "        // from NDSArgs, so convert through the base pointer explicitly.\n"
    "        return std::optional<std::unique_ptr<NDSArgs>>(std::move(uniqueArgs));")

if s == before:
    print("   (no changes: already ported)")
else:
    open(p, "w", encoding="utf-8").write(s)
    print("   BIOS loaders + DSiArgs brace-init retargeted")
PY

# ---------------------------------------------------------------------------
# 2. The renderer. Renderer3D is gone; there is ONE Renderer, reached through
#    GPU::GetRenderer()/SetRenderer(), and it takes a RendererSettings struct.
#    The shell's 12 call sites in MelonInstance.cpp change accordingly.
# ---------------------------------------------------------------------------
say "retargeting the renderer API (Renderer3D -> the unified Renderer)"
python3 - "$CPP/MelonInstance.cpp" <<'PY'
import sys
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
before = s

# Accessor rename. Our core has no Renderer3D accessor at all.
s = s.replace("nds->GPU.GetRenderer3D()", "nds->GPU.GetRenderer()")

# The 3D-only classes were split: SoftRenderer3D / GLRenderer3D / ComputeRenderer3D.
# The unified Renderer is constructed with the NDS reference, and SetRenderer
# lives on NDS, not GPU, in this version.
s = s.replace("nds->GPU.SetRenderer3D(std::make_unique<SoftRenderer>());",
              "nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));")
# ENABLE_OGLRENDERER=OFF means GLRenderer and ComputeRenderer do not exist at all,
# so these cases cannot name them. Fall back to software, which is what the
# selection would have to be anyway on this core.
s = s.replace("nds->SetRenderer(std::make_unique<GLRenderer>(*nds, true));",
              "// No GL/compute renderer in this build; software is the only option.\n"
              "                nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));")
# Compute is not selected by upstream's own frontend any more; keep the software
# path rather than inventing a constructor that may not exist.
s = s.replace("nds->GPU.SetRenderer3D(ComputeRenderer::New());",
              "nds->SetRenderer(std::make_unique<SoftRenderer>(*nds));")

# Per-renderer settings became one struct passed to the renderer.
s = s.replace("static_cast<SoftRenderer&>(nds->GPU.GetRenderer()).SetThreaded(softwareRenderSettings.threadedRendering, nds->GPU);",
              "{ melonDS::RendererSettings rs {}; rs.Threaded = softwareRenderSettings.threadedRendering;\n"
              "              nds->GPU.GetRenderer().SetRenderSettings(rs); }")
s = s.replace("static_cast<GLRenderer&>(nds->GPU.GetRenderer()).SetRenderSettings(glRenderSettings.betterPolygons, glRenderSettings.scale);",
              "{ melonDS::RendererSettings rs {}; rs.BetterPolygons = glRenderSettings.betterPolygons;\n"
              "              rs.ScaleFactor = glRenderSettings.scale;\n"
              "              nds->GPU.GetRenderer().SetRenderSettings(rs); }")
s = s.replace("static_cast<ComputeRenderer&>(nds->GPU.GetRenderer()).SetRenderSettings(computeRenderSettings.scale,computeRenderSettings.highResCoordinates);",
              "{ melonDS::RendererSettings rs {}; rs.ScaleFactor = computeRenderSettings.scale;\n"
              "              rs.HiresCoordinates = computeRenderSettings.highResCoordinates;\n"
              "              nds->GPU.GetRenderer().SetRenderSettings(rs); }")

# `Accelerated` and `SetOutputTexture` no longer exist on the renderer. Software
# vs accelerated is decided by which renderer class is current.
# There is no accelerated renderer in this build, so this is a compile-time
# constant rather than a cast -- GLRenderer does not exist with OGL off.
s = s.replace("bool isRendererAccelerated = nds->GPU.GetRenderer().Accelerated;",
              "// No hardware renderer is built for Android (the core is compiled with\n"
              "    // ENABLE_OGLRENDERER=OFF: its GL is desktop GL, not GLES).\n"
              "    bool isRendererAccelerated = false;")

# GetScaleFactor is GONE from our core's renderer (the only such getter left is on
# the 3D renderer, which is no longer what we hold). The fork's own frontend reads
# the scale from configuration instead -- `.ScaleFactor = cfg.GetInt(...)` -- and
# the shell already carries the same value in its OpenGlRenderSettings. Use that;
# do not invent a getter on the unified Renderer.
s = s.replace("int scale = static_cast<GLRenderer &>(nds->GPU.GetRenderer()).GetScaleFactor();",
              "auto glRenderSettings = static_cast<OpenGlRenderSettings&>(*currentConfiguration->renderSettings);\n"
              "        int scale = glRenderSettings.scale;")

# SetOutputTexture no longer exists: our core's GL renderer composites into its
# OWN textures and exposes them through GetFramebuffers(). The shell handed the
# renderer the Android frame texture instead. Until the GL path is adapted, the
# software path is the one that produces pixels, so do not claim accelerated here.
s = s.replace("        int backBuffer = nds->GPU.FrontBuffer ? 0 : 1;\n"
              "        nds->GPU.GetRenderer().SetOutputTexture(backBuffer, renderFrame->frameTexture);",
              "        // SetOutputTexture is gone in our core: the GL renderer owns its\n"
              "        // textures and returns them through GetFramebuffers(), so the\n"
              "        // Android frame texture cannot be handed in this way. Software\n"
              "        // rendering is the supported path here for now (see the port\n"
              "        // script's note); accelerated output needs the GL path adapted.\n"
              "        isRendererAccelerated = false;")

# The software path then reads nds->GPU.Framebuffer[frontbuf][0..1]. Our core
# COMMENTED THAT OUT; GetFramebuffers() is the only route to pixels and it hands
# back the CURRENT front buffers, so there is no index to choose.
s = s.replace("""        int frontbuf = nds->GPU.FrontBuffer;
        if (nds->GPU.Framebuffer[frontbuf][0] && nds->GPU.Framebuffer[frontbuf][1])
        {
            glBindTexture(GL_TEXTURE_2D, renderFrame->frameTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds->GPU.Framebuffer[frontbuf][0].get());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 192 + 2, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, nds->GPU.Framebuffer[frontbuf][1].get());
            glBindTexture(GL_TEXTURE_2D, 0);
        }""",
"""        // FrontBuffer and Framebuffer[] are GONE from our core (upstream
        // commented the code out); GetFramebuffers() is the only route to pixels
        // and it hands back the CURRENT front buffers, so there is no index to
        // choose. It returns false for a renderer that does not use RAM buffers.
        void* top = nullptr;
        void* bottom = nullptr;
        if (nds->GPU.GetFramebuffers(&top, &bottom) && top && bottom)
        {
            glBindTexture(GL_TEXTURE_2D, renderFrame->frameTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, top);
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 192 + 2, 256, 192, GL_RGBA, GL_UNSIGNED_BYTE, bottom);
            glBindTexture(GL_TEXTURE_2D, 0);
        }""")

if s == before:
    print("   (no changes: already ported)")
else:
    open(p, "w", encoding="utf-8").write(s)
    print("   renderer accessors and settings retargeted")
PY

# ---------------------------------------------------------------------------
# 3. Includes the retargeted code now needs.
# ---------------------------------------------------------------------------
say "ensuring the needed headers are included"
python3 - "$CPP/MelonInstance.cpp" <<'PY'
import sys, os
p = sys.argv[1]
s = open(p, encoding="utf-8").read()
# ⛔ GPU_Soft.h / GPU_OpenGL.h, NOT the GPU3D_* headers. The GPU3D_Soft.h header
# only forward-declares SoftRenderer and defines SoftRenderer3D, so including it
# leaves the type incomplete ("allocation of incomplete type" at the make_unique).
# Only the software renderer. The core is built with ENABLE_OGLRENDERER=OFF for
# Android (our core's GL is stock desktop GL, not GLES -- see the note below), so
# GPU_OpenGL.h / GLRenderer do not exist in that build and must not be included.
need = []
for hdr, marker in (("GPU_Soft.h", "SoftRenderer"),):
    if marker in s and f'#include "{hdr}"' not in s:
        need.append(hdr)
if not need:
    print("   (nothing to add)")
else:
    lines = [l for l in s.split("\n")
             # GPU3D_Compute.h only defines ComputeRenderer3D in our core, and
             # the compute path is retargeted to software, so it is now unused.
             if l.strip() not in ('#include "GPU3D_Soft.h"',
                                  '#include "GPU3D_OpenGL.h"',
                                  '#include "GPU3D_Compute.h"',
                                  '#include "GPU_OpenGL.h"')]
    # insert after the last existing #include at the top
    last = max(i for i, l in enumerate(lines[:60]) if l.startswith("#include"))
    for h in reversed(need):
        lines.insert(last + 1, f'#include "{h}"')
    open(p, "w", encoding="utf-8").write("\n".join(lines))
    print("   added:", ", ".join(need))
PY

# ---------------------------------------------------------------------------
# 4. ScreenshotRenderer.cpp reads the SAME removed members. Fixing only the file
#    that happened to fail first leaves the next one to fail later; sweep.
# ---------------------------------------------------------------------------
say "retargeting the screenshot renderer (same removed members)"
python3 - "$CPP/renderer/ScreenshotRenderer.cpp" <<'PY'
import sys, os
p = sys.argv[1]
if not os.path.exists(p):
    print("   (no ScreenshotRenderer.cpp; nothing to do)"); sys.exit(0)
s = open(p, encoding="utf-8").read()
before = s
old = """        int frontBuffer = gpu->FrontBuffer;
        memcpy(screenshotBuffer, gpu->Framebuffer[frontBuffer][0].get(), 256 * 192 * 4);
        memcpy(&screenshotBuffer[256 * 192], gpu->Framebuffer[frontBuffer][1].get(), 256 * 192 * 4);"""
new = """        // Our core removed FrontBuffer and Framebuffer[]; GetFramebuffers()
        // returns the CURRENT front buffers, so there is no index to choose.
        void* top = nullptr;
        void* bottom = nullptr;
        if (gpu->GetFramebuffers(&top, &bottom) && top && bottom)
        {
            memcpy(screenshotBuffer, top, 256 * 192 * 4);
            memcpy(&screenshotBuffer[256 * 192], bottom, 256 * 192 * 4);
        }"""
if old in s:
    s = s.replace(old, new, 1)
if s == before:
    print("   (no changes: already ported)")
else:
    open(p, "w", encoding="utf-8").write(s)
    print("   screenshot framebuffer read retargeted")
PY

say "shell retargeted to our core's API"
