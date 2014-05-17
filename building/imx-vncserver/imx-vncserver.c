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

/* define the following to enable debug messages */
/* #define DEBUG */
/* #define DEBUG_VERBOSE */

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

#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>

/* libvncserver */
#include "rfb/rfb.h"
#include "rfb/keysym.h"
#include "rfb/rfbregion.h"

/* framebuffer */
#define FB_DEVICE "/dev/fb0"

#define KEY_SOFT1 KEY_UNKNOWN
#define KEY_SOFT2 KEY_UNKNOWN
#define KEY_CENTER KEY_UNKNOWN
#define KEY_SHARP KEY_UNKNOWN
#define KEY_STAR KEY_UNKNOWN

#define VNC_PORT 5900

#define APPNAME "imxvncserver"

static struct fb_var_screeninfo scrinfo;
static int buffers = 2; 
static int fbfd = -1;
static int kbdfd = -1;
static int touchfd = -1;
static unsigned short int *fbmmap = MAP_FAILED;
static unsigned short int *vncbuf;
static unsigned short int *fbbuf;
static int pixels_per_int;

size_t pixels;
size_t bytespp;

int relative_mode = 0;
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
} varblock;

/* event handler callback */
static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl);
static void doptr(int buttonMask, int x, int y, rfbClientPtr cl);

#ifdef DEBUG
# define pr_debug(fmt, ...) \
	 fprintf(stderr, fmt, ## __VA_ARGS__)
#else
# define pr_debug(fmt, ...) do { } while(0)
#endif

#ifdef DEBUG_VERBOSE
# define pr_vdebug(fmt, ...) \
	 pr_debug(fmt, ## __VA_ARGS__)
#else
# define pr_vdebug(fmt, ...) do { } while(0)
#endif

#define pr_info(fmt, ...) \
	fprintf(stdout, fmt, ## __VA_ARGS__)

#define pr_err(fmt, ...) \
	fprintf(stderr, fmt, ## __VA_ARGS__)

static void init_fb(void)
{
	if ((fbfd = open(FB_DEVICE, O_RDONLY)) == -1) {
		perror("open");
		exit(EXIT_FAILURE);
	}

	if (ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) != 0) {
		perror("ioctl");
		exit(EXIT_FAILURE);
	}

	pixels = scrinfo.xres * scrinfo.yres;
	bytespp = scrinfo.bits_per_pixel / 8;

	pr_info("xres=%d, yres=%d, "
			"xresv=%d, yresv=%d, "
			"xoffs=%d, yoffs=%d, "
			"bpp=%d\n", 
	  (int)scrinfo.xres, (int)scrinfo.yres,
	  (int)scrinfo.xres_virtual, (int)scrinfo.yres_virtual,
	  (int)scrinfo.xoffset, (int)scrinfo.yoffset,
	  (int)scrinfo.bits_per_pixel);

	fbmmap = mmap(NULL, buffers * pixels * bytespp,
			PROT_READ, MAP_SHARED, fbfd, 0);

	if (fbmmap == MAP_FAILED) {
		perror("mmap");
		exit(EXIT_FAILURE);
	}
}

static void cleanup_fb(void)
{
        if(fbmmap)
        {
                munmap(fbmmap, buffers * pixels * bytespp);
        }
	if(fbfd != -1)
	{
		close(fbfd);
	}
}

static void init_uinput()                                              
{                                                              
        struct uinput_user_dev   uinp;                         
        int retcode, i;                                        
                                                               
                                                               
        kbdfd = open("/dev/uinput", O_WRONLY | O_NDELAY );     
        printf("open /dev/uinput returned %d.\n", kbdfd);      
        if (kbdfd == 0) {                                      
                printf("Could not open uinput.\n");            
                exit(-1);
        }

        memset(&uinp, 0, sizeof(uinp));
        strncpy(uinp.name, "VNC", 20);
        uinp.id.version = 4;
        uinp.id.bustype = BUS_USB;

        if (!relative_mode) {
                uinp.absmin[ABS_X] = 0;
                uinp.absmax[ABS_X] = scrinfo.xres;
                uinp.absmin[ABS_Y] = 0;
                uinp.absmax[ABS_Y] = scrinfo.yres;
        }
                                                                                
        ioctl(kbdfd, UI_SET_EVBIT, EV_KEY);
                                                                                
        for (i=0; i<KEY_MAX; i++) { //I believe this is to tell UINPUT what keys we can make?
                ioctl(kbdfd, UI_SET_KEYBIT, i);
        }
                                                                                
        ioctl(kbdfd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(kbdfd, UI_SET_KEYBIT, BTN_RIGHT);
        ioctl(kbdfd, UI_SET_KEYBIT, BTN_MIDDLE);
                                                                                
        if (relative_mode) {
                ioctl(kbdfd, UI_SET_EVBIT, EV_REL);
                ioctl(kbdfd, UI_SET_RELBIT, REL_X);
                ioctl(kbdfd, UI_SET_RELBIT, REL_Y);
        }
        else {
                ioctl(kbdfd, UI_SET_EVBIT, EV_ABS);
                ioctl(kbdfd, UI_SET_ABSBIT, ABS_X);
                ioctl(kbdfd, UI_SET_ABSBIT, ABS_Y);
        }

        retcode = write(kbdfd, &uinp, sizeof(uinp));
        printf("First write returned %d.\n", retcode);

        retcode = (ioctl(kbdfd, UI_DEV_CREATE));
        printf("ioctl UI_DEV_CREATE returned %d.\n", retcode);
        if (retcode) {
                printf("Error create uinput device %d.\n", retcode);
                exit(-1);
        }
}

static void cleanup_kbd()
{
    if(kbdfd != -1)
    {
        ioctl(kbdfd, UI_DEV_DESTROY);
        close(kbdfd);
    }
}

/*****************************************************************************/

static void init_fb_server(int argc, char **argv)
{
	pr_info("Initializing server...\n");

	/* Allocate the VNC server buffer to be managed (not manipulated) by 
	 * libvncserver. */
	vncbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(vncbuf != NULL);

	/* Allocate the comparison buffer for detecting drawing updates from frame
	 * to frame. */
	fbbuf = calloc(scrinfo.xres * scrinfo.yres, scrinfo.bits_per_pixel / 2);
	assert(fbbuf != NULL);

	vncscr = rfbGetScreen(&argc, argv, scrinfo.xres, scrinfo.yres,
			5, /* bits per sample */
			2, /* samples per pixel */
			scrinfo.bits_per_pixel / 8  /* bytes/sample */ );

	assert(vncscr != NULL);

	vncscr->desktopName = APPNAME;
	vncscr->frameBuffer = (char *)vncbuf;
	vncscr->alwaysShared = TRUE;
	vncscr->httpDir = NULL;
	vncscr->port = VNC_PORT;

	vncscr->kbdAddEvent = keyevent;
	vncscr->ptrAddEvent = doptr;

	rfbInitServer(vncscr);

	/* Mark as dirty since we haven't sent any updates at all yet. */
	rfbMarkRectAsModified(vncscr, 0, 0, scrinfo.xres, scrinfo.yres);

	/* Bit shifts */
	varblock.r_offset = scrinfo.red.offset + scrinfo.red.length - 5;
	varblock.g_offset = scrinfo.green.offset + scrinfo.green.length - 5;
	varblock.b_offset = scrinfo.blue.offset + scrinfo.blue.length - 5;
	varblock.pixels_per_int = 8 * sizeof(int) / scrinfo.bits_per_pixel;
}

/*****************************************************************************/
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

static void keyevent(rfbBool down, rfbKeySym key, rfbClientPtr cl)
{
        struct input_event       event;

        if(down) {

                memset(&event, 0, sizeof(event));
                gettimeofday(&event.time, NULL);
                event.type = EV_KEY;
                event.code = keysym2scancode(key); //nomodifiers!
                event.value = 1; //key pressed
                write(kbdfd, &event, sizeof(event));

                memset(&event, 0, sizeof(event));
                gettimeofday(&event.time, NULL);
                event.type = EV_SYN;
                event.code = SYN_REPORT; //not sure what this is for? i'm guessing its some kind of sync thing?
                event.value = 0;
                write(kbdfd, &event, sizeof(event));
                        
                        
        } else {
                memset(&event, 0, sizeof(event));
                gettimeofday(&event.time, NULL);
                event.type = EV_KEY;
                event.code = keysym2scancode(key); //nomodifiers!
                event.value = 0; //key realeased
                write(kbdfd, &event, sizeof(event));

                memset(&event, 0, sizeof(event));
                gettimeofday(&event.time, NULL);
                event.type = EV_SYN;
                event.code = SYN_REPORT; //not sure what this is for? i'm guessing its some kind of sync thing?
                event.value = 0;
                write(kbdfd, &event, sizeof(event));

        }
}

static void doptr(int buttonMask, int x, int y, rfbClientPtr cl)
{
        struct input_event       event;

        //printf("mouse: 0x%x at %d,%d\n", buttonMask, x,y);

        memset(&event, 0, sizeof(event));
        gettimeofday(&event.time, NULL);
        if (relative_mode) {
                event.type = EV_REL;
                event.code = REL_X;
                event.value = x - last_x;
        }
        else {
                event.type = EV_ABS;
                event.code = ABS_X;
                event.value = x;
        }
        write(kbdfd, &event, sizeof(event));

        memset(&event, 0, sizeof(event));
        gettimeofday(&event.time, NULL);
        if (relative_mode) {
                event.type = EV_REL;
                event.code = REL_Y;
                event.value = y - last_y;
        }
        else {
                event.type = EV_ABS;
                event.code = ABS_Y;
                event.value = y;
        }
        write(kbdfd, &event, sizeof(event));

        last_x = x;
        last_y = y;

        memset(&event, 0, sizeof(event));
        gettimeofday(&event.time, NULL);
        event.type = EV_SYN;
        event.code = SYN_REPORT;
        event.value = 0;
        write(kbdfd, &event, sizeof(event));
        if (mouse_last != buttonMask) {
                int left_l = mouse_last & 0x1;
                int left_w = buttonMask & 0x1;

                if (left_l != left_w) {
                        memset(&event, 0, sizeof(event));
                        gettimeofday(&event.time, NULL);
                        event.type = EV_KEY;
                        event.code = BTN_LEFT;
                        event.value = left_w;
                        write(kbdfd, &event, sizeof(event));

                        memset(&event, 0, sizeof(event));
                        gettimeofday(&event.time, NULL);
                        event.type = EV_SYN;
                        event.code = SYN_REPORT;
                        event.value = 0;
                        write(kbdfd, &event, sizeof(event));
                }

                int middle_l = mouse_last & 0x2;
                int middle_w = buttonMask & 0x2;

                if (middle_l != middle_w) {
                        memset(&event, 0, sizeof(event));
                        gettimeofday(&event.time, NULL);
                        event.type = EV_KEY;
                        event.code = BTN_MIDDLE;
                        event.value = middle_w >> 1;
                        write(kbdfd, &event, sizeof(event));

                        memset(&event, 0, sizeof(event));
                        gettimeofday(&event.time, NULL);
                        event.type = EV_SYN;
                        event.code = SYN_REPORT;
                        event.value = 0;
                        write(kbdfd, &event, sizeof(event));
                }
                int right_l = mouse_last & 0x4;
                int right_w = buttonMask & 0x4;

                if (right_l != right_w) {
                        memset(&event, 0, sizeof(event));
                        gettimeofday(&event.time, NULL);
                        event.type = EV_KEY;
                        event.code = BTN_RIGHT;
                        event.value = right_w >> 2;
                        write(kbdfd, &event, sizeof(event));

                        memset(&event, 0, sizeof(event));
                        gettimeofday(&event.time, NULL);
                        event.type = EV_SYN;
                        event.code = SYN_REPORT;
                        event.value = 0;
                        write(kbdfd, &event, sizeof(event));
                }

                mouse_last = buttonMask;
        }
}

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

static int get_framebuffer_yoffset()
{
    if(ioctl(fbfd, FBIOGET_VSCREENINFO, &scrinfo) < 0) {
        pr_err("failed to get virtual screen info\n");
        return -1;
    }

    return scrinfo.yoffset;
}

#define PIXEL_FB_TO_RFB(p,r,g,b) \
	((p >> r) & 0x1f001f) | \
	(((p >> g) & 0x1f001f) << 5) | \
	(((p >> b) & 0x1f001f) << 10)

static void update_screen(void)
{
	unsigned int *f, *c, *r;
	int x, y, y_virtual;

	/* get virtual screen info */
	y_virtual = get_framebuffer_yoffset();
	if (y_virtual < 0)
		y_virtual = 0; /* no info, have to assume front buffer */

	varblock.min_x = varblock.min_y = INT_MAX;
	varblock.max_x = varblock.max_y = -1;

	f = (unsigned int *)fbmmap;        /* -> framebuffer         */
	c = (unsigned int *)fbbuf;         /* -> compare framebuffer */
	r = (unsigned int *)vncbuf;        /* -> remote framebuffer  */

	/* jump to right virtual screen */
	f += y_virtual * scrinfo.xres / varblock.pixels_per_int;

	for (y = 0; y < (int) scrinfo.yres; y++) {
		/* Compare every 2 pixels at a time, assuming that changes are
		 * likely in pairs. */
		for (x = 0; x < (int) scrinfo.xres; x += varblock.pixels_per_int) {
			unsigned int pixel = *f;

			if (pixel != *c) {
				*c = pixel; /* update compare buffer */

				/* XXX: Undo the checkered pattern to test the
				 * efficiency gain using hextile encoding. */
				if (pixel == 0x18e320e4 || pixel == 0x20e418e3)
					pixel = 0x18e318e3; /* still needed? */

				/* update remote buffer */
				*r = PIXEL_FB_TO_RFB(pixel,
						varblock.r_offset,
						varblock.g_offset,
						varblock.b_offset);

				if (x < varblock.min_x)
					varblock.min_x = x;
				else {
					if (x > varblock.max_x)
						varblock.max_x = x;
				}

				if (y < varblock.min_y)
					varblock.min_y = y;
				else if (y > varblock.max_y)
					varblock.max_y = y;
			}

			f++, c++;
			r++;
		}
	}

	if (varblock.min_x < INT_MAX) {
		if (varblock.max_x < 0)
			varblock.max_x = varblock.min_x;

		if (varblock.max_y < 0)
			varblock.max_y = varblock.min_y;

		pr_vdebug("Changed frame: %dx%d @ (%d,%d)...\n",
		  (varblock.max_x + 2) - varblock.min_x,
		  (varblock.max_y + 1) - varblock.min_y,
		  varblock.min_x, varblock.min_y);

		rfbMarkRectAsModified(vncscr, varblock.min_x, varblock.min_y,
		  varblock.max_x + 2, varblock.max_y + 1);

		rfbProcessEvents(vncscr, 10000); /* update quickly */
	}
}

void blank_framebuffer()
{
	int i, n = scrinfo.xres * scrinfo.yres / varblock.pixels_per_int;
	for (i = 0; i < n; i++) {
		((int *)vncbuf)[i] = 0;
		((int *)fbbuf)[i] = 0;
	}
}

/*****************************************************************************/

void print_usage(char **argv)
{
	pr_info("%s [-k device] [-t device] [-h]\n"
		"-m number: FB multibuffer count\n",
		"-h : print this help\n",
		APPNAME);
}

void exit_cleanup(void)
{
	pr_info("Cleaning up...\n");
	cleanup_fb();
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
	if(argc > 1)
	{
		int i=1;
		while(i < argc)
		{
			if(*argv[i] == '-')
			{
				switch(*(argv[i] + 1))
				{
				case 'h':
					print_usage(argv);
					exit(0);
					break;
				case 'm':
					i++;
					buffers = atoi(argv[i]);
					break;
				}
			}
			i++;
		}
	}

	pr_info("Initializing framebuffer device " FB_DEVICE "...\n");
	init_fb();

        init_uinput();

	pr_info("Initializing Framebuffer VNC server:\n");
	pr_info("	width:  %d\n", (int)scrinfo.xres);
	pr_info("	height: %d\n", (int)scrinfo.yres);
	pr_info("	bpp:    %d\n", (int)scrinfo.bits_per_pixel);
	pr_info("	port:   %d\n", (int)VNC_PORT);
	init_fb_server(argc, argv);

	atexit(exit_cleanup);
	old_sigint_handler = signal(SIGINT, sigint_handler);

	/* Implement our own event loop to detect changes in the framebuffer. */
	while (1) {
		rfbClientPtr client_ptr;
		while (!vncscr->clientHead) {
			/* sleep until getting a client */
			rfbProcessEvents(vncscr, LONG_MAX);
		}

		/* refresh screen every 100 ms */
		rfbProcessEvents(vncscr, 200 * 500 /* timeout in us */);

		/* all clients closed */
		if (!vncscr->clientHead) {
			blank_framebuffer(vncbuf);
		}

		/* scan screen if at least one client has requested */
		for (client_ptr = vncscr->clientHead; client_ptr; client_ptr = client_ptr->next)
		{
			if (!sraRgnEmpty(client_ptr->requestedRegion)) {
				update_screen();
				break;
			}
		}
	}

	return 0;
}
