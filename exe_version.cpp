//==========================================================================================================
// exe_version.cpp - This embeds our version number inside the executable to parse out later
//==========================================================================================================
#include "changelog.h"

// set this variable as "used" so GCC will not throw an "unused variable" warning during build
static const char __attribute__ ((used)) *EXE_TAG = "$$$>>>EXE_VERSION:" SW_VERSION;
//==========================================================================================================

