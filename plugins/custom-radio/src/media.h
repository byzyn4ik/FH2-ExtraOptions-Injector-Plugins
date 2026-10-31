// Custom Radio: FLAC, WAV, M4A (AAC/ALAC) and WMA files through Windows' Media Foundation.
// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CUSTOM_RADIO_MEDIA_H
#define CUSTOM_RADIO_MEDIA_H
#include "mp3.h"

// The kind of a music file by its extension.
enum { MEDIA_NONE = 0, MEDIA_MP3 = 1, MEDIA_FOUNDATION = 2 };
int media_kind(const wchar_t *name);

// 1 when Media Foundation (mfplat, mfreadwrite) is there; it is loaded on first use.
int media_available(void);

// Title, artist and length of a Media Foundation file: FLAC's Vorbis comments
// (TITLE, ARTIST) and STREAMINFO read from the file itself; the other formats'
// tags from Media Foundation's property handler (System.Title,
// System.Music.Artist) and the length from Media Foundation. The title is the
// file name without its extension when there is none. `track->name` must hold
// the file name.
void media_describe(const wchar_t *path, Track *track);

// Decodes the whole file to a 16-bit PCM WAV image (44-byte header + samples,
// from HeapAlloc): stereo (mono is up-mixed, more channels down-mixed by Media
// Foundation), at the file's rate, or 48000 / 44100 Hz for a higher one. NULL
// (and a log line) when it cannot.
void *media_decode(const wchar_t *path, DWORD *output_size);
#endif
