#pragma once

// OUTPUT is before the separate OSD/subtitle overlay composition. Dynamic
// uniforms keep one compiled pipeline across fades, dragging and window sizes.
inline constexpr const char* kBlurCloudShader = R"CUE(
//!PARAM cloud_alpha
//!TYPE DYNAMIC float
//!MINIMUM 0
//!MAXIMUM 1
0
//!PARAM cloud_x
//!TYPE DYNAMIC float
0.5
//!PARAM cloud_y
//!TYPE DYNAMIC float
0.8
//!PARAM cloud_rx
//!TYPE DYNAMIC float
0.5
//!PARAM cloud_ry
//!TYPE DYNAMIC float
0.15
//!PARAM cloud_sx
//!TYPE DYNAMIC float
0.01
//!PARAM cloud_sy
//!TYPE DYNAMIC float
0.02
//!PARAM cue_on
//!TYPE DYNAMIC float
0
//!PARAM cue_dark
//!TYPE DYNAMIC float
0
//!PARAM cue_px
//!TYPE DYNAMIC float
1
//!PARAM cue_gray
//!TYPE DYNAMIC float
0
//!PARAM cue_cell
//!TYPE DYNAMIC float
16
//!PARAM cue_ax
//!TYPE DYNAMIC float
0.5
//!PARAM cue_ay
//!TYPE DYNAMIC float
0.5
//!PARAM cue_ar
//!TYPE DYNAMIC float
10
//!PARAM cue_bx
//!TYPE DYNAMIC float
0.5
//!PARAM cue_by
//!TYPE DYNAMIC float
0.5
//!PARAM cue_br
//!TYPE DYNAMIC float
0

//!HOOK OUTPUT
//!WHEN cloud_alpha 0 >
//!BIND HOOKED
//!SAVE CLOUD_H
//!WIDTH HOOKED.w 2 /
//!HEIGHT HOOKED.h 2 /
//!DESC Cue cloud continuous horizontal Gaussian
vec4 hook() {
    vec4 original = HOOKED_tex(HOOKED_pos);
    vec2 q = abs(HOOKED_pos - vec2(cloud_x,cloud_y));
    if (cloud_alpha < 0.0001 || q.x > cloud_rx + 3.0*cloud_sx || q.y > cloud_ry + 3.0*cloud_sy) return original;
    // Half-resolution texture, densely sampled at subpixel spacing for a soft 6 px radius.
    // Pair adjacent taps through bilinear filtering: no sparse square-grid replicas.
    vec3 sum = original.rgb;
    float norm = 1.0;
    for (int i=1; i<=21; i+=2) {
        float a=float(i), b=a+1.0;
        float wa=exp(-0.5*a*a/49.0), wb=exp(-0.5*b*b/49.0);
        float weight=wa+wb;
        float offset=(a+wb/weight)*cloud_sx/7.0;
        vec2 lo=clamp(HOOKED_pos-vec2(offset,0.0),HOOKED_pt*.5,vec2(1.0)-HOOKED_pt*.5);
        vec2 hi=clamp(HOOKED_pos+vec2(offset,0.0),HOOKED_pt*.5,vec2(1.0)-HOOKED_pt*.5);
        sum += (HOOKED_tex(lo).rgb+HOOKED_tex(hi).rgb)*weight;
        norm += 2.0*weight;
    }
    return vec4(sum/norm,original.a);
}

//!HOOK OUTPUT
//!WHEN cloud_alpha 0 >
//!BIND CLOUD_H
//!SAVE CLOUD_V
//!WIDTH CLOUD_H.w
//!HEIGHT CLOUD_H.h
//!DESC Cue cloud continuous vertical Gaussian
vec4 hook() {
    vec4 original=CLOUD_H_tex(CLOUD_H_pos);
    vec2 q=abs(CLOUD_H_pos-vec2(cloud_x,cloud_y));
    if (cloud_alpha < 0.0001 || q.x>cloud_rx || q.y>cloud_ry) return original;
    vec3 sum=original.rgb;
    float norm=1.0;
    for (int i=1; i<=21; i+=2) {
        float a=float(i), b=a+1.0;
        float wa=exp(-0.5*a*a/49.0), wb=exp(-0.5*b*b/49.0);
        float weight=wa+wb;
        float offset=(a+wb/weight)*cloud_sy/7.0;
        vec2 lo=clamp(CLOUD_H_pos-vec2(0.0,offset),CLOUD_H_pt*.5,vec2(1.0)-CLOUD_H_pt*.5);
        vec2 hi=clamp(CLOUD_H_pos+vec2(0.0,offset),CLOUD_H_pt*.5,vec2(1.0)-CLOUD_H_pt*.5);
        sum += (CLOUD_H_tex(lo).rgb+CLOUD_H_tex(hi).rgb)*weight;
        norm += 2.0*weight;
    }
    return vec4(sum/norm,original.a);
}

//!HOOK OUTPUT
//!WHEN cloud_alpha 0 >
//!BIND HOOKED
//!BIND CLOUD_V
//!DESC Cue frameless radial cloud composition
vec4 hook() {
    vec4 original=HOOKED_tex(HOOKED_pos);
    vec2 q=(HOOKED_pos-vec2(cloud_x,cloud_y))/max(vec2(cloud_rx,cloud_ry),vec2(.000001));
    float mask=.35*cloud_alpha*(1.0-smoothstep(.25,1.0,length(q)));
    if(mask<.0001) return original;
    vec3 softened=CLOUD_V_tex(CLOUD_V_pos).rgb;
    return vec4(mix(original.rgb,softened*.95,mask),original.a);
}

//!HOOK OUTPUT
//!WHEN cue_on 0 >
//!BIND HOOKED
//!DESC Cue snap / subtitle edit: big pixels, grey, dark; the grey can spread in a pixel ring
// A ring of grey blocks grows from (ax, ay) as ar goes 0..1 (>= 9: everywhere at once); a second
// ring from (bx, by) gives the colour back as br goes 0..1. Edges step in four levels, in blocks.
float cue_ring(vec2 b, vec2 c, float k, vec2 sz, float cell) {
    float far = length(max(c, sz - c));
    float r = k * (far + 4.0 * cell);
    float x = clamp((r - length(b - c)) / (3.0 * cell), 0.0, 1.0);
    return ceil(x * 4.0) / 4.0;
}
vec4 hook() {
    vec2 cell = max(cue_px, 1.0) * HOOKED_pt;
    vec2 p = cue_px > 1.0 ? (floor(HOOKED_pos / cell) + 0.5) * cell : HOOKED_pos;
    vec4 c = HOOKED_tex(clamp(p, HOOKED_pt * .5, vec2(1.0) - HOOKED_pt * .5));
    float m = 1.0;
    if (cue_ar < 9.0 || cue_br > 0.0) {
        vec2 sz = HOOKED_size;
        float bc = max(cue_cell, 1.0);
        vec2 b = (floor(HOOKED_pos * sz / bc) + 0.5) * bc;
        if (cue_ar < 9.0) m = cue_ring(b, vec2(cue_ax, cue_ay) * sz, cue_ar, sz, bc);
        if (cue_br > 0.0) m *= 1.0 - cue_ring(b, vec2(cue_bx, cue_by) * sz, cue_br, sz, bc);
    }
    float luma = dot(c.rgb, vec3(.2126, .7152, .0722));
    vec3 rgb = mix(c.rgb, vec3(luma), clamp(cue_gray, 0.0, 1.0) * m);
    return vec4(rgb * (1.0 - clamp(cue_dark, 0.0, 1.0) * m), c.a);
}
)CUE";
