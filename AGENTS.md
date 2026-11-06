# Notes for coding agents writing piglit tests

This file is guidance for LLM agents (and the humans driving them) adding or
modifying tests in piglit.  The general project rules are in
[CONTRIBUTING.md](CONTRIBUTING.md) and [README.md](README.md); read those too.
This file focuses on the mistakes generated tests most often make.

The short version: piglit already has a helper for almost everything a GL
test needs (drawing, probing, shader building, error checking, subtests,
requirement checks).  A new test that defines its own `draw_rect()`,
`check_pixels()`, or `compile_shader()` is almost always wrong.  Find the
helper, or find an existing test that does the same thing and copy its
pattern.

## Pick the right kind of test

In order of preference:

1. **shader_runner test** (`*.shader_test`, see `tests/shaders/shader_runner.c`
   and the many examples under `tests/spec/*/execution/`).  If the test is
   "compile these shaders, set some uniforms/textures/buffers, draw, probe",
   write it as a shader_test.  These are discovered automatically: no CMake
   or `tests/opengl.py` change is needed, and they are easier to review.
2. **glslparser test** (`*.vert`, `*.frag`, ... with a `[config]` header) for
   "this shader must / must not compile".
3. **C test** using the piglit GL framework, for anything that needs API
   sequences shader_runner can't express.
4. **Python generator** under `generated_tests/` when the tests are many
   mechanical variants of one template.  Commit the generator, not only its
   output.

## C test skeleton

```c
/*
 * Copyright © 2026 Your Name
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * \file foo-bar.c
 *
 * What the test does, and why: which spec text or driver bug it covers.
 * Link the bug report if there is one.
 */

#include "piglit-util-gl.h"

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.supports_gl_core_version = 32;
	config.window_visual = PIGLIT_GL_VISUAL_RGBA | PIGLIT_GL_VISUAL_DOUBLE;
	config.khr_no_error_support = PIGLIT_NO_ERRORS;

PIGLIT_GL_TEST_CONFIG_END

void
piglit_init(int argc, char **argv)
{
	piglit_require_extension("GL_ARB_foo");
	/* one-time setup */
}

enum piglit_result
piglit_display(void)
{
	bool pass = true;

	/* draw, then probe */

	pass = piglit_check_gl_error(GL_NO_ERROR) && pass;

	piglit_present_results();

	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}
```

Style: 8-column tabs, at most 78 columns, `/* */` comments, return type on
its own line, opening brace of a function on its own line.  See
CONTRIBUTING.md.  Keep tests short; most piglit tests are under 300 lines.

## Use the existing utilities

Look in `tests/util/piglit-util-gl.h`, `tests/util/piglit-shader.h`, and
`tests/util/piglit-util.h` before writing any helper function.  Check
`piglit-util-egl.h` and `piglit-glx-util.h` for writing window-system specific
tests.

### Drawing

- `piglit_draw_rect(x, y, w, h)`, `piglit_draw_rect_z(z, x, y, w, h)`, and
  `piglit_draw_rect_tex(...)` draw a quad.  They work in compat, core, and
  GLES; in core profile they make their own VAO and VBO.
- In shaders, call the position input `piglit_vertex` and the texture
  coordinate input `piglit_texcoord`.  `piglit_build_simple_program*()` binds
  them to the locations `piglit_draw_rect*()` uses.
- To draw in window coordinates, use `piglit_ortho_projection()`
  (fixed-function) or `piglit_ortho_uniform()` / `piglit_gen_ortho_uniform()`
  (shaders).  Otherwise pass NDC (-1..1) to `piglit_draw_rect()`.  Don't
  hand-convert pixel rects to NDC and upload your own vertex buffer.
- Test textures: `piglit_rgbw_texture()`, `piglit_checkerboard_texture()`,
  `piglit_depth_texture()`, `piglit_miptree_texture()`,
  `piglit_integer_texture()`, `piglit_array_texture()`,
  `piglit_multisample_texture()`.

### Shaders

- `piglit_build_simple_program(vs, fs)` compiles and links, and FAILs the
  test with the info log if either step fails.  Pass NULL for a stage you
  don't need.
- If you need to do something before linking (`glTransformFeedbackVaryings`,
  `glBindFragDataLocation`, ...), use `piglit_build_simple_program_unlinked()`
  and then `glLinkProgram()` + `piglit_link_check_status()`.
- `piglit_compile_shader_text()` for a single shader object,
  `piglit_build_compute_program()` for compute.

### Probing results

Don't call glReadPixels and loop over pixels yourself.  The probe helpers
read back, compare with `piglit_tolerance` (set from the framebuffer's bit
depth), and print the location and the expected and observed values of the
first mismatch.  Reviewers expect to see that output.

- Solid regions: `piglit_probe_rect_rgba()`, `piglit_probe_rect_rgb()`,
  `piglit_probe_pixel_rgba()`, `piglit_probe_rect_rgba_int()` / `_uint()`
  for integer formats, `piglit_probe_rect_depth()`,
  `piglit_probe_rect_stencil()`.
- A whole expected image: fill a float array with the expected values and
  call `piglit_probe_image_rgba()` or `piglit_probe_image_color()` (which
  takes a format such as `GL_RED`).  This replaces "loop over pixels, work
  out which rect each one is in, compare".
- A test image compared against a reference image: see "Make the rendering
  show what went wrong" below.
- Textures and buffers: `piglit_probe_texel_rect_rgba()`,
  `piglit_probe_buffer()`.
- If the default tolerance is wrong for a format, set it with
  `piglit_set_tolerance_for_bits()`.  Don't hard-code an epsilon.

### Make the rendering show what went wrong

Someone debugging a failure will look at the window (run without `-auto`)
or at a dumped image before they read the code.  Make that picture useful.

- Draw what should be visible in green (blue for a second color) and what
  should not be visible in red.
- When a shader checks a value per pixel, don't just output a fixed red on
  failure.  Put information about the failure in the color, such as the bad
  value itself (scaled to 0..1 if the target is unorm), or which component
  or which check failed.  A failure pattern that shows the wrong value
  across the window often points straight at the bug.

  ```glsl
  if (distance(v, expected) < 0.01)
          color = vec4(0.0, 1.0, 0.0, 1.0);
  else
          color = vec4(1.0, v.x / 16.0, v.y / 16.0, 1.0); /* v is in 0..16 */
  ```

- Render something with visible structure (gradients, texture contents,
  several distinct regions), not a single solid green window.  A solid
  window that comes out green can hide a bug that happens to produce the
  "pass" color.
- A common piglit pattern: draw the result of the path under test in one
  half of the window, and a reference produced by a simpler path (plain
  vertex colors, a texture uploaded directly, fixed-function) in the other
  half, then compare them with
  `piglit_probe_rects_equal(x1, y1, x2, y2, w, h, format)`.  It reads back
  both regions, compares them with the framebuffer's tolerance, and prints
  the first mismatch.  The window shows the two images side by side, and
  the comparison has no hand-computed expected values that can be wrong.
  (`piglit_probe_rect_halves_equal_rgba()` is a shorthand for the
  side-by-side RGBA case.  Use the lower-level
  `piglit_compare_images_color()` only when the images are already in
  memory, for example read with `glGetTexImage`.)

### Errors and requirements

- `piglit_check_gl_error(GL_NO_ERROR)` (or the specific error you expect)
  instead of calling `glGetError()` yourself.
- `piglit_require_extension()`, `piglit_require_gl_version()`,
  `piglit_require_GLSL_version()`, and `piglit_require_minimum_getinteger()`
  report SKIP when the requirement isn't met.  Use them in `piglit_init()`.
- The `supports_gl_*_version` lines in the config block say what context the
  test needs.  Put the real minimum there, not a lower version plus a pile of
  `piglit_require_*` calls that add up to a higher version.
- **Every entry point and enum the test uses must come from that version or
  from an extension the test requires.**  For example, `glTexStorage2D` is in
  GL 4.2.  A test that declares `supports_gl_core_version = 32` and calls it
  must also do `piglit_require_extension("GL_ARB_texture_storage")`, or use
  `glTexImage2D`.  See "Test in a restricted environment" below for how to
  check this.

### Results

- SKIP means "this implementation doesn't have what the test needs".  FAIL
  means "the implementation is wrong".  An incomplete FBO is a FAIL when the
  spec requires the format combination to be renderable, and a SKIP only when
  `GL_FRAMEBUFFER_UNSUPPORTED` is allowed.
- `piglit_report_result()` exits the process.  Calling it from `piglit_init()`
  for setup failures is fine.  Don't call it from inside a subtest (see
  below).
- If the test draws to the window, call `piglit_present_results()` before
  returning from `piglit_display()`.
- Don't print anything on success.  On failure the probe helpers already say
  what went wrong.  Add at most one line of context, such as which iteration
  or which format failed.

### Sizes and run time

- Work out coordinates from `piglit_width` and `piglit_height` rather than
  hard-coding them.  The default window is 160x160.  Only change it with
  `config.window_width` / `config.window_height` when the test needs to.
- If you render to your own FBO, size it for the test, not for a real
  application's screen.  A 1920x1080 8x MSAA buffer, repeated across dozens
  of `tests/opengl.py` variants, makes the suite slow for everyone.  Use
  something like 256x256 unless the bug only shows up at larger sizes, and
  if it does, say why in a comment.
- The test must pass when run with `-fbo` (render to an FBO instead of the
  window), which is how most CI runs piglit.

## Subtests

Use subtests when one test binary checks several independent things.  This
lets the summary show what failed, lets the runner tell which subtest
crashed, and lets people rerun a single case with `-subtest`.

### Use the table-driven API

```c
static enum piglit_result
test_blit(void *data)
{
	...
	return pass ? PIGLIT_PASS : PIGLIT_FAIL;
}

static enum piglit_result
test_texelfetch(void *data)
{
	if (!piglit_is_extension_supported("GL_ARB_foo"))
		return PIGLIT_SKIP;
	...
}

static const struct piglit_subtest subtests[] = {
	{ "blit", "blit", test_blit, NULL },
	{ "texelfetch", "texelfetch", test_texelfetch, NULL },
	{ NULL }
};

PIGLIT_GL_TEST_CONFIG_BEGIN

	config.subtests = subtests;
	config.supports_gl_core_version = 32;
	...

PIGLIT_GL_TEST_CONFIG_END

enum piglit_result
piglit_display(void)
{
	return piglit_run_selected_subtests(subtests,
					    piglit_config->selected_subtests,
					    piglit_config->num_selected_subtests,
					    PIGLIT_SKIP);
}
```

Setting `config.subtests` gets you all of the following without writing any
code for it:

- `-list-subtests` prints the subtest names.
- `-subtest NAME` (repeatable) runs only the named subtests.  `NAME` is
  matched against the `option` field if it is set, otherwise against `name`.
- `piglit_run_selected_subtests()` prints the `enumerate subtests` list
  before running anything.  If the process crashes, the runner marks the
  subtest that was running as `crash` and the rest as `notrun`, instead of
  failing the whole test with no detail.
- Each subtest's result is printed in the format the runner parses, and the
  overall result is merged correctly.

Use the `data` pointer to share one function across parameterized subtests
(for example, one entry per format).  `tests/spec/ext_transform_feedback/max-varyings.c`
is an example.

### If you can't use the table

If the subtest list is only known at run time (it depends on queried limits
or a format list), keep the same three pieces yourself:

1. Build a NULL-terminated array of names and call
   `piglit_register_subtests(names)` before running any of them.
2. Report each one once with `piglit_report_subtest_result(result, "%s", name)`.
3. Accumulate the overall result with
   `piglit_merge_result(&overall, result)`, starting from `PIGLIT_SKIP`, and
   return `overall`.

Don't combine subtest results with `pass = pass && ok`.  That turns a
skipped subtest into a pass or a fail, and it loses WARN.
`piglit_merge_result()` applies FAIL > WARN > PASS > SKIP.
`PIGLIT_SUBTEST_CONDITION(cond, pass, ...)` and
`PIGLIT_SUBTEST_ERROR(err, pass, ...)` are fine for simple pass/fail checks.

### Rules for subtests

- Every enumerated subtest must report exactly one result.  One that never
  reports shows up as `notrun`.
- Subtest names must be unique and stable, because people use them in
  expectation files.  Use lowercase words separated by `-` or spaces.  Don't
  use `"` or `\`, because names are written into JSON without escaping.
- Inside a subtest, return `PIGLIT_SKIP` / `PIGLIT_FAIL`.  Don't call
  `piglit_report_result()` or `piglit_require_*()`: they exit the process and
  drop the remaining subtests.  Do requirements shared by every subtest in
  `piglit_init()`.
- Reset any GL state a subtest changes, so the subtests don't depend on the
  order they run in and `-subtest` works for each one alone.
- Prefer subtests to many `tests/opengl.py` command-line variants when the
  variants are cheap and share setup.  Use separate processes when variants
  need a different context or visual, or are slow enough to want parallel
  runs.

## Registering a C test

1. Add `piglit_add_executable (group-name source.c)` to the directory's
   `CMakeLists.gl.txt` (or `.gles2.txt`, ...).  Keep the list sorted.  The
   executable name is `<lowercase spec dir>-<test name>`, for example
   `ext_transform_feedback-render-to-texture-with-xfb-varyings`.
2. Add it to `tests/opengl.py` in the matching `group_manager` block, near
   related tests.  Tests run concurrently by default.  Pass
   `run_concurrent=False` only when the test really needs to run alone (for
   example front-buffer rendering, window-system interaction, or timing).
3. Check the profile entry produces the command line you expect:

   ```sh
   ./piglit print-cmd -t 'render-to-texture' tests/opengl.py
   ```

## Test in a restricted environment

Passing on your development driver doesn't show the test is correct.
Before submitting, run the test binary directly (`bin/<name> -auto -fbo`)
in each of the following configurations and read the output, not just the
result.

1. **The declared minimum version.**  Limit Mesa to the version in the config
   block:

   ```sh
   # supports_gl_core_version = 32
   MESA_GL_VERSION_OVERRIDE=3.2 MESA_GLSL_VERSION_OVERRIDE=150 bin/foo -auto -fbo
   # supports_gl_compat_version = 30
   MESA_GL_VERSION_OVERRIDE=3.0 MESA_GLSL_VERSION_OVERRIDE=130 bin/foo -auto -fbo
   # compat versions above 3.0 need the COMPAT suffix, e.g. 3.2COMPAT
   ```

   The test should still PASS.

2. **Without each optional extension.**  A version override doesn't hide
   extensions, so a test that uses `glTexStorage2D` will still pass at 3.2 if
   the driver exposes `GL_ARB_texture_storage`.  Disable the extensions the
   test uses one at a time:

   ```sh
   MESA_GL_VERSION_OVERRIDE=3.2 MESA_GLSL_VERSION_OVERRIDE=150 \
   MESA_EXTENSION_OVERRIDE="-GL_ARB_texture_storage" bin/foo -auto -fbo
   ```

   If the test requires the extension, the result should be a SKIP that
   names the missing extension.  Otherwise it should still pass.

3. **Look for the dispatch SKIP.**  If a test calls an entry point that the
   context doesn't support, piglit's dispatch layer prints
   `Function "glFoo" not supported on this implementation` and reports
   **SKIP**, not FAIL.  This is easy to miss.  Seeing that message under
   (1) or (2) means the test is missing a `piglit_require_*()` call or
   declares too low a version.

4. **Other targets the config claims.**  If the test sets
   `supports_gl_es_version`, run it on GLES too.  If it sets
   `khr_no_error_support = PIGLIT_NO_ERRORS`, run it with `-khr_no_error`.
   Only set `PIGLIT_NO_ERRORS` if the test never expects a GL error.

5. **A second driver.**  Run on a driver without the bug, such as llvmpipe
   (`LIBGL_ALWAYS_SOFTWARE=1`), to check the expected values aren't specific
   to one GPU.

6. **The bug.**  For a regression test, confirm it FAILs on the driver
   without the fix, and that the failure output points at the problem.  If
   you can't run the buggy driver, say so in the MR.  Also try breaking the
   expected value on purpose to check that the probe can fail at all.

7. **Subtests.**  Run `bin/foo -list-subtests` and `bin/foo -subtest X -auto -fbo`
   for at least one subtest.  Then run it through the framework to check the
   results file lists each subtest:

   ```sh
   ./piglit run -t 'foo' tests/opengl.py results/foo
   ./piglit summary console results/foo
   ```

## Commit messages and MRs

- Subject: `<spec dir or area>: <what was added or fixed>`, for example
  `ext_transform_feedback: add render-to-texture-with-xfb-varyings`.
- Body: what the test does and which bug it covers (driver, hardware, issue
  link).  Put the same explanation in the test's `\file` comment.
- One test per commit, together with its CMake and `tests/opengl.py`
  changes.  Don't mix formatting changes into functional changes.
