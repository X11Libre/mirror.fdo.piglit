// [config]
// expect_result: fail
// glsl_version: 3.00 es
// require_extensions: GL_OVR_multiview !GL_OVR_multiview2
// check_link: true
// [end config]
//
// From the OVR_multiview spec:
//
//    "It is a compile- or link-time error if any output variable other
//    than gl_Position is statically dependent on gl_ViewID_OVR."
//
// and:
//
//    "NOTE: Implementations that also support OVR_multiview2 may not
//    generate an error if these conditions are violated, even if the
//    OVR_multiview2 extension is not enabled."
//

#version 300 es
#extension GL_OVR_multiview : require

layout(num_views = 2) in;

out vec4 color;

void main()
{
	gl_Position = vec4(1.0);
	color = vec4(float(gl_ViewID_OVR));
}
