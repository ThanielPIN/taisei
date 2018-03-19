/*
 * This software is licensed under the terms of the MIT-License
 * See COPYING for further information.
 * ---
 * Copyright (c) 2011-2018, Lukas Weber <laochailan@web.de>.
 * Copyright (c) 2012-2018, Andrei Alexeyev <akari@alienslab.net>.
 */

#pragma once
#include "taisei.h"

// This is a wrapper header to include cglm the "right way".
// Please include this in any C file where access to the glm_* functions is needed.
// You don't need to include this if you just want the types.

#include "util.h"

#define TAISEI_USE_GLM_CALLS

#ifdef TAISEI_USE_GLM_CALLS
    #include <cglm/call.h>
    #include <cglm/glm2call.h>
#else
    PRAGMA(GCC diagnostic push)
    PRAGMA(GCC diagnostic ignored "-Wdeprecated-declarations")
    #include <cglm/cglm.h>
    PRAGMA(GCC diagnostic pop)
#endif
