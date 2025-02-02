// [config]
// expect_result: pass
// glsl_version: 1.30
// require_extensions: GL_OVR_multiview
// check_link: false
// [end config]
//
// Test that gl_ViewID_OVR is available even back to GLSL 1.30
//

#version 130
#extension GL_OVR_multiview : require

layout(num_views = 2) in;

void main() {
   gl_Position = vec4(1.0);
   gl_Position.x = gl_Position.x + float(gl_ViewID_OVR);
}
