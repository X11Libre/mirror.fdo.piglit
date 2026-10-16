// [config]
// expect_result: fail
// glsl_version: 3.00 es
// require_extensions: GL_OVR_multiview
// check_link: false
// [end config]
//
// Test that gl_ViewID_OVR is not available without enabling the extension.
//

#version 300 es

void main()
{
	gl_Position = vec4(float(gl_ViewID_OVR));
}
