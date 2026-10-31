// forza_injector: plugin API version 2 in the core (sdk/include/forza_plugin.h).
#ifndef FORZA_CORE_H
#define FORZA_CORE_H
#include "extra_options.h"
#include "forza_plugin.h"

// Once per process, after the core's own patches and before the game's code
// runs: loads the plugins that export ForzaPluginEarlyInitialize (found in
// their export tables without loading them) and calls it, in mod load order.
void forza_set_injector_version(const char *version);
void forza_plugins_early(unsigned char *game_image);
// Fills a version-2 context for a plugin around its version-1 context.
void forza_context_fill(ForzaContext *context, const FH2PluginContext *v1);
// TRUE when the DLL file at `path` exports `name` (read from the file).
BOOL forza_file_exports(const wchar_t *path, const char *name);
// Hands `frame` to every subscriber of `event`.
void forza_dispatch(uint32_t event, const void *frame);
// Subscribers of the vehicle event (the vehicle hook runs when there are any).
LONG forza_vehicle_subscribers(void);
#endif
