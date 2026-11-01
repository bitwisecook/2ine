/* Public domain. No rights reserved. Wholly AI-authored. */
/* Native runtime databases use host paths; guest profile names use PrfOpenProfile. */
#ifndef TWOINE_HOST_PROFILES_H
#define TWOINE_HOST_PROFILES_H
#include "os2libs/pmshapi.h"
HINI lx_profile_open_host(const char *name);
#endif
