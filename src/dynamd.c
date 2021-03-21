/* 
MIT/X Consortium License

© 2006-2019 Anselm R Garbe <anselm@garbe.ca>
© 2006-2009 Jukka Salmi <jukka at salmi dot ch>
© 2006-2007 Sander van Dijk <a dot h dot vandijk at gmail dot com>
© 2007-2011 Peter Hartlich <sgkkr at hartlich dot com>
© 2007-2009 Szabolcs Nagy <nszabolcs at gmail dot com>
© 2007-2009 Christof Musik <christof at sendfax dot de>
© 2007-2009 Premysl Hruby <dfenze at gmail dot com>
© 2007-2008 Enno Gottox Boland <gottox at s01 dot de>
© 2008 Martin Hurton <martin dot hurton at gmail dot com>
© 2008 Neale Pickett <neale dot woozle dot org>
© 2009 Mate Nagy <mnagy at port70 dot net>
© 2010-2016 Hiltjo Posthuma <hiltjo@codemadness.org>
© 2010-2012 Connor Lane Smith <cls@lubutu.com>
© 2011 Christoph Lohmann <20h@r-36.net>
© 2015-2016 Quentin Rameau <quinq@fifth.space>
© 2015-2016 Eric Pruitt <eric.pruitt@gmail.com>
© 2016-2017 Markus Teich <markus.teich@stusta.mhn.de>
© 2020-2021 Angel Uniminin <uniminin@zoho.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
the rights to use, copy, modify, merge, publish, distribute, sublicense,
and/or sell copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
DEALINGS IN THE SOFTWARE.
*/

/*
 * DWM UPSTREAM: https://dwm.suckless.org
 * dynamd (Fork of dwm) window manager is designed like any other X client as well.
 * It is driven through handling X events. In contrast to other X clients, a window
 * manager selects for SubstructureRedirectMask on the root window, to receive
 * events about window (dis-)appearance. Only one X connection at a time is
 * allowed to select for this event mask.
 *
 * The event handlers of dynamd are organized in an array which is accessed
 * whenever a new event has been fetched. This allows event dispatching
 * in O(1) time.
 *
 * Each child of the root window is called a client, except windows which have
 * set the override_redirect flag. Clients are organized in a linked client
 * list on each monitor, the focus history is remembered through a stack list
 * on each monitor. Each client contains a bit array to indicate the tags of a
 * client.
 *
 * Keys and tagging rules are organized as arrays and defined in config.h.
 *
 * To understand everything else, start reading main().
 */



#pragma GCC diagnostic ignored "-Wstringop-truncation"

#include <errno.h>
#include <locale.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <X11/cursorfont.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xproto.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xinerama.h>
#include <X11/Xft/Xft.h>
#include <X11/Xlib-xcb.h>
#include <xcb/res.h>

#include "drw.h"
#include "util.h"

/* macros */
#define BUTTONMASK              (ButtonPressMask|ButtonReleaseMask)
#define CLEANMASK(mask)         (mask & ~(numlockmask|LockMask) & (ShiftMask|ControlMask|Mod1Mask|Mod2Mask|Mod3Mask|Mod4Mask|Mod5Mask))
#define INTERSECT(x,y,w,h,m)    (MAX(0, MIN((x)+(w),(m) -> wx+(m) -> ww) - MAX((x),(m) -> wx)) \
                               * MAX(0, MIN((y)+(h),(m) -> wy+(m) -> wh) - MAX((y),(m) -> wy)))
#define ISVISIBLE(C)            ((C -> tags & C -> mon -> tagset[C -> mon -> seltags]))
#define LENGTH(X)               (sizeof X / sizeof X[0])
#define MOUSEMASK               (BUTTONMASK|PointerMotionMask)
#define WIDTH(X)                ((X) -> w + 2 * (X) -> bw)
#define HEIGHT(X)               ((X) -> h + 2 * (X) -> bw)
#define TAGMASK                 ((1 << LENGTH(tags)) - 1)
#define TEXTW(X)                (drw_fontset_getwidth(drw, (X)) + lrpad)

/* enums */
enum { CurNormal, CurResize, CurMove, CurLast }; /* cursor */
enum { SchemeNorm, SchemeSel }; /* color schemes */
enum { NetSupported, NetWMName, NetWMState, NetWMCheck,
       NetWMFullscreen, NetActiveWindow, NetWMWindowType,
       NetWMWindowTypeDialog, NetClientList, NetLast }; /* EWMH atoms */
enum { WMProtocols, WMDelete, WMState, WMTakeFocus, WMLast }; /* default atoms */
enum { ClkTagBar, ClkTabBar, ClkLtSymbol, ClkStatusText,
       ClkClientWin, ClkRootWin, ClkLast }; /* clicks */

typedef union {
    int i;
    unsigned int ui;
    float f;
    const void *v;
} Arg;

typedef struct {
    unsigned int click;
    unsigned int mask;
    unsigned int button;
    void (*func)(const Arg *arg);
    const Arg arg;
} Button;

typedef struct Monitor Monitor;
typedef struct Pertag Pertag;
typedef struct Client Client;

struct Client {
    char name[256];
    float mina, maxa;
    int x, y, w, h;
    int oldx, oldy, oldw, oldh;
    int basew, baseh, incw, inch, maxw, maxh, minw, minh;
    int bw, oldbw;
    unsigned int tags;
    int isfixed, isfloating, isurgent, neverfocus, oldstate, isfullscreen, isterminal, noswallow;
    pid_t pid;
    Client *next;
    Client *snext;
    Client *swallowing;
    Monitor *mon;
    Window win;
};

typedef struct {
    unsigned int mod;
    KeySym keysym;
    void (*func)(const Arg *);
    const Arg arg;
} Key;

typedef struct {
    const char * sig;
    void (*func)(const Arg *);
} Signal;

typedef struct {
    const char *symbol;
    void (*arrange)(Monitor *);
} Layout;

struct Monitor {
    char ltsymbol[16];
    float mfact;
    int nmaster;
    int num;
    int by;               /* bar geometry */
    int ty;               /* tab bar geometry */
    int mx, my, mw, mh;   /* screen size */
    int wx, wy, ww, wh;   /* window area  */
    int gappih;           /* horizontal gap between windows */
    int gappiv;           /* vertical gap between windows */
    int gappoh;           /* horizontal outer gaps */
    int gappov;           /* vertical outer gaps */
    unsigned int seltags;
    unsigned int sellt;
    unsigned int tagset[2];
    int showbar;
    int showtab;
    int topbar;
    int toptab;
    Client *clients;
    Client *sel;
    Client *stack;
    Monitor *next;
    Window barwin;
    Window tabwin;
    int ntabs;
    int tab_widths[25];
    const Layout *lt[2];
    Pertag *pertag;
};

typedef struct {
    const char *class;
    const char *instance;
    const char *title;
    unsigned int tags;
    int isfloating;
    int isterminal;
    int noswallow;
    int monitor;
} Rule;


/* function declarations */
static void applyrules(Client *c);
static int  applysizehints(Client *c, int *x, int *y, int *w, int *h, int interact);
static void arrange(Monitor *m);
static void arrangemon(Monitor *m);
static void attach(Client *c);
static void attachstack(Client *c);
static void buttonpress(XEvent *e);
static void checkotherwm();
static void cleanup();
static void cleanupmon(Monitor *mon);
static void clientmessage(XEvent *e);
static void configure(Client *c);
static void configurenotify(XEvent *e);
static void configurerequest(XEvent *e);
static Monitor *createmon();
static void cyclelayout(const Arg *arg);
static void destroynotify(XEvent *e);
static void detach(Client *c);
static void detachstack(Client *c);
static Monitor *dirtomon(int dir);
static void drawbar(Monitor *m);
static void drawbars();
static void drawtab(Monitor *m);
static void drawtabs();
static void enternotify(XEvent *e);
static void expose(XEvent *e);
static void focus(Client *c);
static void focusin(XEvent *e);
static void focusmon(const Arg *arg);
static void focusstack(const Arg *arg);
static void focuswin(const Arg* arg);
static int  getrootptr(int *x, int *y);
static long getstate(Window w);
static int  gettextprop(Window w, Atom atom, char *text, unsigned int size);
static void grabbuttons(Client *c, int focused);
static void grabkeys();
static void keypress(XEvent *e);
static void killclient(const Arg *arg);
static void manage(Window w, XWindowAttributes *wa);
static void mappingnotify(XEvent *e);
static void maprequest(XEvent *e);
static void motionnotify(XEvent *e);
static void movemouse(const Arg *arg);
static Client *nexttiled(Client *c);
static void pop(Client *);
static void propertynotify(XEvent *e);
static Monitor *recttomon(int x, int y, int w, int h);
static void organizetags(const Arg *arg);
static void resize(Client *c, int x, int y, int w, int h, int interact);
static void resizeclient(Client *c, int x, int y, int w, int h);
static void resizemouse(const Arg *arg);
static void restack(Monitor *m);
static void run();
static void scan();
static int  sendevent(Client *c, Atom proto);
static void sendmon(Client *c, Monitor *m);
static void setclientstate(Client *c, long state);
static void setfocus(Client *c);
static void setfullscreen(Client *c, int fullscreen);
static void setlayout(const Arg *arg);
static void setmfact(const Arg *arg);
static void setup();
static void seturgent(Client *c, int urg);
static void showhide(Client *c);
static void sigchld(int unused);
static void spawn(const Arg *arg);
static void tag(const Arg *arg);
static void tagmon(const Arg *arg);
static void togglebar(const Arg *arg);
static void togglefloating(const Arg *arg);
static void togglefullscr(const Arg *arg);
static void toggletag(const Arg *arg);
static void toggleview(const Arg *arg);
static void unfocus(Client *c, int setfocus);
static void unmanage(Client *c, int destroyed);
static void unmapnotify(XEvent *e);
static void updatebarpos(Monitor *m);
static void updatebars();
static void updateclientlist();
static int  updategeom();
static void updatenumlockmask();
static void updatesizehints(Client *c);
static void updatestatus();
static void updatetitle(Client *c);
static void updatewindowtype(Client *c);
static void updatewmhints(Client *c);
static void view(const Arg *arg);
static Client *wintoclient(Window w);
static Monitor *wintomon(Window w);
static int  xerror(Display *dpy, XErrorEvent *ee);
static int  xerrordummy(Display *dpy, XErrorEvent *ee);
static int  xerrorstart(Display *dpy, XErrorEvent *ee);
static void zoom(const Arg *arg);
static void autostart_exec();
static void shiftview(const Arg *arg);

/* Layouts */
static void centeredmaster(Monitor *m);
static void monocle(Monitor *m);
static void tile(Monitor *m);
static void deck(Monitor *m);
static void fibonacci(Monitor *m, int s);
static void dwindle(Monitor *m);
static void spiral(Monitor *m);
static void grid(Monitor *m);
static void horizgrid(Monitor *m);
static void gaplessgrid(Monitor *m);
static void bstack(Monitor *m);
static void bstackhoriz(Monitor *m);
static void centeredfloatingmaster(Monitor *m);

/* Gaps */
static void togglegaps(const Arg *arg);
static void getgaps(Monitor *m, int *oh, int *ov, int *ih, int *iv, unsigned int *nc);
static void setgaps(int oh, int ov, int ih, int iv);
static void gaps(const Arg *arg);
static void getfacts(Monitor *m, int msize, int ssize, float *mf, float *sf, int *mr, int *sr);


static pid_t getparentprocess(pid_t p);
static int   isdescprocess(pid_t p, pid_t c);
static Client *swallowingclient(Window w);
static Client *termforwin(const Client *c);
static pid_t winpid(Window w);

/* variables */
static const char broken[] = "broken";
static char stext[256];
static int screen;
static int sw, sh;           /* X display screen geometry width, height */
static int bh, blw = 0;      /* bar geometry */
static int th = 0;           /* tab bar geometry */
static int enablegaps = 1;   /* enables gaps, used by togglegaps */
static int lrpad;            /* sum of left and right padding for text */
static int (*xerrorxlib)(Display *, XErrorEvent *);
static unsigned int numlockmask = 0;
static void (*handler[LASTEvent]) (XEvent *) = {
    [ButtonPress] = buttonpress,
    [ClientMessage] = clientmessage,
    [ConfigureRequest] = configurerequest,
    [ConfigureNotify] = configurenotify,
    [DestroyNotify] = destroynotify,
    [EnterNotify] = enternotify,
    [Expose] = expose,
    [FocusIn] = focusin,
    [KeyPress] = keypress,
    [MappingNotify] = mappingnotify,
    [MapRequest] = maprequest,
    [MotionNotify] = motionnotify,
    [PropertyNotify] = propertynotify,
    [UnmapNotify] = unmapnotify
};
static Atom wmatom[WMLast], netatom[NetLast];
static int running = 1;
static Cur *cursor[CurLast];
static Clr **scheme;
static Display *dpy;
static Drw *drw;
static Monitor *mons, *selmon;
static Window root, wmcheckwin;
static xcb_connection_t *xcon;

/* configuration, allows nested code to access above variables */
#include "config.h"

struct Pertag {
    unsigned int curtag, prevtag; /* current and previous tag */
    int nmasters[LENGTH(tags) + 1]; /* number of windows in master area */
    float mfacts[LENGTH(tags) + 1]; /* mfacts per tag */
    unsigned int sellts[LENGTH(tags) + 1]; /* selected layouts */
    const Layout *ltidxs[LENGTH(tags) + 1][2]; /* matrix of tags and layouts indexes  */
    int showbars[LENGTH(tags) + 1]; /* display bar for the current tag */
};


/* compile-time check if all tags fit into an unsigned int bit array. */
struct NumTags { char limitexceeded[LENGTH(tags) > 25 ? -1 : 1]; };

/* dynamd will keep pid's of processes from autostart array and kill them at quit */
static pid_t *autostart_pids;
static size_t autostart_len;



void centeredmaster(Monitor *m) {
    unsigned int i, n;
    int oh, ov, ih, iv;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int lx = 0, ly = 0, lw = 0, lh = 0;
    int rx = 0, ry = 0, rw = 0, rh = 0;
    float mfacts = 0, lfacts = 0, rfacts = 0;
    int mtotal = 0, ltotal = 0, rtotal = 0;
    int mrest = 0, lrest = 0, rrest = 0;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);
    
    if (n == 0) { return; }

    /* initialize areas */
    mx = m -> wx + ov;
    my = m -> wy + oh;
    mh = m -> wh - 2 * oh - ih * ((!m -> nmaster ? n : MIN(n, m -> nmaster)) - 1);
    mw = m -> ww - 2 * ov;
    lh = m -> wh - 2 * oh - ih * (((n - m -> nmaster) / 2) - 1);
    rh = m -> wh - 2 * oh - ih * (((n - m -> nmaster) / 2) - ((n - m -> nmaster) % 2 ? 0 : 1));

    if (m -> nmaster && n > m -> nmaster) {
        /* go mfact box in the center if more than nmaster clients */
        if (n - m -> nmaster > 1) {
            /* || <-S -> | <---M---> | <-S-> || */
            mw = (m -> ww - 2 * ov - 2 * iv) * m -> mfact;
            lw = (m -> ww - mw - 2 * ov - 2 * iv) / 2;
            rw = (m -> ww - mw - 2 * ov - 2 * iv) - lw;
            mx += lw + iv;
        } else {
            /* || <---M---> | <-S-> || */
            mw = (mw - iv) * m -> mfact;
            lw = 0;
            rw = m -> ww - mw - iv - 2 * ov;
        }

        lx = m -> wx + ov;
        ly = m -> wy + oh;
        rx = mx + mw + iv;
        ry = m -> wy + oh;
    }

    /* calculate facts */
    for (n = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), n++) {
        if (!m -> nmaster || n < m -> nmaster) {
            mfacts += 1;
        } else if ((n - m -> nmaster) % 2) {
            lfacts += 1; // total factor of left hand stack area
        } else {
            rfacts += 1; // total factor of right hand stack area
        }
    }

    for (n = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), n++) {
        if (!m -> nmaster || n < m -> nmaster) {
            mtotal += mh / mfacts;
        } else if ((n - m -> nmaster) % 2) {
            ltotal += lh / lfacts;
        } else {
            rtotal += rh / rfacts;
        }
    }

    mrest = mh - mtotal;
    lrest = lh - ltotal;
    rrest = rh - rtotal;

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++) {
        if (!m -> nmaster || i < m -> nmaster) {
            /* nmaster clients are stacked vertically, in the center of the screen */
            resize(c, mx, my, mw - (2 * c -> bw), (mh / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), 0);
            my += HEIGHT(c) + ih;
        } else {
            /* stack clients are stacked vertically */
            if ((i - m -> nmaster) % 2 ) {
                resize(c, lx, ly, lw - (2 * c -> bw), (lh / lfacts) + ((i - 2 * m -> nmaster) < 2 * lrest ? 1 : 0) - (2 * c -> bw), 0);
                ly += HEIGHT(c) + ih;
            } else {
                resize(c, rx, ry, rw - (2 * c -> bw), (rh / rfacts) + ((i - 2 * m -> nmaster) < 2 * rrest ? 1 : 0) - (2 * c -> bw), 0);
                ry += HEIGHT(c) + ih;
            }
        }
    }
}


void monocle(Monitor *m) {
    unsigned int n = 0;
    Client *c;

    for (c = m -> clients; c; c = c -> next) {
         if (ISVISIBLE(c)) { n++; }
    }

    if (n > 0) /* override layout symbol */ {
        snprintf(m -> ltsymbol, sizeof m -> ltsymbol, "[M %d]", n);
    }

    for (c = nexttiled(m -> clients); c; c = nexttiled(c -> next)) {
        resize(c, m -> wx, m -> wy, m -> ww - 2 * c -> bw, m -> wh - 2 * c -> bw, 0);
    }
}


static void tile(Monitor *m) {
    unsigned int i, n;
    int oh, ov, ih, iv;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int sx = 0, sy = 0, sh = 0, sw = 0;
    float mfacts, sfacts;
    int mrest, srest;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);
    
    if (n == 0) { return; }

    sx = mx = m -> wx + ov;
    sy = my = m -> wy + oh;
    mh = m -> wh - 2 * oh - ih * (MIN(n, m -> nmaster) - 1);
    sh = m -> wh - 2 * oh - ih * (n - m -> nmaster - 1);
    sw = mw = m -> ww - 2 * ov;

    if (m -> nmaster && n > m -> nmaster) {
        sw = (mw - iv) * (1 - m -> mfact);
        mw = mw - iv - sw;
        sx = mx + mw + iv;
    }

    getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++)
        if (i < m -> nmaster) {
            resize(c, mx, my, mw - (2 * c -> bw), (mh / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), 0);
            my += HEIGHT(c) + ih;
        } else {
            resize(c, sx, sy, sw - (2 * c -> bw), (sh / sfacts) + ((i - m -> nmaster) < srest ? 1 : 0) - (2 * c -> bw), 0);
            sy += HEIGHT(c) + ih;
        }
}


void deck(Monitor *m) {
    unsigned int i, n;
    int oh, ov, ih, iv;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int sx = 0, sy = 0, sh = 0, sw = 0;
    float mfacts, sfacts;
    int mrest, srest;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);
    
    if (n == 0) { return; }

    sx = mx = m -> wx + ov;
    sy = my = m -> wy + oh;
    sh = mh = m -> wh - 2 * oh - ih * (MIN(n, m -> nmaster) - 1);
    sw = mw = m -> ww - 2 * ov;

    if (m -> nmaster && n > m -> nmaster) {
        sw = (mw - iv) * (1 - m -> mfact);
        mw = mw - iv - sw;
        sx = mx + mw + iv;
        sh = m -> wh - 2 * oh;
    }

    getfacts(m, mh, sh, &mfacts, &sfacts, &mrest, &srest);

    /* override layout symbol */
    if (n - m -> nmaster > 0) {
        snprintf(m -> ltsymbol, sizeof m -> ltsymbol, "[D %d]", n - m -> nmaster);
    }

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++)
        if (i < m -> nmaster) {
            resize(c, mx, my, mw - (2 * c -> bw), (mh / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), 0);
            my += HEIGHT(c) + ih;
        } else {
            resize(c, sx, sy, sw - (2 * c -> bw), sh - (2 * c -> bw), 0);
        }
}


void fibonacci(Monitor *m, int s) {
    unsigned int i, n;
    int nx, ny, nw, nh;
    int oh, ov, ih, iv;
    int nv, hrest = 0, wrest = 0, r = 1;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);
    
    if (n == 0) { return; }

    nx = m -> wx + ov;
    ny = m -> wy + oh;
    nw = m -> ww - 2 * ov;
    nh = m -> wh - 2 * oh;

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next)) {
        if (r) {
            if ((i % 2 && (nh - ih) / 2 <= (bh + 2 * c -> bw))
               || (!(i % 2) && (nw - iv) / 2 <= (bh + 2 * c -> bw))) {
                r = 0;
            }

            if (r && i < n - 1) {
                if (i % 2) {
                    nv = (nh - ih) / 2;
                    hrest = nh - 2 * nv - ih;
                    nh = nv;
                } else {
                    nv = (nw - iv) / 2;
                    wrest = nw - 2 * nv - iv;
                    nw = nv;
                }

                if ((i % 4) == 2 && !s) {
                    nx += nw + iv;
                } else if ((i % 4) == 3 && !s) {
                    ny += nh + ih;
                }
            }

            if ((i % 4) == 0) {
                if (s) {
                    ny += nh + ih;
                    nh += hrest;
                } else {
                    nh -= hrest;
                    ny -= nh + ih;
                }
            } else if ((i % 4) == 1) {
                nx += nw + iv;
                nw += wrest;
            } else if ((i % 4) == 2) {
                ny += nh + ih;
                nh += hrest;

                if (i < n - 1) {
                    nw += wrest;
                }
            } else if ((i % 4) == 3) {
                if (s) {
                    nx += nw + iv;
                    nw -= wrest;
                } else {
                    nw -= wrest;
                    nx -= nw + iv;
                    nh += hrest;
                }
            }

            if (i == 0)    {
                if (n != 1) {
                    nw = (m -> ww - iv - 2 * ov) - (m -> ww - iv - 2 * ov) * (1 - m -> mfact);
                    wrest = 0;
                }
                ny = m -> wy + oh;
            } else if (i == 1) {
                nw = m -> ww - nw - iv - 2 * ov;
            }

            i++;
        }

        resize(c, nx, ny, nw - (2 * c -> bw), nh - (2 * c -> bw), False);
    }
}


void dwindle(Monitor *m) {
    fibonacci(m, 1);
}


void spiral(Monitor *m) {
    fibonacci(m, 0);
}


void grid(Monitor *m) {
    unsigned int i, n;
    int cx, cy, cw, ch, cc, cr, chrest, cwrest, cols, rows;
    int oh, ov, ih, iv;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);

    /* grid dimensions */
    for (rows = 0; rows <= n / 2; rows++) {
        if (rows * rows >= n) {
            break;
        }
    }

    cols = (rows && (rows - 1) * rows >= n) ? rows - 1 : rows;

    /* window geoms (cell height/width) */
    ch = (m -> wh - 2 * oh - ih * (rows - 1)) / (rows ? rows : 1);
    cw = (m -> ww - 2 * ov - iv * (cols - 1)) / (cols ? cols : 1);
    chrest = (m -> wh - 2 * oh - ih * (rows - 1)) - ch * rows;
    cwrest = (m -> ww - 2 * ov - iv * (cols - 1)) - cw * cols;

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++) {
        cc = i / rows;
        cr = i % rows;
        cx = m -> wx + ov + cc * (cw + iv) + MIN(cc, cwrest);
        cy = m -> wy + oh + cr * (ch + ih) + MIN(cr, chrest);
        resize(c, cx, cy, cw + (cc < cwrest ? 1 : 0) - 2 * c -> bw, ch + (cr < chrest ? 1 : 0) - 2 * c -> bw, False);
    }
}


void horizgrid(Monitor *m) {
    Client *c;
    unsigned int n, i;
    int oh, ov, ih, iv;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int sx = 0, sy = 0, sh = 0, sw = 0;
    int ntop, nbottom = 1;
    float mfacts, sfacts;
    int mrest, srest;

    /* Count windows */
    getgaps(m, &oh, &ov, &ih, &iv, &n);
    
    if (n == 0) { return; }

    if (n <= 2) {
        ntop = n;
    } else {
        ntop = n / 2;
        nbottom = n - ntop;
    }

    sx = mx = m -> wx + ov;
    sy = my = m -> wy + oh;
    sh = mh = m -> wh - 2 * oh;
    sw = mw = m -> ww - 2 * ov;

    if (n > ntop) {
        sh = (mh - ih) / 2;
        mh = mh - ih - sh;
        sy = my + mh + ih;
        mw = m -> ww - 2 * ov - iv * (ntop - 1);
        sw = m -> ww - 2 * ov - iv * (nbottom - 1);
    }

    mfacts = ntop;
    sfacts = nbottom;
    mrest = mw - (mw / ntop) * ntop;
    srest = sw - (sw / nbottom) * nbottom;

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++)
        if (i < ntop) {
            resize(c, mx, my, (mw / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), mh - (2 * c -> bw), 0);
            mx += WIDTH(c) + iv;
        } else {
            resize(c, sx, sy, (sw / sfacts) + ((i - ntop) < srest ? 1 : 0) - (2 * c -> bw), sh - (2 * c -> bw), 0);
            sx += WIDTH(c) + iv;
        }
}


void gaplessgrid(Monitor *m) {
    unsigned int i, n;
    int x, y, cols, rows, ch, cw, cn, rn, rrest, crest; // counters
    int oh, ov, ih, iv;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);
    
    if (n == 0) { return; }

    /* grid dimensions */
    for (cols = 0; cols <= n / 2; cols++) {
        if (cols * cols >= n) {
            break;
        }
    }

    /* set layout against the general calculation: not 1:2:2, but 2:3 */
    if (n == 5) {
        cols = 2;
    }

    rows = n / cols;
    cn = rn = 0; // reset column no, row no, client count

    ch = (m -> wh - 2 * oh - ih * (rows - 1)) / rows;
    cw = (m -> ww - 2 * ov - iv * (cols - 1)) / cols;
    rrest = (m -> wh - 2 * oh - ih * (rows - 1)) - ch * rows;
    crest = (m -> ww - 2 * ov - iv * (cols - 1)) - cw * cols;
    x = m -> wx + ov;
    y = m -> wy + oh;

    for (i = 0, c = nexttiled(m -> clients); c; i++, c = nexttiled(c -> next)) {
        if (i / rows + 1 > cols - n % cols) {
            rows = n / cols + 1;
            ch = (m -> wh - 2 * oh - ih * (rows - 1)) / rows;
            rrest = (m -> wh - 2 * oh - ih * (rows - 1)) - ch * rows;
        }
        resize(c, x,
            y + rn*(ch + ih) + MIN(rn, rrest),
            cw + (cn < crest ? 1 : 0) - 2 * c -> bw,
            ch + (rn < rrest ? 1 : 0) - 2 * c -> bw,
            0);

        rn++;

        if (rn >= rows) {
            rn = 0;
            x += cw + ih + (cn < crest ? 1 : 0);
            cn++;
        }
    }
}


static void bstack(Monitor *m) {
    unsigned int i, n;
    int oh, ov, ih, iv;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int sx = 0, sy = 0, sh = 0, sw = 0;
    float mfacts, sfacts;
    int mrest, srest;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);

    if (n == 0) { return; }

    sx = mx = m -> wx + ov;
    sy = my = m -> wy + oh;
    sh = mh = m -> wh - 2 * oh;
    mw = m -> ww - 2 * ov - iv * (MIN(n, m -> nmaster) - 1);
    sw = m -> ww - 2 * ov - iv * (n - m -> nmaster - 1);

    if (m -> nmaster && n > m -> nmaster) {
        sh = (mh - ih) * (1 - m -> mfact);
        mh = mh - ih - sh;
        sx = mx;
        sy = my + mh + ih;
    }

    getfacts(m, mw, sw, &mfacts, &sfacts, &mrest, &srest);

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++) {
        if (i < m -> nmaster) {
            resize(c, mx, my, (mw / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), mh - (2 * c -> bw), 0);
            mx += WIDTH(c) + iv;
        } else {
            resize(c, sx, sy, (sw / sfacts) + ((i - m -> nmaster) < srest ? 1 : 0) - (2 * c -> bw), sh - (2 * c -> bw), 0);
            sx += WIDTH(c) + iv;
        }
    }
}


static void bstackhoriz(Monitor *m) {
    unsigned int i, n;
    int oh, ov, ih, iv;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int sx = 0, sy = 0, sh = 0, sw = 0;
    float mfacts, sfacts;
    int mrest, srest;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);

    if (n == 0) { return; }

    sx = mx = m -> wx + ov;
    sy = my = m -> wy + oh;
    mh = m -> wh - 2 * oh;
    sh = m -> wh - 2 * oh - ih * (n - m -> nmaster - 1);
    mw = m -> ww - 2 * ov - iv * (MIN(n, m -> nmaster) - 1);
    sw = m -> ww - 2 * ov;

    if (m -> nmaster && n > m -> nmaster) {
        sh = (mh - ih) * (1 - m -> mfact);
        mh = mh - ih - sh;
        sy = my + mh + ih;
        sh = m -> wh - mh - 2 * oh - ih * (n - m -> nmaster);
    }

    getfacts(m, mw, sh, &mfacts, &sfacts, &mrest, &srest);

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++) {
        if (i < m -> nmaster) {
            resize(c, mx, my, (mw / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), mh - (2 * c -> bw), 0);
            mx += WIDTH(c) + iv;
        } else {
            resize(c, sx, sy, sw - (2 * c -> bw), (sh / sfacts) + ((i - m -> nmaster) < srest ? 1 : 0) - (2 * c -> bw), 0);
            sy += HEIGHT(c) + ih;
        }
    }
}


void centeredfloatingmaster(Monitor *m) {
    unsigned int i, n;
    float mfacts, sfacts;
    float mivf = 1.0; // master inner vertical gap factor
    int oh, ov, ih, iv, mrest, srest;
    int mx = 0, my = 0, mh = 0, mw = 0;
    int sx = 0, sy = 0, sh = 0, sw = 0;
    Client *c;

    getgaps(m, &oh, &ov, &ih, &iv, &n);

    if (n == 0) { return; }

    sx = mx = m -> wx + ov;
    sy = my = m -> wy + oh;
    sh = mh = m -> wh - 2 * oh;
    mw = m -> ww - 2 * ov - iv * (n - 1);
    sw = m -> ww - 2 * ov - iv * (n - m -> nmaster - 1);

    if (m -> nmaster && n > m -> nmaster) {
        mivf = 0.8;
        /* go mfact box in the center if more than nmaster clients */
        if (m -> ww > m -> wh) {
            mw = m -> ww * m -> mfact - iv * mivf * (MIN(n, m -> nmaster) - 1);
            mh = m -> wh * 0.9;
        } else {
            mw = m -> ww * 0.9 - iv * mivf * (MIN(n, m -> nmaster) - 1);
            mh = m -> wh * m -> mfact;
        }

        mx = m -> wx + (m -> ww - mw) / 2;
        my = m -> wy + (m -> wh - mh - 2 * oh) / 2;

        sx = m -> wx + ov;
        sy = m -> wy + oh;
        sh = m -> wh - 2 * oh;
    }

    getfacts(m, mw, sw, &mfacts, &sfacts, &mrest, &srest);

    for (i = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), i++)
        if (i < m -> nmaster) {
            /* nmaster clients are stacked horizontally, in the center of the screen */
            resize(c, mx, my, (mw / mfacts) + (i < mrest ? 1 : 0) - (2 * c -> bw), mh - (2 * c -> bw), 0);
            mx += WIDTH(c) + iv * mivf;
        } else {
            /* stack clients are stacked horizontally */
            resize(c, sx, sy, (sw / sfacts) + ((i - m -> nmaster) < srest ? 1 : 0) - (2 * c -> bw), sh - (2 * c -> bw), 0);
            sx += WIDTH(c) + iv;
        }
}


void togglegaps(const Arg *arg) {
    enablegaps = !enablegaps;
    arrange(NULL);
}


void getgaps(Monitor *m, int *oh, int *ov, int *ih, 
                     int *iv, unsigned int *nc) {
    unsigned int n, oe, ie;

    oe = ie = enablegaps;
    Client *c;

    for (n = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), n++);

    if (n == 1) {
        oe = 0; // outer gaps disabled when only one client
    }

    *oh = m -> gappoh * oe; // outer horizontal gap
    *ov = m -> gappov * oe; // outer vertical gap
    *ih = m -> gappih * ie; // inner horizontal gap
    *iv = m -> gappiv * ie; // inner vertical gap
    *nc = n;            // number of clients
}


void setgaps(int oh, int ov, int ih, int iv) {
    if (oh < 0) oh = 0;
    if (ov < 0) ov = 0;
    if (ih < 0) ih = 0;
    if (iv < 0) iv = 0;

    selmon -> gappoh = oh;
    selmon -> gappov = ov;
    selmon -> gappih = ih;
    selmon -> gappiv = iv;

    arrange(selmon);
}


void gaps(const Arg *arg) {
    setgaps(
        selmon -> gappoh + arg -> i,
        selmon -> gappov + arg -> i,
        selmon -> gappih + arg -> i,
        selmon -> gappiv + arg -> i
    );
}

void getfacts(Monitor *m, int msize, int ssize, float *mf, 
                      float *sf, int *mr, int *sr) {

    unsigned int n;
    float mfacts, sfacts;
    int mtotal = 0, stotal = 0;
    Client *c;

    for (n = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), n++);

    mfacts = MIN(n, m -> nmaster);
    sfacts = n - m -> nmaster;

    for (n = 0, c = nexttiled(m -> clients); c; c = nexttiled(c -> next), n++) {
        if (n < m -> nmaster) {
            mtotal += msize / mfacts;
        } else {
            stotal += ssize / sfacts;
        }
    }

    *mf = mfacts; // total factor of master area
    *sf = sfacts; // total factor of stack area
    *mr = msize - mtotal; // the remainder (rest) of pixels after an even master split
    *sr = ssize - stotal; // the remainder (rest) of pixels after an even stack split
}


/* execute command from autostart array */
static void autostart_exec() {
    const char *const *p;
    size_t i = 0;

    /* count entries */
    for (p = autostart; *p; autostart_len++, p++) {
        while (*++p);
    }

    autostart_pids = malloc(autostart_len * sizeof(pid_t));

    for (p = autostart; *p; i++, p++) {
        if ((autostart_pids[i] = fork()) == 0) {
            setsid();
            execvp(*p, (char *const *)p);
            fprintf(stderr, "dynamd: execvp %s\n", *p);
            perror(" failed");
            _exit(EXIT_FAILURE);
        }

        /* skip arguments */
        while (*++p);
    }
}


/* function implementations */
void applyrules(Client *c) {
    const char *class, *instance;
    unsigned int i;
    const Rule *r;
    Monitor *m;
    XClassHint ch = { NULL, NULL };

    /* rule matching */
    c -> isfloating = 0;
    c -> tags = 0;
    XGetClassHint(dpy, c -> win, &ch);
    class    = ch.res_class ? ch.res_class : broken;
    instance = ch.res_name  ? ch.res_name  : broken;

    for (i = 0; i < LENGTH(rules); i++) {
        r = &rules[i];

        if ((!r -> title || strstr(c -> name, r -> title)) && 
            (!r -> class || strstr(class, r -> class)) && 
            (!r -> instance || strstr(instance, r -> instance))) {

            c -> isterminal = r -> isterminal;
            c -> noswallow  = r -> noswallow;
            c -> isfloating = r -> isfloating;
            c -> tags |= r -> tags;

            for (m = mons; m && m -> num != r -> monitor; m = m -> next);

            if (m) { c -> mon = m; }
        }
    }

    if (ch.res_class) { XFree(ch.res_class); }
    if (ch.res_name) { XFree(ch.res_name); }
    
    c -> tags = c -> tags & TAGMASK ? c -> tags & TAGMASK : c -> mon -> tagset[c -> mon -> seltags];
}


int applysizehints(Client *c, int *x, int *y, 
                   int *w, int *h, int interact) {

    int baseismin;
    Monitor *m = c -> mon;

    /* set minimum possible */
    *w = MAX(1, *w);
    *h = MAX(1, *h);

    if (interact) {
        if (*x > sw) { *x = sw - WIDTH(c); }
        if (*y > sh) { *y = sh - HEIGHT(c); }

        if (*x + *w + 2 * c -> bw < 0) { *x = 0; }
        if (*y + *h + 2 * c -> bw < 0) { *y = 0; }

    } else {
        if (*x >= m -> wx + m -> ww) { *x = m -> wx + m -> ww - WIDTH(c); }
        if (*y >= m -> wy + m -> wh) { *y = m -> wy + m -> wh - HEIGHT(c); }

        if (*x + *w + 2 * c -> bw <= m -> wx) { *x = m -> wx; }
        if (*y + *h + 2 * c -> bw <= m -> wy) { *y = m -> wy; }
    }

    if (*h < bh) { *h = bh; }
    if (*w < bh) { *w = bh; }

    if (c -> isfloating || !c -> mon -> lt[c -> mon -> sellt] -> arrange) {

        /* see last two sentences in ICCCM 4.1.2.3 */
        baseismin = c -> basew == c -> minw && c -> baseh == c -> minh;

        if (!baseismin) { /* temporarily remove base dimensions */
            *w -= c -> basew;
            *h -= c -> baseh;
        }

        /* adjust for aspect limits */
        if (c -> mina > 0 && c -> maxa > 0) {
            if (c -> maxa < (float)*w / *h) {
                *w = *h * c -> maxa + 0.5;
            } else if (c -> mina < (float)*h / *w) {
                *h = *w * c -> mina + 0.5;
            }
        }

        if (baseismin) { /* increment calculation requires this */
            *w -= c -> basew;
            *h -= c -> baseh;
        }

        /* adjust for increment value */
        if (c -> incw) {
            *w -= *w % c -> incw;
        }

        if (c -> inch) {
            *h -= *h % c -> inch;
        }

        /* restore base dimensions */
        *w = MAX(*w + c -> basew, c -> minw);
        *h = MAX(*h + c -> baseh, c -> minh);

        if (c -> maxw) {
            *w = MIN(*w, c -> maxw);
        }

        if (c -> maxh) {
            *h = MIN(*h, c -> maxh);
        }
    }

    return *x != c -> x || *y != c -> y || *w != c -> w || *h != c -> h;
}


void arrange(Monitor *m) {
    if (m) {
        showhide(m -> stack);
    } else {
        for (m = mons; m; m = m -> next) {
            showhide(m -> stack);
        }
    }

    if (m) {
        arrangemon(m);
        restack(m);
    } else {
        for (m = mons; m; m = m -> next) {
            arrangemon(m);
        }
    }
}


void arrangemon(Monitor *m) {
    updatebarpos(m);
    XMoveResizeWindow(dpy, m -> tabwin, m -> wx, m -> ty, m -> ww, th);
    strncpy(m -> ltsymbol, m -> lt[m -> sellt] -> symbol, sizeof m -> ltsymbol);

    if (m -> lt[m -> sellt] -> arrange) { 
        m -> lt[m -> sellt] -> arrange(m); 
    }
}


void attach(Client *c) {
    c -> next = c -> mon -> clients;
    c -> mon -> clients = c;
}


void attachstack(Client *c) {
    c -> snext = c -> mon -> stack;
    c -> mon -> stack = c;
}


void swallow(Client *p, Client *c) {

    if (c -> noswallow || c -> isterminal) { return; }
    if (c -> noswallow && !1 && c -> isfloating) { return; }

    detach(c);
    detachstack(c);

    setclientstate(c, WithdrawnState);
    XUnmapWindow(dpy, p -> win);

    p -> swallowing = c;
    c -> mon = p -> mon;

    Window w = p -> win;
    p -> win = c -> win;
    c -> win = w;
    updatetitle(p);
    XMoveResizeWindow(dpy, p -> win, p -> x, p -> y, p -> w, p -> h);
    arrange(p -> mon);
    configure(p);
    updateclientlist();
}


void unswallow(Client *c) {
    c -> win = c -> swallowing -> win;

    free(c -> swallowing);
    c -> swallowing = NULL;

    /* unfullscreen the client */
    setfullscreen(c, 0);
    updatetitle(c);
    arrange(c -> mon);
    XMapWindow(dpy, c -> win);
    XMoveResizeWindow(dpy, c -> win, c -> x, c -> y, c -> w, c -> h);
    setclientstate(c, NormalState);
    focus(NULL);
    arrange(c -> mon);
}


void buttonpress(XEvent *e) {
    unsigned int i, x, click, occ = 0;
    Arg arg = {0};
    Client *c;
    Monitor *m;
    XButtonPressedEvent *ev = &e -> xbutton;

    click = ClkRootWin;

    /* focus monitor if necessary */
    if ((m = wintomon(ev -> window)) && m != selmon) {
        unfocus(selmon -> sel, 1);
        selmon = m;
        focus(NULL);
    }

    if (ev -> window == selmon -> barwin) {
        i = x = 0;

        for (c = m -> clients; c; c = c -> next) {
            occ |= c -> tags == 255 ? 0 : c -> tags;
        }

        x += blw;

        if (ev -> x < x) {
            click = ClkLtSymbol;
        } else {
            do {
                /* do not reserve space for vacant tags */
                if (!(occ & 1 << i || m -> tagset[m -> seltags] & 1 << i)) {
                    continue;
                }
                x += TEXTW(tags[i]);
            } while (ev -> x >= x && ++i < LENGTH(tags));

            if (i < LENGTH(tags)) {
                click = ClkTagBar;
                arg.ui = 1 << i;
            } else { 
                click = ClkStatusText;
            }
        }
    }

        if (ev -> window == selmon -> tabwin) {
            i = 0; x = 0;

            for (c = selmon -> clients; c; c = c -> next) {
                if (!ISVISIBLE(c)) { continue; }

                x += selmon -> tab_widths[i];

                if (ev -> x > x) { ++i; } else { break; }

                if (i >= m -> ntabs) { break; }
        }

        if (c) {
            click = ClkTabBar;
            arg.ui = i;
        }
    } else if ((c = wintoclient(ev -> window))) {
        focus(c);
        restack(selmon);
        XAllowEvents(dpy, ReplayPointer, CurrentTime);
        click = ClkClientWin;
    }

    for (i = 0; i < LENGTH(buttons); i++) {
        if (click == buttons[i].click && buttons[i].func && 
            buttons[i].button == ev -> button && 
            CLEANMASK(buttons[i].mask) == CLEANMASK(ev -> state)) {
            buttons[i].func(((click == ClkTagBar || click == ClkTabBar) && 
            buttons[i].arg.i == 0) ? &arg : &buttons[i].arg);
        }
    }
}


void checkotherwm() {
    xerrorxlib = XSetErrorHandler(xerrorstart);
    /* this causes an error if some other window manager is running */
    XSelectInput(dpy, DefaultRootWindow(dpy), SubstructureRedirectMask);
    XSync(dpy, False);
    XSetErrorHandler(xerror);
    XSync(dpy, False);
}


void cleanup() {
    Arg a = {.ui = ~0};
    Layout foo = { "", NULL };
    Monitor *m;
    size_t i;

    view(&a);
    selmon -> lt[selmon -> sellt] = &foo;

    for (m = mons; m; m = m -> next) {
        while (m -> stack) {
            unmanage(m -> stack, 0);
        }
    }

    XUngrabKey(dpy, AnyKey, AnyModifier, root);

    while (mons) {
        cleanupmon(mons);
    }

    for (i = 0; i < CurLast; i++) {
        drw_cur_free(drw, cursor[i]);
    }

    for (i = 0; i < LENGTH(colors); i++) {
        free(scheme[i]);
    }

    XDestroyWindow(dpy, wmcheckwin);
    drw_free(drw);
    XSync(dpy, False);
    XSetInputFocus(dpy, PointerRoot, RevertToPointerRoot, CurrentTime);
    XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
}


void cleanupmon(Monitor *mon) {
    Monitor *m;

    if (mon == mons) {
        mons = mons -> next;
    } else {
        for (m = mons; m && m -> next != mon; m = m -> next);
        m -> next = mon -> next;
    }

    XUnmapWindow(dpy, mon -> barwin);
    XDestroyWindow(dpy, mon -> barwin);
    XUnmapWindow(dpy, mon -> tabwin);
    XDestroyWindow(dpy, mon -> tabwin);
    free(mon);
}


void clientmessage(XEvent *e) {
    XClientMessageEvent *cme = &e -> xclient;
    Client *c = wintoclient(cme -> window);

    if (!c) { return; }

    if (cme -> message_type == netatom[NetWMState]) {
        if (cme -> data.l[1] == netatom[NetWMFullscreen] || 
            cme -> data.l[2] == netatom[NetWMFullscreen]) {
            setfullscreen(c, (cme -> data.l[0] == 1 /* _NET_WM_STATE_ADD    */
                || (cme -> data.l[0] == 2 /* _NET_WM_STATE_TOGGLE */ && !c -> isfullscreen)));
        }

    } else if (cme -> message_type == netatom[NetActiveWindow]) {
        if (c != selmon -> sel && !c -> isurgent) { seturgent(c, 1); }
    }
}


void configure(Client *c) {
    XConfigureEvent ce;

    ce.type = ConfigureNotify;
    ce.display = dpy;
    ce.event = c -> win;
    ce.window = c -> win;
    ce.x = c -> x;
    ce.y = c -> y;
    ce.width = c -> w;
    ce.height = c -> h;
    ce.border_width = c -> bw;
    ce.above = None;
    ce.override_redirect = False;

    XSendEvent(dpy, c -> win, False, StructureNotifyMask, (XEvent *)&ce);
}


void configurenotify(XEvent *e) {
    Monitor *m;
    Client *c;
    XConfigureEvent *ev = &e -> xconfigure;
    int dirty;

    if (ev -> window == root) {
        dirty = (sw != ev -> width || sh != ev -> height);
        sw = ev -> width;
        sh = ev -> height;

        if (updategeom() || dirty) {
            drw_resize(drw, sw, bh);
            updatebars();

            for (m = mons; m; m = m -> next) {
                for (c = m -> clients; c; c = c -> next) {
                     if (c -> isfullscreen) {
                        resizeclient(c, m -> mx, m -> my, m -> mw, m -> mh);
                     }
                }

                XMoveResizeWindow(dpy, m -> barwin, m -> wx, m -> by, m -> ww, bh);
            }

            focus(NULL);
            arrange(NULL);
        }
    }
}


void configurerequest(XEvent *e) {
    Client *c;
    Monitor *m;
    XConfigureRequestEvent *ev = &e -> xconfigurerequest;
    XWindowChanges wc;

    if ((c = wintoclient(ev -> window))) {
        if (ev -> value_mask & CWBorderWidth) {
            c -> bw = ev -> border_width;
        } else if (c -> isfloating || 
                   !selmon -> lt[selmon -> sellt] -> arrange) {

            m = c -> mon;

            if (ev -> value_mask & CWX) {
                c -> oldx = c -> x;
                c -> x = m -> mx + ev -> x;
            }

            if (ev -> value_mask & CWY) {
                c -> oldy = c -> y;
                c -> y = m -> my + ev -> y;
            }

            if (ev -> value_mask & CWWidth) {
                c -> oldw = c -> w;
                c -> w = ev -> width;
            }

            if (ev -> value_mask & CWHeight) {
                c -> oldh = c -> h;
                c -> h = ev -> height;
            }

            if ((c -> x + c -> w) > m -> mx + m -> mw && c -> isfloating) {
                c -> x = m -> mx + (m -> mw / 2 - WIDTH(c) / 2); /* center in x direction */
            }

            if ((c -> y + c -> h) > m -> my + m -> mh && c -> isfloating) {
                c -> y = m -> my + (m -> mh / 2 - HEIGHT(c) / 2); /* center in y direction */
            }

            if ((ev -> value_mask & (CWX|CWY)) && !(ev -> value_mask & (CWWidth|CWHeight))) {
                configure(c);
            }

            if (ISVISIBLE(c)) {
                XMoveResizeWindow(dpy, c -> win, c -> x, c -> y, c -> w, c -> h);
            }
        } else
            configure(c);
    } else {
        wc.x = ev -> x;
        wc.y = ev -> y;
        wc.width = ev -> width;
        wc.height = ev -> height;
        wc.border_width = ev -> border_width;
        wc.sibling = ev -> above;
        wc.stack_mode = ev -> detail;

        XConfigureWindow(dpy, ev -> window, ev -> value_mask, &wc);
    }

    XSync(dpy, False);
}


Monitor *createmon() {
    Monitor *m;
    unsigned int i;

    m = ecalloc(1, sizeof(Monitor));

    m -> tagset[0] = m -> tagset[1] = 1;
    m -> mfact = mfact;
    m -> nmaster = nmaster;
    m -> showbar = 1;
    m -> showtab = 1;
    m -> topbar = 1;
    m -> toptab = 0;
    m -> ntabs  = 0;
    m -> gappih = 10;
    m -> gappiv = 10;
    m -> gappoh = 10;
    m -> gappov = 10;
    m -> lt[0] = &layouts[0];
    m -> lt[1] = &layouts[1 % LENGTH(layouts)];

    strncpy(m -> ltsymbol, layouts[0].symbol, sizeof m -> ltsymbol);

    m -> pertag = ecalloc(1, sizeof(Pertag));
    m -> pertag -> curtag = m -> pertag -> prevtag = 1;

    for (i = 0; i <= LENGTH(tags); i++) {
        m -> pertag -> nmasters[i] = m -> nmaster;
        m -> pertag -> mfacts[i] = m -> mfact;

        m -> pertag -> ltidxs[i][0] = m -> lt[0];
        m -> pertag -> ltidxs[i][1] = m -> lt[1];
        m -> pertag -> sellts[i] = m -> sellt;

        m -> pertag -> showbars[i] = m -> showbar;
    }

    return m;
}


void cyclelayout(const Arg *arg) {
    Layout *l;

    for (l = (Layout *)layouts; l != selmon -> lt[selmon -> sellt]; l++);

    if (arg -> i > 0) {
        if (l -> symbol && (l + 1) -> symbol) {
            setlayout(&((Arg) { .v = (l + 1) }));
        }
        else {
            setlayout(&((Arg) { .v = layouts }));
        }
    } else {
        if (l != layouts && (l - 1) -> symbol) {
            setlayout(&((Arg) { .v = (l - 1) }));
        } else {
            setlayout(&((Arg) { .v = &layouts[LENGTH(layouts) - 2] }));
        }
    }
}


void destroynotify(XEvent *e) {
    Client *c;
    XDestroyWindowEvent *ev = &e -> xdestroywindow;

    if ((c = wintoclient(ev -> window))) {
        unmanage(c, 1);
    } else if ((c = swallowingclient(ev -> window))) {
        unmanage(c -> swallowing, 1);
    }
}


void detach(Client *c) {
    Client **tc;

    for (tc = &c -> mon -> clients; *tc && *tc != c; tc = &(*tc) -> next);

    *tc = c -> next;
}


void detachstack(Client *c) {
    Client **tc, *t;

    for (tc = &c -> mon -> stack; *tc && *tc != c; tc = &(*tc) -> snext);
    
    *tc = c -> snext;

    if (c == c -> mon -> sel) {
        for (t = c -> mon -> stack; t && !ISVISIBLE(t); t = t -> snext);
        c -> mon -> sel = t;
    }
}


Monitor *dirtomon(int dir) {
    Monitor *m = NULL;

    if (dir > 0) {
        if (!(m = selmon -> next)) {
            m = mons;
        }
    } else if (selmon == mons) {
        for (m = mons; m -> next; m = m -> next);
    }
    else {
        for (m = mons; m -> next != selmon; m = m -> next);
    }

    return m;
}


void drawbar(Monitor *m) {
    int x, w, sw = 0;
    unsigned int i, occ = 0, urg = 0;
    Client *c;

    /* draw status first so it can be overdrawn by tags later */
    if (m == selmon) { /* status is only drawn on selected monitor */
        drw_setscheme(drw, scheme[SchemeNorm]);
        sw = TEXTW(stext) - lrpad + 2; /* 2px right padding */
        drw_text(drw, m -> ww - sw, 0, sw, bh, 0, stext, 0);
    }

    for (c = m -> clients; c; c = c -> next) {
        occ |= c -> tags == 255 ? 0 : c -> tags;
        if (c -> isurgent) { urg |= c -> tags; }
    }

    x = 0;
    w = blw = TEXTW(m -> ltsymbol);
    drw_setscheme(drw, scheme[SchemeNorm]);
    x = drw_text(drw, x, 0, w, bh, lrpad / 2, m -> ltsymbol, 0);

    for (i = 0; i < LENGTH(tags); i++) {
        /* do not draw vacant tags */
        if (!(occ & 1 << i || m -> tagset[m -> seltags] & 1 << i)) { continue; }

        w = TEXTW(tags[i]);
        drw_setscheme(drw, scheme[m -> tagset[m -> seltags] & 1 << i ? SchemeSel : SchemeNorm]);
        drw_text(drw, x, 0, w, bh, lrpad / 2, tags[i], urg & 1 << i);

        x += w;
    }
    
    if ((w = m -> ww - sw - x) > bh) {
        drw_setscheme(drw, scheme[SchemeNorm]);
        drw_rect(drw, x, 0, w, bh, 1, 1);
    }

    drw_map(drw, m -> barwin, 0, 0, m -> ww, bh);
}


void drawbars() {
    Monitor *m;

    for (m = mons; m; m = m -> next) { drawbar(m); }
}


void drawtabs() {
    Monitor *m;

    for (m = mons; m; m = m -> next) { drawtab(m); }
}


static int cmpint(const void *p1, const void *p2) {
  /* The actual arguments to this function are "pointers to
     pointers to char", but strcmp(3) arguments are "pointers
     to char", hence the following cast plus dereference */
  return *((int*) p1) > * (int*) p2;
}


void drawtab(Monitor *m) {
    Client *c;
    int i;
    int sorted_label_widths[25];
    int tot_width = 0;
    int maxsize = bh;
    int x = 0, w = 0;

    /* Calculates number of labels and their width */
    m -> ntabs = 0;

    for (c = m -> clients; c; c = c -> next) {
        if (!ISVISIBLE(c)) {
            continue;
        }

        m -> tab_widths[m -> ntabs] = TEXTW(c -> name);
        tot_width += m -> tab_widths[m -> ntabs];
        ++m -> ntabs;

        if (m -> ntabs >= 25) {
            break;
        }
    }

    if (tot_width > m -> ww) { //not enough space to display the labels, they need to be truncated
      memcpy(sorted_label_widths, m -> tab_widths, sizeof(int) * m -> ntabs);
      qsort(sorted_label_widths, m -> ntabs, sizeof(int), cmpint);

      for (i = 0; i < m -> ntabs; ++i) {
        if (tot_width + (m -> ntabs - i) * sorted_label_widths[i] > m -> ww) {
            break;
        }

        tot_width += sorted_label_widths[i];
      }
      maxsize = (m -> ww - tot_width) / (m -> ntabs - i);
    } else {
      maxsize = m -> ww;
    }

    i = 0;

    for (c = m -> clients; c; c = c -> next) {
      if (!ISVISIBLE(c)) { continue; }

      if (i >= m -> ntabs) { break; }

      if (m -> tab_widths[i] >  maxsize) {
          m -> tab_widths[i] = maxsize;
      }

      w = m -> tab_widths[i];

      drw_setscheme(drw, scheme[(c == m -> sel) ? SchemeSel : SchemeNorm]);
      drw_text(drw, x, 0, w, th, 0, c -> name, 0);

      x += w;
      ++i;
    }

    drw_setscheme(drw, scheme[SchemeNorm]);

    /* cleans interspace between window names and current viewed tag label */
    w = m -> ww - x;
    drw_text(drw, x, 0, w, th, 0, "", 0);

    /* view info */
    x += w;

    drw_text(drw, x, 0, 0, th, 0, 0, 0);
    drw_map(drw, m -> tabwin, 0, 0, m -> ww, th);
}


void enternotify(XEvent *e) {
    Client *c;
    Monitor *m;
    XCrossingEvent *ev = &e -> xcrossing;

    if ((ev -> mode != NotifyNormal || 
        ev -> detail == NotifyInferior) && 
        ev -> window != root) {
            return;
        }

    c = wintoclient(ev -> window);
    m = c ? c -> mon : wintomon(ev -> window);

    if (m != selmon) {
        unfocus(selmon -> sel, 1);
        selmon = m;
    } else if (!c || c == selmon -> sel) { return; }

    focus(c);
}


void expose(XEvent *e) {
    Monitor *m;
    XExposeEvent *ev = &e -> xexpose;

    if (ev -> count == 0 && (m = wintomon(ev -> window))) {
        drawbar(m);
        drawtab(m);
    }
}


void focus(Client *c) {
    if (!c || !ISVISIBLE(c)) {
        for (c = selmon -> stack; c && !ISVISIBLE(c); c = c -> snext);
    }

    if (selmon -> sel && selmon -> sel != c) {
        unfocus(selmon -> sel, 0);
    }

    if (c) {
        if (c -> mon != selmon) { selmon = c -> mon; }

        if (c -> isurgent) {
            seturgent(c, 0);
        }

        detachstack(c);
        attachstack(c);
        grabbuttons(c, 1);
        XSetWindowBorder(dpy, c -> win, scheme[SchemeSel][ColBorder].pixel);
        setfocus(c);
    } else {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }

    selmon -> sel = c;

    drawbars();
    drawtabs();
}


/* there are some broken focus acquiring clients needing extra handling */
void focusin(XEvent *e) {
    XFocusChangeEvent *ev = &e -> xfocus;

    if (selmon -> sel && ev -> window != selmon -> sel -> win) {
        setfocus(selmon -> sel);
    }
}


void focusmon(const Arg *arg) {
    Monitor *m;

    if (!mons -> next) {
        return;
    }

    if ((m = dirtomon(arg -> i)) == selmon) {
        return;
    }

    unfocus(selmon -> sel, 0);
    selmon = m;
    focus(NULL);
}


void focusstack(const Arg *arg) {
    Client *c = NULL, *i;

    if (!selmon -> sel) {
        return;
    }

    if (arg -> i > 0) {
        for (c = selmon -> sel -> next; c && !ISVISIBLE(c); c = c -> next);

        if (!c) {
            for (c = selmon -> clients; c && !ISVISIBLE(c); c = c -> next);
        }
    } else {
        for (i = selmon -> clients; i != selmon -> sel; i = i -> next) {
            if (ISVISIBLE(i)) {
                c = i;
            }
        }

        if (!c) {
            for (; i; i = i -> next) {
                if (ISVISIBLE(i)) {
                    c = i;
                }
            }
        }
    }

    if (c) {
        focus(c);
        restack(selmon);
    }
}


void focuswin(const Arg* arg) {
    int iwin = arg -> i;
    Client* c = NULL;

    for (c = selmon -> clients; c && (iwin || !ISVISIBLE(c)) ; c = c -> next) {
        if (ISVISIBLE(c)) { --iwin; }
    };

    if (c) {
        focus(c);
        restack(selmon);
    }
}


Atom getatomprop(Client *c, Atom prop) {
    int di;
    unsigned long dl;
    unsigned char *p = NULL;
    Atom da, atom = None;

    if (XGetWindowProperty(dpy, c -> win, prop, 0L, sizeof atom, False, XA_ATOM, 
                           &da, &di, &dl, &dl, &p) == Success && p) {

        atom = *(Atom *)p;
        XFree(p);
    }

    return atom;
}


int getrootptr(int *x, int *y) {
    int di;
    unsigned int dui;
    Window dummy;

    return XQueryPointer(dpy, root, &dummy, &dummy, x, y, &di, &di, &dui);
}


long getstate(Window w) {
    int format;
    long result = -1;
    unsigned char *p = NULL;
    unsigned long n, extra;
    Atom real;

    if (XGetWindowProperty(dpy, w, wmatom[WMState], 0L, 2L, False, wmatom[WMState],
                           &real, &format, &n, &extra, 
                           (unsigned char **)&p) != Success) {
            return -1;
    }

    if (n != 0) { result = *p; }

    XFree(p);

    return result;
}


int gettextprop(Window w, Atom atom, char *text, unsigned int size) {
    char **list = NULL;
    int n;
    XTextProperty name;

    if (!text || size == 0) {
        return 0;
    }

    text[0] = '\0';

    if (!XGetTextProperty(dpy, w, &name, atom) || !name.nitems) {
        return 0;
    }

    if (name.encoding == XA_STRING) {
        strncpy(text, (char *)name.value, size - 1);
    } else {
        if (XmbTextPropertyToTextList(dpy, &name, &list, &n) >= Success && n > 0 && *list) {
            strncpy(text, *list, size - 1);
            XFreeStringList(list);
        }
    }

    text[size - 1] = '\0';
    XFree(name.value);

    return 1;
}


void grabbuttons(Client *c, int focused) {
    updatenumlockmask();

    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };

        XUngrabButton(dpy, AnyButton, AnyModifier, c -> win);

        if (!focused) {
            XGrabButton(dpy, AnyButton, AnyModifier, c -> win, False,
                        BUTTONMASK, GrabModeSync, GrabModeSync, None, None);
        }

        for (i = 0; i < LENGTH(buttons); i++) {
            if (buttons[i].click == ClkClientWin) {
                for (j = 0; j < LENGTH(modifiers); j++) {
                    XGrabButton(dpy, buttons[i].button,
                        buttons[i].mask | modifiers[j],
                        c -> win, False, BUTTONMASK,
                        GrabModeAsync, GrabModeSync, None, None);
                }
            }
        }

    }
}


void grabkeys() {
    updatenumlockmask();

    {
        unsigned int i, j;
        unsigned int modifiers[] = { 0, LockMask, numlockmask, numlockmask|LockMask };
        KeyCode code;

        XUngrabKey(dpy, AnyKey, AnyModifier, root);

        for (i = 0; i < LENGTH(keys); i++) {
            if ((code = XKeysymToKeycode(dpy, keys[i].keysym))) {
                for (j = 0; j < LENGTH(modifiers); j++) {
                    XGrabKey(dpy, code, keys[i].mod | modifiers[j], root,
                    True, GrabModeAsync, GrabModeAsync);
                }
            }
        }

    }

}


static int isuniquegeom(XineramaScreenInfo *unique, size_t n, 
                        XineramaScreenInfo *info) {

    while (n--) {
        if (unique[n].x_org == info -> x_org && 
            unique[n].y_org == info -> y_org && 
            unique[n].width == info -> width && 
            unique[n].height == info -> height) {
                return 0;
            }
    }

    return 1;
}


void keypress(XEvent *e) {
    unsigned int i;
    KeySym keysym;
    XKeyEvent *ev;

    ev = &e -> xkey;
    keysym = XKeycodeToKeysym(dpy, (KeyCode)ev -> keycode, 0);

    for (i = 0; i < LENGTH(keys); i++) {
        if (keysym == keys[i].keysym && 
        CLEANMASK(keys[i].mod) == CLEANMASK(ev -> state)
        && keys[i].func) {
            keys[i].func(&(keys[i].arg));
        }
    }
}


void killclient(const Arg *arg) {
    if (!selmon -> sel) { return; }

    if (!sendevent(selmon -> sel, wmatom[WMDelete])) {
        XGrabServer(dpy);
        XSetErrorHandler(xerrordummy);
        XSetCloseDownMode(dpy, DestroyAll);
        XKillClient(dpy, selmon -> sel -> win);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }
}


void manage(Window w, XWindowAttributes *wa) {
    Client *c, *t = NULL, *term = NULL;
    Window trans = None;
    XWindowChanges wc;

    c = ecalloc(1, sizeof(Client));
    c -> win = w;
    c -> pid = winpid(w);

    /* geometry */
    c -> x = c -> oldx = wa -> x;
    c -> y = c -> oldy = wa -> y;
    c -> w = c -> oldw = wa -> width;
    c -> h = c -> oldh = wa -> height;
    c -> oldbw = wa -> border_width;

    updatetitle(c);
    if (XGetTransientForHint(dpy, w, &trans) && (t = wintoclient(trans))) {
        c -> mon = t -> mon;
        c -> tags = t -> tags;
    } else {
        c -> mon = selmon;
        applyrules(c);
        term = termforwin(c);
    }

    if (c -> x + WIDTH(c) > c -> mon -> mx + c -> mon -> mw) {
        c -> x = c -> mon -> mx + c -> mon -> mw - WIDTH(c);
    }

    if (c -> y + HEIGHT(c) > c -> mon -> my + c -> mon -> mh) {
        c -> y = c -> mon -> my + c -> mon -> mh - HEIGHT(c);
    }

    c -> x = MAX(c -> x, c -> mon -> mx);

    /* only fix client y-offset, if the client center might cover the bar */
    c -> y = MAX(c -> y, ((c -> mon -> by == c -> mon -> my) && 
             (c -> x + (c -> w / 2) >= c -> mon -> wx) && 
             (c -> x + (c -> w / 2) < c -> mon -> wx + c -> mon -> ww)) ? bh : c -> mon -> my);

    c -> bw = 2;

    wc.border_width = c -> bw;

    XConfigureWindow(dpy, w, CWBorderWidth, &wc);
    XSetWindowBorder(dpy, w, scheme[SchemeNorm][ColBorder].pixel);
    configure(c); /* propagates border_width, if size doesn't change */
    updatewindowtype(c);
    updatesizehints(c);
    updatewmhints(c);

    c -> x = c -> mon -> mx + (c -> mon -> mw - WIDTH(c)) / 2;
    c -> y = c -> mon -> my + (c -> mon -> mh - HEIGHT(c)) / 2;

    XSelectInput(dpy, w, EnterWindowMask|FocusChangeMask|PropertyChangeMask|StructureNotifyMask);
    grabbuttons(c, 0);

    if (!c -> isfloating) {
         c -> isfloating = c -> oldstate = trans != None || c -> isfixed;
    }

    if (c -> isfloating) {
        XRaiseWindow(dpy, c -> win);
    }

    attach(c);
    attachstack(c);
    XChangeProperty(dpy, root, netatom[NetClientList], XA_WINDOW, 32, PropModeAppend,
        (unsigned char *) &(c -> win), 1);
    XMoveResizeWindow(dpy, c -> win, c -> x + 2 * sw, c -> y, c -> w, c -> h); /* some windows require this */
    setclientstate(c, NormalState);

    if (c -> mon == selmon) {
        unfocus(selmon -> sel, 0);
    }

    c -> mon -> sel = c;
    arrange(c -> mon);

    XMapWindow(dpy, c -> win);

    if (term) { swallow(term, c); }

    focus(NULL);
}


void mappingnotify(XEvent *e) {
    XMappingEvent *ev = &e -> xmapping;

    XRefreshKeyboardMapping(ev);

    if (ev -> request == MappingKeyboard) {
        grabkeys();
    }
}


void maprequest(XEvent *e) {
    static XWindowAttributes wa;
    XMapRequestEvent *ev = &e -> xmaprequest;

    if (!XGetWindowAttributes(dpy, ev -> window, &wa)) { return; }

    if (wa.override_redirect) { return; }

    if (!wintoclient(ev -> window)) {
        manage(ev -> window, &wa);
    }
}


void motionnotify(XEvent *e) {
    static Monitor *mon = NULL;
    Monitor *m;
    XMotionEvent *ev = &e -> xmotion;

    if (ev -> window != root) {
        return;
    }

    if ((m = recttomon(ev -> x_root, ev -> y_root, 1, 1)) != mon && mon) {
        unfocus(selmon -> sel, 1);
        selmon = m;
        focus(NULL);
    }

    mon = m;
}


void movemouse(const Arg *arg) {
    int x, y, ocx, ocy, nx, ny;
    Client *c;
    Monitor *m;
    XEvent ev;
    Time lasttime = 0;

    if (!(c = selmon -> sel)) {
        return;
    }

    if (c -> isfullscreen) /* no support moving fullscreen windows by mouse */ { return; }

    restack(selmon);
    ocx = c -> x;
    ocy = c -> y;

    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
        None, cursor[CurMove] -> cursor, CurrentTime) != GrabSuccess) { return; }

    if (!getrootptr(&x, &y)) { return; }

    do {
        XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
        switch(ev.type) {
            case ConfigureRequest:
            case Expose:
            case MapRequest:
                handler[ev.type](&ev);
                break;
            case MotionNotify:
                if ((ev.xmotion.time - lasttime) <= (1000 / 60)) { continue; }
                
                lasttime = ev.xmotion.time;

                nx = ocx + (ev.xmotion.x - x);
                ny = ocy + (ev.xmotion.y - y);

                if (abs(selmon -> wx - nx) < 32) {
                    nx = selmon -> wx;
                } else if (abs((selmon -> wx + selmon -> ww) - (nx + WIDTH(c))) < 32) {
                    nx = selmon -> wx + selmon -> ww - WIDTH(c);
                }

                if (abs(selmon -> wy - ny) < 32) {
                    ny = selmon -> wy;
                } else if (abs((selmon -> wy + selmon -> wh) - (ny + HEIGHT(c))) < 32) {
                    ny = selmon -> wy + selmon -> wh - HEIGHT(c);
                }

                if (!c -> isfloating && selmon -> lt[selmon -> sellt] -> arrange && 
                   (abs(nx - c -> x) > 32 || abs(ny - c -> y) > 32)) {
                       togglefloating(NULL);
                }

                if (!selmon -> lt[selmon -> sellt] -> arrange || c -> isfloating) {
                    resize(c, nx, ny, c -> w, c -> h, 1);
                }

                break;
        }

    } while (ev.type != ButtonRelease);

    XUngrabPointer(dpy, CurrentTime);

    if ((m = recttomon(c -> x, c -> y, c -> w, c -> h)) != selmon) {
        sendmon(c, m);
        selmon = m;
        focus(NULL);
    }
}


Client *nexttiled(Client *c) {
    for (; c && (c -> isfloating || !ISVISIBLE(c)); c = c -> next);

    return c;
}


void pop(Client *c) {
    detach(c);
    attach(c);
    focus(c);
    arrange(c -> mon);
}


void propertynotify(XEvent *e) {
    Client *c;
    Window trans;
    XPropertyEvent *ev = &e -> xproperty;

    if ((ev -> window == root) && (ev -> atom == XA_WM_NAME)) {
        updatestatus();
    } else if (ev -> state == PropertyDelete) {
        return; /* ignore */
    } else if ((c = wintoclient(ev -> window))) {
            switch(ev -> atom) {
                default: break;
                case XA_WM_TRANSIENT_FOR:
                    if (!c -> isfloating && (XGetTransientForHint(dpy, c -> win, &trans)) &&
                        (c -> isfloating = (wintoclient(trans)) != NULL)) { arrange(c -> mon); }
                    break;
                case XA_WM_NORMAL_HINTS:
                    updatesizehints(c);
                    break;
                case XA_WM_HINTS:
                    updatewmhints(c);
                    drawbars();
                    drawtabs();
                    break;
            }

                if (ev -> atom == XA_WM_NAME || ev -> atom == netatom[NetWMName]) {
                    updatetitle(c);
                    drawtab(c -> mon);
                }

                if (ev -> atom == netatom[NetWMWindowType]) { updatewindowtype(c); }
            }
}


Monitor *recttomon(int x, int y, int w, int h) {
    Monitor *m, *r = selmon;
    int a, area = 0;

    for (m = mons; m; m = m -> next) {
        if ((a = INTERSECT(x, y, w, h, m)) > area) {
            area = a;
            r = m;
        }
    }

    return r;
}


void organizetags(const Arg *arg) {
	Client *c;
	unsigned int occ, unocc, i;
	unsigned int tagdest[LENGTH(tags)];

	occ = 0;

	for (c = selmon -> clients; c; c = c -> next) {
        occ |= (1 << (ffs(c->tags)-1));
    }

	unocc = 0;

	for (i = 0; i < LENGTH(tags); ++i) {
		while (unocc < i && (occ & (1 << unocc))) {
            unocc++;
        }

		if (occ & (1 << i)) {
			tagdest[i] = unocc;
			occ &= ~(1 << i);
			occ |= 1 << unocc;
		}
	}

	for (c = selmon -> clients; c; c = c -> next) {
        c -> tags = 1 << tagdest[ffs(c -> tags) -1];
    }

	if (selmon -> sel) {
        selmon -> tagset[selmon -> seltags] = selmon -> sel -> tags;
    }

	arrange(selmon);
}


void resize(Client *c, int x, int y, int w, int h, int interact) {
    if (applysizehints(c, &x, &y, &w, &h, interact)) {
        resizeclient(c, x, y, w, h);
    }
}


void resizeclient(Client *c, int x, int y, int w, int h) {
    XWindowChanges wc;

    c -> oldx = c -> x; c -> x = wc.x = x;
    c -> oldy = c -> y; c -> y = wc.y = y;
    c -> oldw = c -> w; c -> w = wc.width = w;
    c -> oldh = c -> h; c -> h = wc.height = h;
    wc.border_width = c -> bw;

    if (((nexttiled(c -> mon -> clients) == c && !nexttiled(c -> next))
        || &monocle == c -> mon -> lt[c -> mon -> sellt] -> arrange)
        && !c -> isfullscreen && !c -> isfloating) {
            c -> w = wc.width += c -> bw * 2;
            c -> h = wc.height += c -> bw * 2;
            wc.border_width = 0;
    }

    XConfigureWindow(dpy, c -> win, CWX|CWY|CWWidth|CWHeight|CWBorderWidth, &wc);
    configure(c);
    XSync(dpy, False);
}


void resizemouse(const Arg *arg) {
    int ocx, ocy, nw, nh;
    Client *c;
    Monitor *m;
    XEvent ev;
    Time lasttime = 0;

    if (!(c = selmon -> sel)) { return; }
    if (c -> isfullscreen) /* no support resizing fullscreen windows by mouse */ { return; }

    restack(selmon);
    ocx = c -> x;
    ocy = c -> y;

    if (XGrabPointer(dpy, root, False, MOUSEMASK, GrabModeAsync, GrabModeAsync,
        None, cursor[CurResize] -> cursor, CurrentTime) != GrabSuccess) { return; }

    XWarpPointer(dpy, None, c -> win, 0, 0, 0, 0, c -> w + c -> bw - 1, c -> h + c -> bw - 1);

    do {
        XMaskEvent(dpy, MOUSEMASK|ExposureMask|SubstructureRedirectMask, &ev);
        switch(ev.type) {
            case ConfigureRequest:
            case Expose:
            case MapRequest:
                handler[ev.type](&ev);
                break;
            case MotionNotify:
                if ((ev.xmotion.time - lasttime) <= (1000 / 60)) { continue; }
                lasttime = ev.xmotion.time;

                nw = MAX(ev.xmotion.x - ocx - 2 * c -> bw + 1, 1);
                nh = MAX(ev.xmotion.y - ocy - 2 * c -> bw + 1, 1);

                if (c -> mon -> wx + nw >= selmon -> wx && 
                    c -> mon -> wx + nw <= selmon -> wx + selmon -> ww && 
                    c -> mon -> wy + nh >= selmon -> wy && 
                    c -> mon -> wy + nh <= selmon -> wy + selmon -> wh) {
                        if (!c -> isfloating && selmon -> lt[selmon -> sellt] -> arrange
                        && (abs(nw - c -> w) > 32 || abs(nh - c -> h) > 32)) {
                            togglefloating(NULL);
                        }
                }

                if (!selmon -> lt[selmon -> sellt] -> arrange || c -> isfloating) {
                    resize(c, c -> x, c -> y, nw, nh, 1);
                }

                break;
        }
    } while (ev.type != ButtonRelease);

    XWarpPointer(dpy, None, c -> win, 0, 0, 0, 0, c -> w + c -> bw - 1, c -> h + c -> bw - 1);
    XUngrabPointer(dpy, CurrentTime);

    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));

    if ((m = recttomon(c -> x, c -> y, c -> w, c -> h)) != selmon) {
        sendmon(c, m);
        selmon = m;
        focus(NULL);
    }

}


void restack(Monitor *m) {
    Client *c;
    XEvent ev;
    XWindowChanges wc;

    drawbar(m);
    drawtab(m);

    if (!m -> sel) { return; }

    if (m -> sel -> isfloating || !m -> lt[m -> sellt] -> arrange) {
        XRaiseWindow(dpy, m -> sel -> win);
    }

    if (m -> lt[m -> sellt] -> arrange) {
        wc.stack_mode = Below;
        wc.sibling = m -> barwin;

        for (c = m -> stack; c; c = c -> snext) {
            if (!c -> isfloating && ISVISIBLE(c)) {
                XConfigureWindow(dpy, c -> win, CWSibling|CWStackMode, &wc);
                wc.sibling = c -> win;
            }
        }
    }

    XSync(dpy, False);

    while (XCheckMaskEvent(dpy, EnterWindowMask, &ev));
}


void run() {
    XEvent ev;

    /* main event loop */
    XSync(dpy, False);
    while (running && !XNextEvent(dpy, &ev)) {
        if (handler[ev.type]) { handler[ev.type](&ev); /* call handler */ }
    }
}


void scan() {
    unsigned int i, num;
    Window d1, d2, *wins = NULL;
    XWindowAttributes wa;

    if (XQueryTree(dpy, root, &d1, &d2, &wins, &num)) {
        for (i = 0; i < num; i++) {
            if (!XGetWindowAttributes(dpy, wins[i], &wa) || 
                wa.override_redirect || XGetTransientForHint(dpy, wins[i], &d1)) { continue; }

            if (wa.map_state == IsViewable || getstate(wins[i]) == IconicState) {  
                manage(wins[i], &wa); 
            }
        }

        for (i = 0; i < num; i++) { /* now the transients */
            if (!XGetWindowAttributes(dpy, wins[i], &wa)) { continue; }

            if (XGetTransientForHint(dpy, wins[i], &d1) && 
               (wa.map_state == IsViewable || getstate(wins[i]) == IconicState)) {  
                manage(wins[i], &wa); 
            }
        }

        if (wins) {
            XFree(wins);
        }
    }
}


void sendmon(Client *c, Monitor *m) {
    if (c -> mon == m) {
        return;
    }

    unfocus(c, 1);
    detach(c);
    detachstack(c);

    c -> mon = m;
    c -> tags = m -> tagset[m -> seltags]; /* assign tags of target monitor */

    attach(c);
    attachstack(c);
    focus(NULL);
    arrange(NULL);
}


void setclientstate(Client *c, long state) {
    long data[] = { state, None };

    XChangeProperty(dpy, c -> win, wmatom[WMState], wmatom[WMState], 32,
                    PropModeReplace, (unsigned char *)data, 2);
}


int sendevent(Client *c, Atom proto) {
    int n;
    Atom *protocols;
    int exists = 0;
    XEvent ev;

    if (XGetWMProtocols(dpy, c -> win, &protocols, &n)) {
        while (!exists && n--) {
            exists = protocols[n] == proto;
        }

        XFree(protocols);
    }
    if (exists) {
        ev.type = ClientMessage;
        ev.xclient.window = c -> win;
        ev.xclient.message_type = wmatom[WMProtocols];
        ev.xclient.format = 32;
        ev.xclient.data.l[0] = proto;
        ev.xclient.data.l[1] = CurrentTime;
        XSendEvent(dpy, c -> win, False, NoEventMask, &ev);
    }

    return exists;
}


void shiftview(const Arg *arg) {
    Arg shifted;

    if(arg -> i > 0) /* left circular shift */ {
        shifted.ui = (selmon -> tagset[selmon -> seltags] << arg -> i)
           | (selmon -> tagset[selmon -> seltags] >> (LENGTH(tags) - arg -> i));
    } else /* right circular shift */ {
        shifted.ui = selmon -> tagset[selmon -> seltags] >> (- arg -> i)
           | selmon -> tagset[selmon -> seltags] << (LENGTH(tags) + arg -> i);
    }

    view(&shifted);
}


void setfocus(Client *c) {
    if (!c -> neverfocus) {
        XSetInputFocus(dpy, c -> win, RevertToPointerRoot, CurrentTime);
        XChangeProperty(dpy, root, netatom[NetActiveWindow],
                        XA_WINDOW, 32, PropModeReplace,
                        (unsigned char *) &(c -> win), 1);
    }

    sendevent(c, wmatom[WMTakeFocus]);
}


void setfullscreen(Client *c, int fullscreen) {
    if (fullscreen && !c -> isfullscreen) {
        XChangeProperty(dpy, c -> win, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)&netatom[NetWMFullscreen], 1);

        c -> isfullscreen = 1;
        c -> oldstate = c -> isfloating;
        c -> oldbw = c -> bw;
        c -> bw = 0;
        c -> isfloating = 1;

        resizeclient(c, c -> mon -> mx, c -> mon -> my, c -> mon -> mw, c -> mon -> mh);
        XRaiseWindow(dpy, c -> win);

    } else if (!fullscreen && c -> isfullscreen) {
        XChangeProperty(dpy, c -> win, netatom[NetWMState], XA_ATOM, 32,
                        PropModeReplace, (unsigned char*)0, 0);

        c -> isfullscreen = 0;
        c -> isfloating = c -> oldstate;
        c -> bw = c -> oldbw;
        c -> x = c -> oldx;
        c -> y = c -> oldy;
        c -> w = c -> oldw;
        c -> h = c -> oldh;

        resizeclient(c, c -> x, c -> y, c -> w, c -> h);
        arrange(c -> mon);
    }
}


void setlayout(const Arg *arg) {
    if (!arg || !arg -> v || arg -> v != selmon -> lt[selmon -> sellt]) {
        selmon -> sellt = selmon -> pertag -> sellts[selmon -> pertag -> curtag] ^= 1;
    }

    if (arg && arg -> v) { 
        selmon -> lt[selmon -> sellt] = selmon -> pertag -> ltidxs[selmon -> pertag -> curtag][selmon -> sellt] = (Layout *)arg -> v; 
    }

    strncpy(selmon -> ltsymbol, selmon -> lt[selmon -> sellt] -> symbol, sizeof selmon -> ltsymbol);

    if (selmon -> sel) {
        arrange(selmon);
    } else {
        drawbar(selmon);
    }
}


/* arg > 1.0 will set mfact absolutely */
void setmfact(const Arg *arg) {
    float f;

    if (!arg || !selmon -> lt[selmon -> sellt] -> arrange) { return; }

    f = arg -> f < 1.0 ? arg -> f + selmon -> mfact : arg -> f - 1.0;

    if (f < 0.1 || f > 0.9) { return; }

    selmon -> mfact = selmon -> pertag -> mfacts[selmon -> pertag -> curtag] = f;
    arrange(selmon);
}


void setup() {
    int i;
    XSetWindowAttributes wa;
    Atom utf8string;

    /* clean up any zombies immediately */
    sigchld(0);

    /* init screen */
    screen = DefaultScreen(dpy);
    sw = DisplayWidth(dpy, screen);
    sh = DisplayHeight(dpy, screen);
    root = RootWindow(dpy, screen);
    drw = drw_create(dpy, screen, root, sw, sh);

    if (!drw_fontset_create(drw, fonts, LENGTH(fonts)))
        die("no fonts could be loaded.");

    lrpad = drw -> fonts -> h;
    bh = 32 ? 32 : drw -> fonts -> h + 2;
    th = bh;
    updategeom();

    /* init atoms */
    utf8string = XInternAtom(dpy, "UTF8_STRING", False);
    wmatom[WMProtocols] = XInternAtom(dpy, "WM_PROTOCOLS", False);
    wmatom[WMDelete] = XInternAtom(dpy, "WM_DELETE_WINDOW", False);
    wmatom[WMState] = XInternAtom(dpy, "WM_STATE", False);
    wmatom[WMTakeFocus] = XInternAtom(dpy, "WM_TAKE_FOCUS", False);
    netatom[NetActiveWindow] = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);
    netatom[NetSupported] = XInternAtom(dpy, "_NET_SUPPORTED", False);
    netatom[NetWMName] = XInternAtom(dpy, "_NET_WM_NAME", False);
    netatom[NetWMState] = XInternAtom(dpy, "_NET_WM_STATE", False);
    netatom[NetWMCheck] = XInternAtom(dpy, "_NET_SUPPORTING_WM_CHECK", False);
    netatom[NetWMFullscreen] = XInternAtom(dpy, "_NET_WM_STATE_FULLSCREEN", False);
    netatom[NetWMWindowType] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE", False);
    netatom[NetWMWindowTypeDialog] = XInternAtom(dpy, "_NET_WM_WINDOW_TYPE_DIALOG", False);
    netatom[NetClientList] = XInternAtom(dpy, "_NET_CLIENT_LIST", False);

    /* init cursors */
    cursor[CurNormal] = drw_cur_create(drw, XC_left_ptr);
    cursor[CurResize] = drw_cur_create(drw, XC_sizing);
    cursor[CurMove] = drw_cur_create(drw, XC_fleur);

    /* init appearance */
    scheme = ecalloc(LENGTH(colors), sizeof(Clr *));

    for (i = 0; i < LENGTH(colors); i++) {
        scheme[i] = drw_scm_create(drw, colors[i], 3);
    }

    /* init bars */
    updatebars();
    updatestatus();

    /* supporting window for NetWMCheck */
    wmcheckwin = XCreateSimpleWindow(dpy, root, 0, 0, 1, 1, 0, 0, 0);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *) &wmcheckwin, 1);
    XChangeProperty(dpy, wmcheckwin, netatom[NetWMName], utf8string, 8,
                    PropModeReplace, (unsigned char *) "dynamd", 3);
    XChangeProperty(dpy, root, netatom[NetWMCheck], XA_WINDOW, 32,
                    PropModeReplace, (unsigned char *) &wmcheckwin, 1);

    /* EWMH support per view */
    XChangeProperty(dpy, root, netatom[NetSupported], XA_ATOM, 32,
                    PropModeReplace, (unsigned char *) netatom, NetLast);
    XDeleteProperty(dpy, root, netatom[NetClientList]);

    /* select events */
    wa.cursor = cursor[CurNormal] -> cursor;
    wa.event_mask = SubstructureRedirectMask|SubstructureNotifyMask
        |ButtonPressMask|PointerMotionMask|EnterWindowMask
        |LeaveWindowMask|StructureNotifyMask|PropertyChangeMask;
    XChangeWindowAttributes(dpy, root, CWEventMask|CWCursor, &wa);
    XSelectInput(dpy, root, wa.event_mask);
    grabkeys();
    focus(NULL);
}


void seturgent(Client *c, int urg) {
    XWMHints *wmh;
    c -> isurgent = urg;
    
    if (!(wmh = XGetWMHints(dpy, c -> win))) { return; }

    wmh -> flags = urg ? (wmh -> flags | XUrgencyHint) : (wmh -> flags & ~XUrgencyHint);
    XSetWMHints(dpy, c -> win, wmh);
    XFree(wmh);
}


void showhide(Client *c) {
    if (!c) { return; }

    if (ISVISIBLE(c)) {
        /* show clients top down */
        XMoveWindow(dpy, c -> win, c -> x, c -> y);
        if ((!c -> mon -> lt[c -> mon -> sellt] -> arrange || c -> isfloating) && 
             !c -> isfullscreen) {
            resize(c, c -> x, c -> y, c -> w, c -> h, 0);
        }
        showhide(c -> snext);
    } else {
        /* hide clients bottom up */
        showhide(c -> snext);
        XMoveWindow(dpy, c -> win, WIDTH(c) * -2, c -> y);
    }
}


void sigchld(int unused) {
    pid_t pid;

    if (signal(SIGCHLD, sigchld) == SIG_ERR) { die("can't install SIGCHLD handler:"); }

    while (0 < (pid = waitpid(-1, NULL, WNOHANG))) {
        pid_t *p, *lim;

        if (!(p = autostart_pids)) {
            continue;
        }

        lim = &p[autostart_len];

        for (; p < lim; p++) {
            if (*p == pid) {
                *p = -1;
                break;
            }
        }

    }
}


void spawn(const Arg *arg) {
    if (fork() == 0) {
        if (dpy) {
            close(ConnectionNumber(dpy));
        }

        setsid();
        execvp(((char **)arg -> v)[0], (char **)arg -> v);
        fprintf(stderr, "dynamd: execvp %s", ((char **)arg -> v)[0]);
        perror(" failed");
        exit(EXIT_SUCCESS);
    }
}


void tag(const Arg *arg) {
    if (selmon -> sel && arg -> ui & TAGMASK) {
        selmon -> sel -> tags = arg -> ui & TAGMASK;
        focus(NULL);
        arrange(selmon);
    }
}


void tagmon(const Arg *arg) {
    if (!selmon -> sel || !mons -> next) {
        return;
    }

    sendmon(selmon -> sel, dirtomon(arg -> i));
}


void togglebar(const Arg *arg) {
    selmon -> showbar = selmon -> pertag -> showbars[selmon -> pertag -> curtag] = !selmon -> showbar;
    updatebarpos(selmon);
    XMoveResizeWindow(dpy, selmon -> barwin, selmon -> wx, selmon -> by, selmon -> ww, bh);
    arrange(selmon);
}


void togglefloating(const Arg *arg) {
    if (!selmon -> sel) { return; }
    if (selmon -> sel -> isfullscreen) /* no support for fullscreen windows */ { return; }

    selmon -> sel -> isfloating = !selmon -> sel -> isfloating || selmon -> sel -> isfixed;

    if (selmon -> sel -> isfloating) {
        resize(selmon -> sel, selmon -> sel -> x, selmon -> sel -> y,
               selmon -> sel -> w, selmon -> sel -> h, 0);
    }

    arrange(selmon);
}


void togglefullscr(const Arg *arg) {
  if (selmon -> sel) {
      setfullscreen(selmon -> sel, !selmon -> sel -> isfullscreen);
  }
}


void toggletag(const Arg *arg) {
    unsigned int newtags;

    if (!selmon -> sel) { return; }

    newtags = selmon -> sel -> tags ^ (arg -> ui & TAGMASK);

    if (newtags) {
        selmon -> sel -> tags = newtags;
        focus(NULL);
        arrange(selmon);
    }
}


void toggleview(const Arg *arg) {
    unsigned int newtagset = selmon -> tagset[selmon -> seltags] ^ (arg -> ui & TAGMASK);
    int i;

    if (newtagset) {
        selmon -> tagset[selmon -> seltags] = newtagset;

        if (newtagset == ~0) {
            selmon -> pertag -> prevtag = selmon -> pertag -> curtag;
            selmon -> pertag -> curtag = 0;
        }

        /* test if the user did not select the same tag */
        if (!(newtagset & 1 << (selmon -> pertag -> curtag - 1))) {
            selmon -> pertag -> prevtag = selmon -> pertag -> curtag;
            for (i = 0; !(newtagset & 1 << i); i++);
            selmon -> pertag -> curtag = i + 1;
        }

        /* apply settings for this view */
        selmon -> nmaster = selmon -> pertag -> nmasters[selmon -> pertag -> curtag];
        selmon -> mfact = selmon -> pertag -> mfacts[selmon -> pertag -> curtag];
        selmon -> sellt = selmon -> pertag -> sellts[selmon -> pertag -> curtag];
        selmon -> lt[selmon -> sellt] = selmon -> pertag -> ltidxs[selmon -> pertag -> curtag][selmon -> sellt];
        selmon -> lt[selmon -> sellt^1] = selmon -> pertag -> ltidxs[selmon -> pertag -> curtag][selmon -> sellt^1];

        if (selmon -> showbar != selmon -> pertag -> showbars[selmon -> pertag -> curtag]) {
            togglebar(NULL);
        }

        focus(NULL);
        arrange(selmon);
    }
}


void unfocus(Client *c, int setfocus) {
    if (!c) { return; }

    grabbuttons(c, 0);
    XSetWindowBorder(dpy, c -> win, scheme[SchemeNorm][ColBorder].pixel);

    if (setfocus) {
        XSetInputFocus(dpy, root, RevertToPointerRoot, CurrentTime);
        XDeleteProperty(dpy, root, netatom[NetActiveWindow]);
    }
}


void unmanage(Client *c, int destroyed) {
    Monitor *m = c -> mon;
    XWindowChanges wc;

    if (c -> swallowing) {
        unswallow(c);
        return;
    }

    Client *s = swallowingclient(c -> win);

    if (s) {
        free(s -> swallowing);
        s -> swallowing = NULL;
        arrange(m);
        focus(NULL);

        return;
    }

    detach(c);
    detachstack(c);

    if (!destroyed) {
        wc.border_width = c -> oldbw;
        XGrabServer(dpy); /* avoid race conditions */
        XSetErrorHandler(xerrordummy);
        XConfigureWindow(dpy, c -> win, CWBorderWidth, &wc); /* restore border */
        XUngrabButton(dpy, AnyButton, AnyModifier, c -> win);
        setclientstate(c, WithdrawnState);
        XSync(dpy, False);
        XSetErrorHandler(xerror);
        XUngrabServer(dpy);
    }

    free(c);

    if (!s) {
        arrange(m);
        focus(NULL);
        updateclientlist();
    }
}


void unmapnotify(XEvent *e) {
    Client *c;
    XUnmapEvent *ev = &e -> xunmap;

    if ((c = wintoclient(ev -> window))) {
        if (ev -> send_event) {
            setclientstate(c, WithdrawnState);
        } else {
            unmanage(c, 0);
        }
    }
}


void updatebars() {
    Monitor *m;
    XSetWindowAttributes wa = {
        .override_redirect = True,
        .background_pixmap = ParentRelative,
        .event_mask = ButtonPressMask|ExposureMask
    };

    XClassHint ch = {"dynamd", "dynamd"};

    for (m = mons; m; m = m -> next) {
        if (m -> barwin) { continue; }

        m -> barwin = XCreateWindow(dpy, root, m -> wx, m -> by, m -> ww, bh, 0, DefaultDepth(dpy, screen),
                CopyFromParent, DefaultVisual(dpy, screen),
                CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);

        XDefineCursor(dpy, m -> barwin, cursor[CurNormal] -> cursor);
        XMapRaised(dpy, m -> barwin);

        m -> tabwin = XCreateWindow(dpy, root, m -> wx, m -> ty, m -> ww, th, 0, DefaultDepth(dpy, screen),
                        CopyFromParent, DefaultVisual(dpy, screen),
                        CWOverrideRedirect|CWBackPixmap|CWEventMask, &wa);

        XDefineCursor(dpy, m -> tabwin, cursor[CurNormal] -> cursor);
        XMapRaised(dpy, m -> tabwin);
        XSetClassHint(dpy, m -> barwin, &ch);
    }
}


void updatebarpos(Monitor *m) {
    Client *c;
    int nvis = 0;

    m -> wy = m -> my;
    m -> wh = m -> mh;

    if (m -> showbar) {
        m -> wh -= bh;
        m -> by = m -> topbar ? m -> wy : m -> wy + m -> wh;

        if ( m -> topbar ) {
            m -> wy += bh;
        }
    } else {
         m -> by = -bh;
    }

    for (c = m -> clients; c; c = c -> next) {
        if (ISVISIBLE(c)) { ++nvis; }
    }

    if ((nvis > 1) && (m -> lt[m -> sellt] -> arrange == monocle)) {

        m -> wh -= th;
        m -> ty = m -> toptab ? m -> wy : m -> wy + m -> wh;

        if ( m -> toptab ) {
            m -> wy += th;
        }
    } else {
        m -> ty = -th;
    }
}


void updateclientlist() {
    Client *c;
    Monitor *m;

    XDeleteProperty(dpy, root, netatom[NetClientList]);

    for (m = mons; m; m = m -> next) {
        for (c = m -> clients; c; c = c -> next) {
            XChangeProperty(dpy, root, netatom[NetClientList],
                            XA_WINDOW, 32, PropModeAppend,
                            (unsigned char *) &(c -> win), 1);
        }
    }
}


int updategeom() {
    int dirty = 0;

    if (XineramaIsActive(dpy)) {
        int i, j, n, nn;
        Client *c;
        Monitor *m;
        XineramaScreenInfo *info = XineramaQueryScreens(dpy, &nn);
        XineramaScreenInfo *unique = NULL;

        for (n = 0, m = mons; m; m = m -> next, n++);

        /* only consider unique geometries as separate screens */
        unique = ecalloc(nn, sizeof(XineramaScreenInfo));
        for (i = 0, j = 0; i < nn; i++) {
             if (isuniquegeom(unique, j, &info[i])) {
                memcpy(&unique[j++], &info[i], sizeof(XineramaScreenInfo));
             }
        }

        XFree(info);
        nn = j;

        if (n <= nn) { /* new monitors available */
            for (i = 0; i < (nn - n); i++) {
                for (m = mons; m && m -> next; m = m -> next);

                if (m) {
                    m -> next = createmon();
                } else {
                    mons = createmon();
                }
            }

            for (i = 0, m = mons; i < nn && m; m = m -> next, i++)
                if (i >= n || unique[i].x_org != m -> mx || unique[i].y_org != m -> my
                || unique[i].width != m -> mw || unique[i].height != m -> mh) {

                    dirty = 1;
                    m -> num = i;
                    m -> mx = m -> wx = unique[i].x_org;
                    m -> my = m -> wy = unique[i].y_org;
                    m -> mw = m -> ww = unique[i].width;
                    m -> mh = m -> wh = unique[i].height;
                    updatebarpos(m);
                }
        } else { /* less monitors available nn < n */
            for (i = nn; i < n; i++) {
                for (m = mons; m && m -> next; m = m -> next);

                while ((c = m -> clients)) {
                    dirty = 1;
                    m -> clients = c -> next;
                    detachstack(c);
                    c -> mon = mons;
                    attach(c);
                    attachstack(c);
                }
                if (m == selmon) { selmon = mons; }

                cleanupmon(m);
            }
        }

        free(unique);
    } else {   
        /* default monitor setup */
        if (!mons) {
             mons = createmon();
        }

        if (mons -> mw != sw || mons -> mh != sh) {
            dirty = 1;
            mons -> mw = mons -> ww = sw;
            mons -> mh = mons -> wh = sh;
            updatebarpos(mons);
        }
    }

    if (dirty) {
        selmon = mons;
        selmon = wintomon(root);
    }

    return dirty;
}


void updatenumlockmask() {
    unsigned int i, j;
    XModifierKeymap *modmap;

    numlockmask = 0;
    modmap = XGetModifierMapping(dpy);

    for (i = 0; i < 8; i++) {
        for (j = 0; j < modmap -> max_keypermod; j++) {
            if (modmap -> modifiermap[i * modmap -> max_keypermod + j]
                == XKeysymToKeycode(dpy, XK_Num_Lock)) {
                    numlockmask = (1 << i);
                }
        }
    }

    XFreeModifiermap(modmap);
}


void updatesizehints(Client *c) {
    long msize;
    XSizeHints size;

    if (!XGetWMNormalHints(dpy, c -> win, &size, &msize)) {
        /* size is uninitialized, ensure that size.flags aren't used */
        size.flags = PSize;
    }

    if (size.flags & PBaseSize) {
        c -> basew = size.base_width;
        c -> baseh = size.base_height;
    } else if (size.flags & PMinSize) {
        c -> basew = size.min_width;
        c -> baseh = size.min_height;
    } else {
        c -> basew = c -> baseh = 0;
    }

    if (size.flags & PResizeInc) {
        c -> incw = size.width_inc;
        c -> inch = size.height_inc;
    } else {
        c -> incw = c -> inch = 0;
    }

    if (size.flags & PMaxSize) {
        c -> maxw = size.max_width;
        c -> maxh = size.max_height;
    } else {
        c -> maxw = c -> maxh = 0;
    }

    if (size.flags & PMinSize) {
        c -> minw = size.min_width;
        c -> minh = size.min_height;
    } else if (size.flags & PBaseSize) {
        c -> minw = size.base_width;
        c -> minh = size.base_height;
    } else {
        c -> minw = c -> minh = 0;
    }

    if (size.flags & PAspect) {
        c -> mina = (float)size.min_aspect.y / size.min_aspect.x;
        c -> maxa = (float)size.max_aspect.x / size.max_aspect.y;
    } else {
        c -> maxa = c -> mina = 0.0;
    }

    c -> isfixed = (c -> maxw && c -> maxh && c -> maxw == c -> minw && c -> maxh == c -> minh);
}


void updatestatus() {
    if (!gettextprop(root, XA_WM_NAME, stext, sizeof(stext))) {
        strcpy(stext, "dynamd");
    }

    drawbar(selmon);
}


void updatetitle(Client *c) {
    if (!gettextprop(c -> win, netatom[NetWMName], c -> name, sizeof c -> name)) {
        gettextprop(c -> win, XA_WM_NAME, c -> name, sizeof c -> name);
    }

    if (c -> name[0] == '\0') /* hack to mark broken clients */ {
        strcpy(c -> name, broken);
    }
}


void updatewindowtype(Client *c) {
    Atom state = getatomprop(c, netatom[NetWMState]);
    Atom wtype = getatomprop(c, netatom[NetWMWindowType]);

    if (state == netatom[NetWMFullscreen]) {
        setfullscreen(c, 1);
    }

    if (wtype == netatom[NetWMWindowTypeDialog]) {
        c -> isfloating = 1;
    }
}


void updatewmhints(Client *c) {
    XWMHints *wmh;

    if ((wmh = XGetWMHints(dpy, c -> win))) {
        if (c == selmon -> sel && wmh -> flags & XUrgencyHint) {
            wmh -> flags &= ~XUrgencyHint;
            XSetWMHints(dpy, c -> win, wmh);
        } else {
            c -> isurgent = (wmh -> flags & XUrgencyHint) ? 1 : 0;
        }

        if (wmh -> flags & InputHint) {
            c -> neverfocus = !wmh -> input;
        } else {
            c -> neverfocus = 0;
        }

        XFree(wmh);
    }
}


void view(const Arg *arg) {
    int i;
    unsigned int tmptag;

    if ((arg -> ui & TAGMASK) == selmon -> tagset[selmon -> seltags]) {
        return;
    }

    selmon -> seltags ^= 1; /* toggle sel tagset */

    if (arg -> ui & TAGMASK) {
         selmon -> tagset[selmon -> seltags] = arg -> ui & TAGMASK;
        selmon -> pertag -> prevtag = selmon -> pertag -> curtag;

        if (arg -> ui == ~0) {
            selmon -> pertag -> curtag = 0;
        } else {
            for (i = 0; !(arg -> ui & 1 << i); i++);
            selmon -> pertag -> curtag = i + 1;
        }
    } else {
        tmptag = selmon -> pertag -> prevtag;
        selmon -> pertag -> prevtag = selmon -> pertag -> curtag;
        selmon -> pertag -> curtag = tmptag;
    }

    selmon -> nmaster = selmon -> pertag -> nmasters[selmon -> pertag -> curtag];
    selmon -> mfact = selmon -> pertag -> mfacts[selmon -> pertag -> curtag];
    selmon -> sellt = selmon -> pertag -> sellts[selmon -> pertag -> curtag];
    selmon -> lt[selmon -> sellt] = selmon -> pertag -> ltidxs[selmon -> pertag -> curtag][selmon -> sellt];
    selmon -> lt[selmon -> sellt^1] = selmon -> pertag -> ltidxs[selmon -> pertag -> curtag][selmon -> sellt^1];

    if (selmon -> showbar != selmon -> pertag -> showbars[selmon -> pertag -> curtag]) {
        togglebar(NULL);
    }

    focus(NULL);
    arrange(selmon);
}


pid_t winpid(Window w) {

    pid_t result = 0;

    xcb_res_client_id_spec_t spec = {0};
    spec.client = w;
    spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;

    xcb_generic_error_t *e = NULL;
    xcb_res_query_client_ids_cookie_t c = xcb_res_query_client_ids(xcon, 1, &spec);
    xcb_res_query_client_ids_reply_t *r = xcb_res_query_client_ids_reply(xcon, c, &e);

    if (!r) { return (pid_t)0; }

    xcb_res_client_id_value_iterator_t i = xcb_res_query_client_ids_ids_iterator(r);

    for (; i.rem; xcb_res_client_id_value_next(&i)) {
        spec = i.data -> spec;
        if (spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) {
            uint32_t *t = xcb_res_client_id_value_value(i.data);
            result = *t;
            break;
        }
    }

    free(r);

    if (result == (pid_t)-1) {
        result = 0;
    }

    return result;
}


pid_t getparentprocess(pid_t p) {
    unsigned int v = 0;

    FILE *f;
    char buf[256];
    snprintf(buf, sizeof(buf) - 1, "/proc/%u/stat", (unsigned)p);

    if (!(f = fopen(buf, "r"))) { return 0; }
    
    if (fscanf(f, "%*u %*s %*c %u", &v));
    fclose(f);

    return (pid_t)v;
}


int isdescprocess(pid_t p, pid_t c) {
    while (p != c && c != 0) {
        c = getparentprocess(c);
    }

    return (int)c;
}


Client *termforwin(const Client *w) {
    Client *c;
    Monitor *m;

    if (!w -> pid || w -> isterminal) {
        return NULL;
    }

    for (m = mons; m; m = m -> next) {
        for (c = m -> clients; c; c = c -> next) {
            if (c -> isterminal && !c -> swallowing && c -> pid && 
                isdescprocess(c -> pid, w -> pid)) {
                    return c;
                }
        }
    }

    return NULL;
}


Client *swallowingclient(Window w) {
    Client *c;
    Monitor *m;

    for (m = mons; m; m = m -> next) {
        for (c = m -> clients; c; c = c -> next) {
            if (c -> swallowing && c -> swallowing -> win == w) {
                return c;
            }
        }
    }

    return NULL;
}


Client *wintoclient(Window w) {
    Client *c;
    Monitor *m;

    for (m = mons; m; m = m -> next) {
        for (c = m -> clients; c; c = c -> next) {
            if (c -> win == w) {
                return c;
            }
        }
    }

    return NULL;
}


Monitor *wintomon(Window w) {
    int x, y;
    Client *c;
    Monitor *m;

    if (w == root && getrootptr(&x, &y)) {
        return recttomon(x, y, 1, 1);
    }

    for (m = mons; m; m = m -> next) {
        if (w == m -> barwin || w == m -> tabwin) {
            return m;
        }
    }

    if ((c = wintoclient(w))) {
        return c -> mon;
    }

    return selmon;
}


/* There's no way to check accesses to destroyed windows, thus those cases are
 * ignored (especially on UnmapNotify's). Other types of errors call Xlibs
 * default error handler, which may call exit. */
int xerror(Display *dpy, XErrorEvent *ee) {
    if (ee -> error_code == BadWindow
    || (ee -> request_code == X_SetInputFocus && ee -> error_code == BadMatch)
    || (ee -> request_code == X_PolyText8 && ee -> error_code == BadDrawable)
    || (ee -> request_code == X_PolyFillRectangle && ee -> error_code == BadDrawable)
    || (ee -> request_code == X_PolySegment && ee -> error_code == BadDrawable)
    || (ee -> request_code == X_ConfigureWindow && ee -> error_code == BadMatch)
    || (ee -> request_code == X_GrabButton && ee -> error_code == BadAccess)
    || (ee -> request_code == X_GrabKey && ee -> error_code == BadAccess)
    || (ee -> request_code == X_CopyArea && ee -> error_code == BadDrawable)) { return 0; }

    fprintf(stderr, "dynamd: fatal error: request code=%d, error code=%d\n",
            ee -> request_code, ee -> error_code);

    return xerrorxlib(dpy, ee); /* may call exit */
}


int xerrordummy(Display *dpy, XErrorEvent *ee) {
    return 0;
}


/* Startup Error handler to check if another window manager
 * is already running. */
int xerrorstart(Display *dpy, XErrorEvent *ee) {
    die("dynamd window manager is already running!");

    return -1;
}


void zoom(const Arg *arg) {
    Client *c = selmon -> sel;

    if (!selmon -> lt[selmon -> sellt] -> arrange
    || (selmon -> sel && selmon -> sel -> isfloating)) { return; }

    if (c == nexttiled(selmon -> clients)) {
        if (!c || !(c = nexttiled(c -> next))) {
            return;
        }
    }

    pop(c);
}

int main(int argc, char *argv[]) {
    if (!setlocale(LC_CTYPE, "") || !XSupportsLocale()) {
        fputs("warning: no locale support\n", stderr);
    }

    if (!(dpy = XOpenDisplay(NULL))) {
        die("dynamd: cannot open display");
    }

    if (!(xcon = XGetXCBConnection(dpy))) {
        die("dynamd: cannot get xcb connection\n");
    }

    checkotherwm();
    autostart_exec();
    setup();
    scan();
    run();
    cleanup();
    XCloseDisplay(dpy);

    return EXIT_SUCCESS;
}
