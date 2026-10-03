#pragma once

// deadbeef.h includes <unistd.h> for ssize_t, which is all it uses from it.
// MSVC has neither; this directory is on the include path for MSVC builds only.

#include <stddef.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef ptrdiff_t ssize_t;
#endif
