// The GL header the core is built with on PC (MELONDS_GL_HEADER, see desktop/CMakeLists.txt): the
// same GLES 3.2 headers as Android, but with the functions as pointers loaded at run time, since
// Windows has no GLES library to link against. The context is a real GLES 3.2 one (WGL's ES
// profile), so the renderers' "#version 320 es" shaders run unchanged.
#pragma once

#ifndef GL_GLES_PROTOTYPES
#define GL_GLES_PROTOTYPES 0
#endif
#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include "pc_gl_loader.h"

// what GPU3D_ComputeStats.h looks its extension functions up with, in place of eglGetProcAddress
void* PcGl_GetProcAddress(const char* name);
#define MELONDS_GL_GET_PROC_ADDRESS PcGl_GetProcAddress
