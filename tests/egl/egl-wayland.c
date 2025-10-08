/*
 * Copyright © 2024 Collabora, Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <wayland-client.h>
#include <wayland-egl.h>

struct display {
	struct wl_display *display;
	struct wl_registry *registry;
	struct wl_compositor *compositor;
};

struct window {
	struct wl_egl_window *native;
	struct wl_surface *surface;
};

static void
registry_handle_global(void *data, struct wl_registry *registry,
		       uint32_t name, const char *interface, uint32_t version)
{
	struct display *d = data;

	if (strcmp(interface, "wl_compositor") == 0) {
		d->compositor =
			wl_registry_bind(registry, name,
					 &wl_compositor_interface,
					 version < 4 ? 4 : version);
	}
}

static void
registry_handle_global_remove(void *data, struct wl_registry *registry,
			      uint32_t name)
{
}

static const struct wl_registry_listener registry_listener = {
	registry_handle_global,
	registry_handle_global_remove
};

struct display *
create_wayland_display(void)
{
	struct display *d = calloc(1, sizeof(struct display));
	if (!d) {
		fprintf(stderr, "Failed to allocate display structure\n");
		return NULL;
	}

	d->display = wl_display_connect(NULL);
	if (d->display == NULL) {
		fprintf(stderr, "Can't connect to display\n");
		free(d);
		return 0;
	}

	d->registry = wl_display_get_registry(d->display);
	wl_registry_add_listener(d->registry,
				 &registry_listener, d);

	wl_display_dispatch(d->display);
	wl_display_roundtrip(d->display);

	return d;
}

struct wl_display *
get_wayland_native_display(struct display *d)
{
	return d->display;
}

void *
create_wayland_window(struct display *d, int w, int h)
{
	struct window window = { 0 };

	window.surface = wl_compositor_create_surface(d->compositor);
	window.native = wl_egl_window_create(window.surface, w, h);

	fprintf(stderr, "created a window\n");

	wl_surface_commit(window.surface);
	wl_display_roundtrip(d->display);

	return window.native;
}
