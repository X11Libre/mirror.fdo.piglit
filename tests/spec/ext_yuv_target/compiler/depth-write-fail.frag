// [config]
// expect_result: fail
// glsl_version: 3.10 es
// require_extensions: GL_EXT_YUV_target
// [end config]
//

#version 300 es
#extension GL_EXT_YUV_target : require

layout(yuv) out highp vec4 color;

void main()
{
    color = vec4(0.0, 1.0, 0.0, 1.0);
    gl_FragDepth = 0.5;
}
