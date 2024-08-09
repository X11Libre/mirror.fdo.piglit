// [config]
// expect_result: pass
// glsl_version: 3.30
// require_extensions: GL_ARB_bindless_texture
// [end config]

#version 330
#extension GL_ARB_bindless_texture: require

uniform uvec2 handleOffset;

out vec4 finalColor;

void main()
{
	/* Test that using a sampler constructor directly in the texture
	 * builtin compiles as expected.
	 */
	finalColor = texture(samplerCube(handleOffset), vec3(0));
}
