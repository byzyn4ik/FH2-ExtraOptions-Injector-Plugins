// Xtra Options: what a feature's own code (features/<feature>/*.c, listed in
// feature.json "code") gives src/patch_feature.c.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef XTRA_FEATURE_CODE_H
#define XTRA_FEATURE_CODE_H
#include "forza_patch.h"
#include "forza_plugin.h"

// Only for a feature whose feature.json sets "context": true. Called once,
// before feature_prepare, with forza_injector's context (its log, the game
// image) and XtraOptions.ini's path, for settings beyond the group keys.
void feature_context(const ForzaContext *context, const wchar_t *ini);
// Called once with the game image, before any group is applied: fills the
// bytes only known at run time (addresses of the feature's callbacks).
void feature_prepare(unsigned char *game_image);
// The patches of `group` (an Extra Options group name) that the code adds
// to its byte patches, applied with them all or nothing; NULL and *count 0
// for none.
const ForzaPatch *feature_extra(const char *group, uint32_t *count);
#endif
