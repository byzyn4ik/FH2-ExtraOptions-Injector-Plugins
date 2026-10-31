# FH2 runtime provider

`src/` is the coupled game runtime extracted from the injector's Extra Options
0.4.4 base. The source now belongs to the plugin repository. Its native feature
modules, settings ABI, file verification, transactions and deferred plugin loader
retain their previous behavior. `ownership.json` excludes groups implemented by
the independent DLLs; all transaction passes enforce that ownership.

`src/dllmain.c` exports the optional provider API defined by the injector SDK's
`forza_runtime.h`. It returns 0 for a different package/build, 1 after successful
FH2 initialization, and -1 after a claimed failure. Once initialized it cannot
be initialized again. Ordinary v1/v2 plugin entry points are harmless, so stock
Extra Options and this provider's own deferred loader do not duplicate hooks.

`legacy-plugins/` preserves the original bundled Weather Control and Garage
Engine Rev plugin source. `tools/` preserves the original mod/API documentation
and utilities. These reference assets are not automatically compiled or shipped
by Xtra Options. `NOTICE` records the original source and license attribution.

Only FH2 1.0.0.64 is claimed. The generic injector can initialize other games
without this runtime; it never substitutes FH2 addresses into another title.

Run the parent build with `--test`; `tests/run_tests.py` uses the actual FH2 EXE
without executing game code. Individual code modules may also provide focused
offline tests, such as `features/aero-tuning/tests/aero_tuning_test.c`.
