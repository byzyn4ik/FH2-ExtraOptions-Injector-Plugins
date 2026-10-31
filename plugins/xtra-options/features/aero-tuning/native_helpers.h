// Local helpers for the standalone aero feature; no injector implementation dependency.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef XTRA_AERO_NATIVE_HELPERS_H
#define XTRA_AERO_NATIVE_HELPERS_H
#include "forza_patch.h"

static void memory_copy(void *destination, const void *source, size_t size) {
    unsigned char *out = destination;
    const unsigned char *in = source;
    while (size--) *out++ = *in++;
}

// ReadProcessMemory also handles a stale native object pointer without dereferencing it here.
static int memory_read(const void *address, void *out, size_t size) {
    SIZE_T read = 0;
    return address && out && size <= UINTPTR_MAX - (uintptr_t)address &&
           ReadProcessMemory(GetCurrentProcess(), address, out, size, &read) && read == size;
}

static int memory_read_pointer(const void *address, void **value) {
    return memory_read(address, value, sizeof(*value)) && *value;
}

static int float_is_finite(float value) {
    uint32_t bits;
    memory_copy(&bits, &value, sizeof(bits));
    return (bits & 0x7f800000u) != 0x7f800000u;
}
#endif
