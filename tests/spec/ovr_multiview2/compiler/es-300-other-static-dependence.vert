// [config]
// expect_result: pass
// glsl_version: 3.00 es
// require_extensions: GL_OVR_multiview2
// check_link: false
// [end config]
//
// From the OVR_multiview2 spec:
//
//    "Delete the paragraph which states:
//
//    It is a compile- or link-time error if any output variable other
//    than gl_Position is statically dependent on gl_ViewID_OVR."
//

#version 300 es
#extension GL_OVR_multiview2 : require

layout(num_views = 2) in;

out vec4 color;

void main()
{
	gl_Position = vec4(1.0);
	color = vec4(float(gl_ViewID_OVR));
}
