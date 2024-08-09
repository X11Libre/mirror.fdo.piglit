// [config]
// expect_result: pass
// glsl_version: 3.30
// require_extensions: GL_ARB_bindless_texture GL_ARB_shader_image_load_store
// [end config]

#version 330
#extension GL_ARB_bindless_texture: require
#extension GL_ARB_shader_image_load_store: enable

uniform uvec2 handle;

out vec4 finalColor;

void main()
{
	/* Test that using an image constructor directly in the imageStore
	 * builtin compiles as expected.
	 */
	imageStore(image2D(handle), ivec2(0, 0), vec4(1, 2, 3, 4));
}
