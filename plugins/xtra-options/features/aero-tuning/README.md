# Aero Tuning

`AeroTuning.dll` owns the four `AERO_TUNING` byte patches and both native
front/rear range callbacks from Extra Options 0.4.4. It reads
`[Experiments] UnlockAeroTuning` from `XtraOptions.ini`; the default is `0`.

Enabling this experimental feature makes the downforce sliders available
and maps fixed aero to the matching race aero ranges and default. This can
change PI and handling, including at the default tuning position.

The two callbacks use 12-byte absolute jump thunks at RVAs `0x1061234`
and `0x1061064`. All four call-site patches and both thunks must validate
together before the feature changes the image. Native range selection,
queries and offsets are preserved from the original implementation.

Only the inspected FH2 1.0.0.64 image is supported. Stock Extra Options
already implements this feature, so the shared plugin entry points leave it
to that loader instead of installing a second set of hooks.

`tests/aero_tuning_test.c` exercises query selection, fixed-range eligibility,
invalid native pointers and the generated callback thunks without executing
game code. The Xtra Options suite additionally checks the real game's bytes
and applies the complete feature to a mapped copy of its executable.
