/**************************************************************************

xserver-xorg-input-gesture

Copyright (c) 2011 Samsung Electronics Co., Ltd All Rights Reserved

Contact: Sung-Jin Park <sj76.park@samsung.com>
         Sangjin LEE <lsj119@samsung.com>

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the
"Software"), to deal in the Software without restriction, including
without limitation the rights to use, copy, modify, merge, publish,
distribute, sub license, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to
the following conditions:

The above copyright notice and this permission notice (including the
next paragraph) shall be included in all copies or substantial portions
of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
IN NO EVENT SHALL PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR
ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <linux/input.h>
#include <linux/types.h>

#include <xf86_OSproc.h>

#include <unistd.h>

#include <xf86.h>
#include <xf86Xinput.h>
#include <exevents.h>
#include <xorgVersion.h>
#include <xkbsrv.h>

#ifdef HAVE_PROPERTIES
#include <X11/Xatom.h>
#include <xserver-properties.h>
/* 1.6 has properties, but no labels */
#ifdef AXIS_LABEL_PROP
#define HAVE_LABELS
#else
#undef HAVE_LABELS
#endif

#endif

#include <stdio.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <fcntl.h>
#include <xorg-server.h>
#include <xorgVersion.h>
#include <xf86Module.h>
#include <X11/Xatom.h>
#include "gesture.h"
#include <xorg/mi.h>

char *strcasestr(const char *s, const char *find);
static void printk(const char* fmt, ...) __attribute__((format(printf, 1, 0)));

//Basic functions
static int GesturePreInit(InputDriverPtr  drv, InputInfoPtr pInfo, int flags);
static void GestureUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags);
static pointer GesturePlug(pointer module, pointer options, int *errmaj, int  *errmin);
static void GestureUnplug(pointer p);
static int GestureControl(DeviceIntPtr    device,int what);
static int GestureInit(DeviceIntPtr device);
static void GestureFini(DeviceIntPtr device);
static void GestureReadInput(InputInfoPtr pInfo);

//other initializers
ErrorStatus GestureRegionsInit(void);

//event queue handling functions
ErrorStatus GestureInitEQ(void);
ErrorStatus GestureFiniEQ(void);
ErrorStatus GestureEnqueueEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
ErrorStatus GestureEventsFlush(void);
void GestureEventsDrop(void);

//utility functions
ErrorStatus GestureRegionsReinit(void);
void GestureEnable(int enable, Bool prop, DeviceIntPtr dev);
void GestureCbEventsGrabbed(Mask *pGrabMask, GestureGrabEventPtr *pGrabEvent);
void GestureCbEventsSelected(Window win, Mask *pEventMask);
WindowPtr GestureGetEventsWindow(void);

//Enqueued event handlers and enabler/disabler
static ErrorStatus GestureEnableEventHandler(InputInfoPtr pInfo);
static ErrorStatus GestureDisableEventHandler(void);
static CARD32 GestureTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg);
static CARD32 GestureEventTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg);
void GestureHandleMTSyncEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleButtonPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleButtonReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleMotionEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleKeyPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleKeyReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);

void GestureEmulateHWKey(DeviceIntPtr dev, int keycode);

//Gesture recognizer helper
static Bool PointInBorderSize(WindowPtr pWin, int x, int y);
static WindowPtr GestureWindowOnXY(int x, int y);
Bool GestureHasFingerEventMask(int eventType, int num_finger);

//Gesture recognizer and handlers
void GestureRecognize(int type, InternalEvent *ev, DeviceIntPtr device);
void GestureRecognize_GroupTap(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_GroupFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int start_point, int direction);
void GestureRecognize_GroupHold(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureHandleGesture_Flick(int num_of_fingers, int distance, Time duration, int direction);
void GestureHandleGesture_Tap(int num_finger, int tap_repeat, int cx, int cy);
void GestureHandleGesture_Hold(int num_fingers, int cx, int cy, Time holdtime, int kinds);
ErrorStatus GestureFlushOrDrop(void);

#ifdef HAVE_PROPERTIES
//function related property handling
static void GestureInitProperty(DeviceIntPtr dev);
static int GestureSetProperty(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr val, BOOL checkonly);
#endif

static Atom prop_gesture_recognizer_onoff = None;

GestureDevicePtr g_pGesture = NULL;
_X_EXPORT InputDriverRec GESTURE = {
    1,
    "gesture",
    NULL,
    GesturePreInit,
    GestureUnInit,
    NULL,
    0
};

static XF86ModuleVersionInfo GestureVersionRec =
{
    "gesture",
    MODULEVENDORSTRING,
    MODINFOSTRING1,
    MODINFOSTRING2,
    XORG_VERSION_CURRENT,
    PACKAGE_VERSION_MAJOR, PACKAGE_VERSION_MINOR,
    PACKAGE_VERSION_PATCHLEVEL,
    ABI_CLASS_XINPUT,
    ABI_XINPUT_VERSION,
    MOD_CLASS_XINPUT,
    {0, 0, 0, 0}
};

_X_EXPORT XF86ModuleData gestureModuleData =
{
    &GestureVersionRec,
    &GesturePlug,
    &GestureUnplug
};

static void
printk(const char* fmt, ...)
{
	static FILE* fp = NULL;
	static char init = 0;
	va_list argptr;

	if(!init && !fp)
	{
		fp = fopen("/dev/kmsg", "wt");
		init = 1;
	}

	if(!fp) return;

	va_start(argptr, fmt);
	vfprintf(fp, fmt, argptr);
	fflush(fp);
	va_end(argptr);
}

static Bool
PointInBorderSize(WindowPtr pWin, int x, int y)
{
    BoxRec box;
    if( pixman_region_contains_point (&pWin->borderSize, x, y, &box) )
	return TRUE;

    return FALSE;
}

static WindowPtr
GestureWindowOnXY(int x, int y)
{
    WindowPtr pWin;
    BoxRec box;
    SpritePtr pSprite;
    DeviceIntPtr pDev = g_pGesture->master_pointer;

    pSprite = pDev->spriteInfo->sprite;
    pSprite->spriteTraceGood = 1;	/* root window still there */
    pWin = RootWindow(pDev)->firstChild;

    while (pWin)
    {
	if ((pWin->mapped) &&
	    (x >= pWin->drawable.x - wBorderWidth (pWin)) &&
	    (x < pWin->drawable.x + (int)pWin->drawable.width +
	     wBorderWidth(pWin)) &&
	    (y >= pWin->drawable.y - wBorderWidth (pWin)) &&
	    (y < pWin->drawable.y + (int)pWin->drawable.height +
	     wBorderWidth (pWin))
	    /* When a window is shaped, a further check
	     * is made to see if the point is inside
	     * borderSize
	     */
	    && (!wBoundingShape(pWin) || PointInBorderSize(pWin, x, y))
	    && (!wInputShape(pWin) ||
		RegionContainsPoint(wInputShape(pWin),
				    x - pWin->drawable.x,
				    y - pWin->drawable.y, &box))
#ifdef ROOTLESS
    /* In rootless mode windows may be offscreen, even when
     * they're in X's stack. (E.g. if the native window system
     * implements some form of virtual desktop system).
     */
		&& !pWin->rootlessUnhittable
#endif
	    )
	{
	    if (pSprite->spriteTraceGood >= pSprite->spriteTraceSize)
	    {
		pSprite->spriteTraceSize += 10;
		pSprite->spriteTrace = realloc(pSprite->spriteTrace,
		                    pSprite->spriteTraceSize*sizeof(WindowPtr));
	    }
	    pSprite->spriteTrace[pSprite->spriteTraceGood++] = pWin;
	    pWin = pWin->firstChild;
	}
	else
	    pWin = pWin->nextSib;
    }
    return pSprite->spriteTrace[pSprite->spriteTraceGood-1];
}

Bool
GestureHasFingerEventMask(int eventType, int num_finger)
{
	Bool ret = FALSE;
	Mask eventmask = (1L << eventType);

	if( (g_pGesture->grabMask & eventmask) &&
		(g_pGesture->GrabEvents[eventType].pGestureGrabWinInfo[num_finger].window != None) )
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[X11][GestureHasFingerEventMask] TRUE !! Has grabMask\n");
#endif//__DETAIL_DEBUG__
		return TRUE;
	}

	if( g_pGesture->eventMask & eventmask )
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[X11][GestureHasFingerEventMask] TRUE !! Has eventMask\n");
#endif//__DETAIL_DEBUG__
		return TRUE;
	}

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureHasFingerEventMask] FALSE !! eventType=%d, num_finger=%d\n", eventType, num_finger);
#endif//__DETAIL_DEBUG__

	return ret;
}

static CARD32
GestureEventTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	int event_type = *(int *)arg;

	switch( event_type )
	{
		case GestureNotifyHold:
#ifdef __DETAIL_DEBUG__
			ErrorF("[GestureEventTimerHandler] GestureNotifyHold (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupHold(event_type, NULL, NULL, 0, 1);
			break;
		case GestureNotifyTap:
#ifdef __DETAIL_DEBUG__
			ErrorF("[GestureEventTimerHandler] GestureNotifyTap (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupTap(event_type, NULL, NULL, 0, 1);
			break;
		default:
#ifdef __DETAIL_DEBUG__
			ErrorF("[GestureEventTimerHandler] unknown event_type (=%d)\n", event_type);
#endif//__DETAIL_DEBUG__
			if(timer)
				ErrorF("[GestureEventTimerHandler] timer=%x\n", (unsigned int)timer);
	}

	return 0;
}

void
GestureHandleGesture_Tap(int num_finger, int tap_repeat, int cx, int cy)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyTapEvent tev;

	//skip non-tap events and single finger tap
	if( !tap_repeat || num_finger <= 1 )
		return;

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureHandleGesture_Tap] num_finger=%d, tap_repeat=%d, cx=%d, cy=%d\n",
		num_finger, tap_repeat, cx, cy);
#endif//__DETAIL_DEBUG__

	g_pGesture->recognized_gesture |= WTapFilterMask;
	memset(&tev, 0, sizeof(xGestureNotifyTapEvent));
	tev.type = GestureNotifyTap;
	tev.kind = GestureDone;
	tev.num_finger = num_finger;
	tev.tap_repeat = tap_repeat;
	tev.interval = 0;
	tev.cx = cx;
	tev.cy = cy;

	target_win = g_pGesture->GrabEvents[GestureNotifyTap].pGestureGrabWinInfo[num_finger].window;
	target_pWin = g_pGesture->GrabEvents[GestureNotifyTap].pGestureGrabWinInfo[num_finger].pWin;

	if( g_pGesture->grabMask && (target_win != None) )
	{
		tev.window = target_win;
	}
	else
	{
		tev.window = g_pGesture->gestureWin;
	}

	ErrorF("[X11][GestureHandleGesture_Tap] tev.window=0x%x, g_pGesture->grabMask=0x%x\n", (unsigned int)tev.window, (unsigned int)g_pGesture->grabMask);

	GestureSendEvent(target_pWin, GestureNotifyTap, GestureTapMask, (xGestureCommonEvent *)&tev);
}

void
GestureHandleGesture_Flick(int num_of_fingers, int distance, Time duration, int direction)
{
#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureHandleGesture_Flick] num_fingers=%d, distance=%d, duration=%d, direction=%d\n",
		num_of_fingers, distance, duration, direction);
#endif//__DETAIL_DEBUG__
	switch(direction)
	{
		case FLICK_NORTHWARD:
			ErrorF("[X11][GestureHandleGesture_Flick] Flick Down \n");
			GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_down);
			break;

		case FLICK_SOUTHWARD:
			ErrorF("[X11][GestureHandleGesture_Flick] Flick Up \n");
			GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_up);
			break;
		case FLICK_WESTWARD:
			if(g_pGesture->power_pressed == 2)
			{
				ErrorF("[X11][GestureHandleGesture_Flick] Flick Right & power_pressed\n");
				GestureEmulateHWKey(g_pGesture->hwkey_dev, 122);
			}
			break;
		default:
			break;
	}
	g_pGesture->recognized_gesture |= WFlickFilterMask;
}

void GestureHandleGesture_Hold(int num_fingers, int cx, int cy, Time holdtime, int kinds)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyHoldEvent hev;

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureHandleGesture_Hold] num_fingers=%d, cx=%d, cy=%d, holdtime=%d, kinds=%d\n",
				num_fingers, cx, cy, holdtime, kinds);
#endif//__DETAIL_DEBUG__

	g_pGesture->recognized_gesture |= WHoldFilterMask;
	memset(&hev, 0, sizeof(xGestureNotifyHoldEvent));
	hev.type = GestureNotifyHold;
	hev.kind = kinds;
	hev.num_finger = num_fingers;
	hev.holdtime = holdtime;
	hev.cx = cx;
	hev.cy = cy;

	if(g_pGesture->GrabEvents)
	{
		target_win = g_pGesture->GrabEvents[GestureNotifyHold].pGestureGrabWinInfo[num_fingers].window;
		target_pWin = g_pGesture->GrabEvents[GestureNotifyHold].pGestureGrabWinInfo[num_fingers].pWin;
	}
	else
	{
		target_win = None;
		target_pWin = None;
	}

	if( g_pGesture->grabMask && (target_win != None) )
	{
		hev.window = target_win;
	}
	else
	{
		hev.window = g_pGesture->gestureWin;
	}

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureHandleGesture_Hold] hev.window=0x%x, g_pGesture->grabMask=0x%x\n", hev.window, g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyHold, GestureHoldMask, (xGestureCommonEvent *)&hev);
}


void
GestureRecognize_GroupTap(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired)
{
	static int num_pressed = 0;
	static int base_area_size = 0;

	int cx, cy;
	int area_size;

	static int state = 0;
	static int mbits = 0;
	static int base_cx;
	static int base_cy;
	static pixman_box16_t base_box_ext;

	static int tap_repeat = 0;
	static int prev_num_pressed = 0;

	static OsTimerPtr tap_event_timer = NULL;
	static int event_type = GestureNotifyTap;

	if( timer_expired )
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[GroupTap][Timer] state=%d\n", state);
#endif//__DETAIL_DEBUG__

		switch( state )
		{
			case 1://first tap initiation check
				if( num_pressed )
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("[GroupTap][Timer][state=1] Tap time expired !(num_pressed=%d, tap_repeat=%d)\n", tap_repeat, num_pressed, tap_repeat);
#endif//__DETAIL_DEBUG__
					state = 0;
					goto cleanup_tap;
				}
				break;

			case 2:
				if( tap_repeat <= 1 )
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("[GroupTap][Timer][state=2] %d finger SINGLE TAP !(ignored)\n", prev_num_pressed);
#endif//__DETAIL_DEBUG__
					state = 0;
					goto cleanup_tap;
				}

#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupTap][Timer][state=2]  tap_repeat=%d, num_pressed=%d\n", tap_repeat, num_pressed);
#endif//__DETAIL_DEBUG__
				if( GestureHasFingerEventMask(GestureNotifyTap, prev_num_pressed) )
				{
					GestureHandleGesture_Tap(prev_num_pressed, tap_repeat, base_cx, base_cy);
				}
				goto cleanup_tap;
				break;
		}

		return;
	}

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags |= PressFlagTap;

			if( g_pGesture->num_pressed < 2 )
				return;

			if( (!base_area_size || g_pGesture->num_pressed > num_pressed) )
			{
				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
				base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
				base_box_ext.x1 = base_cx-TAP_MOVE_THRESHOLD;
				base_box_ext.y1 = base_cy-TAP_MOVE_THRESHOLD;
				base_box_ext.x2 = base_cx+TAP_MOVE_THRESHOLD;
				base_box_ext.y2 = base_cy+TAP_MOVE_THRESHOLD;
				state = 1;
				TimerCancel(tap_event_timer);
				tap_event_timer = TimerSet(tap_event_timer, 0, g_pGesture->singletap_threshold, GestureEventTimerHandler, (int *)&event_type);
			}
			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			ErrorF("[GroupTap][P][num_pressed=%d] AREA_SIZE(area.extents)=%d, base_cx=%d, base_cy=%d\n", num_pressed, base_area_size, base_cx, base_cy);
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagTap ) )
				break;

			if( num_pressed < 2 )
				return;

			if( num_pressed != g_pGesture->num_pressed )
			{
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupTap][M][cleanup] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				//goto cleanup_tap;
			}

			mbits |= (1 << idx);
			if( mbits == (pow(2, num_pressed)-1) )
			{
				area_size = AREA_SIZE(&g_pGesture->area.extents);
				cx = AREA_CENTER_X(&g_pGesture->area.extents);
				cy = AREA_CENTER_Y(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupTap][M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
				ErrorF("[GroupTap][M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
				ErrorF("[GroupTap][M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
#endif//__DETAIL_DEBUG__

				if( ABS(base_area_size-area_size) >= TAP_AREA_THRESHOLD )
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("[GroupTap][M] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, ABS(base_area_size-area_size));
#endif//__DETAIL_DEBUG__
					goto cleanup_tap;
				}

				if( !INBOX(&base_box_ext, cx, cy) )
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("[GroupTap][M] current center coordinates is not in base coordinates box !\n");
#endif//__DETAIL_DEBUG__
					goto cleanup_tap;
				}
			}
			break;

		case ET_ButtonRelease:
			if( g_pGesture->num_pressed )
				break;

			if( !tap_repeat )
			{
				prev_num_pressed = num_pressed;
			}

			tap_repeat++;

#ifdef __DETAIL_DEBUG__
			ErrorF("[GroupTap][R] tap_repeat=%d, num_pressed=%d, prev_num_pressed=%d\n", tap_repeat, num_pressed, prev_num_pressed);
#endif//__DETAIL_DEBUG__

			if(( num_pressed != prev_num_pressed ) || (!GestureHasFingerEventMask(GestureNotifyTap, num_pressed)) )
			{
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupTap][R] num_pressed(=%d) != prev_num_pressed(=%d) OR %d finger tap event was not grabbed/selected !\n",
					num_pressed, prev_num_pressed, num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_tap;
			}

			if( tap_repeat < MAX_TAP_REPEATS )
			{
				state = 2;
				TimerCancel(tap_event_timer);
				tap_event_timer = TimerSet(tap_event_timer, 0, g_pGesture->doubletap_threshold, GestureEventTimerHandler, (int *)&event_type);
				base_area_size = num_pressed = 0;
				break;
			}

#ifdef __DETAIL_DEBUG__
			ErrorF("[GroupTap][R] %d finger %s\n", num_pressed, (tap_repeat==2) ? "DBL_TAP" : "TRIPLE_TAP");
#endif//__DETAIL_DEBUG__

			if( GestureHasFingerEventMask(GestureNotifyTap, num_pressed) )
				GestureHandleGesture_Tap(num_pressed, tap_repeat, base_cx, base_cy);

			if( tap_repeat >= MAX_TAP_REPEATS )
			{
				goto cleanup_tap;
			}

			prev_num_pressed = num_pressed;
			num_pressed = 0;
			break;
	}

	return;

cleanup_tap:

	if( 0 == state )
		g_pGesture->recognized_gesture &= ~WTapFilterMask;
	g_pGesture->filter_mask |= WTapFilterMask;

	if( g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL )
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[GroupTap][cleanup] GestureFlushOrDrop() !\n");
#endif//__DETAIL_DEBUG__

		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
	}

	num_pressed = 0;
	tap_repeat = 0;
	prev_num_pressed = 0;
	mbits = 0;
	state = 0;
	TimerCancel(tap_event_timer);
	return;
}

void
GestureRecognize_GroupFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int start_point, int direction)
{
	static int num_pressed = 0;
	static Time base_time = 0;
	Time duration;
	int distance;
	static int diff_base_coord = 0;
	static int diff_base_minor_coord = 0;
	static int diff_current_coord = 0;
	static int false_diff_count = 0;
	static int false_minor_diff_count = 0;
	static float angle = 0.0f;
	static int angle_base_x=0, angle_base_y=0;

	if( g_pGesture->num_pressed > 1 )
		goto cleanup_flick;
	if( (start_point <= FLICK_POINT_NONE) || (FLICK_POINT_MAX <= start_point) )
		goto cleanup_flick;

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags = PressFlagFlick;
			base_time = GetTimeInMillis();
			num_pressed = g_pGesture->num_pressed;
			switch(start_point)
			{
				case FLICK_POINT_UP:
					if( g_pGesture->fingers[idx].py > g_pGesture->flick_press_area)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickDown] press coord is out of bound. (%d, %d)\n", g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}

					angle_base_y = diff_base_coord = diff_current_coord = g_pGesture->fingers[idx].py;
					angle_base_x = g_pGesture->fingers[idx].px;
					break;

				case FLICK_POINT_DOWN:
					if( g_pGesture->fingers[idx].py < g_pGesture->screen_height - g_pGesture->flick_press_area)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickUp] press coord is out of bound. (%d, %d)\n", g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}
					angle_base_y = diff_base_coord = diff_current_coord = g_pGesture->fingers[idx].py;
					angle_base_x = g_pGesture->fingers[idx].px;
					break;

				case FLICK_POINT_LEFT:
					if( g_pGesture->fingers[idx].px > g_pGesture->flick_press_area)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickLeft] press coord is out of bound. (%d, %d)\n", g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}
					angle_base_y = diff_base_coord = diff_current_coord = g_pGesture->fingers[idx].py;
					angle_base_x = g_pGesture->fingers[idx].px;
					break;

				default:
					goto cleanup_flick;
					break;
			}

			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagFlick ) )
				break;

			switch(start_point)
			{
				case FLICK_POINT_UP:
					diff_base_coord = diff_current_coord;
					diff_current_coord = g_pGesture->fingers[idx].my;

					if( (diff_current_coord - diff_base_coord) < 0 )
						false_diff_count++;
					if (false_diff_count > FLICK_FALSE_Y_DIFF_COUNT)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickDown][M][F] false_diff_count: %d > %d\n", false_diff_count, FLICK_FALSE_Y_DIFF_COUNT);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}

					if ((g_pGesture->fingers[idx].my < g_pGesture->flick_press_area) &&
						(abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px) >(int)( g_pGesture->screen_width/2)) )
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickDown][M][F] move x: %d - %d, y coord: %d\n", g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].my);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}

					if( (g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py) > g_pGesture->flick_minimum_height)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickDown][M] %d - %d < %d(min_size), angle_base_coord (%d, %d)\n", g_pGesture->fingers[idx].my, g_pGesture->fingers[idx].py, g_pGesture->flick_minimum_height, angle_base_x, angle_base_y);
#endif//__DETAIL_DEBUG__
						if(abs(g_pGesture->fingers[idx].mx - angle_base_x) == 0)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickDown][M] abs(%d - %d) = 0\n", g_pGesture->fingers[idx].mx, angle_base_x);
#endif//__DETAIL_DEBUG__
							angle = 1.0f;
						}
						else
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickDown][M] angle_base_x: %d, angle_base_y: %d\n", angle_base_x, angle_base_y);
#endif//__DETAIL_DEBUG__

							int y_diff = abs(g_pGesture->fingers[idx].my - angle_base_y);
							int x_diff = abs(g_pGesture->fingers[idx].mx - angle_base_x);
							angle = (float)y_diff / (float)x_diff;
						}

						if ( angle < 0.27f)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickDown][M][F] %d / %d = %f (angle)\n", abs(g_pGesture->fingers[idx].my - angle_base_y), abs(g_pGesture->fingers[idx].mx - angle_base_x), angle);
#endif//__DETAIL_DEBUG__
							goto cleanup_flick;
						}

						distance = g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py;
						duration = GetTimeInMillis() - base_time;

						GestureHandleGesture_Flick(num_pressed, distance, duration, direction);
						goto cleanup_flick_recognized;
					}
					else
					{
						if( (g_pGesture->fingers[idx].mx - diff_base_minor_coord) < 0 )
							false_minor_diff_count++;
						if (false_minor_diff_count> FLICK_FALSE_X_DIFF_COUNT)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickDown][M][F] false_minor_diff_count: %d > %d\n", false_minor_diff_count, FLICK_FALSE_X_DIFF_COUNT);
#endif//__DETAIL_DEBUG__
							goto cleanup_flick;
						}
					}

					if (g_pGesture->fingers[idx].my < g_pGesture->flick_press_area)
					{
						angle_base_x = g_pGesture->fingers[idx].px;
						angle_base_y = g_pGesture->fingers[idx].py;
					}

					break;
				case FLICK_POINT_DOWN:
					diff_base_coord = diff_current_coord;
					diff_current_coord = g_pGesture->fingers[idx].my;

					if( (diff_base_coord - diff_current_coord) < 0 )
						false_diff_count++;
					if (false_diff_count > FLICK_FALSE_Y_DIFF_COUNT)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickUp][M] false_diff_count: %d > %d\n", false_diff_count, FLICK_FALSE_Y_DIFF_COUNT);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}

					if( (g_pGesture->fingers[idx].py - g_pGesture->fingers[idx].my) > g_pGesture->flick_minimum_height)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickUp][R] %d - %d < %d(min_size)\n", g_pGesture->fingers[idx].my, g_pGesture->fingers[idx].py, g_pGesture->flick_minimum_height);
#endif//__DETAIL_DEBUG__
						if(abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px) == 0)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickUp][R] abs(%d - %d) = 0\n", g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].px);
#endif//__DETAIL_DEBUG__
							angle = 1.0f;
						}
						else
						{
							int y_diff = abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py);
							int x_diff = abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px);
							angle = (float)y_diff / (float)x_diff;
						}

						if ( angle <0.5f)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickUp][R] %d / %d = %f (angle)\n", abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py), abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px), angle);
#endif//__DETAIL_DEBUG__
							goto cleanup_flick;
						}

						distance = g_pGesture->fingers[idx].py - g_pGesture->fingers[idx].my;
						duration = GetTimeInMillis() - base_time;

						GestureHandleGesture_Flick(num_pressed, distance, duration, direction);
						goto cleanup_flick_recognized;
					}
					break;
				case FLICK_POINT_LEFT:
					diff_base_coord = diff_current_coord;
					diff_current_coord = g_pGesture->fingers[idx].mx;

					if( (diff_current_coord - diff_base_coord) < 0 )
						false_diff_count++;
					if (false_diff_count > FLICK_FALSE_Y_DIFF_COUNT)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickLeft][M] false_diff_count: %d > %d\n", false_diff_count, FLICK_FALSE_Y_DIFF_COUNT);
#endif//__DETAIL_DEBUG__
						goto cleanup_flick;
					}

					if( (g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px) > g_pGesture->flick_minimum_height)
					{
#ifdef __DETAIL_DEBUG__
						ErrorF("[FlickLeft][M] %d - %d < %d(min_size)\n", g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].px, g_pGesture->flick_minimum_height);
#endif//__DETAIL_DEBUG__
						if(abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py) == 0)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickLeft][M] abs(%d - %d) = 0\n", g_pGesture->fingers[idx].my, g_pGesture->fingers[idx].py);
#endif//__DETAIL_DEBUG__
							angle = 1.0f;
						}
						else
						{
							int y_diff = abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py);
							int x_diff = abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px);
							angle = (float)x_diff / (float)y_diff;
						}

						if ( angle < 0.5f)
						{
#ifdef __DETAIL_DEBUG__
							ErrorF("[FlickLeft][M] %d / %d = %f (angle)\n", abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px), abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py), angle);
#endif//__DETAIL_DEBUG__
							goto cleanup_flick;
						}

						distance = g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px;
						duration = GetTimeInMillis() - base_time;

						GestureHandleGesture_Flick(num_pressed, distance, duration, direction);
						goto cleanup_flick_recognized;
					}

					break;
				default:
					goto cleanup_flick;
					break;
			}
			break;

		case ET_ButtonRelease:
			goto cleanup_flick;
			break;
	}

	return;

cleanup_flick:
	ErrorF("[Flick][R] clenup_flick\n");
	g_pGesture->recognized_gesture &= ~WFlickFilterMask;

cleanup_flick_recognized:
	ErrorF("[Flick][R] cleanup_flick_recognized\n");
	g_pGesture->filter_mask |= WFlickFilterMask;
	num_pressed = 0;
	base_time = 0;
	false_diff_count = 0;
	diff_base_coord = 0;
	diff_current_coord = 0;
	angle = 0.0f;
	angle_base_x = angle_base_y = 0;
	return;
}

void GestureRecognize_GroupHold(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired)
{
	static int num_pressed = 0;
	static int base_area_size = 0;
	static Time base_time = 0;
	static int base_cx;
	static int base_cy;
	int cx, cy;
	static pixman_box16_t base_box_ext;
	int area_size;
	static int state = GestureEnd;

	static OsTimerPtr hold_event_timer = NULL;
	static int event_type = GestureNotifyHold;

	if( timer_expired )
	{
		if( state <= GestureBegin )
			state++;

#ifdef __DETAIL_DEBUG__
		switch( state )
		{
			case GestureBegin:
				ErrorF("[GroupHold] HOLD Begin !\n");
				break;

			case GestureUpdate:
				ErrorF("[GroupHold] HOLD Update !\n");
				break;
		}
#endif//__DETAIL_DEBUG__

		if( GestureHasFingerEventMask(GestureNotifyHold, num_pressed) )
		{
			GestureHandleGesture_Hold(num_pressed, base_cx, base_cy, GetTimeInMillis()-base_time, state);
			hold_event_timer = TimerSet(hold_event_timer, 0, g_pGesture->hold_time_threshold, GestureEventTimerHandler, (int *)&event_type);
		}
		return;
	}

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags |= PressFlagHold;

			if( g_pGesture->num_pressed < 2 )
				return;

			if( !base_area_size || g_pGesture->num_pressed > num_pressed )
			{
				if( state != GestureEnd )
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("[GroupHold][P][cleanup] No num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_hold;
				}

				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
				base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
				base_time = GetTimeInMillis();
				base_box_ext.x1 = base_cx-g_pGesture->hold_move_threshold;
				base_box_ext.y1 = base_cy-g_pGesture->hold_move_threshold;
				base_box_ext.x2 = base_cx+g_pGesture->hold_move_threshold;
				base_box_ext.y2 = base_cy+g_pGesture->hold_move_threshold;
				event_type = GestureNotifyHold;
				hold_event_timer = TimerSet(hold_event_timer, 0, g_pGesture->hold_time_threshold, GestureEventTimerHandler, (int *)&event_type);
			}
			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			ErrorF("[GroupHold][P]][num_pressed=%d] AREA_SIZE(area.extents)=%d, base_cx=%d, base_cy=%d\n", num_pressed, base_area_size, base_cx, base_cy);
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagHold ) )
				break;

			if( num_pressed < 2 )
				return;

			if( num_pressed != g_pGesture->num_pressed )
			{
				if( state != GestureEnd )
				{
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupHold][M][cleanup] No num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_hold;
				}
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupHold][M][cleanup] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				//goto cleanup_hold;
			}

			area_size = AREA_SIZE(&g_pGesture->area.extents);
			cx = AREA_CENTER_X(&g_pGesture->area.extents);
			cy = AREA_CENTER_Y(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
			ErrorF("[GroupHold][M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
			ErrorF("[GroupHold][M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
			ErrorF("[GroupHold][M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
#endif//__DETAIL_DEBUG__

			if(area_size > 0 && base_area_size > 0)
			{
				if( ((area_size > base_area_size) ? (double)area_size / (double)base_area_size : (double)base_area_size / (double) area_size) >= g_pGesture->hold_area_threshold)
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("[GroupHold][M] No diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%lf)!\n", area_size, base_area_size, ((area_size > base_area_size) ? (double)area_size / (double)base_area_size : (double)base_area_size / (double) area_size));
#endif//__DETAIL_DEBUG__
					goto cleanup_hold;
				}
			}

			if( !INBOX(&base_box_ext, cx, cy) )
			{
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupHold][M] No current center coordinates is not in base coordinates box !\n");
				ErrorF("[GroupHold][M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
				ErrorF("[GroupHold][M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
#endif//__DETAIL_DEBUG__
				goto cleanup_hold;
			}
			break;

		case ET_ButtonRelease:
			if( state != GestureEnd && num_pressed >= 2)
			{
#ifdef __DETAIL_DEBUG__
				ErrorF("[GroupHold][R][cleanup] No num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_hold;
			}

			//ErrorF("[GroupHold][R] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
			if( g_pGesture->num_pressed )
				break;

			goto cleanup_hold;
			break;
	}

	return;

cleanup_hold:

	if( state == GestureBegin || state == GestureUpdate )
	{
		state = GestureEnd;
		if( GestureHasFingerEventMask(GestureNotifyHold, num_pressed) )
		{
			GestureHandleGesture_Hold(num_pressed, base_cx, base_cy, GetTimeInMillis()-base_time, state);
		}
	}
	else
	{
		g_pGesture->recognized_gesture &= ~WHoldFilterMask;
	}

	g_pGesture->filter_mask |= WHoldFilterMask;
	num_pressed = 0;
	base_area_size = 0;
	base_time = 0;
	base_cx = base_cy = 0;
	state = GestureEnd;
	base_box_ext.x1 = base_box_ext.x2 = base_box_ext.y1 = base_box_ext.y2 = 0;
	TimerCancel(hold_event_timer);
	return;
}

static inline void
GestureEnableDisable()
{
	GestureEnable(1, FALSE, g_pGesture->this_device);
#if 0
	if((g_pGesture->grabMask) || (g_pGesture->lastSelectedWin != None))
	{
		GestureEnable(1, FALSE, g_pGesture->this_device);
	}
	else
	{
		GestureEnable(0, FALSE, g_pGesture->this_device);
	}
#endif
}

void
GestureCbEventsGrabbed(Mask *pGrabMask, GestureGrabEventPtr *pGrabEvent)
{
	g_pGesture->grabMask = *pGrabMask;
	g_pGesture->GrabEvents = (GestureGrabEventPtr)pGrabEvent;
	GestureEnableDisable();
}

void
GestureCbEventsSelected(Window win, Mask *pEventMask)
{
	g_pGesture->lastSelectedWin = win;
	g_pGesture->lastSelectedMask = (pEventMask) ? *pEventMask : 0;
	GestureEnableDisable();
}

WindowPtr
GestureGetEventsWindow(void)
{
	Mask mask;
	WindowPtr pWin;

	pWin = GestureWindowOnXY(g_pGesture->fingers[0].px, g_pGesture->fingers[0].py);

	if( pWin )
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[X11][GestureGetEventsWindow] pWin->drawable.id=0x%x\n", pWin->drawable.id);
#endif//__DETAIL_DEBUG__
		g_pGesture->gestureWin = pWin->drawable.id;
	}
	else
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[X11][GestureGetEventsWindow] GestureWindowOnXY returns NULL !\n");
#endif//__DETAIL_DEBUG__
		return NULL;
	}

	if(g_pGesture->gestureWin == g_pGesture->lastSelectedWin)
	{
		g_pGesture->eventMask = g_pGesture->lastSelectedMask;
		goto nonempty_eventmask;
	}

	//check selected event(s)
	if( !GestureHasSelectedEvents(pWin, &g_pGesture->eventMask) )
	{
		g_pGesture->eventMask = 0;
	}
	else
	{
		g_pGesture->lastSelectedWin = g_pGesture->gestureWin;
		g_pGesture->lastSelectedMask = g_pGesture->eventMask;
	}

	if( !g_pGesture->eventMask && !g_pGesture->grabMask)
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[X11][GestureGetEventsWindow] No grabbed events or no events were selected for window(0x%x) !\n", pWin->drawable.id);
#endif//__DETAIL_DEBUG__
		return NULL;
	}

nonempty_eventmask:

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureGetEventsWindow] g_pGesture->eventMask=0x%x\n", g_pGesture->eventMask);
#endif//__DETAIL_DEBUG__

	mask = (GESTURE_FILTER_MASK_ALL & ~(g_pGesture->grabMask | g_pGesture->eventMask));

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureGetEventsWindow] g_pGesture->filter_mask=0x%x, mask=0x%x\n", g_pGesture->filter_mask, mask);
#endif//__DETAIL_DEBUG__

	g_pGesture->filter_mask = mask;

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureGetEventsWindow] g_pGesture->filter_mask=0x%x\n", g_pGesture->filter_mask);
#endif//__DETAIL_DEBUG__

	return pWin;
}

static CARD32
GestureSingleFingerTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	g_pGesture->filter_mask |= WTapFilterMask;
	g_pGesture->filter_mask |= WHoldFilterMask;

	if( (g_pGesture->event_sum[0] == BTN_PRESSED) && ( (g_pGesture->flick_pressed_point <= FLICK_POINT_NONE) && (FLICK_POINT_MAX <= g_pGesture->flick_pressed_point) ) )
	{
#ifdef __DETAIL_DEBUG__
		ErrorF("[SingleFingerTimer] press_point: %d\n", g_pGesture->flick_pressed_point);
#endif//__DETAIL_DEBUG_
		g_pGesture->filter_mask |= WFlickFilterMask;
	}

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][Single] expired !\n");
#endif//__DETAIL_DEBUG__

	if(g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL)
	{
		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
	}

	return 0;
}

void
GestureRecognize(int type, InternalEvent *ev, DeviceIntPtr device)
{
	int i;
	static OsTimerPtr single_finger_timer = NULL;
	int idx = -1;

	if( PROPAGATE_EVENTS == g_pGesture->ehtype ||
		device->id < g_pGesture->first_fingerid )
		return;

	for( i = 0 ; i < g_pGesture->num_mt_devices ; i++ )
	{
		if( device->id == g_pGesture->mt_devices[i]->id )
		{
			idx = i;
			break;
		}
	}

	if( idx < 0 )
		return;

	switch( type )
	{
		case ET_ButtonPress:
			if( idx == 0 )
				g_pGesture->event_sum[0] = BTN_PRESSED;
			g_pGesture->fingers[idx].ptime = ev->any.time;
			g_pGesture->fingers[idx].px = ev->device_event.root_x;
			g_pGesture->fingers[idx].py = ev->device_event.root_y;

			g_pGesture->finger_rects[idx].extents.x1 = ev->device_event.root_x - FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.x2 = ev->device_event.root_x + FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.y1 =  ev->device_event.root_y - FINGER_HEIGHT;
			g_pGesture->finger_rects[idx].extents.y2 =  ev->device_event.root_y + FINGER_HEIGHT;

			g_pGesture->num_pressed++;
			g_pGesture->inc_num_pressed = g_pGesture->num_pressed;

			if( g_pGesture->inc_num_pressed == 1 )
			{
				pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
#ifdef __DETAIL_DEBUG__
				ErrorF("[P][g_pGesture->inc_num_pressed=1] AREA_SIZE(area.extents)=%d\n", AREA_SIZE(&g_pGesture->area.extents));
#endif//__DETAIL_DEBUG__
			}
			else
			{
				pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
				for( i = 1 ; i < g_pGesture->inc_num_pressed ; i++ )
				{
					pixman_region_union(&g_pGesture->area, &g_pGesture->area, &g_pGesture->finger_rects[i]);
				}
#ifdef __DETAIL_DEBUG__
				ErrorF("[P][g_pGesture->inc_num_pressed=%d] AREA_SIZE(area.extents)=%d\n", g_pGesture->inc_num_pressed, AREA_SIZE(&g_pGesture->area.extents));
#endif//__DETAIL_DEBUG__
			}
#ifdef __DETAIL_DEBUG__
			ErrorF("[X11][GestureRecognize][P] g_pGesture->finger_rects\n");
#endif//__DETAIL_DEBUG__
			if(g_pGesture->num_pressed == 1)
			{
				single_finger_timer = TimerSet(single_finger_timer, 0, 50, GestureSingleFingerTimerHandler, NULL);

				if( g_pGesture->fingers[idx].py <= g_pGesture->flick_press_area)
				{
					if((!g_pGesture->activate_flick_down)
						|| (g_pGesture->fingers[idx].px <= (g_pGesture->flick_press_area_left_right))
						|| (g_pGesture->fingers[idx].px >= (g_pGesture->screen_width - g_pGesture->flick_press_area_left_right)) )
						g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
					else
						g_pGesture->flick_pressed_point = FLICK_POINT_UP;
				}
				else if(g_pGesture->fingers[idx].py >= (g_pGesture->screen_height - g_pGesture->flick_press_area))
				{
					if(!g_pGesture->activate_flick_up)
						g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
					else
						g_pGesture->flick_pressed_point = FLICK_POINT_DOWN;
				}
				else if( g_pGesture->fingers[idx].px <= g_pGesture->flick_press_area_left)
				{
					if(!g_pGesture->activate_flick_right)
						g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
					else
						g_pGesture->flick_pressed_point = FLICK_POINT_LEFT;
				}
#ifdef __DETAIL_DEBUG__
				ErrorF("[GestureRecognize] flick_press_point: %d\n", g_pGesture->flick_pressed_point);
#endif//__DETAIL_DEBUG__

				if( (g_pGesture->flick_pressed_point <= FLICK_POINT_NONE) || (FLICK_POINT_MAX <= g_pGesture->flick_pressed_point) )
				{
					g_pGesture->filter_mask |= WFlickFilterMask;
					g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
				}
				else
				{
					g_pGesture->flick_direction = ( g_pGesture->flick_pressed_point - 1 ) * 2;
					if( (g_pGesture->flick_direction == FLICK_WESTWARD) && (g_pGesture->power_pressed != 2) )
					{
						ErrorF("[GestureRecognize] Flick WesWard is disable when power is not pressed\n");
						g_pGesture->filter_mask |= WFlickFilterMask;
						g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
					}
					if( (g_pGesture->flick_direction < FLICK_NORTHWARD) || (FLICK_NORTHWESTWARD < g_pGesture->flick_direction) )
					{
						ErrorF("[GestureRecognize] Invalid flick direction\n");
						g_pGesture->filter_mask |= WFlickFilterMask;
						g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
					}
#ifdef __DETAIL_DEBUG__
					ErrorF("[GestureRecognize] flick_direction: %d\n", g_pGesture->flick_direction);
#endif//__DETAIL_DEBUG__
				}
			}
			else
			{
				TimerCancel(single_finger_timer);
				single_finger_timer = NULL;
			}
			break;

		case ET_Motion:
			if( !g_pGesture->fingers[idx].ptime )
				return;

			g_pGesture->fingers[idx].mx = ev->device_event.root_x;
			g_pGesture->fingers[idx].my = ev->device_event.root_y;

			if( (g_pGesture->inc_num_pressed < 2) && (idx == 0) && (g_pGesture->event_sum[0] == BTN_PRESSED) )
			{
				g_pGesture->event_sum[0] += BTN_MOVING;
#ifdef __DETAIL_DEBUG__
				ErrorF("no seconds finger comming\n");
#endif//__DETAIL_DEBUG__
				if(g_pGesture->event_sum[0] >= 7)
				{
#ifdef __DETAIL_DEBUG__
					ErrorF("Moving Limit\n");
#endif//__DETAIL_DEBUG__
					g_pGesture->filter_mask |= WTapFilterMask;
					g_pGesture->filter_mask |= WHoldFilterMask;
				}
				if(g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL)
					goto flush_or_drop;
			}

			g_pGesture->finger_rects[idx].extents.x1 = ev->device_event.root_x - FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.x2 = ev->device_event.root_x + FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.y1 =  ev->device_event.root_y - FINGER_HEIGHT;
			g_pGesture->finger_rects[idx].extents.y2 =  ev->device_event.root_y + FINGER_HEIGHT;

			if( g_pGesture->inc_num_pressed == 1 )
			{
				pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
#ifdef __DETAIL_DEBUG__
				ErrorF("[M][g_pGesture->inc_num_pressed=1] AREA_SIZE(area)=%d\n", AREA_SIZE(&g_pGesture->area.extents));
#endif//__DETAIL_DEBUG__
			}
			else
			{
				pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
				for( i = 1 ; i < g_pGesture->inc_num_pressed ; i++ )
				{
					pixman_region_union(&g_pGesture->area, &g_pGesture->area, &g_pGesture->finger_rects[i]);
				}
#ifdef __DETAIL_DEBUG__
				ErrorF("[M][g_pGesture->inc_num_pressed=%d] AREA_SIZE(area)=%d\n", g_pGesture->inc_num_pressed, AREA_SIZE(&g_pGesture->area.extents));
#endif//__DETAIL_DEBUG__
			}
			break;

		case ET_ButtonRelease:
			g_pGesture->fingers[idx].rtime = ev->any.time;
			g_pGesture->fingers[idx].rx = ev->device_event.root_x;
			g_pGesture->fingers[idx].ry = ev->device_event.root_y;

			g_pGesture->num_pressed--;
			if( g_pGesture->num_pressed <= 0 )
			{
#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureRecognize] All fingers were released !\n");
#endif//__DETAIL_DEBUG__
			}
			break;
	}
	if( g_pGesture->filter_mask != GESTURE_WATCH_FILTER_MASK_ALL )
	{
		if( !(g_pGesture->filter_mask & WFlickFilterMask) )
		{
			GestureRecognize_GroupFlick(type, ev, device, idx, g_pGesture->flick_pressed_point, g_pGesture->flick_direction);
		}
		if( !(g_pGesture->filter_mask & WTapFilterMask) )
		{
			GestureRecognize_GroupTap(type, ev, device, idx, 0);
		}
		if( !(g_pGesture->filter_mask & WHoldFilterMask) )
		{
			GestureRecognize_GroupHold(type, ev, device, idx, 0);
		}
	}

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureRecognize][N] g_pGesture->filter_mask = 0x%x\n", g_pGesture->filter_mask);
	ErrorF("[X11][GestureRecognize][N] g_pGesture->GESTURE_WATCH_FILTER_MASK_ALL = 0x%x\n", GESTURE_WATCH_FILTER_MASK_ALL);
	ErrorF("[X11][GestureRecognize][N] g_pGesture->recognized_gesture=0x%x\n", g_pGesture->recognized_gesture);
#endif//__DETAIL_DEBUG__

	if( g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL )
	{
		if( !g_pGesture->recognized_gesture )
			goto flush_or_drop;
		else if( !g_pGesture->num_pressed )
			goto flush_or_drop;
 	}

	if( g_pGesture->recognized_gesture )
	{
		if( g_pGesture->ehtype == KEEP_EVENTS )
			GestureEventsDrop();
		g_pGesture->ehtype = IGNORE_EVENTS;
	}
	return;

flush_or_drop:
#ifdef __DETAIL_DEBUG__
	ErrorF("[GestureRecognize] GestureFlushOrDrop() !\n");
#endif//__DETAIL_DEBUG__
	if( ERROR_INVALPTR == GestureFlushOrDrop() )
	{
		GestureControl(g_pGesture->this_device, DEVICE_OFF);
	}
}


ErrorStatus GestureFlushOrDrop(void)
{
	ErrorStatus err;

	if( g_pGesture->recognized_gesture )
	{
		g_pGesture->ehtype = IGNORE_EVENTS;
		GestureEventsDrop();
	}
	else
	{
		g_pGesture->ehtype = PROPAGATE_EVENTS;

		err = GestureEventsFlush();
		if( ERROR_NONE != err )
			return err;

#ifdef __DETAIL_DEBUG__
		ErrorF("[X11][GestureFlushOrDrop][F] g_pGesture->filter_mask = 0x%x\n", g_pGesture->filter_mask);
		ErrorF("[X11][GestureFlushOrDrop][F] g_pGesture->GESTURE_WATCH_FILTER_MASK_ALL = 0x%x\n", GESTURE_WATCH_FILTER_MASK_ALL);
		ErrorF("[X11][GestureFlushOrDrop][F] g_pGesture->recognized_gesture=0x%x\n", g_pGesture->recognized_gesture);
#endif//__DETAIL_DEBUG__
	}

	err = GestureRegionsReinit();
	if( ERROR_NONE != err )
		return err;

	g_pGesture->pTempWin = NULL;
	g_pGesture->inc_num_pressed = g_pGesture->num_pressed = 0;
	g_pGesture->event_sum[0] = 0;

	return ERROR_NONE;
}

void
GestureHandleMTSyncEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
	int i;

#ifdef __DEBUG_EVENT_HANDLER__
	ErrorF("\n[X11][GestureHandleMTSyncEvent] (%d:%d) time:%d cur:%d\n",
			ev->any_event.deviceid, ev->any_event.sync, (int)ev->any.time, (int)GetTimeInMillis());
#endif//__DEBUG_EVENT_HANDLER__

	if(!g_pGesture->is_active)
	{
		g_pGesture->ehtype = PROPAGATE_EVENTS;
		return;
	}

	if( MTOUCH_FRAME_SYNC_BEGIN == ev->any_event.sync )
	{
		g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_BEGIN;
		g_pGesture->ehtype = KEEP_EVENTS;
		g_pGesture->filter_mask = 0;
		g_pGesture->recognized_gesture = 0;
		g_pGesture->num_pressed = 0;

		for( i=0 ; i < g_pGesture->num_mt_devices ; i++ )
			g_pGesture->fingers[i].ptime = 0;
	}
	else if( MTOUCH_FRAME_SYNC_END == ev->any_event.sync )
	{
		g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_END;
		g_pGesture->ehtype = PROPAGATE_EVENTS;
		g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
	}
}

void GestureEmulateHWKey(DeviceIntPtr dev, int keycode)
{
	if(dev)
	{
		xf86PostKeyboardEvent(dev, keycode, 1);
		xf86PostKeyboardEvent(dev, keycode, 0);
	}
}

void
GestureHandleButtonPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	ErrorF("[X11][GestureHandleButtonPEvent] devid=%d time:%d cur:%d (%d, %d)\n", device->id, ev->any.time, GetTimeInMillis(), ev->device_event.root_x, ev->device_event.root_y);
#endif//__DEBUG_EVENT_HANDLER__
	switch( g_pGesture->ehtype )
	{
		case KEEP_EVENTS:
			if( ERROR_INVALPTR == GestureEnqueueEvent(screen_num, ev,  device) )
			{
				GestureControl(g_pGesture->this_device, DEVICE_OFF);
				return;
			}

			if( g_pGesture->num_mt_devices )
				GestureRecognize(ET_ButtonPress, ev, device);
			else
				device->public.processInputProc(ev, device);
			break;

		case PROPAGATE_EVENTS:
			device->public.processInputProc(ev, device);
			break;

		case IGNORE_EVENTS:
			GestureRecognize(ET_ButtonPress, ev, device);
			break;

		default:
			break;
	}
}

void
GestureHandleMotionEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	ErrorF("[X11][GestureHandleMotionEvent] devid=%d time:%d cur:%d (%d, %d)\n", device->id, ev->any.time, GetTimeInMillis(), ev->device_event.root_x, ev->device_event.root_y);
#endif//__DEBUG_EVENT_HANDLER__
	switch( g_pGesture->ehtype )
	{
		case KEEP_EVENTS:
			if( ERROR_INVALPTR == GestureEnqueueEvent(screen_num, ev,  device) )
			{
				GestureControl(g_pGesture->this_device, DEVICE_OFF);
				return;
			}

			if( g_pGesture->num_mt_devices )
				GestureRecognize(ET_Motion, ev, device);
			else
				device->public.processInputProc(ev, device);
			break;

		case PROPAGATE_EVENTS:
			device->public.processInputProc(ev, device);
			break;

		case IGNORE_EVENTS:
			GestureRecognize(ET_Motion, ev, device);
			break;

		default:
			break;
	}

}

void
GestureHandleButtonReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	ErrorF("[X11][GestureHandleButtonREvent] devid=%d time:%d cur:%d (%d, %d)\n", device->id, ev->any.time, GetTimeInMillis(), ev->device_event.root_x, ev->device_event.root_y);
#endif//__DEBUG_EVENT_HANDLER__
	switch( g_pGesture->ehtype )
	{
		case KEEP_EVENTS:
			if( ERROR_INVALPTR == GestureEnqueueEvent(screen_num, ev,  device) )
			{
				GestureControl(g_pGesture->this_device, DEVICE_OFF);
				return;
			}

			if( g_pGesture->num_mt_devices )
				GestureRecognize(ET_ButtonRelease, ev, device);
			else
				device->public.processInputProc(ev, device);
			break;

		case PROPAGATE_EVENTS:
			device->public.processInputProc(ev, device);
#if 0
			GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_down);
			GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_up);
#endif
			break;

		case IGNORE_EVENTS:
			GestureRecognize(ET_ButtonRelease, ev, device);
			break;

		default:
			break;
	}
 }

void
GestureHandleKeyPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
	if( (ev->device_event.detail.key == 124) && (g_pGesture->power_pressed != 0) )
	{
		g_pGesture->power_pressed = 2;
#ifdef __DETAIL_DEBUG__
		ErrorF("[GestureHandleKeyPressEvent] power key pressed devid: %d, hwkey_id: %d\n", device->id, g_pGesture->hwkey_id);
		ErrorF("[GestureHandleKeyPressEvent] power_pressed: %d\n", g_pGesture->power_pressed);
#endif//__DETAIL_DEBUG__
		g_pGesture->power_device = device;
	}
	device->public.processInputProc(ev, device);
}

void
GestureHandleKeyReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
	if( (ev->device_event.detail.key == 124) && (g_pGesture->power_pressed != 0) )
	{
		g_pGesture->power_pressed = 1;
#ifdef __DETAIL_DEBUG__
		ErrorF("[GestureHandleKeyReleaseEvent] power key released devid: %d, hwkey_id: %d\n", device->id, g_pGesture->hwkey_id);
		ErrorF("[GestureHandleKeyReleaseEvent] power_pressed: %d\n", g_pGesture->power_pressed);
#endif//__DETAIL_DEBUG__
		g_pGesture->power_device = device;
	}
	device->public.processInputProc(ev, device);
}

static ErrorStatus
GestureEnableEventHandler(InputInfoPtr pInfo)
 {
 	Bool res;
	GestureDevicePtr pGesture = pInfo->private;

	res = GestureInstallResourceStateHooks();

	if( !res )
	{
		ErrorF("[X11][GestureEnableEventHandler] Failed on GestureInstallResourceStateHooks() !\n");
		return ERROR_ABNORMAL;
	}

	res = GestureSetMaxNumberOfFingers((int)MAX_MT_DEVICES);

	if( !res )
	{
		ErrorF("[X11][GestureEnableEventHandler] Failed on GestureSetMaxNumberOfFingers(%d) !\n", (int)MAX_MT_DEVICES);
		goto failed;
	}

	res = GestureRegisterCallbacks(GestureCbEventsGrabbed, GestureCbEventsSelected);

	if( !res )
	{
		ErrorF("[X11][GestureEnableEventHandler] Failed to register callbacks for GestureEventsGrabbed(), GestureEventsSelected() !\n");
		goto failed;
	}

	pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 5000, GestureTimerHandler, pInfo);

	if( !pGesture->device_setting_timer )
	{
		ErrorF("[X11][GestureEnableEventHandler] Failed to set time for detecting devices !\n");
		goto failed;
	}

	return ERROR_NONE;

failed:
	GestureUninstallResourceStateHooks();
	GestureUnsetMaxNumberOfFingers();

	return ERROR_ABNORMAL;
}

static ErrorStatus
GestureDisableEventHandler(void)
{
	ErrorStatus err = ERROR_NONE;

	mieqSetHandler(ET_ButtonPress, NULL);
	mieqSetHandler(ET_ButtonRelease, NULL);
	mieqSetHandler(ET_Motion, NULL);
	mieqSetHandler(ET_KeyPress, NULL);
	mieqSetHandler(ET_KeyRelease, NULL);
	mieqSetHandler(ET_MTSync, NULL);

	err = GestureFiniEQ();

	if( ERROR_INVALPTR == err )
	{
		ErrorF("[X11][GestureDisableEventHandler] EQ is invalid or was freed already !\n");
	}

	GestureRegisterCallbacks(NULL, NULL);
	GestureUninstallResourceStateHooks();

	return err;
}

static CARD32
GestureTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	InputInfoPtr pInfo = (InputInfoPtr)arg;
	GestureDevicePtr pGesture;
	int idx;
	DeviceIntPtr dev;

	if(!pInfo)
	{
		ErrorF("[X11][%s] pInfo is NULL !\n", __FUNCTION__);
		goto failed;
	}

	pGesture = pInfo->private;

	idx = 0;
	for( dev = inputInfo.pointer ; dev; dev = dev->next )
	{
		if(IsMaster(dev) && IsPointerDevice(dev))
		{
			pGesture->master_pointer = dev;
			ErrorF("[X11][GestureTimerHandler][id:%d] Master Pointer=%s\n", dev->id, pGesture->master_pointer->name);
			continue;
		}

		if(IsXTestDevice(dev, NULL) && IsPointerDevice(dev))
		{
			pGesture->xtest_pointer = dev;
			ErrorF("[X11][GestureTimerHandler][id:%d] XTest Pointer=%s\n", dev->id, pGesture->xtest_pointer->name);
			continue;
		}

		if(IsPointerDevice(dev))
		{
			if( idx >= MAX_MT_DEVICES )
			{
				ErrorF("[X11][GestureTimerHandler] Number of mt device is over MAX_MT_DEVICES(%d) !\n",
					MAX_MT_DEVICES);
				continue;
			}
			pGesture->mt_devices[idx] = dev;
			ErrorF("[X11][GestureTimerHandler][id:%d] MT device[%d] name=%s\n", dev->id, idx, pGesture->mt_devices[idx]->name);
			idx++;
		}
	}

	for( dev = inputInfo.keyboard ; dev; dev = dev->next )
	{
		if(g_pGesture->hwkey_name && !strncmp(dev->name, g_pGesture->hwkey_name, strlen(dev->name)))
		{
			g_pGesture->hwkey_id = dev->id;
			g_pGesture->hwkey_dev = dev;

			ErrorF("[X11][%s] hwkey_name has been found. hwkey_id=%d (hwkey_dev->name:%s)\n", __FUNCTION__, g_pGesture->hwkey_id, g_pGesture->hwkey_dev->name);
			break;
		}
		else if(!strcasestr(dev->name, "keyboard") && strcasestr(dev->name, "key") && !IsXTestDevice(dev, NULL) && !IsMaster(dev))
		{
			g_pGesture->hwkey_id = dev->id;
			g_pGesture->hwkey_dev = dev;

			ErrorF("[X11][%s] hwkey has been found. hwkey_id=%d (hwkey_dev->name:%s)\n", __FUNCTION__, g_pGesture->hwkey_id, g_pGesture->hwkey_dev->name);
			break;
		}
	}

	if(!g_pGesture->hwkey_id)
	{
		g_pGesture->hwkey_id = inputInfo.keyboard->id;
		g_pGesture->hwkey_dev = inputInfo.keyboard;

		ErrorF("[X11][%s] No hwkey has been found. Back key will go through VCK. hwkey_id=%d (hwkey_dev->name:%s)\n",
			__FUNCTION__, g_pGesture->hwkey_id, g_pGesture->hwkey_dev->name);
	}

	if( !pGesture->master_pointer || !pGesture->xtest_pointer )
	{
		ErrorF("[X11][GestureTimerHandler] Failed to get info of master pointer or XTest pointer !\n");
		pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 0, NULL, NULL);
		pGesture->num_mt_devices = 0;

		return 0;
	}

	TimerCancel(pGesture->device_setting_timer);
	pGesture->device_setting_timer = NULL;
	pGesture->num_mt_devices = idx;

	if( !pGesture->num_mt_devices )
	{
		ErrorF("[X11][GestureTimerHandler] Failed to mt device information !\n");
		TimerCancel(pGesture->device_setting_timer);
		pGesture->device_setting_timer = NULL;
		pGesture->num_mt_devices = 0;
	    	pGesture->first_fingerid = -1;
		return 0;
	}

	pGesture->first_fingerid = pGesture->mt_devices[0]->id;
	memset(pGesture->fingers, 0, sizeof(TouchStatus)*pGesture->num_mt_devices);
	g_pGesture->pTempWin = NULL;
	g_pGesture->inc_num_pressed = 0;

	if( ERROR_NONE != GestureRegionsInit() || ERROR_NONE != GestureInitEQ() )
	{
		goto failed;
	}

	mieqSetHandler(ET_ButtonPress, GestureHandleButtonPressEvent);
	mieqSetHandler(ET_ButtonRelease, GestureHandleButtonReleaseEvent);
	mieqSetHandler(ET_Motion, GestureHandleMotionEvent);
	mieqSetHandler(ET_KeyPress, GestureHandleKeyPressEvent);
	mieqSetHandler(ET_KeyRelease, GestureHandleKeyReleaseEvent);

	//if( pGesture->is_active )
		mieqSetHandler(ET_MTSync, GestureHandleMTSyncEvent);

	return 0;

failed:

	GestureUninstallResourceStateHooks();
	GestureUnsetMaxNumberOfFingers();

	return 0;
}

BOOL
IsXTestDevice(DeviceIntPtr dev, DeviceIntPtr master)
{
	if (IsMaster(dev))
		return FALSE;

	if (master)
		return (dev->xtest_master_id == master->id);

	return (dev->xtest_master_id != 0);
}

void
GestureEnable(int enable, Bool prop, DeviceIntPtr dev)
{
	if((!enable) && (g_pGesture->is_active))
	{
		g_pGesture->ehtype = PROPAGATE_EVENTS;
		mieqSetHandler(ET_MTSync, NULL);
		g_pGesture->is_active = 0;
		ErrorF("[X11][GestureEnable] Disabled !\n");
	}
	else if((enable) && (!g_pGesture->is_active))
	{
		g_pGesture->ehtype = KEEP_EVENTS;
		mieqSetHandler(ET_MTSync, GestureHandleMTSyncEvent);
		g_pGesture->is_active = 1;
		ErrorF("[X11][GestureEnable] Enabled !\n");
	}

	if(!prop)
		 XIChangeDeviceProperty(dev, prop_gesture_recognizer_onoff, XA_INTEGER, 32, PropModeReplace, 1, &g_pGesture->is_active, FALSE);
}

ErrorStatus
GestureRegionsInit(void)
{
	int i;

	if( !g_pGesture )
		return ERROR_INVALPTR;

	pixman_region_init(&g_pGesture->area);

	for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
	{
		pixman_region_init_rect (&g_pGesture->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
	}

	return ERROR_NONE;
}

ErrorStatus
GestureRegionsReinit(void)
{
	if( !g_pGesture )
	{
		ErrorF("[X11][GestureRegionsReinit] Invalid pointer access !\n");
		return ERROR_INVALPTR;
	}

	pixman_region_init(&g_pGesture->area);

	return ERROR_NONE;
}

ErrorStatus
GestureInitEQ(void)
{
	int i;
	IEventPtr tmpEQ;

	tmpEQ = (IEventRec *)calloc(GESTURE_EQ_SIZE, sizeof(IEventRec));

	if( !tmpEQ )
	{
		ErrorF("[X11][GestureInitEQ] Failed to allocate memory for EQ !\n");
		return ERROR_ALLOCFAIL;
	}

	for( i = 0 ; i < GESTURE_EQ_SIZE ; i++ )
	{
		tmpEQ[i].event = (InternalEvent *)malloc(sizeof(InternalEvent));
		if( !tmpEQ[i].event )
		{
			ErrorF("[X11][GestureInitEQ] Failed to allocation memory for each event buffer in EQ !\n");
			i--;
			while(i >= 0 && tmpEQ[i].event)
			{
				free(tmpEQ[i].event);
				tmpEQ[i].event = NULL;
			}
			free (tmpEQ);
			tmpEQ = NULL;
			return ERROR_ALLOCFAIL;
		}
	}

	g_pGesture->EQ = tmpEQ;
	g_pGesture->headEQ = g_pGesture->tailEQ = 0;

	return ERROR_NONE;
}

ErrorStatus
GestureFiniEQ(void)
{
	int i;

	if( !g_pGesture || !g_pGesture->EQ )
		return ERROR_INVALPTR;

	for( i = 0 ; i < GESTURE_EQ_SIZE ; i++ )
	{
		if( g_pGesture->EQ[i].event )
		{
			free(g_pGesture->EQ[i].event);
			g_pGesture->EQ[i].event = NULL;
		}
	}

	free(g_pGesture->EQ);
	g_pGesture->EQ = NULL;

	return ERROR_NONE;
}

ErrorStatus
GestureEnqueueEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
	int tail;

	if( !g_pGesture || !g_pGesture->EQ )
	{
		ErrorF("[X11][GestureEnqueueEvent] Invalid pointer access !\n");
		return ERROR_INVALPTR;
	}

	tail = g_pGesture->tailEQ;

	if( tail >= GESTURE_EQ_SIZE )
	{
		ErrorF("[X11][GestureEnqueueEvent] Gesture EQ is full !\n");
		printk("[X11][GestureEnqueueEvent] Gesture EQ is full...Force Gesture Flush !\n");
		GestureEventsFlush();
		return ERROR_EQFULL;
	}

#ifdef __DETAIL_DEBUG__
	switch( ev->any.type )
	{
		case ET_ButtonPress:
			ErrorF("[X11][GestureEnqueueEvent] ET_ButtonPress (id:%d)\n", device->id);
			break;

		case ET_ButtonRelease:
			ErrorF("[X11][GestureEnqueueEvent] ET_ButtonRelease (id:%d)\n", device->id);
			break;

		case ET_Motion:
			ErrorF("[X11][GestureEnqueueEvent] ET_Motion (id:%d)\n", device->id);
			break;
	}
#endif//__DETAIL_DEBUG__

	g_pGesture->EQ[tail].device = device;
	g_pGesture->EQ[tail].screen_num = screen_num;
	memcpy(g_pGesture->EQ[tail].event, ev, sizeof(InternalEvent));//need to be optimized
	g_pGesture->tailEQ++;

	return ERROR_NONE;
}

ErrorStatus
GestureEventsFlush(void)
{
	int i;
	DeviceIntPtr device;

	if( !g_pGesture->EQ )
	{
		ErrorF("[X11][GestureEventsFlush] Invalid pointer access !\n");
		return ERROR_INVALPTR;
	}

#ifdef __DETAIL_DEBUG__
	ErrorF("[X11][GestureEventsFlush]\n");
#endif//__DETAIL_DEBUG__
	for( i = g_pGesture->headEQ ; i < g_pGesture->tailEQ ; i++)
	{
		device = g_pGesture->EQ[i].device;
		device->public.processInputProc(g_pGesture->EQ[i].event, device);
	}
	for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
		g_pGesture->event_sum[i] = 0;
	g_pGesture->headEQ = g_pGesture->tailEQ = 0;//Free EQ

	return ERROR_NONE;
}

void
GestureEventsDrop(void)
{
	g_pGesture->headEQ = g_pGesture->tailEQ = 0;//Free EQ
}

#ifdef HAVE_PROPERTIES
static void
GestureInitProperty(DeviceIntPtr dev)
{
	int rc;

	prop_gesture_recognizer_onoff = MakeAtom(GESTURE_RECOGNIZER_ONOFF, strlen(GESTURE_RECOGNIZER_ONOFF),  TRUE);
	rc = XIChangeDeviceProperty(dev, prop_gesture_recognizer_onoff, XA_INTEGER, 32, PropModeReplace, 1, &g_pGesture->is_active, FALSE);

	if (rc != Success)
		return;

	XISetDevicePropertyDeletable(dev, prop_gesture_recognizer_onoff, FALSE);
}

static int
GestureSetProperty(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr val,
                 BOOL checkonly)
{
	if( prop_gesture_recognizer_onoff == atom )
	{
		int data;
		if( val->format != 32 || val->type != XA_INTEGER || val->size != 1 )
			return BadMatch;

		if( !checkonly )
		{
			data = *((int *)val->data);
			GestureEnable(data, TRUE, dev);
		}
	}
	return Success;
}
#endif//HAVE_PROPERTIES

static int
GestureInit(DeviceIntPtr device)
{
#ifdef HAVE_PROPERTIES
	GestureInitProperty(device);
	XIRegisterPropertyHandler(device, GestureSetProperty, NULL, NULL);
#endif
	//GestureEnable(1, FALSE, g_pGesture->this_device);
	return Success;
}

static void
GestureFini(DeviceIntPtr device)
{
	XIRegisterPropertyHandler(device, NULL, NULL, NULL);
}

static pointer
GesturePlug(pointer module, pointer options, int *errmaj, int *errmin)
{
	xf86AddInputDriver(&GESTURE, module, 0);
	return module;
}

static void
GestureUnplug(pointer p)
{
}

static int
GesturePreInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
    int rc = BadAlloc;
    GestureDevicePtr    pGesture;

    pGesture = calloc(1, sizeof(GestureDeviceRec));

    if (!pGesture) {
        pInfo->private = NULL;
        //xf86DeleteInput(pInfo, 0);
        goto error;
    }

    g_pGesture = pGesture;
    pInfo->private = pGesture;
    pInfo->flags = 0;
    pInfo->read_input = GestureReadInput; /* new data avl */
    pInfo->switch_mode = NULL; /* toggle absolute/relative mode */
    pInfo->device_control = GestureControl; /* enable/disable dev */
    /* process driver specific options */
    pGesture->device = xf86SetStrOption(pInfo->options, "Device", "/dev/null");
    pGesture->is_active = xf86SetIntOption(pInfo->options, "Activate", 0);
    pGesture->gestureWin = None;
    pGesture->lastSelectedWin = None;
    pGesture->power_pressed = 1;
    pGesture->hwkey_id = 0;
    pGesture->hwkey_dev = NULL;
    pGesture->hwkey_name = xf86SetStrOption(pInfo->options, "BackHWKeyName", NULL);
    pGesture->screen_width = xf86SetIntOption(pInfo->options,"ScreenWidth", 0);
    pGesture->screen_height = xf86SetIntOption(pInfo->options,"ScreenHeight", 0);
    pGesture->hwkeycode_flick_down = xf86SetIntOption(pInfo->options, "FlickDownKeycode", 0);
    pGesture->hwkeycode_flick_up = xf86SetIntOption(pInfo->options, "FlickUpKeycode", 0);
    pGesture->flick_press_area = xf86SetIntOption(pInfo->options, "FlickPressArea", 0);
    pGesture->flick_press_area_left = xf86SetIntOption(pInfo->options, "FlickPressArea_LEFT", 0);
    pGesture->flick_press_area_left_right = xf86SetIntOption(pInfo->options, "FlickPressArea_LEFT_RIGHT", 0);
    pGesture->flick_minimum_height = xf86SetIntOption(pInfo->options, "FlickMinimumHeight", 0);
    pGesture->shutdown_keycode = xf86SetIntOption(pInfo->options, "ShutdownKeycode", 0);
    pGesture->singletap_threshold= xf86SetIntOption(pInfo->options, "SingleTapThresHold", 0);
    pGesture->doubletap_threshold= xf86SetIntOption(pInfo->options, "DoubleTapThresHold", 0);
    pGesture->hold_area_threshold = xf86SetRealOption(pInfo->options, "HoldAreaThresHold", 0);
    pGesture->hold_move_threshold = xf86SetIntOption(pInfo->options, "HoldMoveThresHold", 0);
    pGesture->hold_time_threshold = xf86SetIntOption(pInfo->options, "HoldTimeThresHold", 0);
    pGesture->activate_flick_down = xf86SetIntOption(pInfo->options, "ActivateFlickDown", 0);
    pGesture->activate_flick_up = xf86SetIntOption(pInfo->options, "ActivateFlickUp", 0);
    pGesture->activate_flick_right = xf86SetIntOption(pInfo->options, "ActivateFlickRight", 0);

	ErrorF("[X11][%s] ###############################################################\n", __FUNCTION__);
    ErrorF("[X11][%s] screen_width=%d, screen_height=%d\n", __FUNCTION__,
        pGesture->screen_width, pGesture->screen_height);
    ErrorF("[X11][%s] FlickDownKeycode=%d, FlickUpKeycode=%d\n", __FUNCTION__,
        pGesture->hwkeycode_flick_down, pGesture->hwkeycode_flick_up);
    ErrorF("[X11][%s] flick_press_area=%d, flick_press_area_left: %d, flick_press_area_left_right: %d, flick_minimum_height=%d\n", __FUNCTION__,
        pGesture->flick_press_area, pGesture->flick_press_area_left, pGesture->flick_press_area_left_right, pGesture->flick_minimum_height);
    ErrorF("[X11][%s] ShutdownKeycode=%d\n", __FUNCTION__, pGesture->shutdown_keycode);
    ErrorF("[X11][%s] singletap_threshold=%d, doubletap_threshold=%d\n", __FUNCTION__, pGesture->singletap_threshold, pGesture->doubletap_threshold);
    ErrorF("[X11][%s] hold_area_threshold: %f, hold_move_threshold: %d, hold_time_threshold: %d\n", __FUNCTION__,
        pGesture->hold_area_threshold, pGesture->hold_move_threshold, pGesture->hold_time_threshold);
    ErrorF("[X11][%s] activate_flick_down=%d, activate_flick_up=%d, activate_flick_right=%d\n", __FUNCTION__,
		pGesture->activate_flick_down, pGesture->activate_flick_up, pGesture->activate_flick_right);
    ErrorF("[X11][%s] ###############################################################\n", __FUNCTION__);

    if(pGesture->hwkey_name)
		ErrorF("[X11][%s] hwkey_name=%s\n", __FUNCTION__, pGesture->hwkey_name);

    pGesture->mtsync_status = MTOUCH_FRAME_SYNC_END;
    g_pGesture->grabMask = g_pGesture->eventMask = 0;

    xf86Msg(X_INFO, "%s: Using device %s.\n", pInfo->name, pGesture->device);

    /* process generic options */
    xf86CollectInputOptions(pInfo, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    pInfo->fd = -1;

    return Success;

error:
    if (pInfo->fd >= 0)
        close(pInfo->fd);
    return rc;
}

static void
GestureUnInit(InputDriverPtr drv, InputInfoPtr pInfo, int flags)
{
	GestureDevicePtr pGesture = pInfo->private;

	g_pGesture = pGesture = NULL;
	pInfo->private = NULL;

	xf86DeleteInput(pInfo, 0);
}

static int
GestureControl(DeviceIntPtr device, int what)
{
    InputInfoPtr  pInfo = device->public.devicePrivate;
    GestureDevicePtr pGesture = pInfo->private;

    switch(what)
    {
        case DEVICE_INIT:
	     GestureInit(device);
            break;

        /* Switch device on.  Establish socket, start event delivery.  */
        case DEVICE_ON:
            xf86Msg(X_INFO, "%s: On.\n", pInfo->name);

            if (device->public.on)
                    break;

            device->public.on = TRUE;
	     pGesture->this_device = device;
	     pGesture->num_mt_devices = 0;
	     if( ERROR_ABNORMAL == GestureEnableEventHandler(pInfo) )
	     	goto device_off;
            break;

       case DEVICE_OFF:
device_off:
	     GestureDisableEventHandler();
	     GestureFini(device);
	     pGesture->this_device = NULL;
             xf86Msg(X_INFO, "%s: Off.\n", pInfo->name);

            if (!device->public.on)
                break;

            pInfo->fd = -1;
            device->public.on = FALSE;
            break;

      case DEVICE_CLOSE:
            /* free what we have to free */
            break;
    }
    return Success;
}

static void
GestureReadInput(InputInfoPtr pInfo)
{
}

