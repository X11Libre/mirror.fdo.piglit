
/**
 * \file egl-wayland.h
 * Common util for EGL/Wayland tests.
 */

#ifndef EGL_WAYLAND_H
#define EGL_WAYLAND_H

struct display;
struct wl_display;

struct display *
create_wayland_display(void);

struct wl_display *
get_wayland_native_display(struct display *d);

void *
create_wayland_window(struct display *display, int w, int h);

#endif
