// Offline tests of the native aero selection helpers. No game code is executed.
// SPDX-License-Identifier: GPL-3.0-or-later
#include <stdio.h>
#include <string.h>
#include "../aero_tuning.c"

static int failures;
static void check(int ok, const char *what) {
    printf("%s %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) ++failures;
}

int main(void) {
    char query[768];
    check(make_query(query, 123, 0, 1) &&
          strstr(query, "List_UpgradeCarBodyFrontBumper") &&
          strstr(query, "Current.CarBodyID=Race.CarBodyID") &&
          strstr(query, "Current.Id=123 AND Race.Level=3"),
          "front aero first seeks the matching body family and part id");
    check(make_query(query, 456, 1, 1) &&
          strstr(query, "List_UpgradeRearWing") &&
          strstr(query, "Current.Ordinal=Race.Ordinal") &&
          strstr(query, "Current.Id=456 AND Race.Level=3"),
          "rear aero first seeks the matching car ordinal and part id");
    check(make_query(query, 456, 1, 0) && !strstr(query, "Current") &&
          strstr(query, "A.Downforce1>A.Downforce0 ORDER BY Race.Sequence,Race.Id LIMIT 1"),
          "fallback still requires an adjustable race range in deterministic order");

    AeroDefinition fixed = {.available = 1, .downforce_min = 2, .downforce_max = 2,
                            .drag_min = 1, .drag_max = 3};
    check(fixed_downforce(&fixed), "available fixed aero is eligible");
    fixed.available = 0;
    check(!fixed_downforce(&fixed), "unavailable aero is ineligible");
    fixed.available = 1; fixed.downforce_max = 3;
    check(!fixed_downforce(&fixed), "already adjustable aero is unchanged");
    fixed.downforce_max = fixed.downforce_min;
    uint32_t invalid = 0x7f800000;
    memory_copy(&fixed.drag_max, &invalid, 4);
    check(!fixed_downforce(&fixed), "infinite drag is rejected");
    invalid = 0x7fc00001;
    memory_copy(&fixed.drag_max, &invalid, 4);
    check(!fixed_downforce(&fixed), "NaN drag is rejected");

    int copied = 0, source = 123;
    check(memory_read(&source, &copied, sizeof(source)) && copied == source &&
          !memory_read(NULL, &copied, 4) && !memory_read((void *)1, &copied, 4),
          "native reads accept valid storage and safely reject bad pointers");

    unsigned char *base = (unsigned char *)(uintptr_t)0x140000000;
    feature_prepare(base);
    uint32_t count;
    const ForzaPatch *patches = feature_extra("AERO_TUNING", &count);
    check(patches && count == 2 && patches[0].rva == 0x1061234 && patches[1].rva == 0x1061064,
          "front and rear thunks keep the original Extra Options RVAs");
    uintptr_t callbacks[] = {(uintptr_t)load_front, (uintptr_t)load_rear};
    for (unsigned i = 0; i < 2; ++i) {
        uintptr_t target;
        memory_copy(&target, patches[i].patched + 2, 8);
        check(patches[i].size == 12 && patches[i].patched[0] == 0x48 && patches[i].patched[1] == 0xb8 &&
              patches[i].patched[10] == 0xff && patches[i].patched[11] == 0xe0 && target == callbacks[i],
              "absolute jump thunk reaches the feature callback");
    }
    check(!feature_extra("OTHER", &count) && count == 0, "no patches are supplied for another feature group");
    return failures != 0;
}
