/*
 * MPlayer
 * 
 * Video driver for serdisplib - 0.9.6
 * 
 * by Wolfgang Astleitner <mrwastl@users.sourceforge.net>
 * 
 * Code started:  2004-10-31
 * Version 0.1 :  2004-10-31
 * Version 0.2 - 0.? : experimental stuff, changes, colour support, ...
 * Version 0.9.0: 2007-02-26: approaching production-level. code-review. no need for serdisplib-headerfiles and library at compiletime -> dlopen() / dlsys()
 * Version 0.9.1: 2007-03-25: improved non-dithered drawing for greyscale displays, some code optimising
 * Version 0.9.2: 2009-01-19: adaptions for new colour (fixed 4 byte length)
 * Version 0.9.3: 2009-06-13: adaptions for new API
 * Version 0.9.4: 2014-12-07: added support for default device
 * Version 0.9.5: 2014-12-15: added help, added option for debug info, corrected viewmode 2, cleaned up code
 *                2014-12-15: corrections for static compilation mode
 * Version 0.9.6: 2016-05-15: added checks whether serdisp_cliparea() may be used reliably
 *                            removed support for very old serdisp version not supporting new colour functions to clean up code
 *                            get rid of compiler warning when calling sws_scale()
 *
 */

#include "../config.h"

#if (CONFIG_SERDISP == 2)
  #undef SERDISP_STATIC
#else
  #define SERDISP_STATIC
#endif

#include <stdio.h>
#include <stdlib.h>

#include <sys/stat.h>
#include <unistd.h>

#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <time.h>
#include <string.h>
#include <errno.h>


#include "video_out.h"
#include "video_out_internal.h"
#include "aspect.h"
#include "libswscale/swscale.h"
#include "libmpcodecs/vf_scale.h"
#include "sub/sub.h"

#include "osdep/keycodes.h"
#include "m_option.h"
#include "mp_msg.h"
#include "subopt-helper.h"


#ifdef SERDISP_STATIC
  #include "serdisplib/serdisp.h"
#else
  #include <dlfcn.h>   /* for dlopen() / dlsym() */
#endif


static vo_info_t info = {
    "serdisplib",
    "serdisp",
    "Wolfgang Astleitner <mrwastl@users.sourceforge.net>",
    ""
};

const LIBVO_EXTERN(serdisp)



/* some defines for OSD (margin, dimension rel. to display geometry) */
#define SD_OSD_BORDERGAP 10
#define SD_OSD_MARGIN    2
#define SD_OSD_HIPERCENT 10
#define SD_OSD_MINHEIGHT 6

/* range for greyvalues: [0 - 255] */
#define MAX_GREYVALUE 255

/* used for the sws */
static uint8_t * image[3] = {0,0,0};
static int image_stride[3];

/* image infos */
static int image_format;
static int image_width;
static int image_height;
static int image_colours;
static int screen_x, screen_y;
static int screen_w, screen_h;
static int src_width;
static int src_height;


static int istruecolour = 0;               /* monochrome/greyscale or truecolour image */
static int isclipareasave = 1;             /* is it save to use serdisp_cliparea()? */

static  float serdisp_flag_gamma = 1.0;    /* gamma value */
static  int serdisp_flag_gamma_enable = 0; /* gamma correction enabled (1) or disabled (0) */
static  int serdisp_flag_bandpass = 0;     /* band pass filter for dithering (< value: black, > value: white) */
static  int serdisp_flag_threshold = 127;  /* threshold value for monochrome displays (higher than this value: set pixel, else: don't set) */
static  int serdisp_flag_viewmode = 0;
static  int serdisp_flag_algo = 1;         /* 0: threshold,  1: floyd steinberg */
static  int serdisp_flag_debug = 0;        /* 0: no debug info, 1: show debug info */


static uint32_t  fg_colour;                   /* foreground colour */
static uint32_t  bg_colour;                   /* background colour */

static int  osd_updated = 0;                  /* needed for OSD cleanup hack */
static int  osd_height = -1;                  /* pre-calculated position and size values for osd */
static int  osd_margin = SD_OSD_MARGIN;
static int  osd_bar_height = -1;
static int  osd_posy = -1;

static struct SwsContext *sws=NULL;

/* our version of the playmodes :) */

extern void mplayer_put_key(int code);

/* extra parameters */


static long     serdisp_version;             /* serdisplib version returned from library */

static const char help_msg[] =
    "\n-vo serdisp command line help:\n"
    "Example: mplayer -vo serdisp:name=sdl:device=out?:viewmode=1:options=brightness=30\n"
    "\nOptions:\n"
    "    name (required)\n"
    "      driver-name in serdisplib (e.g.: 'rs232')\n"
    "    device (optional, if not given, default device for <name> is used (serdisplib >= 2.00))\n"
    "      device string (e.g.: 'RS232?/dev/usb/ttyUSB0')\n"
    "      NOTE: all ':' need to be replaced by '?' because mplayer uses ':' as a separator\n"
    "    options (optional)\n"
    "      options string (e.g.: 'brightness=30;rot=90')\n"
    "    backlight (default: 1)\n"
    "      0: switch backlight off\n"
    "      1: switch backlight on\n"
    "    viewmode (default: 0)\n"
    "      0 : normal (fit video into screen)\n"
    "      1 : fit only width into screen (height might be clipped)\n"
    "      2 : fit only height into screen (width might be clipped)\n"
    "    debug (default: 0)\n"
    "      0: no debug information\n"
    "      1: print debug information\n"
    "\n\n"
    "  Options only applicable to monochrome or greyscale displays:\n"
    "    dither (default: 1) \n"
    "      0 : threshold\n"
    "      1 : floyd steinberg\n"
    "    threshold (only valid for monochrome displays, default: 127)\n"
    "      threshold value for threshold dithering, some value out of [0, 255]\n"
    "    bandpass (default: 30)\n"
    "      bandpass value for floyd steinberg dithering, some value out of [0, 255]\n"
    "    gamma (default: 1.0)\n"
    "      gamma correction\n"
    "\n"
    ;



#ifdef SERDISP_STATIC
  static serdisp_t*      dd;
  static serdisp_CONN_t* sdcd;

/*  extern char*           sd_errormsg;   */              /* extra error message */
  #define fp_serdisp_getversioncode            serdisp_getversioncode
  #define fp_SDCONN_open                       SDCONN_open
  #define fp_serdisp_init                      serdisp_init
  #define fp_serdisp_rewrite                   serdisp_rewrite
  #define fp_serdisp_update                    serdisp_update
  #define fp_serdisp_clear                     serdisp_clear
  #define fp_serdisp_isoption                  serdisp_isoption
  #define fp_serdisp_setoption                 serdisp_setoption
  #define fp_serdisp_getoption                 serdisp_getoption
  #define fp_serdisp_getwidth                  serdisp_getwidth
  #define fp_serdisp_getheight                 serdisp_getheight
  #define fp_serdisp_getcolours                serdisp_getcolours
  #define fp_serdisp_getdepth                  serdisp_getdepth
  #define fp_serdisp_getpixelaspect            serdisp_getpixelaspect
  #define fp_serdisp_quit                      serdisp_quit

  #define fp_serdisp_setsdcol                  serdisp_setsdcol
  #define fp_serdisp_setsdgrey                 serdisp_setsdgrey
  #define fp_serdisp_getsdcol                  serdisp_getsdcol

  #define fp_serdisp_cliparea                  serdisp_cliparea
  #define fp_serdisp_defaultdevice             serdisp_defaultdevice
#else
  char* errmsg; /* error message returned by dlerror() */
  /* default background/foreground colours */
  #define SD_COL_BLACK      0xFF000000
  #define SD_COL_WHITE      0xFFFFFFFF

  /* calculation of serdisp version information */
  #define SERDISP_VERSION(a,b) ((long)(((a) << 8) + (b)))
  #define SERDISP_VERSION_GET_MAJOR(_c)  ((int)( (_c) >> 8 ))
  #define SERDISP_VERSION_GET_MINOR(_c)  ((int)( (_c) & 0xFF ))

  /* dyn.loaded stuff: dyn.loaded libraries, function pointers, ... */
  static void*    sdhnd;                       /* serdisplib handle */
  static void*    dd;                          /* display descriptor */
  static void*    sdcd;                        /* serdisp connect descriptor */
  static char*    sd_errormsg;                 /* extra error message */

  static long     (*fp_serdisp_getversioncode) (void);

  static void*    (*fp_SDCONN_open)            (const char sdcddev[]);

  static void*    (*fp_serdisp_init)           (void* sdcd, const char dispname[], const char extra[]);
  static void     (*fp_serdisp_rewrite)        (void* dd);
  static void     (*fp_serdisp_update)         (void* dd);
  static void     (*fp_serdisp_clear)          (void* dd);
  static int      (*fp_serdisp_isoption)       (void* dd, const char* optionname);
  static void     (*fp_serdisp_setoption)      (void* dd, const char* optionname, long value);
  static long     (*fp_serdisp_getoption)      (void* dd, const char* optionname, int* typesize);
  static int      (*fp_serdisp_getwidth)       (void* dd);
  static int      (*fp_serdisp_getheight)      (void* dd);
  static int      (*fp_serdisp_getcolours)     (void* dd);
  static int      (*fp_serdisp_getdepth)       (void* dd);
  static int      (*fp_serdisp_getpixelaspect) (void* dd);
  static void     (*fp_serdisp_quit)           (void* dd);

  static void     (*fp_serdisp_setsdcol)       (void* dd, int x, int y, uint32_t colour);
  static void     (*fp_serdisp_setsdgrey)      (void* dd, int x, int y, unsigned char grey);
  static uint32_t (*fp_serdisp_getsdcol)       (void* dd, int x, int y);

  static int      (*fp_serdisp_cliparea)       (void* dd, int x, int y, int w, int h, int sx, int sy, int cw, int ch, int inpmode, unsigned char* content);
  static char*    (*fp_serdisp_defaultdevice)  (const char* dispname);

  /* some macros to simplify life */
  #define dlerror_die(_symbol) { if ( (errmsg = dlerror()) != 0  ) { \
                                 mp_msg(MSGT_VO,MSGL_ERR,"dlsym(): cannot load symbol %s. additional info: %s\n", _symbol, errmsg); \
                                 return VO_ERROR; \
                                 } \
                               }
#endif  /* SERDISP_STATIC */

static void     (*drawing_algo)              (unsigned char** image, int sx, int sy, int w, int h);


static char* my_replace(char *str, const char fromchar, const char tochar) {
  int i;
  if (!str)
    return NULL;
  for (i = 0; i < strlen(str); i++)
    if (str[i] == fromchar)
      str[i] = tochar;
  return str;
}


/* *********************************
   drawingalgo_dithergrey(image, sx, sy, w, h)
   *********************************
   dithers a frame on a monochrome/greyscale display using floyd-steinberg dithering
   *********************************
   image  ... mplayer frame
   sx/sy  ... phys. start position
   w/h    ... width/height of frame
   *********************************
   --
*/
static void drawingalgo_dithergrey(unsigned char** image, int sx, int sy, int w, int h) {
  int x, y;
  int xslop, dslop;
  int yslop[w];
  int i, j, k, t, q;

  double f = 0.0;

  unsigned char* buffer = image[0];

  t = ((MAX_GREYVALUE + 1) * 2) / image_colours;  /* threshold factor */
  q = MAX_GREYVALUE / (image_colours-1);          /* quantisation factor */
  j = (9 * MAX_GREYVALUE) / 32;
  for (x = 0; x < w; x++)
    yslop[x] = j;

  for (y = 0; y < h; y++) {
    xslop = (7 * MAX_GREYVALUE) / 32;
    dslop = MAX_GREYVALUE / 32;

    for (x = 0; x < w; x++) {
      i = buffer[x+(sx-screen_x) + (y+(sy-screen_y)) * image_width];

      if (serdisp_flag_gamma_enable) {
        f = pow((double)i / 255.0, 1.0 / serdisp_flag_gamma);

        i = (int)(f * 255.0);
      }

      if (serdisp_flag_bandpass) {
        if (i >= (MAX_GREYVALUE - serdisp_flag_bandpass) ) 
          i = MAX_GREYVALUE;
        else if (i <= serdisp_flag_bandpass) 
          i = 0;
      }

      i += xslop + yslop[x];
      j = (i / t) *q;
      if (j > MAX_GREYVALUE) 
        j = MAX_GREYVALUE;   /* should never occur (but to be sure ...) */

      fp_serdisp_setsdgrey(dd, x+sx, y+sy, j);

      i = i - j;
      k = (i >> 4);
      xslop = 7 * k;
      yslop[x] = (5*k) + dslop;
      if (x > 0)
        yslop[x-1] += 3 * k;

      dslop = i - (15 * k);
    }
  }
}


/* *********************************
   drawingalgo_directgrey(image, sx, sy, w, h)
   *********************************
   draws a frame without using dithering
   *********************************
   image  ... mplayer frame
   sx/sy  ... phys. start position
   w/h    ... width/height of frame
   *********************************
   --
*/
static void drawingalgo_directgrey(unsigned char** image, int sx, int sy, int w, int h) {
  int x, y;
  int i;
  double f = 0.0;

  unsigned char* buffer = image[0];

  for (y = 0; y < h; y++) {
    for (x = 0; x < w; x++) {
      i = buffer[x+(sx-screen_x)  +  (y+(sy-screen_y)) * image_width];

      if (serdisp_flag_gamma_enable) {
        f = pow((double)i / 255.0, 1.0 / serdisp_flag_gamma);

        i = (int)(f * 255.0);
      }

      /* if monochrome-display: consider threshold value (default value: 127) */
      if (image_colours == 2) {
        fp_serdisp_setsdgrey(dd, x+sx, y+sy, ((i <= serdisp_flag_threshold) ? 0 : MAX_GREYVALUE) );
      } else {
        fp_serdisp_setsdgrey(dd, x+sx, y+sy, i);  /* let serdisplib do the greyvalue matching if greyscale display */
      }
    }
  }
}


/* *********************************
   drawingalgo_truecolour(image, sx, sy, w, h)
   *********************************
   draws a frame directly on a colour display (without dithering, ...)
   *********************************
   image  ... mplayer frame
   sx/sy  ... phys. start position
   w/h    ... width/height of frame
   *********************************
   --
*/
static void drawingalgo_truecolour(unsigned char** image, int sx, int sy, int w, int h) {
  int x, y;
  int r, g, b;
  /*long i;*/

  int diff_x = sx - screen_x;
  int diff_y = sy - screen_y;

  int shifty;
  int shiftx;

  unsigned char* buffer = image[0];

  if (isclipareasave) {
    fp_serdisp_cliparea(dd, sx, sy, w, h, diff_x, diff_y, image_width, image_height, 24, buffer);
  } else {
    shifty = diff_y * image_width;
    for (y = 0; y < h; y++) {
      for (x = 0; x < w; x++) {
        shiftx = (x + diff_x + shifty );
        shiftx += shiftx+shiftx;  /* to avoid ' * 3 ' */
        r = buffer[shiftx++];
        g = buffer[shiftx++];
        b = buffer[shiftx  ];

        fp_serdisp_setsdcol(dd, x+sx, y+sy, 0xFF000000 | (r << 16) | (g << 8) | b);
      }
      shifty += image_width;
    }
  }
}





static int preinit(const char *arg) {
  char* dispname = NULL;
  char* sdcddev   = NULL;
  char* serdisp_options   = NULL;    /* serdisplib options (wiring, ... ) */

  int serdisp_flag_backlight = 1;    /* backlight on (1) or off (0) */

  int serdisp_flag_showhelp = 0;

  const opt_t subopts[] = {
    {"name",      OPT_ARG_MSTRZ, &dispname,  NULL},
    {"device",    OPT_ARG_MSTRZ, &sdcddev,  NULL},
    {"options",   OPT_ARG_MSTRZ, &serdisp_options,  NULL},
    {"backlight", OPT_ARG_BOOL,  &serdisp_flag_backlight, NULL},
    {"viewmode",  OPT_ARG_INT,   &serdisp_flag_viewmode, NULL},
    {"dither",    OPT_ARG_INT,   &serdisp_flag_algo, NULL},
    {"threshold", OPT_ARG_INT,   &serdisp_flag_threshold, NULL},
    {"bandpass",  OPT_ARG_INT,   &serdisp_flag_bandpass, NULL},
    {"gamma",     OPT_ARG_FLOAT, &serdisp_flag_gamma, NULL},
    {"help",      OPT_ARG_BOOL,  &serdisp_flag_showhelp, NULL},
    {"debug",     OPT_ARG_BOOL,  &serdisp_flag_debug, NULL},
    {NULL, 0, NULL, NULL}
  };


  if ( (subopt_parse(arg, subopts) != 0) || serdisp_flag_showhelp) {
    mp_msg(MSGT_VO, MSGL_FATAL, help_msg);
    return VO_ERROR;
  }

  if (sdcddev) {
    my_replace(sdcddev, '?', ':');
  }

  if (serdisp_options) {
    my_replace(serdisp_options, '?', ':');
  }

  if (serdisp_flag_debug) {
    fprintf(stderr, "vo_serdisp.preinit(): name / device:  %s / %s\n", dispname, sdcddev); 
    fprintf(stderr, "vo_serdisp.preinit(): options: %s\n", (serdisp_options) ? serdisp_options : "(none)"); 
  }

#ifdef SERDISP_STATIC
  #ifndef SD_SUPP_ARCHINDEP_SDCOL_FUNCTIONS
    #error "vo_serdisp.c: serdisp library too old (no support for new colour functions)"
  #endif
#else
  /* dyn-load library and init. function pointers */
  errno = 0;

  /* try default library path first */
  sdhnd = dlopen("libserdisp.so", RTLD_LAZY);
  if (!sdhnd) { /* try /usr/local/lib */
    sdhnd = dlopen("/usr/local/lib/libserdisp.so", RTLD_LAZY);
  }

  if (!sdhnd) { /* serdisplib seems not to be installed */
    mp_msg(MSGT_VO,MSGL_ERR,"vo_serdisp: unable to dynamically load library '%s'. additional info: %s\n", "libserdisp.so", strerror(errno));
    return VO_ERROR;
  }

  dlerror(); /* clear error code */

  /* get serdisp version */
  fp_serdisp_getversioncode = (long int (*)(void)) dlsym(sdhnd, "serdisp_getversioncode");
  if (dlerror() || ((serdisp_version = fp_serdisp_getversioncode()) < SERDISP_VERSION(1,96))) { /* no function serdisp_getversioncode() or <= 1.96 */
    mp_msg(MSGT_VO,MSGL_ERR,"vo_serdisp: serdisplib too old (needs to be at least 1.96)\n");
    return VO_ERROR;
  }

  if (serdisp_flag_debug) {
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp: detected serdisplib version: %d.%d\n", 
           SERDISP_VERSION_GET_MAJOR(serdisp_version), SERDISP_VERSION_GET_MINOR(serdisp_version)
          );
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp: name: %s  device: %s\n", dispname, sdcddev);
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp: options: %s\n", (serdisp_options) ? serdisp_options : "(none)");
  }

  /* fetching symbols for function pointers and error message */
  fp_SDCONN_open = (void*(*)(const char*)) dlsym(sdhnd, "SDCONN_open");
  dlerror_die("SDCONN_open");

  fp_serdisp_quit = (void (*)(void*)) dlsym(sdhnd, "serdisp_quit");
  dlerror_die("serdisp_quit");

  fp_serdisp_isoption = (int (*)(void*, const char*)) dlsym(sdhnd, "serdisp_isoption");
  dlerror_die("serdisp_isoption");

  fp_serdisp_setoption = (void (*)(void*, const char*, long int)) dlsym(sdhnd, "serdisp_setoption");
  dlerror_die("serdisp_setoption");

  fp_serdisp_getoption = (long int(*)(void*, const char*, int*)) dlsym(sdhnd, "serdisp_getoption");
  dlerror_die("serdisp_getoption");

  fp_serdisp_init = (void*(*)(void*, const char*, const char*)) dlsym(sdhnd, "serdisp_init");
  dlerror_die("serdisp_init");

  fp_serdisp_rewrite = (void (*)(void*)) dlsym(sdhnd, "serdisp_rewrite");
  dlerror_die("serdisp_rewrite");

  fp_serdisp_update = (void (*)(void*)) dlsym(sdhnd, "serdisp_update");
  dlerror_die("serdisp_update");

  fp_serdisp_clear = (void (*)(void*)) dlsym(sdhnd, "serdisp_clear");
  dlerror_die("serdisp_clear");

  fp_serdisp_getwidth = (int (*)(void*)) dlsym(sdhnd, "serdisp_getwidth");
  dlerror_die("serdisp_getwidth");

  fp_serdisp_getheight = (int (*)(void*)) dlsym(sdhnd, "serdisp_getheight");
  dlerror_die("serdisp_getheight");

  fp_serdisp_getcolours = (int (*)(void*)) dlsym(sdhnd, "serdisp_getcolours");
  dlerror_die("serdisp_getcolours");

  fp_serdisp_getpixelaspect = (int (*)(void*)) dlsym(sdhnd, "serdisp_getpixelaspect");
  dlerror_die("serdisp_getpixelaspect");

  fp_serdisp_getdepth = (int (*)(void*)) dlsym(sdhnd, "serdisp_getdepth");
  dlerror_die("serdisp_getdepth");

  sd_errormsg = (char*) dlsym(sdhnd, "sd_errormsg");
  dlerror_die("sd_errormsg");

  fp_serdisp_setsdcol = (void (*)(void*, int, int, uint32_t)) dlsym(sdhnd, "serdisp_setsdcol");
  dlerror_die("serdisp_setsdcol");

  fp_serdisp_setsdgrey = (void (*)(void*, int, int, unsigned char)) dlsym(sdhnd, "serdisp_setsdgrey");
  dlerror_die("serdisp_setsdgrey");

  fp_serdisp_getsdcol = (uint32_t (*)(void*, int, int)) dlsym(sdhnd, "serdisp_getsdcol");
  dlerror_die("serdisp_getsdcol");

  fp_serdisp_cliparea = (int (*)(void*, int, int, int, int, int, int, int, int, int, unsigned char*)) dlsym(sdhnd, "serdisp_cliparea");
  dlerror(); /* clear error code */

  fp_serdisp_defaultdevice = (char* (*)(const char*)) dlsym(sdhnd, "serdisp_defaultdevice");
  dlerror(); /* clear error code */

  /* done loading all required symbols */
#endif /* SERDISP_STATIC */

#ifdef SERDISP_STATIC
  if (! sdcddev) {
#else
  if (! sdcddev && fp_serdisp_defaultdevice) {
#endif
    char* tempdev = (char*) fp_serdisp_defaultdevice(dispname);
    sdcddev = (char*) malloc( strlen(tempdev) + 1);
    if (sdcddev) {
      strncpy(sdcddev, tempdev, strlen(tempdev)) ;
      sdcddev[strlen(tempdev)] = '\0';
    } else {
      mp_msg(MSGT_VO,MSGL_ERR,"vo_serdisp: unable to allocate memory for defaukt device string (device %s)\n", sdcddev);
      return VO_ERROR;
    }
  }

  sdcd = fp_SDCONN_open(sdcddev);

  if (sdcd == (void*)0) {
    mp_msg(MSGT_VO,MSGL_ERR,"vo_serdisp: unable to open output device %s, additional info: %s\n", sdcddev, sd_errormsg);
    return VO_ERROR;
  }

  dd = fp_serdisp_init(sdcd, dispname, (serdisp_options) ? serdisp_options : "");

  if (!dd) {
    mp_msg(MSGT_VO,MSGL_ERR,"vo_serdisp: unknown display or unable to open %s, additional info: %s\n", dispname, sd_errormsg);
    return VO_ERROR;
  }

  /* pre-init some flags, function pointers, ... */
  if (fp_serdisp_isoption(dd, "SELFEMITTING") && fp_serdisp_getoption(dd, "SELFEMITTING", 0)) {
    fg_colour = SD_COL_WHITE;
    bg_colour = SD_COL_BLACK;
  } else {
    fg_colour = SD_COL_BLACK;
    bg_colour = SD_COL_WHITE;
  }

  image_colours = fp_serdisp_getcolours(dd);

  /* colour depth >= 8 ==> truecolour  (even if display w/ 256 grey-levels) */
  if (fp_serdisp_getdepth(dd) >= 8) {
    istruecolour = 1;
  }


  fp_serdisp_clear(dd);
  fp_serdisp_setoption(dd, "BACKLIGHT", serdisp_flag_backlight);


  image[0] = (uint8_t*)malloc( fp_serdisp_getwidth(dd) * fp_serdisp_getheight(dd) * 4);
  image[1] = NULL;
  image[2] = NULL;

  if (!image[0]) {
    mp_msg(MSGT_VO,MSGL_ERR,"vo_serdisp: unable to allocate array for image\n");
    return VO_ERROR;
  }

  if (!istruecolour) {
    switch (serdisp_flag_algo) {
      case 0:
       drawing_algo = &drawingalgo_directgrey;
       break;
      case 1:
       drawing_algo = &drawingalgo_dithergrey;
       break;
      default:
       drawing_algo = &drawingalgo_dithergrey;
    }
  } else {
    drawing_algo = &drawingalgo_truecolour;
  }

  /* osd size and position */
  /* osd height is relative to display height */
  osd_height = fp_serdisp_getheight(dd) / SD_OSD_HIPERCENT;
  osd_bar_height = osd_height - 2 * osd_margin;

  if (osd_bar_height < SD_OSD_MINHEIGHT) {
    osd_bar_height = SD_OSD_MINHEIGHT;
    osd_margin = 1;
    osd_height = osd_bar_height + osd_margin * 2;
  }
  osd_posy = (fp_serdisp_getheight(dd) - osd_height) ;

  return 0;
}


static int
config(uint32_t width, uint32_t height, uint32_t d_width,
       uint32_t d_height, uint32_t flags, char *title,
       uint32_t format) {
  /*
   * main init
   * called by mplayer
   */

  double fact_w = 1.0, fact_h = 1.0, fact;
 
  /* normalised width and height. width = 100, height is calculated using pixel aspect ratio and pixel geometry */
  int aspect_w, aspect_h;

  image_format = format;

  aspect_save_orig(width,height);
  aspect_save_prescale(d_width,d_height);

  /* calculate display area dimension that will be used. also do aspect-ratio correction for displays with non-quadratic pixels */
  aspect_w = 100;
  aspect_h = (fp_serdisp_getpixelaspect(dd) * fp_serdisp_getheight(dd)) / fp_serdisp_getwidth(dd);


  fact_w = (double)d_width  / (double) aspect_w;
  fact_h = (double)d_height / (double) aspect_h;

  switch (serdisp_flag_viewmode) {
    case 1:
      fact = fact_w;
      break;
    case 2:
      fact = fact_h;
      break;
    default:
      fact = (fact_w > fact_h) ? fact_w : fact_h;
  }

  screen_w = (int)( ((double)d_width / fact) * ( (double)(fp_serdisp_getwidth(dd)) / (double)aspect_w)  );
  screen_h = (int)( ((double)d_height / fact) * ( (double)(fp_serdisp_getheight(dd)) / (double)aspect_h)  );

  if (serdisp_flag_viewmode == 0) {
    /* clip potential rounding errors */
    if (screen_w > fp_serdisp_getwidth(dd)) screen_w = fp_serdisp_getwidth(dd);
    if (screen_h > fp_serdisp_getheight(dd)) screen_h = fp_serdisp_getheight(dd);
  }

  /* check whether it is save to use serdisp_cliparea() */
  if (
#ifndef SERDISP_STATIC
    (! fp_serdisp_cliparea) ||
#endif
    (istruecolour && (screen_w != fp_serdisp_getwidth(dd)) )
  ) {
    isclipareasave = 0;
  }

  screen_x = (fp_serdisp_getwidth(dd) -screen_w) >> 1;
  screen_y = (fp_serdisp_getheight(dd)-screen_h) >> 1;

  src_width = width;
  src_height = height;
  image_width = screen_w;
  image_height = screen_h;

  if(sws) 
    sws_freeContext(sws);

  sws = sws_getContextFromCmdLine(src_width,src_height,image_format,
  image_width,image_height, (istruecolour) ? IMGFMT_RGB24 : IMGFMT_Y8);

  if (istruecolour) {
    image_stride[0] = image_width * 3;
    image_stride[1] = 0; 
    image_stride[2] = 0;
  } else {
    image_stride[0] = image_width;
    image_stride[1] = 0; 
    image_stride[2] = 0;
  }

  if (serdisp_flag_debug) {
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp.config(): serdisplib version: %d.%d\n", SERDISP_VERSION_GET_MAJOR(serdisp_version), SERDISP_VERSION_GET_MINOR(serdisp_version)); 
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp.config(): incoming params: width/height: %d/%d -> d_width/d_height: %d/%d  flags: %04x\n", 
                                width, height, d_width, d_height, flags);
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp.config(): + aspect ratio corr.: src_w/_h: %d/%d -> image_w/_h: %d/%d  pixel asp.ratio: %.2f\n", 
                               src_width, src_height, image_width, image_height, (fp_serdisp_getpixelaspect(dd) / 100.0));
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp.config(): dest geometry: x/y/w/h: %d/%d/%d/%d (factor: %f)\n", screen_x, screen_y, screen_w, screen_h, fact);
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp.config(): phys. display dimensions: w/h: %d/%d\n", fp_serdisp_getwidth(dd), fp_serdisp_getheight(dd));
    mp_msg(MSGT_VO, MSGL_INFO, "vo_serdisp.config(): flags: algo: %d, threshold: %d, gamma[enabled=%d]: %.2f, viewmode: %d, cliparea: %d\n", 
                               serdisp_flag_algo, serdisp_flag_threshold, serdisp_flag_gamma_enable, serdisp_flag_gamma, serdisp_flag_viewmode, isclipareasave);
  }

  return 0;
}


static int 
query_format(uint32_t format) {
    /*
     * ...are we able to... ?
     * called by mplayer
     * All input format supported by the sws
     */

/* taken from vo_dga.c */
    /* serdisplib only supports RGB-like colour spaces, no YUV ones */
    if ((format & IMGFMT_BGR_MASK) == IMGFMT_BGR ) {
        return VFCAP_CSP_SUPPORTED | VFCAP_SWSCALE | VFCAP_OSD;
    }
    return 0;
}

static int 
draw_frame(uint8_t *src[]) {
  int stride[3] = { 0 , 0 , 0 };

  switch(image_format) {
  case IMGFMT_BGR15:
  case IMGFMT_BGR16:
    stride[0] = src_width*2;
    break;
  case IMGFMT_IYU2:
  case IMGFMT_BGR24:
    stride[0] = src_width*3;
    break;
  case IMGFMT_BGR32:
    stride[0] = src_width*4;
    break;
  }

  sws_scale(sws,(const uint8_t* const *)src,stride,0,src_height,image,image_stride); 

  /* small hack which cleans potential remainders of a previously drawn OSD */
  /* the drawing routines might not use the hole display area for drawing a frame (because of aspect ratio a.s.o.), 
     but the OSD always uses the bottom of the display. 
     parts of the OSD might not be reached by the drawing routines() and because of this ugly remainders would occur.
     this hack simply fills the space not reached by the drawing routines using the background colour */
  if (osd_updated) {  /* only apply the hack after an OSD draw event */
    int i,j;
    for (j = osd_posy; j < osd_posy + osd_height; j++) {
      for (i = 0 ; i < fp_serdisp_getwidth(dd); i++) {
        fp_serdisp_setsdcol(dd, i, j, bg_colour);
      }
    }
    osd_updated = 0;  /* do this only once */
  }

  drawing_algo(image, screen_x, screen_y, screen_w, screen_h);

  return 0;
}


static int 
draw_slice(uint8_t *src[], int stride[], int w, int h, int x, int y) {
  int dx1 = screen_x + (x * screen_w / src_width);
  int dy1 = screen_y + (y * screen_h / src_height);
  int dx2 = screen_x + ((x+w) * screen_w / src_width);
  int dy2 = screen_y + ((y+h) * screen_h / src_height);

  sws_scale(sws, (const uint8_t* const*) src, stride, y, h, image, image_stride);
  drawing_algo(image, dx1, dy1, dx2-dx1, dy2-dy1);

  return 0;
}


static void 
flip_page(void) {

  /* print out */
  fp_serdisp_update(dd);
}


static void 
uninit(void) {
  /*
   * THE END
   */
  if (image[0]) {
    free(image[0]);
    image[0] = 0;
  }
  fp_serdisp_quit(dd);
}



static void
draw_osd(void) {
  int i,j, s;
  int bar_width;
  int bordergap = SD_OSD_BORDERGAP;

  if (vo_osd_progbar_type != -1) {
    bar_width = ((fp_serdisp_getwidth(dd) - 2 * bordergap  ) * vo_osd_progbar_value) / 255;
    /* draw background using foreground colour */
    for (j = osd_posy; j < osd_posy + osd_height; j++) {
      for (i = 0; i < fp_serdisp_getwidth(dd); i++) {
        fp_serdisp_setsdcol(dd, i,j, fg_colour);
      }
    }
    /* draw progress-bar using background colour */
    for (j = osd_posy + osd_margin ; j < osd_posy + osd_margin + osd_bar_height ; j++) {
      for (i = 0 ; i < bar_width ; i++) {
        fp_serdisp_setsdcol(dd, bordergap + i, j, bg_colour);
      }
      for (s = 0; s < 5; s++) {
        i = bordergap +   (((fp_serdisp_getwidth(dd) - bordergap*2) / 4) * s);
        if (!(s % 2) || (j % 2)) {
          fp_serdisp_setsdcol(dd, i ,j, fp_serdisp_getsdcol(dd, i, j) ^ 0xFFFFFF);
        }
      }
    }
    osd_updated = 1; /* clean up remainders of osd */
  }
}



static void
check_events(void) {
}


static int
control(uint32_t request, void *data) {
  switch (request) {
    case VOCTRL_QUERY_FORMAT:
      return query_format(*((uint32_t*)data));
  }
  return VO_NOTIMPL;
}

