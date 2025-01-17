// [config]
// expect_result: fail
// glsl_version: 1.30
// require_extensions: GL_OVR_multiview
// check_link: false
// [end config]
//
// From the OVR_multiview spec:
//
//    "If this layout qualifier is declared more than once in the same shader,
//    all those declarations must set num_views to the same value; otherwise a
//    compile-time error results."
//

#version 130
#extension GL_OVR_multiview : require

layout(num_views = 3) in;
layout(num_views = 2) in;

void main() {
   gl_Position = vec4(1.0);
}
