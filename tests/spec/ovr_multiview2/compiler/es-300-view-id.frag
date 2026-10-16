// [config]
// expect_result: pass
// glsl_version: 3.00 es
// require_extensions: GL_OVR_multiview2
// check_link: false
// [end config]
//
// From the OVR_multiview2 spec:
//
//    "If the OVR_multiview2 extension is enabled, the OVR_multiview extension
//    is also implicitly enabled."
//
// so gl_ViewID_OVR is available in a GLSL ES 3.00 fragment shader.
//

#version 300 es
#extension GL_OVR_multiview2 : require

precision mediump float;

out vec4 frag_color;

void main()
{
	frag_color = vec4(float(gl_ViewID_OVR));
}
