@id(2) override clip_plane : i32 = 0i;

override v : bool = (clip_plane != 0i);

@id(0) override alpha_test_func : i32 = 0i;

override v_1 : bool = (alpha_test_func == 1i);

override v_2 : bool = (alpha_test_func == 2i);

override v_3 : bool = (alpha_test_func == 3i);

var<private> out_color : vec4<f32>;

@group(1u) @binding(1u) var texture0_sampler : sampler;

@group(1u) @binding(0u) var texture0_image : texture_2d<f32>;

fn main_inner(frag_color : vec4<f32>, frag_tex_coord : vec2<f32>, frag_clip_dist : f32) {
  out_color = (frag_color * textureSample(texture0_image, texture0_sampler, frag_tex_coord));
  var v_4 : bool;
  if (v) {
    v_4 = (frag_clip_dist < 0.0f);
  } else {
    v_4 = v;
  }
  if (v_4) {
    discard;
    return;
  }
  if (v_1) {
    if ((out_color.w == 0.0f)) {
      discard;
      return;
    }
  } else if (v_2) {
    if ((out_color.w >= 0.5f)) {
      discard;
      return;
    }
  } else if (v_3) {
    if ((out_color.w < 0.5f)) {
      discard;
      return;
    }
  }
}

@fragment
fn main(@location(0u) frag_color : vec4<f32>, @location(1u) frag_tex_coord : vec2<f32>, @location(3u) frag_clip_dist : f32) -> @location(0u) vec4<f32> {
  main_inner(frag_color, frag_tex_coord, frag_clip_dist);
  return out_color;
}
