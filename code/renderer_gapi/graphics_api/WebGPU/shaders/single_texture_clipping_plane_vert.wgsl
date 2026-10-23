enable clip_distances;

struct gl_PerVertex {
  gl_Position : vec4<f32>,
  gl_ClipDistance : array<f32, 1u>,
}

var<private> v : gl_PerVertex;

struct Transform_1 {
  clip_space_xform : mat4x4<f32>,
  eye_space_xform : mat3x4<f32>,
  clipping_plane : vec4<f32>,
}

@group(0u) @binding(0u) var<uniform> v_1 : Transform_1;

var<private> frag_clip_dist : f32;

var<private> frag_color : vec4<f32>;

var<private> frag_tex_coord : vec2<f32>;

fn main_inner(in_position : vec3<f32>, in_color : vec4<f32>, in_tex_coord : vec2<f32>) {
  var p : vec4<f32>;
  p = vec4<f32>(in_position.x, in_position.y, in_position.z, 1.0f);
  v.gl_Position = (v_1.clip_space_xform * p);
  let v_2 = v_1.clipping_plane;
  let v_3 = (p * v_1.eye_space_xform);
  v.gl_ClipDistance[0i] = dot(v_2, vec4<f32>(v_3.x, v_3.y, v_3.z, 1.0f));
  frag_clip_dist = v.gl_ClipDistance[0i];
  frag_color = in_color;
  frag_tex_coord = in_tex_coord;
  v.gl_Position.y = -(v.gl_Position.y);
}

struct tint_symbol {
  @builtin(position)
  gl_Position : vec4<f32>,
  @builtin(clip_distances)
  gl_ClipDistance : array<f32, 1u>,
  @location(3u)
  frag_clip_dist : f32,
  @location(0u)
  frag_color : vec4<f32>,
  @location(1u)
  frag_tex_coord : vec2<f32>,
}

@vertex
fn main(@location(0u) in_position : vec3<f32>, @location(1u) in_color : vec4<f32>, @location(2u) in_tex_coord : vec2<f32>) -> tint_symbol {
  main_inner(in_position, in_color, in_tex_coord);
  return tint_symbol(v.gl_Position, v.gl_ClipDistance, frag_clip_dist, frag_color, frag_tex_coord);
}
