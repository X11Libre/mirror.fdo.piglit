// [config]
// expect_result: fail
// glsl_version: 3.10 es
// require_extensions: GL_EXT_YUV_target
// [end config]
//

#version 300 es
#extension GL_EXT_YUV_target : require

out highp vec4 color;

void main()
{
    yuvCscStandardEXT csc = itu_601;
    if (itu_601 == itu_709)
        color = vec4(0.0, 1.0, 0.0, 1.0);
    else
        color = vec4(1.0, 0.0, 0.0, 1.0);
}
