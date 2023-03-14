/*
 * Copyright © 2010 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    jian.j.zhao@intel.com
 */

/*
 *  Simple test case on the intel_swap_event.
 *  
 */

#include "piglit-util-gl.h"
#include "piglit-glx-util.h"
#include "GL/glx.h"
#include <sys/time.h>


/* return current time (in seconds) */
static double
current_time(void)
{
    return (double) piglit_time_get_nano() / 1000000000.0;
}

#define STACK_L 10

static GLboolean fullscreen = GL_FALSE;    /* Create a fullscreen window */
static GLboolean verbose = GL_FALSE;    /* Disable verbose.  */
static GLboolean Automatic = GL_FALSE;    /* Test automatically.  */
static GLboolean interval_0 = GL_FALSE;   /* Test with swap interval 0.  */
int event_base, Glx_event, count=0, swap_count=0, event_count=0;
int  event_count_total=0, frames_total=0;
double swap_start[STACK_L],swap_returned[STACK_L];
char * swap_event_type=NULL;
double start_time;

/** Draw single frame, do SwapBuffers, compute FPS */
static void
draw_frame(Display *dpy, Window win)
{
    static int frames = 0;
    double t = current_time();

    GLfloat duration = t - start_time;
    if (duration >= 3.0) {
        if (Automatic) {
		printf("glXSwapBuffers is called %d times and there were "
		       "%d swap events received in past %3.1f seconds.\n",
		       swap_count, event_count, duration);
		if (event_count != 0) {
			printf("swap type was %s.\n", swap_event_type);
			piglit_report_result(PIGLIT_PASS);
		} else {
			piglit_report_result(PIGLIT_FAIL);
		}
        }
        start_time = t;
        frames = 0;
        swap_count= 0;
        event_count= 0;
    }
         
    if (frames_total & 1) {
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glColor3f(1.0f, 1.0f, 1.0f);
    } else {
        glClearColor(1.0f, 0.0f, 0.0f, 0.0f);
        glColor3f(1.0f, 0.0f, 0.0f);
    }
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    count=frames_total%STACK_L;
    swap_start[count] = current_time();
    glXSwapBuffers(dpy, win);
    swap_returned[count] = current_time();
    
    frames++;
    frames_total++;
    swap_count++;
}

/**
 * Remove window border/decorations.
 */
static void
no_border( Display *dpy, Window w)
{
    static const unsigned MWM_HINTS_DECORATIONS = (1 << 1);
    static const int PROP_MOTIF_WM_HINTS_ELEMENTS = 5;
    
    typedef struct
    {
        unsigned long       flags;
        unsigned long       functions;
        unsigned long       decorations;
        long                inputMode;
        unsigned long       status;
    } PropMotifWmHints;
    
    PropMotifWmHints motif_hints;
    Atom prop, proptype;
    unsigned long flags = 0;
    
    /* setup the property */
    motif_hints.flags = MWM_HINTS_DECORATIONS;
    motif_hints.decorations = flags;
    
    /* get the atom for the property */
    prop = XInternAtom( dpy, "_MOTIF_WM_HINTS", True );
    if (!prop) {
        /* something went wrong! */
        return;
    }
    
    /* not sure this is correct, seems to work, XA_WM_HINTS didn't work */
    proptype = prop;
    
    XChangeProperty( dpy, w,                         /* display, window */
                     prop, proptype,                 /* property, type */
                     32,                             /* format: 32-bit datums */
                     PropModeReplace,                /* mode */
                     (unsigned char *) &motif_hints, /* data */
                     PROP_MOTIF_WM_HINTS_ELEMENTS    /* nelements */
                   );
}


/*
 * Create an RGB, double-buffered window.
 * Return the window and context handles.
 */
static void
make_window( Display *dpy, const char *name,
             int x, int y, int width, int height,
             Window *winRet, GLXContext *ctxRet, GLXWindow *glxWinRet)
{
    int attribs[] = {
            GLX_DRAWABLE_TYPE, GLX_WINDOW_BIT,
            GLX_RENDER_TYPE,   GLX_RGBA_BIT,
            GLX_DOUBLEBUFFER,  True,  /* Request a double-buffered color buffer with */
            GLX_RED_SIZE,      1,     /* the maximum number of bits per component    */
            GLX_GREEN_SIZE,    1,
            GLX_BLUE_SIZE,     1,
            None };

    int scrnum, nelements;
    XSetWindowAttributes attr;
    unsigned long mask;
    Window root;
    Window win;
    GLXContext ctx;
    XVisualInfo *visinfo;
    GLXFBConfig *fbc;
    GLXWindow gwin;
    Bool ret;
    
    scrnum = DefaultScreen( dpy );
    root = RootWindow( dpy, scrnum );
    
    if (fullscreen) {
        x = 0; y = 0;
        width = DisplayWidth( dpy, scrnum );
        height = DisplayHeight( dpy, scrnum );
    }
    
    fbc = glXChooseFBConfig(dpy, scrnum, attribs, &nelements);
    if (!fbc) {
	    printf("Error: couldn't get framebuffer config\n");
	    piglit_report_result(PIGLIT_FAIL);
    }
    visinfo = glXGetVisualFromFBConfig(dpy, fbc[0]);
    if (!visinfo) {
        XFree(fbc);
        printf("Error: couldn't get an RGB, Double-buffered visual\n");
        piglit_report_result(PIGLIT_SKIP);
    }
    ctx = glXCreateNewContext(dpy, fbc[0], GLX_RGBA_TYPE, 0, GL_TRUE);

    /* window attributes */
    attr.background_pixel = 0;
    attr.border_pixel = 0;
    attr.colormap = XCreateColormap( dpy, root, visinfo->visual, AllocNone);
    attr.event_mask = StructureNotifyMask | ExposureMask | KeyPressMask;
    /* XXX this is a bad way to get a borderless window! */
    mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

    win = XCreateWindow( dpy, root, x, y, width, height,
                     0, visinfo->depth, InputOutput,
                     visinfo->visual, mask, &attr );
    XMapWindow(dpy, win);
    gwin = glXCreateWindow(dpy, fbc[0], win, attribs);
    ret = glXMakeContextCurrent(dpy, gwin, gwin, ctx);
    if (!ret)
	    printf("make current failed: %d\n", glGetError());
    glXSelectEvent(dpy, gwin, GLX_BUFFER_SWAP_COMPLETE_INTEL_MASK);

    if (fullscreen)
        no_border(dpy, win);

    
    XFree(visinfo);
    XFree(fbc);
    
    *winRet = win;
    *ctxRet = ctx;
    *glxWinRet = gwin;
}


static int64_t last_sbc;

/**
 * Only handle one glx event.
 */
void
handle_event(Display *dpy, Window win, XEvent *event)
{
    (void) dpy;

    if ( Glx_event == event->type) {
        XEvent * event_p=event;
        GLXBufferSwapComplete * glx_event=(GLXBufferSwapComplete *) event_p;
        static double t_last=-1.0;
        double_t time = current_time();
        if (t_last < 0) {
            t_last=time;
        }
        if ( time - t_last >= 3.0) {
            if ( verbose ) {
                count=event_count_total%STACK_L;
                printf("It receives the recent event at %lf seconds, and that\
 glXSwapBuffers was called at %lf seconds, its swap returned at %lf seconds, so\
 the total time of glXSwapBuffers takes is %lf seconds.\n", time,
swap_start[count], swap_returned[count], (time - swap_start[count]));
            }
            t_last=time;
        }

        if (glx_event->drawable != win) {
            printf("Error: swap event was not on X11 Drawable\n");
            piglit_report_result(PIGLIT_FAIL);
        }

	if (glx_event->sbc == 0) {
		printf("Error: swap event returned 0 swap count\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	if (glx_event->sbc == last_sbc) {
		printf("Error: swap event count did not change\n");
		piglit_report_result(PIGLIT_FAIL);
	}

	last_sbc = glx_event->sbc;

	if (verbose)
		printf("swap event: ust %lld\tmsc %lld\tsbc %lld\n",
		       (unsigned long long)glx_event->ust,
		       (unsigned long long)glx_event->msc,
		       (unsigned long long)glx_event->sbc);
        switch (glx_event->event_type) {
        case GLX_EXCHANGE_COMPLETE_INTEL:
            swap_event_type="GLX_EXCHANGE_COMPLETE_INTEL";
            event_count++;
            event_count_total++;
            break;
        case GLX_COPY_COMPLETE_INTEL:
            swap_event_type="GLX_COPY_COMPLETE_INTEL";
            event_count++;
            event_count_total++;
            break;
        case GLX_FLIP_COMPLETE_INTEL:
            swap_event_type="GLX_FLIP_COMPLETE_INTEL";
            event_count++;
            event_count_total++;
            break;
        }
    }
}


static void
event_loop(Display *dpy, GLXWindow glxWin, Window win)
{
    start_time = current_time();

    while (1) {
        while (XPending(dpy) > 0) {
            XEvent event;
            XNextEvent(dpy, &event);
            Glx_event=event_base + GLX_BufferSwapComplete;
            handle_event(dpy, win, &event);
        }
        
        draw_frame(dpy, glxWin);
    }
}


static void
usage(void)
{
    printf("Usage:\n");
    printf("  -fullscreen             run in fullscreen mode\n");
    printf("  -v       verbose mode, have more log\n");
    printf("  -auto       test automatically \n");
    printf(" --interval-0    test swap event delivery with swap interval 0\n");
}
 

int
main(int argc, char *argv[])
{
    unsigned int winWidth = 30, winHeight = 30;
    int x = 0, y = 0;
    Display *dpy;
    Window win;
    GLXWindow glxWin;
    GLXContext ctx;
    char *dpyName = NULL;
    int i, error_base, ret;
    
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-auto") == 0) {
            Automatic = GL_TRUE;
        }
        else if (strcmp(argv[i], "-v") == 0) {
            verbose = GL_TRUE;
        }
        else if (strcmp(argv[i], "-fullscreen") == 0) {
            fullscreen = GL_TRUE;
        }
        else if (strcmp(argv[i], "--interval-0") == 0) {
            interval_0 = GL_TRUE;
        }
        else {
            usage();
            piglit_report_result(PIGLIT_SKIP);
        }
    }
    
    dpy = XOpenDisplay(dpyName);
    if (!dpy) {
        printf("Error: couldn't open display %s\n",
            dpyName ? dpyName : getenv("DISPLAY"));
        piglit_report_result(PIGLIT_FAIL);
    }
    
    make_window(dpy, "Swap event test", x, y, winWidth, winHeight, &win, &ctx, &glxWin);
    
    piglit_require_glx_extension(dpy, "GLX_INTEL_swap_event");
    
    glXQueryExtension(dpy, &error_base, &event_base);
    
    if (interval_0) {
        piglit_require_glx_extension(dpy, "GLX_INTEL_swap_event");
        PFNGLXSWAPINTERVALMESAPROC pglXSwapIntervalMESA = (PFNGLXSWAPINTERVALMESAPROC)
            glXGetProcAddressARB((const GLubyte *)"glXSwapIntervalMESA");

        ret = (*pglXSwapIntervalMESA)(0);
        if ( ret ) {
	    printf("Failed to set swap interval to 0 (%d).\n", ret);
            piglit_report_result(PIGLIT_FAIL);
        }
    }
    piglit_dispatch_default_init(PIGLIT_DISPATCH_GL);
    event_loop(dpy, glxWin, win);
    
    glXDestroyContext(dpy, ctx);
    XDestroyWindow(dpy, win);
    XCloseDisplay(dpy);
    
    return 0;
}
