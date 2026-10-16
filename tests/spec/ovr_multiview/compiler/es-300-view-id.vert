// [config]
// expect_result: pass
// glsl_version: 3.00 es
// require_extensions: GL_OVR_multiview
// check_link: false
// [end config]
//
// Test that gl_ViewID_OVR and the num_views layout qualifier are available
// in GLSL ES 3.00.
//

#version 300 es
#extension GL_OVR_multiview : require

layout(num_views = 2) in;

void main()
{
	gl_Position = vec4(1.0);
	gl_Position.x += float(gl_ViewID_OVR);
}
