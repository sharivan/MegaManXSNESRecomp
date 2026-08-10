#pragma once

/*
 * MSVC C compatibility declarations for the legacy Visual Studio harness.
 *
 * Some shared snesrecomp headers/functions are arranged in an order accepted
 * by the clang/gcc CMake builds but MSVC's C front-end diagnoses the first use
 * as an implicit int declaration. Force-including this tiny declaration shim
 * keeps the project source-compatible without changing the framework ABI.
 */
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

uint8 *RomPtr(uint32_t addr);
void RtlApuLock(void);
void RtlApuUnlock(void);

#ifdef __cplusplus
}
#endif
