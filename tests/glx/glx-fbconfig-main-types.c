/*
 * SPDX-License-Identifier: MIT
 */

/** @file glx-fbconfig-main-types.c
 *
 * Check that the main fbconfig types exist: an RGBA8 window config with
 * double buffering and depth/stencil, plus its variants for each feature the
 * driver offers anywhere (single buffering, every advertised sample count,
 * sRGB, pixmaps, pbuffers, texture-from-pixmap). A feature that no
 * config has is skipped, so this only fails when a feature exists but not on
 * the main config. Each type must also be found by glXChooseFBConfig, and
 * every matching config must give a context whose framebuffer matches it.
 */

#include "piglit-util-gl.h"
#include "piglit-glx-util.h"

int piglit_width = 32;
int piglit_height = 32;

struct config {
	GLXFBConfig fbc;
	int render_type, drawable_type, doublebuffer;
	int r, g, b, a, depth, stencil;
	int sample_buffers, samples;
	int srgb, tfp_rgb, tfp_rgba;
};

struct type {
	const char *name;
	bool alpha, doublebuffer, srgb, tfp_rgb, tfp_rgba;
	int drawable, samples;
};

static Display *dpy;
static struct config *configs;
static int num_configs;
static bool have_srgb_ext, have_tfp_ext, have_composite;

static void
get_configs(void)
{
	GLXFBConfig *fbcs = glXGetFBConfigs(dpy, DefaultScreen(dpy), &num_configs);

	configs = calloc(num_configs, sizeof(*configs));
	for (int i = 0; i < num_configs; i++) {
		struct config *c = &configs[i];

		c->fbc = fbcs[i];
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_RENDER_TYPE, &c->render_type);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_DRAWABLE_TYPE, &c->drawable_type);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_DOUBLEBUFFER, &c->doublebuffer);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_RED_SIZE, &c->r);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_GREEN_SIZE, &c->g);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_BLUE_SIZE, &c->b);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_ALPHA_SIZE, &c->a);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_DEPTH_SIZE, &c->depth);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_STENCIL_SIZE, &c->stencil);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_SAMPLE_BUFFERS, &c->sample_buffers);
		glXGetFBConfigAttrib(dpy, c->fbc, GLX_SAMPLES, &c->samples);
		if (have_srgb_ext)
			glXGetFBConfigAttrib(dpy, c->fbc, GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB, &c->srgb);
		if (have_tfp_ext) {
			glXGetFBConfigAttrib(dpy, c->fbc, GLX_BIND_TO_TEXTURE_RGB_EXT, &c->tfp_rgb);
			glXGetFBConfigAttrib(dpy, c->fbc, GLX_BIND_TO_TEXTURE_RGBA_EXT, &c->tfp_rgba);
		}
		if (!c->sample_buffers)
			c->samples = 0;
	}
	XFree(fbcs);
}

static bool
is_rgba(const struct config *c)
{
	return (c->render_type & GLX_RGBA_BIT) && !(c->render_type & GLX_RGBA_FLOAT_BIT_ARB);
}

static bool
matches(const struct config *c, const struct type *t)
{
	return is_rgba(c) &&
	       (c->drawable_type & t->drawable) == t->drawable &&
	       c->r == 8 && c->g == 8 && c->b == 8 &&
	       c->a == (t->alpha ? 8 : 0) &&
	       !!c->doublebuffer == t->doublebuffer &&
	       c->samples == t->samples &&
	       (!t->srgb || c->srgb) &&
	       (!t->tfp_rgb || c->tfp_rgb) &&
	       (!t->tfp_rgba || c->tfp_rgba) &&
	       /* depth/stencil only matter for window rendering */
	       (!(t->drawable & GLX_WINDOW_BIT) || (c->depth >= 24 && c->stencil >= 8));
}

static bool
choose_finds(const struct type *t)
{
	int attribs[64], n = 0, num = 0;
	bool found = false;
	GLXFBConfig *chosen;

	attribs[n++] = GLX_RENDER_TYPE; attribs[n++] = GLX_RGBA_BIT;
	attribs[n++] = GLX_DRAWABLE_TYPE; attribs[n++] = t->drawable;
	attribs[n++] = GLX_RED_SIZE; attribs[n++] = 8;
	attribs[n++] = GLX_GREEN_SIZE; attribs[n++] = 8;
	attribs[n++] = GLX_BLUE_SIZE; attribs[n++] = 8;
	attribs[n++] = GLX_ALPHA_SIZE; attribs[n++] = t->alpha ? 8 : 0;
	attribs[n++] = GLX_DOUBLEBUFFER; attribs[n++] = t->doublebuffer;
	attribs[n++] = GLX_SAMPLE_BUFFERS; attribs[n++] = t->samples ? 1 : 0;
	attribs[n++] = GLX_SAMPLES; attribs[n++] = t->samples;
	if (t->drawable & GLX_WINDOW_BIT) {
		attribs[n++] = GLX_DEPTH_SIZE; attribs[n++] = 24;
		attribs[n++] = GLX_STENCIL_SIZE; attribs[n++] = 8;
	}
	if (t->srgb) {
		attribs[n++] = GLX_FRAMEBUFFER_SRGB_CAPABLE_ARB; attribs[n++] = True;
	}
	if (t->tfp_rgb) {
		attribs[n++] = GLX_BIND_TO_TEXTURE_RGB_EXT; attribs[n++] = True;
	}
	if (t->tfp_rgba) {
		attribs[n++] = GLX_BIND_TO_TEXTURE_RGBA_EXT; attribs[n++] = True;
	}
	attribs[n++] = None;

	chosen = glXChooseFBConfig(dpy, DefaultScreen(dpy), attribs, &num);
	for (int i = 0; i < num && !found; i++) {
		for (int j = 0; j < num_configs && !found; j++)
			found = configs[j].fbc == chosen[i] && matches(&configs[j], t);
	}
	XFree(chosen);
	return found;
}

static bool
check_framebuffer(const struct config *c)
{
	GLint r = 0, g = 0, b = 0, a = 0, depth = 0, stencil = 0, samples = 0;
	GLboolean doublebuffer = GL_FALSE;
	bool ok = true;

	glGetIntegerv(GL_RED_BITS, &r);
	glGetIntegerv(GL_GREEN_BITS, &g);
	glGetIntegerv(GL_BLUE_BITS, &b);
	glGetIntegerv(GL_ALPHA_BITS, &a);
	glGetIntegerv(GL_DEPTH_BITS, &depth);
	glGetIntegerv(GL_STENCIL_BITS, &stencil);
	glGetIntegerv(GL_SAMPLES, &samples);
	glGetBooleanv(GL_DOUBLEBUFFER, &doublebuffer);

	if (r != c->r || g != c->g || b != c->b || a != c->a ||
	    depth != c->depth || stencil != c->stencil) {
		fprintf(stderr, "  rgba/depth/stencil %d%d%d%d/%d/%d, fbconfig has "
			"%d%d%d%d/%d/%d\n", r, g, b, a, depth, stencil,
			c->r, c->g, c->b, c->a, c->depth, c->stencil);
		ok = false;
	}
	if (samples != c->samples) {
		fprintf(stderr, "  GL_SAMPLES %d, fbconfig has %d\n", samples, c->samples);
		ok = false;
	}
	if (!!doublebuffer != !!c->doublebuffer) {
		fprintf(stderr, "  GL_DOUBLEBUFFER %d, fbconfig has %d\n",
			doublebuffer, c->doublebuffer);
		ok = false;
	}
	if (c->srgb && piglit_is_extension_supported("GL_EXT_framebuffer_sRGB")) {
		GLboolean srgb = GL_FALSE;

		glGetBooleanv(GL_FRAMEBUFFER_SRGB_CAPABLE_EXT, &srgb);
		if (!srgb) {
			fprintf(stderr, "  GL_FRAMEBUFFER_SRGB_CAPABLE_EXT is false\n");
			ok = false;
		}
	}
	return piglit_check_gl_error(GL_NO_ERROR) && ok;
}

/* make a context current on a drawable of the type's kind and check that GL sees the fbconfig */
static bool
check_context(const struct config *c, const struct type *t)
{
	static const int pbuffer_attribs[] = {
		GLX_PBUFFER_WIDTH, 32, GLX_PBUFFER_HEIGHT, 32, None
	};
	XVisualInfo *vinfo = glXGetVisualFromFBConfig(dpy, c->fbc);
	GLXDrawable draw = None;
	Window win = None;
	Pixmap pixmap = None;
	GLXContext ctx;
	bool ok = false;
	int config_id = 0;

	glXGetFBConfigAttrib(dpy, c->fbc, GLX_FBCONFIG_ID, &config_id);

	if (t->drawable & GLX_WINDOW_BIT) {
		if (vinfo) {
			win = piglit_get_glx_window_unmapped(dpy, vinfo);
			draw = glXCreateWindow(dpy, c->fbc, win, NULL);
		}
	} else if (t->drawable & GLX_PIXMAP_BIT) {
		int depth = vinfo ? vinfo->depth : (c->a ? 32 : 24);

		pixmap = XCreatePixmap(dpy, DefaultRootWindow(dpy), 32, 32, depth);
		draw = glXCreatePixmap(dpy, c->fbc, pixmap, NULL);
	} else {
		draw = glXCreatePbuffer(dpy, c->fbc, pbuffer_attribs);
	}

	ctx = glXCreateNewContext(dpy, c->fbc, GLX_RGBA_TYPE, NULL, True);
	if (draw && ctx && glXMakeContextCurrent(dpy, draw, draw, ctx)) {
		piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);
		ok = check_framebuffer(c);
		glXMakeContextCurrent(dpy, None, None, NULL);
	} else {
		fprintf(stderr, "  couldn't make a context current\n");
	}
	if (!ok)
		fprintf(stderr, "%s: fbconfig 0x%x\n", t->name, config_id);

	if (ctx)
		glXDestroyContext(dpy, ctx);
	if (t->drawable & GLX_WINDOW_BIT) {
		if (draw)
			glXDestroyWindow(dpy, draw);
		if (win)
			XDestroyWindow(dpy, win);
	} else if (t->drawable & GLX_PIXMAP_BIT) {
		glXDestroyPixmap(dpy, draw);
		XFreePixmap(dpy, pixmap);
	} else if (draw) {
		glXDestroyPbuffer(dpy, draw);
	}
	XFree(vinfo);
	return ok;
}

/* reports "<type>", "<type> choose" and "<type> context" as separate results */
static void
check_type(enum piglit_result *result, const struct type *t, bool is_offered)
{
	enum piglit_result exists = PIGLIT_SKIP, choose = PIGLIT_SKIP, context = PIGLIT_SKIP;

	/* windows with alpha only exist through Composite's ARGB visuals */
	if ((t->drawable & GLX_WINDOW_BIT) && t->alpha && !have_composite)
		is_offered = false;

	if (is_offered) {
		exists = PIGLIT_FAIL;
		for (int i = 0; i < num_configs; i++) {
			if (!matches(&configs[i], t))
				continue;
			exists = PIGLIT_PASS;
			if (!check_context(&configs[i], t))
				context = PIGLIT_FAIL;
			else if (context == PIGLIT_SKIP)
				context = PIGLIT_PASS;
		}
		if (exists == PIGLIT_FAIL)
			fprintf(stderr, "%s: no such fbconfig\n", t->name);
		else if (choose_finds(t))
			choose = PIGLIT_PASS;
		else {
			fprintf(stderr, "%s: exists, but glXChooseFBConfig "
				"doesn't return it\n", t->name);
			choose = PIGLIT_FAIL;
		}
	}

	piglit_report_subtest_result(exists, "%s", t->name);
	piglit_report_subtest_result(choose, "%s choose", t->name);
	piglit_report_subtest_result(context, "%s context", t->name);
	piglit_merge_result(result, exists);
	piglit_merge_result(result, choose);
	piglit_merge_result(result, context);
}

typedef bool (*config_pred)(const struct config *c);

static bool has_rgb8(const struct config *c) { return c->r == 8 && c->a == 0; }
static bool has_single_window(const struct config *c) { return !c->doublebuffer && (c->drawable_type & GLX_WINDOW_BIT); }
static bool has_srgb(const struct config *c) { return c->srgb; }
static bool has_pixmap(const struct config *c) { return c->drawable_type & GLX_PIXMAP_BIT; }
static bool has_pbuffer(const struct config *c) { return c->drawable_type & GLX_PBUFFER_BIT; }
static bool has_tfp_rgba(const struct config *c) { return c->tfp_rgba; }
static bool has_tfp_rgb8(const struct config *c) { return c->tfp_rgb && c->a == 0; }

/* whether any RGBA config has the property, i.e. whether the driver offers it */
static bool
offered(config_pred pred)
{
	for (int i = 0; i < num_configs; i++) {
		if (is_rgba(&configs[i]) && pred(&configs[i]))
			return true;
	}
	return false;
}

int
main(int argc, char **argv)
{
	enum piglit_result result = PIGLIT_SKIP;
	bool sample_counts[65] = { false };
	char name[128];

	dpy = XOpenDisplay(NULL);
	if (!dpy) {
		fprintf(stderr, "couldn't open display\n");
		piglit_report_result(PIGLIT_FAIL);
	}
	piglit_require_glx_version(dpy, 1, 3);
	have_srgb_ext = piglit_is_glx_extension_supported(dpy, "GLX_ARB_framebuffer_sRGB") ||
			piglit_is_glx_extension_supported(dpy, "GLX_EXT_framebuffer_sRGB");
	have_tfp_ext = piglit_is_glx_extension_supported(dpy, "GLX_EXT_texture_from_pixmap");
	int opcode, event, error;
	have_composite = XQueryExtension(dpy, "Composite", &opcode, &event, &error);

	get_configs();

	for (int i = 0; i < num_configs; i++) {
		if (is_rgba(&configs[i]) && (configs[i].drawable_type & GLX_WINDOW_BIT) &&
		    configs[i].samples > 0 && configs[i].samples < (int)ARRAY_SIZE(sample_counts))
			sample_counts[configs[i].samples] = true;
	}

	const struct type base = {
		"rgba8 window", .alpha = true, .doublebuffer = true,
		.drawable = GLX_WINDOW_BIT,
	};
	check_type(&result, &base, true);

	struct type t = base;
	t.name = "rgb8 window";
	t.alpha = false;
	check_type(&result, &t, offered(has_rgb8));

	t = base;
	t.name = "rgba8 window single-buffered";
	t.doublebuffer = false;
	check_type(&result, &t, offered(has_single_window));

	t = base;
	t.name = "rgba8 window sRGB";
	t.srgb = true;
	check_type(&result, &t, offered(has_srgb));

	for (int s = 1; s < (int)ARRAY_SIZE(sample_counts); s++) {
		if (!sample_counts[s])
			continue;

		t = base;
		snprintf(name, sizeof(name), "rgba8 window %dx msaa", s);
		t.name = name;
		t.samples = s;
		check_type(&result, &t, true);

		t.srgb = true;
		snprintf(name, sizeof(name), "rgba8 window %dx msaa sRGB", s);
		check_type(&result, &t, offered(has_srgb));
	}

	t = base;
	t.name = "rgba8 pixmap";
	t.drawable = GLX_PIXMAP_BIT;
	t.doublebuffer = false;
	check_type(&result, &t, offered(has_pixmap));

	t.name = "rgba8 pixmap bind-to-texture rgba";
	t.tfp_rgba = true;
	check_type(&result, &t, offered(has_tfp_rgba));

	t.name = "rgb8 pixmap bind-to-texture rgb";
	t.alpha = false;
	t.tfp_rgba = false;
	t.tfp_rgb = true;
	check_type(&result, &t, offered(has_tfp_rgb8));

	t = base;
	t.name = "rgba8 pbuffer";
	t.drawable = GLX_PBUFFER_BIT;
	check_type(&result, &t, offered(has_pbuffer));

	free(configs);
	XCloseDisplay(dpy);
	piglit_report_result(result);
	return 0;
}
