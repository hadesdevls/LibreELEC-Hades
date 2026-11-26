// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 LibreELEC Hades
//
// Stand-in of Kodi's system_gl.h for fpown_gl_test.cpp: OpenGL ES 3 with the extensions of GLES 2, as Kodi's own
// takes them for the GBM build of Hades (HAS_GLES=3: GLES2/gl2.h, GLES2/gl2ext.h, GLES3/gl3.h).
#pragma once

#if !defined(GL_GLEXT_PROTOTYPES)
#define GL_GLEXT_PROTOTYPES
#endif
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <GLES3/gl3.h>
