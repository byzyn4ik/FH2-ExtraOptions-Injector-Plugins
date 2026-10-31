// Performance for Forza Horizon 2 (Extra Options plugin).
// SPDX-License-Identifier: GPL-3.0-or-later
//
// The Nitrous gauge (nos_gauge.h): one quad per draw, the
// mode in Target.w. Output is premultiplied (blend ONE, INV_SRC_ALPHA).
//   1 (bar): Fast & Furious' FZMaterialPresetRadialNitrousBar pixel shader,
//     ported from its DXBC in Fast & Furious' executable: a ring segment on
//     the left of the speedometer, four segments, filled bottom to top in
//     A_Color_Nitrous_Fill (#2DB2FF), the rest white at 0.3; while boosting
//     (Active) the fill brightens towards its tip and a faint bluish light
//     spreads around the tip. Ratio = the tank, Active = the boost level.
//   2 (outline): Fast & Furious' HUD_Nos_Outline_Radial_Bar texture (BC7,
//     premultiplied, read from the player's install).
// build.py compiles "vs" (vs_5_0) and "ps" (ps_5_0) into obj/nos_shaders.h.
cbuffer Nos : register(b0) { float4 Rect; float4 Target; float4 Params; float4 Extra; };
// Target: back-buffer width, height, brightness (HDR), mode. Params: ratio,
// active, level, alpha. Extra: unused.
Texture2D Outline : register(t0);
SamplerState Linear : register(s0);

struct V { float4 position : SV_Position; float2 uv : TEXCOORD0; };
V vs(uint id : SV_VertexID) {
    float2 corner = float2(id & 1, id >> 1);
    float2 pixel = Rect.xy + corner * Rect.zw;
    V o;
    o.position = float4(pixel.x / Target.x * 2 - 1, 1 - pixel.y / Target.y * 2, 0, 1);
    o.uv = corner;
    return o;
}

float tick(float p) {
    float f = frac(saturate(p) * 4 - 0.5);
    return saturate((f - 0.468) * 83.333336) * saturate((0.532 - f) * 83.333336);
}

float4 colorize(float p, float A, float extra, float ratio, float active, float s, float halo) {
    const float4 fill = float4(0x2D, 0xB2, 0xFF, 0xFF) / 255.0;
    float unfilled = saturate((p - ratio) * 333.333344);
    float4 c = fill + unfilled * (float4(1, 1, 1, 0.3) - fill);
    float4 r4 = float4(1, 1, 1, 0.95 - 0.65 * unfilled);
    c = r4 + saturate(p * 333.333344) * (c - r4);
    A *= c.w;
    float3 rgb = (extra + c.rgb) * A;
    if (ratio > 0) {
        float filled = p < ratio ? 1 : 0;
        float q = p / ratio - 1;
        float g = saturate(filled > 0 ? q * 2 + 1 : 1 - ratio * 20 * q);
        float h = abs(g * cos(s * 3.14159) * active * 0.2) * g * (2 * active);
        float glow = filled * A * (g * 2 * active - h) + h * halo;
        rgb += glow * float3(0.6, 0.6, 1.0);
    }
    return float4(rgb, A);
}

float4 radial_bar(float2 uv, float ratio, float active) {
    float x = uv.x * 2 - 1, y = uv.y * 2 - 1;
    // The polar angle as Fast & Furious' shader computes it (its atan2).
    float a = min(abs(y), abs(x)) / max(max(abs(y), abs(x)), 1e-9);
    float w = a * a;
    float poly = (((0.020835 * w - 0.085133) * w + 0.180141) * w - 0.330299) * w + 0.999866;
    float t = a * poly + (abs(y) < abs(x) ? a * poly * -2 + 1.570796 : 0);
    t += -y < y ? -3.14159274 : 0;
    bool flip = max(-y, x) >= -max(-y, x) && min(-y, x) < -min(-y, x);
    float theta = flip ? -t : t;
    float r = sqrt(x * x + y * y);
    float p = (theta * 0.159155 + 0.347) * 4.22833;
    float span = saturate((p + 0.001) * 333.333344) * saturate((-p + 1.001) * 333.333344);
    float outer = saturate((0.8 - r) * 100), outer_tick = saturate((0.71 - r) * 100);
    float A = (outer + tick(p) * (outer_tick - outer)) * saturate((r - 0.732) * 100) * span;
    // Fast & Furious adds the tip's light (h) at zero alpha over its whole
    // quad: a wedge reaching into its own NOS dial. Forza Horizon 2's
    // speedometer fills that disc, so the light is kept to the ring's band
    // (r 0.70..0.84): nothing but the bar is drawn over the speedometer.
    float halo = saturate((r - 0.70) * 40) * saturate((0.84 - r) * 40);
    return colorize(p, A, saturate((0.732 - r) * 50), ratio, active, saturate((r - 0.702) * 7.812502), halo);
}

float4 ps(V input) : SV_Target {
    float4 c;
    uint mode = (uint)Target.w;
    if (mode == 1)
        c = radial_bar(input.uv, Params.x, Params.y) * Params.w;
    else
        c = Outline.Sample(Linear, input.uv).bgra * Params.w;
    return float4(c.rgb * Target.z, c.a);
}
