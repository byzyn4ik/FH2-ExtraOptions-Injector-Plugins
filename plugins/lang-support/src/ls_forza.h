// Language Support: the parts of forza_injector's plugin API version 2 it uses.
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The same layout as forza-injector/sdk/include/forza_plugin.h (ForzaContext,
// API version 2), declared here so the plugin builds without the injector's
// SDK. forza_injector calls ForzaPluginInitialize in place of
// FH2PluginInitialize when a DLL exports both, and ForzaPluginEarlyInitialize
// once the game image is mapped, before the game's code runs. Extra Options
// calls neither.
#pragma once
#include "fh2_plugin.h"

enum { LS_FORZA_GAME_FH2 = 1, LS_FORZA_GAME_FH2FF = 2 };

typedef struct LsForzaContext {
    uint32_t size;
    uint32_t api_version;
    const FH2PluginContext *v1;
    uint32_t game;
    const char *game_id;
    const char *title_id;
    uint32_t game_timestamp;
    void *host_image;
    uint32_t host_timestamp;
    uint32_t host_image_size;
    const char *injector_version;
    BOOL (*subscribe)(uint32_t event, void *callback, void *user);
} LsForzaContext;
