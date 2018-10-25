/*
 * fbvncserver.c
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * Started with original fbvncserver for the iPAQ and Zaurus.
 * 	http://fbvncserver.sourceforge.net/
 *
 * Modified by Jim Huang <jserv.tw@gmail.com>
 * 	- Simplified and sizing down
 * 	- Performance tweaks
 *
 * Modified by Steve Guo (letsgoustc)
 *  - Added keyboard/pointer input
 *
 * Modified by Matus Kral <matuskral@me.com>
 *      - rewritten keyboard and pointing device code
 *      - by implementing uinput event driver
 *
 * NOTE: depends libvncserver.
 */

#define VERSION "2.5.4"

#ifndef IMX
#define RPI
#endif

/* define the following to enable debug messages */
#define DEBUG
#ifdef DEBUG
#define DEBUG_VERBOSE
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>

#include <sys/stat.h>
#include <sys/sysmacros.h>             /* For makedev() */

#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/fb.h>

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "rfb/rfbregion.h"

#ifdef RPI
#include "bcm_host.h"
#endif

/* framebuffer */
#ifdef RPI
#define FB_DEVICE "- (dispmanx)"
#endif

#ifdef IMX
#define FB_DEVICE "/dev/fb0"
#endif

#define KEY_SOFT1 KEY_UNKNOWN
#define KEY_SOFT2 KEY_UNKNOWN
#define KEY_CENTER KEY_UNKNOWN
#define KEY_SHARP KEY_UNKNOWN
#define KEY_STAR KEY_UNKNOWN

#ifdef IMX
#define APPNAME "imx-vncserver"
#endif
#ifdef RPI
#define APPNAME "rpi-vncserver"
#endif

#define SCREEN_MAXWIDTH  1920
#define SCREEN_MAXHEIGHT 1080

#define SCREEN_BYTESPP 2

#include <stdio.h>
#include <openssl/md4.h>

#include "xxhash.h"

static int debug = 0;

static int kbdfd = -1;
static int touchfd = -1;
//static unsigned short int *fbmmap = MAP_FAILED;
unsigned char *fbmmap = MAP_FAILED;
//static unsigned short int *vncbuf;
unsigned char *vncbuf = NULL;
//static unsigned short int *fbbuf;
static int cmp_lines = 4;

static long update_usec = 333 * 1000;

MD4_CTX mdContext;
struct hash_t {
	unsigned char md[MD4_DIGEST_LENGTH];
	uint32_t xxh32;
};

struct hash_t *fb_hashtable;

typedef enum {
	PTR_NONE = 0,
	PTR_ABSOLUTE,
	PTR_RELATIVE
} PTR_MODE;

PTR_MODE ptr_mode = PTR_ABSOLUTE;
int last_x;
int last_y;
int mouse_last = 0;

__sighandler_t old_sigint_handler = NULL;
static rfbScreenInfoPtr vncscr;

static int xmin, xmax;
static int ymin, ymax;

/* part of the frame differerencing algorithm. */
static struct varblock_t {
	int min_x;
	int min_y;
	int max_x;
	int max_y;
	int r_offset;
	int g_offset;
	int b_offset;
	int pixels_per_int;
	size_t bytespp;
	size_t pixels;
} varblock;

/* event handler callback */
static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl);

#ifdef DEBUG
# define pr_debug(fmt, ...) \
	if (debug) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
# define pr_debug(fmt, ...) do { } while(0)
#endif

#ifdef DEBUG_VERBOSE
# define pr_vdebug(fmt, ...) \
	if (debug > 1) fprintf(stderr, fmt, ## __VA_ARGS__)
#else
# define pr_vdebug(fmt, ...) do { } while(0)
#endif

#define pr_info(fmt, ...) \
	fprintf(stdout, fmt, ## __VA_ARGS__)

#define pr_err(fmt, ...) \
	fprintf(stderr, fmt, ## __VA_ARGS__)


#ifdef IMX
static struct fb_var_screeninfo scrinfo;
static struct fb_var_screeninfo scrinfo_now;
static int fbfd = -1;
#endif

#ifdef RPI
unsigned long pitch;
DISPMANX_DISPLAY_HANDLE_T   display = DISPMANX_NO_HANDLE;
DISPMANX_RESOURCE_HANDLE_T  resource;
DISPMANX_MODEINFO_T         scrinfo;
void                       *image;
VC_IMAGE_TYPE_T type = VC_IMAGE_RGB565;
VC_IMAGE_TRANSFORM_T		transform = 0;
VC_RECT_T			rect;
#endif

int padded_width;
int screen_width = 0;
int screen_height = 0;

int screen_maxwidth = SCREEN_MAXWIDTH;
int screen_maxheight = SCREEN_MAXHEIGHT;

static int init_fb(void)
{
	int		resulution_changed = 0;
#ifdef RPI
	uint32_t	vc_image_ptr;

	if (display == DISPMANX_NO_HANDLE) {

		display = vc_dispmanx_display_open(0 /* screen */);
		int ret = vc_dispmanx_display_get_info(display, &scrinfo);
		assert(ret == 0);

		if (screen_width != scrinfo.width || screen_height != scrinfo.height) {
			resulution_changed = 1;
		}

		screen_width = scrinfo.width;
		screen_height = scrinfo.height;

		/* DispmanX expects buffer rows to be aligned to a 32 bit boundarys */
		pitch = ALIGN_UP(SCREEN_BYTESPP * screen_width, 32);
		padded_width = pitch/SCREEN_BYTESPP;

		screen_maxwidth = ALIGN_UP(SCREEN_BYTESPP * SCREEN_MAXWIDTH, 32) / SCREEN_BYTESPP;

		varblock.pixels = screen_width * screen_height;
		varblock.bytespp = SCREEN_BYTESPP;

		resource = vc_dispmanx_resource_create(type, screen_width, screen_height, &vc_image_ptr);
		if (resulution_changed) {
			pr_debug("init_fb(): xres=%d, yres=%d, bpp=%d\n",  screen_width, screen_height, (int)varblock.bytespp * 8);
		}
		else {
			pr_vdebug("init_fb()\n");
		}

		/* Bit shifts */
		varblock.r_offset = 11;
		varblock.g_offset = 6;
		varblock.b_offset = 0;

		varblock.pixels_per_int = sizeof(unsigned int) / varblock.bytespp;
	}
#endif
#ifdef IMX
        struct fb_fix_screeninfo finfo;
        unsigned long page_mask, fb_mem_offset;

        if (fbfd < 0) {
		if ((fbfd = open(FB_DEVICE, O_RDONLY)) == -1) {
			perror("open");
			exit(EXIT_FAILURE);
		}

		if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0) {
			perror("ioctl");
			exit(EXIT_FAILURE);
		}
		if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) != 0) {
			perror("ioctl FBIOGET_FSCREENINFO");
			exit(EXIT_FAILURE);
		}

		if (screen_width != scrinfo.xres || screen_height != screen_height) {
			resulution_changed = 1;
		}

		screen_width = padded_width = scrinfo.xres;
		screen_height = scrinfo.yres;

		varblock.pixels = screen_width * screen_height;
		varblock.bytespp = scrinfo.bits_per_pixel / 8;

		pr_debug("init_fb():xres=%d, yres=%d, "
				"xresv=%d, yresv=%d, "
				"xoffs=%d, yoffs=%d, "
				"bpp=%d\n",
		  screen_width, screen_height,
		  (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
		  (int)scrinfo.xoffset, (int)scrinfo.yoffset,
		  (int)scrinfo.bits_per_pixel);

		page_mask = (unsigned long)getpagesize()-1;
		fb_mem_offset = (unsigned long)(finfo.smem_start) & page_mask;

		fbmmap = mmap(NULL, (scrinfo.yres_virtual/screen_height) * varblock.pixels * varblock.bytespp/*finfo.smem_len+fb_mem_offset*/,
				PROT_READ, MAP_SHARED | MAP_NORESERVE, fbfd, 0);

		if (fbmmap == MAP_FAILED) {
			perror("mmap");
			exit(EXIT_FAILURE);
		}
		pr_debug("fbmmap addr %lx\n", (long)fbmmap);

		/* Bit shifts */
		varblock.r_offset = scrinfo.red.offset + scrinfo.red.length - 5;
		varblock.g_offset = scrinfo.green.offset + scrinfo.green.length - 5;
		varblock.b_offset = scrinfo.blue.offset + scrinfo.blue.length - 5;

		varblock.pixels_per_int = sizeof(unsigned int) / varblock.bytespp;
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo_now) < 0) {
		pr_err("failed to get virtual screen info\n");
		scrinfo_now.yoffset = 0;
	}

	if (scrinfo.xres != scrinfo_now.xres || scrinfo.yres != scrinfo_now.yres) {
		resulution_changed = 1;
	}
#endif
	return resulution_changed;
}


static void deinit_fb(void)
{
#ifdef RPI
	if (display != DISPMANX_NO_HANDLE) {
		pr_vdebug("deinit_fb()\n");
		vc_dispmanx_resource_delete(resource);
		vc_dispmanx_display_close(display);
		display = DISPMANX_NO_HANDLE;
	}
#endif
#ifdef IMX
        if (fbmmap) {
		pr_vdebug("deinit_fb()\n");
		munmap(fbmmap, (scrinfo.yres_virtual/screen_height) * varblock.pixels * varblock.bytespp);
		fbmmap = MAP_FAILED;
	}
	if (fbfd != -1) {
		close(fbfd);
		fbfd = -1;
	}
#endif
}


static void init_uinput()
{
	struct uinput_setup usetup;
	struct uinput_abs_setup uabs;

	int retcode, i;

	kbdfd = open("/dev/uinput", O_WRONLY | O_NDELAY );
	pr_debug("open /dev/uinput returned %d.\n", kbdfd);
	if (kbdfd == 0) {
		pr_err("Could not open uinput.\n");
		exit(-1);
	}

	ioctl(kbdfd, UI_SET_EVBIT, EV_KEY);

	for (i = 0; i < KEY_MAX; i++) { //I believe this is to tell UINPUT what keys we can make?
		ioctl(kbdfd, UI_SET_KEYBIT, i);
	}

	ioctl(kbdfd, UI_SET_KEYBIT, BTN_LEFT);
	ioctl(kbdfd, UI_SET_KEYBIT, BTN_RIGHT);
	ioctl(kbdfd, UI_SET_KEYBIT, BTN_MIDDLE);

	memset(&usetup, 0, sizeof(usetup));
	strcpy(usetup.name, "Dell USB Keyboard");
	usetup.id.version = 4;
	usetup.id.bustype = BUS_USB;
	retcode = ioctl(kbdfd, UI_DEV_SETUP, &usetup);
	pr_debug("ioctl UI_DEV_SETUP returned %d.\n", retcode);

	switch (ptr_mode) {
		case PTR_RELATIVE:
			ioctl(kbdfd, UI_SET_EVBIT, EV_REL);
			ioctl(kbdfd, UI_SET_RELBIT, REL_X);
			ioctl(kbdfd, UI_SET_RELBIT, REL_Y);
			break;
		case PTR_ABSOLUTE:
			ioctl(kbdfd, UI_SET_EVBIT, EV_ABS);

			memset(&uabs, 0, sizeof(uabs));
			uabs.code = ABS_X;
			//uabs.absinfo.minimum = 0;
			uabs.absinfo.maximum = screen_width;
			uabs.absinfo.resolution = 10;
			retcode = ioctl(kbdfd, UI_ABS_SETUP, &uabs);
			pr_debug("ioctl UI_ABS_SETUP returned %d.\n", retcode);

			memset(&uabs, 0, sizeof(uabs));
			uabs.code = ABS_Y;
			//uabs.absinfo.minimum = 0;
			uabs.absinfo.maximum = screen_height;
			uabs.absinfo.resolution = 10;
			retcode = ioctl(kbdfd, UI_ABS_SETUP, &uabs);
			pr_debug("ioctl UI_ABS_SETUP returned %d.\n", retcode);
			break;
		default:
			break;
	}

	retcode = ioctl(kbdfd, UI_DEV_CREATE);
	pr_debug("ioctl UI_DEV_CREATE returned %d.\n", retcode);
	if (retcode) {
		pr_err("Error create uinput device %d.\n", retcode);
		exit(-1);
	}

}

static void cleanup_kbd()
{
	if (kbdfd != -1)	{
		ioctl(kbdfd, UI_DEV_DESTROY);
		close(kbdfd);
	}
}

/*****************************************************************************/

static void init_fb_server(int argc, char **argv)
{
	pr_info("Initializing server: %dx%dx%d\n", padded_width, screen_height, varblock.bytespp);

	if (!debug) {
		rfbLogEnable(0);
	}

	/* Allocate the VNC server buffer to be managed (not manipulated) by 
	 * libvncserver. */
	vncbuf = calloc(screen_maxwidth * screen_maxheight, varblock.bytespp);
	assert(vncbuf);

	/* Allocate the comparison buffer for detecting drawing updates from frame
	 * to frame. */
	fb_hashtable = calloc(screen_maxheight / cmp_lines, sizeof(struct hash_t));
	assert(fb_hashtable);
#ifdef RPI
	fbmmap = calloc(screen_maxwidth * screen_maxheight, varblock.bytespp);
	assert(fbmmap);
#endif
	vncscr = rfbGetScreen(&argc, argv, padded_width, screen_height,
			5, /* 8 bits per sample */
			2, /* 2 samples per pixel */
			varblock.bytespp);
	assert(vncscr);

	if (strcmp(vncscr->desktopName, "LibVNCServer") == 0)
		vncscr->desktopName = APPNAME;
	vncscr->frameBuffer = vncbuf;
	vncscr->alwaysShared = TRUE;

	vncscr->kbdAddEvent = keyevent;
	if (ptr_mode != PTR_NONE) {
		vncscr->ptrAddEvent = ptrevent;
	}

	rfbInitServer(vncscr);
	/* Mark as dirty since we haven't sent any updates at all yet. */
	rfbMarkRectAsModified(vncscr, 0, 0, screen_width, screen_height);
}

/*****************************************************************************/
#if 0
void injectKeyEvent(uint16_t code, uint16_t value)
{
	struct input_event ev;
	memset(&ev, 0, sizeof(ev));
	gettimeofday(&ev.time,0);
	ev.type = EV_KEY;
	ev.code = code;
	ev.value = value;
	if(write(kbdfd, &ev, sizeof(ev)) < 0)
	{
		pr_err("write event failed, %s\n", strerror(errno));
	}

	pr_vdebug("injectKey (%d, %d)\n", code , value);
}
#endif

/* device independent */
static int keysym2scancode(rfbKeySym key)
{
    int scancode = 0;

    int code = (int) key;
    if (code>='0' && code<='9') {
        scancode = (code & 0xF) - 1;
        if (scancode<0) scancode += 10;
            scancode += KEY_1;
    } else if (code>=0xFF50 && code<=0xFF58) {
        static const uint16_t map[] =
            {  KEY_HOME, KEY_LEFT, KEY_UP, KEY_RIGHT, KEY_DOWN,
            KEY_PAGEUP, KEY_PAGEDOWN, KEY_END, 0 };
        scancode = map[code & 0xF];
    } else if (code>=0xFFE1 && code<=0xFFEE) {
        static const uint16_t map[] =
            {  KEY_LEFTSHIFT, KEY_LEFTSHIFT,
            KEY_LEFTCTRL, KEY_LEFTCTRL,
            KEY_LEFTSHIFT, KEY_LEFTSHIFT,
            0,0,            
            KEY_LEFTALT, KEY_RIGHTALT,
            0, 0, 0, 0 };
        scancode = map[code & 0xF];
    } else if ((code>='A' && code<='Z') || (code>='a' && code<='z')) {
        static const uint16_t map[] = {
            KEY_A, KEY_B, KEY_C, KEY_D, KEY_E,
            KEY_F, KEY_G, KEY_H, KEY_I, KEY_J,
            KEY_K, KEY_L, KEY_M, KEY_N, KEY_O,
            KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T,
            KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z };
        scancode = map[(code & 0x5F) - 'A'];
    } else {        
        switch (code) {
        case XK_space:    scancode = KEY_SPACE;       break;

        case XK_exclam: scancode = KEY_1; break;
        case XK_at:     scancode         = KEY_2; break;
        case XK_numbersign:    scancode      = KEY_3; break;
        case XK_dollar:    scancode  = KEY_4; break;
        case XK_percent:    scancode = KEY_5; break;
        case XK_asciicircum:    scancode     = KEY_6; break;
        case XK_ampersand:    scancode       = KEY_7; break;
        case XK_asterisk:    scancode        = KEY_8; break;
        case XK_parenleft:    scancode       = KEY_9; break;
        case XK_parenright:    scancode      = KEY_0; break;
        case XK_minus:    scancode   = KEY_MINUS; break;
        case XK_underscore:    scancode      = KEY_MINUS; break;
        case XK_equal:    scancode   = KEY_EQUAL; break;
        case XK_plus:    scancode    = KEY_EQUAL; break;
        case XK_BackSpace:    scancode       = KEY_BACKSPACE; break;
        case XK_Tab:    scancode             = KEY_TAB; break;

        case XK_braceleft:    scancode        = KEY_LEFTBRACE;     break;
        case XK_braceright:    scancode       = KEY_RIGHTBRACE;     break;
        case XK_bracketleft:    scancode      = KEY_LEFTBRACE;     break;
        case XK_bracketright:    scancode     = KEY_RIGHTBRACE;     break;
        case XK_Return:    scancode  = KEY_ENTER;     break;

        case XK_semicolon:    scancode        = KEY_SEMICOLON;     break;
        case XK_colon:    scancode    = KEY_SEMICOLON;     break;
        case XK_apostrophe:    scancode       = KEY_APOSTROPHE;     break;
        case XK_quotedbl:    scancode         = KEY_APOSTROPHE;     break;
        case XK_grave:    scancode    = KEY_GRAVE;     break;
        case XK_asciitilde:    scancode       = KEY_GRAVE;     break;
        case XK_backslash:    scancode        = KEY_BACKSLASH;     break;
        case XK_bar:    scancode              = KEY_BACKSLASH;     break;

        case XK_comma:    scancode    = KEY_COMMA;      break;
        case XK_less:    scancode     = KEY_COMMA;      break;
        case XK_period:    scancode   = KEY_DOT;      break;
        case XK_greater:    scancode  = KEY_DOT;      break;
        case XK_slash:    scancode    = KEY_SLASH;      break;
        case XK_question:    scancode         = KEY_SLASH;      break;
        case XK_Caps_Lock:    scancode        = KEY_CAPSLOCK;      break;

        case XK_F1:    scancode               = KEY_F1; break;
        case XK_F2:    scancode               = KEY_F2; break;
        case XK_F3:    scancode               = KEY_F3; break;
        case XK_F4:    scancode               = KEY_F4; break;
        case XK_F5:    scancode               = KEY_F5; break;
        case XK_F6:    scancode               = KEY_F6; break;
        case XK_F7:    scancode               = KEY_F7; break;
        case XK_F8:    scancode               = KEY_F8; break;
        case XK_F9:    scancode               = KEY_F9; break;
        case XK_F10:    scancode              = KEY_F10; break;
        case XK_Num_Lock:    scancode         = KEY_NUMLOCK; break;
        case XK_Scroll_Lock:    scancode      = KEY_SCROLLLOCK; break;

        case XK_Page_Down:    scancode        = KEY_PAGEDOWN; break;
        case XK_Insert:    scancode   = KEY_INSERT; break;
        case XK_Delete:    scancode   = KEY_DELETE; break;
        case XK_Page_Up:    scancode  = KEY_PAGEUP; break;
        case XK_Escape:    scancode   = KEY_ESC; break;

        case 0x0003:    scancode = KEY_CENTER;      break;
        }
    }

    return scancode;
}

typedef struct kev_event {
	struct input_event key;
	struct input_event syn;
} KEY_EVENT;

static void kev(unsigned int key, int down)
{
	KEY_EVENT kev;

	memset(&kev, 0, sizeof(kev));

	gettimeofday(&kev.key.time, NULL);
	kev.syn.time = kev.key.time;
	kev.key.type = EV_KEY;
	kev.key.code = key;
	kev.key.value = down ? 1: 0 ; //key pressed/released
	kev.syn.type = EV_SYN;
	kev.syn.code = SYN_REPORT; //not sure what this is for? i'm guessing its some kind of sync thing?
	write(kbdfd, &kev, sizeof(kev));

	pr_debug("key event %04x %s\n", kev.key.code, down ? "pressed" : "released");
}

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
	kev(keysym2scancode(key), down);
}

typedef struct ptr_event {
	struct input_event ptr_x;
	struct input_event ptr_y;
	struct input_event syn;
} PTR_EVENT;

static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl)
{
	PTR_EVENT pev;

	memset(&pev, 0, sizeof(pev));

	gettimeofday(&pev.ptr_x.time, NULL);
	pev.ptr_y.time = pev.syn.time = pev.ptr_x.time;

	pev.syn.type = EV_SYN;
	pev.syn.code = SYN_REPORT;

	int sync_xy = 0;

	switch (ptr_mode) {
		case PTR_RELATIVE:
			pev.ptr_x.type = pev.ptr_y.type = EV_REL;
			pev.ptr_x.code = REL_X;
			pev.ptr_y.code = REL_Y;
			if (x <= 0) {
				pev.ptr_x.value = 1 - screen_width;
				sync_xy = 1;
			}
			else if (x >= (screen_width - 1)) {
				pev.ptr_x.value = screen_width - 1;
				sync_xy = 1;
			}
			else {
				pev.ptr_x.value = x - last_x;
				last_x = x;
			}
			if (y <= 0) {
				pev.ptr_y.value = 1 - screen_height;
				sync_xy = 1;
			}
			else if (y >= (screen_height - 1)) {
				pev.ptr_y.value = screen_height - 1;
				sync_xy = 1;
			}
			else {
				pev.ptr_y.value = y - last_y;
				last_y = y;
			}
			last_x = x;
			last_y = y;
			if (sync_xy) {
				pr_debug("ptr event x=%d, y=%d, dx=%d, dy=%d\n", x, y, pev.ptr_x.value, pev.ptr_y.value);
			}
			break;
		case PTR_ABSOLUTE:
			pev.ptr_x.type = pev.ptr_y.type = EV_ABS;
			pev.ptr_x.code = ABS_X;
			pev.ptr_y.code = ABS_Y;
			pev.ptr_x.value = x;
			pev.ptr_y.value = y;
			break;
	}
	write(kbdfd, &pev, sizeof(pev));

        if (mouse_last != buttonMask) {
                int left_l = mouse_last & 0x1;
                int left_w = buttonMask & 0x1;
                if (left_l != left_w) {
			kev(BTN_LEFT, left_w);
                }

                int middle_l = mouse_last & 0x2;
                int middle_w = buttonMask & 0x2;
		if (middle_l != middle_w) {
			kev(BTN_MIDDLE, middle_w >> 1);
                }

                int right_l = mouse_last & 0x4;
                int right_w = buttonMask & 0x4;
		if (right_l != right_w) {
			kev(BTN_RIGHT, right_w >> 2);
                }

                mouse_last = buttonMask;
        }
}

#if 0
static void ptrevent(int buttonMask, int x, int y, rfbClientPtr cl)
{
	/* Indicates either pointer movement or a pointer button press or release. The pointer is
now at (x-position, y-position), and the current state of buttons 1 to 8 are represented
by bits 0 to 7 of button-mask respectively, 0 meaning up, 1 meaning down (pressed).
On a conventional mouse, buttons 1, 2 and 3 correspond to the left, middle and right
buttons on the mouse. On a wheel mouse, each step of the wheel upwards is represented
by a press and release of button 4, and each step downwards is represented by
a press and release of button 5. 
  From: http://www.vislab.usyd.edu.au/blogs/index.php/2009/05/22/an-headerless-indexed-protocol-for-input-1?blog=61 */
	
	pr_vdebug("Got ptrevent: %04x (x=%d, y=%d)\n", buttonMask, x, y);
	if(buttonMask & 1) {
		// Simulate left mouse event as touch event
		injectTouchEvent(1, x, y);
		injectTouchEvent(0, x, y);
	}
}
#endif

void blank_framebuffer()
{
	memset(vncbuf, 0, screen_width * screen_height / varblock.pixels_per_int);
	memset(fb_hashtable, 0, screen_height / cmp_lines * sizeof(struct hash_t));
}

#define PIXEL_FB_TO_RFB(p,r,g,b) \
	((p >> r) & 0x1f001f) | \
	(((p >> g) & 0x1f001f) << 5) | \
	(((p >> b) & 0x1f001f) << 10)

static int update_screen(void)
{
	unsigned int *f;
	uint32_t c;
	unsigned int *r;
	int i, x, y;

#ifdef RPI
	if (init_fb() > 0) {
		pr_info("Screen resolution changed to %dx%d\n",  scrinfo.width, scrinfo.height);
		rfbProcessEvents(vncscr, 100);
		rfbNewFramebuffer(vncscr, (char *)vncbuf, padded_width, screen_height, 5, 2, varblock.bytespp);
		pr_debug("rfbNewFramebuffer %dx%d %d\n", padded_width, screen_height, varblock.bytespp);
		blank_framebuffer();
		rfbMarkRectAsModified(vncscr, 0, 0, screen_width, screen_height);
		deinit_fb();
		return TRUE;
	}
	vc_dispmanx_snapshot(display, resource, transform);
	vc_dispmanx_rect_set(&rect, 0, 0, screen_width, screen_height);
	vc_dispmanx_resource_read_data(resource, &rect, fbmmap, pitch);
	deinit_fb();
#endif

#ifdef IMX
	if (init_fb() > 0) {
		pr_info("Screen resolution changed to %dx%d\n", scrinfo_now.xres, scrinfo_now.yres);
		rfbProcessEvents(vncscr, 100);
		deinit_fb();
		init_fb();
		rfbNewFramebuffer(vncscr, (char *)vncbuf, padded_width, screen_height, 5, 2, varblock.bytespp);
		pr_debug("rfbNewFramebuffer %dx%d %d\n", padded_width, screen_height, varblock.bytespp);
		blank_framebuffer();
		rfbMarkRectAsModified(vncscr, 0, 0, screen_width, screen_height);
		return TRUE;
	}
	pr_vdebug(" %d :::: %d \n", scrinfo_now.yoffset, varblock.pixels_per_int);
#endif

	varblock.min_x = varblock.min_y = INT_MAX;
	varblock.max_x = varblock.max_y = -1;

	/* jump to right virtual screen */
	for (y = 0; y < screen_height; y+=cmp_lines)
	{
#ifdef RPI
		f = (unsigned int *)(fbmmap + (y * padded_width * varblock.bytespp));
#endif
#ifdef IMX
		f = (unsigned int *)(fbmmap + ((y + scrinfo_now.yoffset) * padded_width * varblock.bytespp));
#endif
		c = XXH32((unsigned char *)f, padded_width * cmp_lines * varblock.bytespp, 0);
		if (c == fb_hashtable[y/cmp_lines].xxh32)
			continue;

		r = (unsigned int *)(vncbuf + (y * padded_width * varblock.bytespp));
		fb_hashtable[y/cmp_lines].xxh32 = c;

		for (x = 0; x < padded_width * cmp_lines; x += varblock.pixels_per_int)
		{
			unsigned int pixel = *f;

			*r = PIXEL_FB_TO_RFB(pixel,
					varblock.r_offset,
					varblock.g_offset,
					varblock.b_offset);

			f++;
			r++;
		}
		if (y < varblock.min_y)
			varblock.min_y = y;
		if (y + cmp_lines> varblock.max_y)
			varblock.max_y = y + cmp_lines;
	}

	if (varblock.min_y != INT_MAX)
	{
		varblock.min_x = 0;
		varblock.max_x = padded_width-1;
	}

	if (varblock.min_x < INT_MAX)
	{
		if (varblock.max_x < 0)
			varblock.max_x = varblock.min_x;

		if (varblock.max_y < 0)
			varblock.max_y = varblock.min_y;

		pr_vdebug("Changed frame: %dx%d @ (%d,%d)...\n",
		  (varblock.max_x) - varblock.min_x,
		  (varblock.max_y) - varblock.min_y,
		  varblock.min_x, varblock.min_y);

		rfbMarkRectAsModified(vncscr, varblock.min_x, varblock.min_y,
		  varblock.max_x, varblock.max_y);

		rfbProcessEvents(vncscr, update_usec / 3); /* update quickly */
		return TRUE;
	}
	return FALSE;

}

/*****************************************************************************/

void print_usage(char **argv)
{
	pr_info("%s version %s\nusage:\n  %s [-c lines] %s[-p mode] [-u msec] [-h[ rfb]]\n",
		APPNAME, VERSION, APPNAME,
#ifdef DEBUG
		"[-d] "
#else
		""
#endif
		);
	pr_info("    -c     : use <lines> lines for hashing (default %d)\n", cmp_lines);
	pr_info("    -d[d]  : enable debug output, -dd is more verbose\n");
	pr_info("    -p     : use <mode> as pointer mode (none, abs, rel)\n");
	pr_info("    -u     : polling screen interval in <msec> ms (default %d)\n", update_usec / 1000);
	pr_info("    -h     : print this help\n");
	pr_info("    -h rfb : print additional rfb options\n");
}

void exit_cleanup(void)
{
#ifdef RPI
	if (fbmmap)
		free(fbmmap);
	bcm_host_deinit();
#endif
	pr_info("Cleaning up...\n");
#ifdef IMX
	deinit_fb();
#endif
	cleanup_kbd();
}

void sigint_handler(int arg)
{
	if (old_sigint_handler)
		old_sigint_handler(arg);

	if (vncbuf)
		free(vncbuf);

	rfbScreenCleanup(vncscr);

	pr_err("<break> exit.\n");
	exit(-1);
}

int main(int argc, char **argv)
{
	if (argc > 1) {
		int i = 1;
		while (i < argc) {
			if(*argv[i] == '-') {
				switch(*(argv[i] + 1)) 	{
					case 'h':
						if (++i < argc && strcmp(argv[i], "rfb") == 0) {
							rfbUsage();
						}
						else {
							print_usage(argv);
						}
						exit(0);
						break;
					case 'c':
						cmp_lines = atoi(argv[++i]);
						break;
					case 'p':
						if (++i < argc) {
							if (strcmp(argv[i], "none") == 0) {
								ptr_mode = PTR_NONE;
							}
							else if (strcmp(argv[i], "abs") == 0) {
								ptr_mode = PTR_ABSOLUTE;
							}
							else if (strcmp(argv[i], "rel") == 0) {
								ptr_mode = PTR_RELATIVE;
							}
						}
						break;
					case 'd':
						debug = 1;
						if (*(argv[i] + 2) == 'd')
							++debug;
						break;
					case 'u':
						update_usec = atol(argv[++i]) * 1000;
						break;
				}
			}
			i++;
		}
	}

	pr_info("%s version %s\n\n", APPNAME, VERSION);
	pr_info("Initializing framebuffer device: %s\n", FB_DEVICE);
#ifdef RPI
	bcm_host_init();
#endif
	init_fb();
	init_uinput();
	init_fb_server(argc, argv);

	pr_info("\nFramebuffer VNC server initialized:\n");
	pr_info("	width:		%d\n", (int)screen_width);
	pr_info("	height:		%d\n", (int)screen_height);
	pr_info("	bpp:		%d\n", (int)16);
	pr_info("	bytespp:	%d\n", (int)varblock.bytespp);
	pr_info("	pixelperint:	%d\n", (int)varblock.pixels_per_int);
	pr_info("	port:		%d\n\n", vncscr->port);
	pr_info("	hash lines:	%d\n\n", (int)cmp_lines);
	pr_info("offsets:\n");
	pr_info("	R:		%d\n", varblock.r_offset);
	pr_info("	G:		%d\n", varblock.g_offset);
	pr_info("	B:		%d\n", varblock.b_offset);

	atexit(exit_cleanup);
	old_sigint_handler = signal(SIGINT, sigint_handler);

	pr_info("\nsuccessfully started\n");

	/* Implement our own event loop to detect changes in the framebuffer. */
	while (1) {
		rfbClientPtr client_ptr;
		if (!vncscr->clientHead) {
			update_screen();
			deinit_fb();
			/* sleep until getting a client */
			while (!vncscr->clientHead) {
				rfbProcessEvents(vncscr, LONG_MAX);
			}
			/* Send KEY_LEFTSHIFT to wakeup screen if necessary */
			keyevent(TRUE, 0xFFE1, NULL);
			keyevent(FALSE, 0xFFE1, NULL);
		}

		/* scan screen if at least one client has requested */
		for (client_ptr = vncscr->clientHead; client_ptr; client_ptr = client_ptr->next) {
			if (!sraRgnEmpty(client_ptr->requestedRegion)) {
				if (update_screen()) {
					break;
				}
			}
			/* refresh screen every 150 ms */
			rfbProcessEvents(vncscr, update_usec);
		}
	}

	return 0;
}
