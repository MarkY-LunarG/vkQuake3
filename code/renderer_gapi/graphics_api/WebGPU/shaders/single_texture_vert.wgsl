@id(2) override clip_plane : i32 = 0i;

override v : bool = (clip_plane != 0i);

struct gl_PerVertex {
  gl_Position : vec4<f32>,
}

var<private> v_1 : gl_PerVertex;

struct Transform_1 {
  clip_space_xform : mat4x4<f32>,
  eye_space_xform : mat3x4<f32>,
  clipping_plane : vec4<f32>,
}

@group(0u) @binding(0u) var<uniform> v_2 : Transform_1;

var<private> frag_clip_dist : f32;

var<private> frag_color : vec4<f32>;

var<private> frag_tex_coord : vec2<f32>;

fn main_inner(in_position : vec3<f32>, in_color : vec4<f32>, in_tex_coord : vec2<f32>) {
  var p : vec4<f32>;
  p = vec4<f32>(in_position.x, in_position.y, in_position.z, 1.0f);
  v_1.gl_Position = (v_2.clip_space_xform * p);
  if (v) {
    let v_3 = v_2.clipping_plane;
    let v_4 = (p * v_2.eye_space_xform);
    frag_clip_dist = dot(v_3, vec4<f32>(v_4.x, v_4.y, v_4.z, 1.0f));
  }
  frag_color = in_color;
  frag_tex_coord = in_tex_coord;
  v_1.gl_Position.y = -(v_1.gl_Position.y);
}

struct tint_symbol {
  @builtin(position)
  gl_Position : vec4<f32>,
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
  return tint_symbol(v_1.gl_Position, frag_clip_dist, frag_color, frag_tex_coord);
}
