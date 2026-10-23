#version 450

#ifdef GAPI_WEBGPU
// Group 0 has the vertex shader constants. The textures are in groups 1 and 2.
#define TEXTURE0_SET 1
#define TEXTURE1_SET 2
#else
#define TEXTURE0_SET 0
#define TEXTURE1_SET 1
#endif

layout(set = TEXTURE0_SET, binding = 0) uniform sampler2D texture0;

layout(location = 0) in vec4 frag_color;
layout(location = 1) in vec2 frag_tex_coord;
layout(location = 3) in float frag_clip_dist;

layout(location = 0) out vec4 out_color;

layout (constant_id = 0) const int alpha_test_func = 0;
layout (constant_id = 2) const int clip_plane = 0;

void main() {
    // Sample before the discard: WGSL permits texture sampling only in
    // uniform control flow.
    out_color = frag_color * texture(texture0, frag_tex_coord);

    if (clip_plane != 0 && frag_clip_dist < 0.0) discard;

    if (alpha_test_func == 1) {
        if (out_color.a == 0.0f) discard;
    } else if (alpha_test_func == 2) {
        if (out_color.a >= 0.5f) discard;
    } else if (alpha_test_func == 3) {
        if (out_color.a < 0.5f) discard;
    }
}
