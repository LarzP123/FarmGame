#ifndef GFXLINUX
#define GFXLINUX

#include "charbitmap.c"

/* ---------------------------------------------------------
   Minimal X11 opaque types + constants for headerless build
   --------------------------------------------------------- */

/* GC is an opaque pointer */
struct _XGC;

/* Minimal XImage */
struct XImage {
    int width;
    int height;
    int xoffset;
    int format;
    char *data;
    int byte_order;
    int bitmap_unit;
    int bitmap_bit_order;
    int bitmap_pad;
    int depth;
    int bytes_per_line;
    int bits_per_pixel;
    unsigned long red_mask;
    unsigned long green_mask;
    unsigned long blue_mask;
};

/* Visual is opaque */
struct XVisual;

/* XSetWindowAttributes – must match Xlib layout */
struct XSetWindowAttributes {
    unsigned long background_pixmap;
    unsigned long background_pixel;
    unsigned long border_pixmap;
    unsigned long border_pixel;
    int bit_gravity;
    int win_gravity;
    int backing_store;
    unsigned long backing_planes;
    unsigned long backing_pixel;
    int save_under;
    long event_mask;
    long do_not_propagate_mask;
    int override_redirect;
    unsigned long colormap;
    unsigned long cursor;
};

/* XSizeHints – must match Xlib layout */
struct _XSizeHints {
    long flags;
    int x,y;
    int width,height;
    int min_width,min_height;
    int max_width,max_height;
    int width_inc,height_inc;
    struct { int x,y; } min_aspect;
    struct { int x,y; } max_aspect;
    int base_width,base_height;
    int win_gravity;
};

/* Event substructures */
struct _XKeyEvent {
    int type;
    unsigned long serial;
    int send_event;
    struct _XDisplay *display;
    unsigned long window;
    unsigned long root;
    unsigned long subwindow;
    unsigned long time;
    int x,y;
    int x_root,y_root;
    unsigned int state;
    unsigned int keycode;
    int same_screen;
};

struct _XButtonEvent {
    int type;
    unsigned long serial;
    int send_event;
    struct _XDisplay *display;
    unsigned long window;
    unsigned long root;
    unsigned long subwindow;
    unsigned long time;
    int x,y;
    int x_root,y_root;
    unsigned int state;
    unsigned int button;
    int same_screen;
};

/* XEvent union */
union _XEvent {
    int type;
    struct _XKeyEvent xkey;
    struct _XButtonEvent xbutton;
    long pad[24];
};

/* ---------------------------------------------------------
   Xlib function prototypes
   --------------------------------------------------------- */

extern struct _XDisplay *XOpenDisplay(char *);
extern int XCloseDisplay(struct _XDisplay *);
extern int XDefaultScreen(struct _XDisplay *);
extern struct Visual *XDefaultVisual(struct _XDisplay *,int);
extern unsigned long XBlackPixel(struct _XDisplay *,int);
extern int XDefaultDepth(struct _XDisplay *,int);
extern unsigned long XRootWindow(struct _XDisplay *,int);

extern unsigned long XCreateWindow(struct _XDisplay *,unsigned long,
    int,int,unsigned int,unsigned int,unsigned int,int,unsigned int,
    struct Visual *,unsigned long,struct XSetWindowAttributes *);

extern int XMapWindow(struct _XDisplay *,unsigned long);
extern int XStoreName(struct _XDisplay *,unsigned long,const char *);
extern struct _XGC * XCreateGC(struct _XDisplay *,unsigned long,unsigned long,void *);
extern int XFreeGC(struct _XDisplay *,struct _XGC *);
extern int XDestroyWindow(struct _XDisplay *,unsigned long);
extern int XFlush(struct _XDisplay *);

extern int XPending(struct _XDisplay *);
extern int XNextEvent(struct _XDisplay *,union _XEvent *);

extern struct XImage  *XCreateImage(struct _XDisplay *,struct Visual *,unsigned int,int,int,char *,unsigned int,unsigned int,int,int);

extern int XDestroyImage(struct XImage *);
extern int XSetWMNormalHints(struct _XDisplay *,unsigned long,struct _XSizeHints *);
extern unsigned long XLookupKeysym(struct _XKeyEvent *,int);
extern int XPutImage(struct _XDisplay *,unsigned long,struct _XGC *,struct XImage *,int,int,int,int,unsigned int,unsigned int);

/* ---------------------------------------------------------
   Screen + color
   --------------------------------------------------------- */

#define SCREENW 800
#define SCREENH 600

#define COLRGB(r,g,b) ((unsigned int)(((r)<<16)|((g)<<8)|(b)))
#define BLACK   COLRGB(0  ,0  ,0  )
#define WHITE   COLRGB(255,255,255)
#define RED     COLRGB(255,0  ,0  )
#define GREEN   COLRGB(0  ,255,0  )
#define BLUE    COLRGB(0  ,0  ,255)
#define YELLOW  COLRGB(255,255,0  )
#define CYAN    COLRGB(0  ,255,255)
#define MAGENTA COLRGB(255,0  ,255)

/* utils.asm */
extern int  abs_val(int x);
extern void mem_set(void *dst,int val,unsigned int n);

/* syscalls */
extern long clone(long(*fn)(void *),void *stack,int flags,void *arg);
extern int tgkill(int tgid,int tid,int sig);
extern int getpid(void);
extern int gettid(void);
extern void *mmap(void *,unsigned long,int,int,int,long);
extern void *mem_alloc(unsigned long size);
extern void mem_free(void *ptr);

static void *create_thread(void *lpThreadAttributes,
    unsigned long dw_stack_size,long(*lp_start_address)(void *),
    void *lp_parameter,unsigned long dw_creation_flags,unsigned long *lp_thread_id) {
    (void)lpThreadAttributes;
    (void)dw_stack_size;
    (void)dw_creation_flags;

    char *stack=(char *)mmap((void *)0,524288,0x1|0x2,0x02|0x20|0x20000,-1,0);
    if (stack==((void *)-1)) {
        return (void *)-1;
    }

    unsigned long stack_top=(unsigned long)(stack+524288);
    stack_top&=~0xFUL;

    long tid=clone(lp_start_address,(void *)stack_top,(0x00000100|0x00000200|0x00000400|0x00000800|0x00010000|0x80000000),lp_parameter);

    if (lp_thread_id) {
        *lp_thread_id=(unsigned long)tid;
    }

    return (void *)(long)tid;
}

/* ── X11 backend state ─────────────────────────────────────────────────── */

static unsigned int *frame_buffer=0;

static struct _XDisplay *g_display=0;
static unsigned long g_window=0;
static struct _XGC *g_gc=0;
static struct XImage *g_ximage=0;
static int g_screen=0;
static int g_running=1;

static int click_x=0;
static int click_y=0;

void gfx_clear(int height_start) {
    mem_set(frame_buffer+SCREENW*height_start,0,4*SCREENW*(SCREENH-height_start));
}

int gfx_init(const char *title) {
    struct Visual            *visual;
    struct XSetWindowAttributes attrs;
    unsigned long      attr_mask;
    struct _XSizeHints         hints;

    g_display=XOpenDisplay(0);
    if (!g_display) {
        return 0;
    }

    g_screen=XDefaultScreen(g_display);
    visual=XDefaultVisual(g_display,g_screen);

    attr_mask=(1L<<1)|(1L<<11);
    attrs.background_pixmap=0;
    attrs.background_pixel=XBlackPixel(g_display,g_screen);
    attrs.border_pixmap=0;
    attrs.border_pixel=0;
    attrs.bit_gravity=0;
    attrs.win_gravity=0;
    attrs.backing_store=0;
    attrs.backing_planes=0;
    attrs.backing_pixel=0;
    attrs.save_under=0;
    attrs.event_mask=(1L<<0)|(1L<<2)|(1L<<17);
    attrs.do_not_propagate_mask=0;
    attrs.override_redirect=0;
    attrs.colormap=0;
    attrs.cursor=0;

    g_window=XCreateWindow(
        g_display,
        XRootWindow(g_display,g_screen),
        0,0,SCREENW,SCREENH,0,
        XDefaultDepth(g_display,g_screen),
        1,
        visual,
        attr_mask,
        &attrs
    );
    if (!g_window) {
        return 0;
    }

    hints.flags=(1L<<4)|(1L<<5);
    hints.x=0;
    hints.y=0;
    hints.width=SCREENW;
    hints.height=SCREENH;
    hints.min_width=SCREENW;
    hints.min_height=SCREENH;
    hints.max_width=SCREENW;
    hints.max_height=SCREENH;
    hints.width_inc=0;
    hints.height_inc=0;
    hints.min_aspect.x=hints.min_aspect.y=0;
    hints.max_aspect.x=hints.max_aspect.y=0;
    hints.base_width=0;
    hints.base_height=0;
    hints.win_gravity=0;

    XSetWMNormalHints(g_display,g_window,&hints);

    XStoreName(g_display,g_window,title);
    XMapWindow(g_display,g_window);

    g_gc=XCreateGC(g_display,g_window,0,0);

    frame_buffer=(unsigned int *)mem_alloc(4 * SCREENW * SCREENH);
    if (!frame_buffer) {
        return 0;
    }

    g_ximage=XCreateImage(
        g_display,visual,
        (unsigned int)XDefaultDepth(g_display,g_screen),
        2,0,(char *)frame_buffer,
        SCREENW,SCREENH,
        32,SCREENW*4
    );
    if (!g_ximage) {
        return 0;
    }

    gfx_clear(0);
    return 1;
}

void gfx_shutdown(void) {
    if (g_ximage) {
        g_ximage->data=0;
        XDestroyImage(g_ximage);
        g_ximage=0;
    }
    if (g_gc) {
        XFreeGC(g_display,g_gc);
        g_gc=0;
    }
    if (g_window) {
        XDestroyWindow(g_display,g_window);
        g_window=0;
    }
    if (g_display) {
        XCloseDisplay(g_display);
        g_display=0;
    }
    if (frame_buffer) {
        mem_free(frame_buffer);
        frame_buffer=0;
    }
}

int gfx_present(void) {
    union _XEvent ev;

    while (XPending(g_display)) {
        XNextEvent(g_display,&ev);
        switch (ev.type) {
            case 17:
            case 33:
                g_running=0;
                break;

            case 2: {
                unsigned long ks=XLookupKeysym(&ev.xkey,0);
                if (ks == 0xff1b) {
                    g_running=0;
                }
                break;
            }

            case 4:
                if (ev.xbutton.button == 1) {
                    click_x=ev.xbutton.x;
                    click_y=ev.xbutton.y;
                }
                break;
        }
    }

    XPutImage(g_display,g_window,g_gc,g_ximage,
              0,0,0,0,SCREENW,SCREENH);
    XFlush(g_display);
    return g_running;
}

#endif
