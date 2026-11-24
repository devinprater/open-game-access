/* glsm/glsmsym.h — OGA's GL-free stand-in for libretro-common's header.
 *
 * libretro.c (and vidext_libretro.c) include <glsm/glsmsym.h> unconditionally
 * for GLideN64's OpenGL context plumbing. The real header pulls in the GL
 * headers of whichever platform it is built for. This app builds the core's
 * SOFTWARE path only (angrylion RDP, see scripts/core-sources.sh), where
 * libretro.c never asks for a GL context: it calls glsm_ctl only when the RDP
 * plugin is GLideN64, which Core/n64_core.cpp never selects. So this header
 * declares exactly the names those two files use and nothing that needs GL,
 * and Core/n64/ sits FIRST on the include path (m64p_inc) so it wins.
 *
 * The enum is libretro-common's own glsm_state_ctl.h, which has no GL in it.
 * glsm_ctl itself is defined in Core/n64_m64p_stubs.c and answers false. */
#ifndef OGA_GLSM_SYM_SHIM_H
#define OGA_GLSM_SYM_SHIM_H

#include <stdbool.h>
#include <libretro.h>
#include <glsm/glsm_state_ctl.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef bool (*glsm_imm_vbo_draw)(void *);
typedef bool (*glsm_imm_vbo_disable)(void *);
typedef bool (*glsm_framebuffer_lock)(void *);

typedef struct glsm_ctx_params
{
   glsm_framebuffer_lock    framebuffer_lock;
   glsm_imm_vbo_draw        imm_vbo_draw;
   glsm_imm_vbo_disable     imm_vbo_disable;
   retro_hw_context_reset_t context_reset;
   retro_hw_context_reset_t context_destroy;
   retro_environment_t environ_cb;
   bool stencil;
   unsigned major;
   unsigned minor;
   enum retro_hw_context_type context_type;
} glsm_ctx_params_t;

#ifdef __cplusplus
}
#endif

#endif
