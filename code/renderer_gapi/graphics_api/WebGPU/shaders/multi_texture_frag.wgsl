@id(2) override clip_plane : i32 = 0i;

override v : bool = (clip_plane != 0i);

@id(1) override color_op : i32 = 0i;

override v_1 : bool = (color_op != 0i);

@id(0) override alpha_test_func : i32 = 0i;

override v_2 : bool = (alpha_test_func == 1i);

override v_3 : bool = (alpha_test_func == 2i);

override v_4 : bool = (alpha_test_func == 3i);

@group(1u) @binding(1u) var texture0_sampler : sampler;

@group(1u) @binding(0u) var texture0_image : texture_2d<f32>;

@group(2u) @binding(1u) var texture1_sampler : sampler;

@group(2u) @binding(0u) var texture1_image : texture_2d<f32>;

var<private> out_color : vec4<f32>;

fn main_inner(frag_color : vec4<f32>, frag_tex_coord0 : vec2<f32>, frag_tex_coord1 : vec2<f32>, frag_clip_dist : f32) {
  var color_a : vec4<f32>;
  var color_b : vec4<f32>;
  color_a = (frag_color * textureSample(texture0_image, texture0_sampler, frag_tex_coord0));
  color_b = textureSample(texture1_image, texture1_sampler, frag_tex_coord1);
  var v_5 : bool;
  if (v) {
    v_5 = (frag_clip_dist < 0.0f);
  } else {
    v_5 = v;
  }
  if (v_5) {
    discard;
    return;
  }
  if (v_1) {
    let v_6 = (color_a.xyz + color_b.xyz);
    out_color = vec4<f32>(v_6.x, v_6.y, v_6.z, (color_a.w * color_b.w));
  } else {
    out_color = (color_a * color_b);
  }
  if (v_2) {
    if ((out_color.w == 0.0f)) {
      discard;
      return;
    }
  } else if (v_3) {
    if ((out_color.w >= 0.5f)) {
      discard;
      return;
    }
  } else if (v_4) {
    if ((out_color.w < 0.5f)) {
      discard;
      return;
    }
  }
}

@fragment
fn main(@location(0u) frag_color : vec4<f32>, @location(1u) frag_tex_coord0 : vec2<f32>, @location(2u) frag_tex_coord1 : vec2<f32>, @location(3u) frag_clip_dist : f32) -> @location(0u) vec4<f32> {
  main_inner(frag_color, frag_tex_coord0, frag_tex_coord1, frag_clip_dist);
  return out_color;
}
