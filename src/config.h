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


/* Appearance */
static const char *fonts[]          = { "MonoLisa:size=15" };
static const char *colors[][3]      = {
    /*                       fg         bg         border  */
    [SchemeNorm] =         { "#ababab", "#222222", "#222222" },
    [SchemeSel]  =         { "#eeeeee", "#222222", "#ff4545" },
};

/* Window */
static const float mfact     = 0.56; /* Factor of master area size [0.05..0.95] */
static const int nmaster     = 1;    /* Number of clients in master area */

/* TAGS */
static const char *tags[] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", 
                              "10", "11", "12", "13", "14", "15", "16", "17", 
                              "18", "19", "20", "21", "22", "23", "24", "25", };

/* Startup Script */
static const char *const autostart[] = {
    "sh", "-c", "/home/uniminin/dynamd/startup/startup.sh", NULL,
    NULL
};

/* Window Rules */
static const Rule rules[] = {
    /* Class            Instance  Title            Tags Mask  IsFloating  IsTerminal  NoSwallow  Monitor */
    { "Alacritty",      NULL,     NULL,            0,         0,          1,           0,        -1 },
    { NULL,             NULL,     "Event Tester",  0,         0,          0,           1,        -1 },
};

/* Window Layouts */
/* First entry is Default */
static const Layout layouts[] = {
    /* symbol    arrange function */
    { "[|W|]",   centeredmaster },          /* Centered */
    { "[M]",     monocle },                 /* Monocle with Tabs */
    { "[T]",     tile },                    /* Tile */
    { "[D]",     deck },                    /* Deck */
    { "[@~]",    dwindle },                 /* Fibonacci -> dwindle */
    { "[~@]",    spiral },                  /* Fibonacci -> Spiral */
    { "[G]",     grid },                    /* Grid */
    { "[GH]",    horizgrid },               /* Grid Hor */
    { "[:G:]",   gaplessgrid },             /* Grid GL */
    { "[TTT]",   bstack },                  /* BottomStack Ver */
    { "[===]",   bstackhoriz },             /* BottomStack Hor */
    { "[|=|]",   centeredfloatingmaster },  /* Floating Centered */
    { "[=]",     NULL },                    /* Floating */
    { NULL,      NULL },                    /* Cycle */
};


/* Key Definitions */
#define ALT   Mod1Mask
#define SUPER Mod4Mask
#define CTRL  ControlMask
#define SHIFT ShiftMask
#define TAGKEYS(KEY,TAG) \
    { SUPER,         KEY,   view,   {.ui = 1 << TAG} }, \
    { SUPER|SHIFT,   KEY,   tag,    {.ui = 1 << TAG} },


/* App Commands */
static const char *alacritty[]  = { "alacritty", NULL };
static const char *flameshot[]  = { "flameshot", "gui", NULL };
static const char *dmenu[]      = { "dmenu_run", "-nb", "black", "-sb", "white", "-nf",
                                    "#858585", "-sf", "black", "-fn", "'MonoLisa-18'", NULL };
static const char *rofi[]       = { "rofi", "-modi", "drun", "-show", "drun", "-theme",
                                    "sidetab", "-matching", "fuzzy", NULL };
static const char *pcmanfm[]    = { "pcmanfm", NULL };


/* Keybindings */
static Key keys[] = {
    /* Modifier                 Key         Function        Argument           */
    /* Apps */
    { SUPER,                    XK_Return,  spawn,          { .v = alacritty } },
    { SUPER,                    XK_space,   spawn,          { .v = flameshot } },
    { SUPER,                    XK_d,       spawn,          { .v = dmenu } },
    { SUPER,                    XK_r,       spawn,          { .v = rofi } },
    { SUPER,                    XK_e,       spawn,          { .v = pcmanfm } },

    /* Window Focus */
    { SUPER,                    XK_Right,   focusstack,     { .i = +1 } },
    { SUPER,                    XK_Left,    focusstack,     { .i = -1 } },

    /* Move Windows */
    { SUPER|SHIFT,              XK_Right,   movestack,      {.i = +1 } },
    { SUPER|SHIFT,              XK_Left,    movestack,      {.i = -1 } },

    /* Resize Windows */
    { SUPER|CTRL,               XK_Right,   setmfact,       { .f = +0.05 } },
    { SUPER|CTRL,               XK_Left,    setmfact,       { .f = -0.05 } },

    /* Resize Gaps */
    { SUPER,                    XK_equal,   gaps,           { .i = +1 } },
    { SUPER,                    XK_minus,   gaps,           { .i = -1 } },

    /* Monitor Focus */
    { SUPER|CTRL,               XK_period,  focusmon,       { .i = +1 } },
    { SUPER|CTRL,               XK_comma,   focusmon,       { .i = -1 } },

    /* Move Window to R/L Monitor */
    { SUPER|SHIFT,              XK_period,  tagmon,         { .i = +1 } },
    { SUPER|SHIFT,              XK_comma,   tagmon,         { .i = -1 } },

    /* Move Two Recent Windows */
    { SUPER|SHIFT,              XK_Return,  zoom,           { 0 } },

    /* Toggle Fullscreen */
    { SUPER,                    XK_f,       togglefullscr,  { 0 } },

    /* Close Window */
    { SUPER,                    XK_q,       killclient,     { 0 } },

    /* Toggle Bar */
    { SUPER,                    XK_b,       togglebar,      { 0 } },

    /* Toggle Gaps */
    { SUPER,                    XK_g,       togglegaps,     { 0 } },

    /* Toggle Floating in a Window */
    { SUPER|SHIFT,              XK_f,       togglefloating, { 0 } },

    /* Forward/Backward to one Tag */
    { SUPER,                    XK_s,       shiftview,      { .i = +1 } },
    { SUPER,                    XK_a,       shiftview,      { .i = -1 } },

    /* Organize Tags */
    { SUPER|SHIFT,              XK_r,       organizetags,   {0} },

    /* Cycle Layout */
    { SUPER,                    XK_x,       cyclelayout,    { .i = +1 } },
    { SUPER,                    XK_z,       cyclelayout,    { .i = -1 } },

    /* Cycle Two Recent Tags */
    { SUPER,                    XK_Tab,     view,           { 0 } },

    /* View all Windows from all Tags */
    { SUPER,                    XK_0,       view,           { .ui = ~0 } },


    /* TAGS */
    TAGKEYS(XK_1, 0)
    TAGKEYS(XK_2, 1)
    TAGKEYS(XK_3, 2)
    TAGKEYS(XK_4, 3)
    TAGKEYS(XK_5, 4)
    TAGKEYS(XK_6, 5)
    TAGKEYS(XK_7, 6)
    TAGKEYS(XK_8, 7)
    TAGKEYS(XK_9, 8)
};

/* Button Definitions */
static Button buttons[] = {
    /* click                event mask      button          function        argument */
    { ClkLtSymbol,          0,              Button1,        setlayout,      {.v = &layouts[0]} },
    { ClkLtSymbol,          0,              Button3,        setlayout,      {.v = &layouts[12]} },
    { ClkClientWin,         SUPER,          Button1,        movemouse,      {0} },
    { ClkClientWin,         SUPER,          Button2,        togglefloating, {0} },
    { ClkClientWin,         SUPER,          Button3,        resizemouse,    {0} },
    { ClkTagBar,            0,              Button1,        view,           {0} },
    { ClkTagBar,            0,              Button3,        toggleview,     {0} },
    { ClkTagBar,            SUPER,          Button1,        tag,            {0} },
    { ClkTagBar,            SUPER,          Button3,        toggletag,      {0} },
    { ClkTabBar,            0,              Button1,        focuswin,       {0} },
};
