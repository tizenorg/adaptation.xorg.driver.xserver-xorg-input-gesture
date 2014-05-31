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
#include <xdbg.h>

#define MGEST	XDBG_M('G','E','S','T')

static void printk(const char* fmt, ...) __attribute__((format(printf, 1, 0)));
extern char *strcasestr(const char *s, const char *find);
extern ScreenPtr miPointerCurrentScreen(void);

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
static inline void GestureEnableDisable();
void GestureCbEventsGrabbed(Mask *pGrabMask, GestureGrabEventPtr *pGrabEvent);
void GestureCbEventsSelected(Window win, Mask *pEventMask);
WindowPtr GestureGetEventsWindow(void);
static Bool GestureHasFingersEvents(int eventType);

//Enqueued event handlers and enabler/disabler
static ErrorStatus GestureEnableEventHandler(InputInfoPtr pInfo);
static ErrorStatus GestureDisableEventHandler(void);
static CARD32 GestureTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg);
static CARD32 GestureEventTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg);
static CARD32 GesturePalmEventTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg);
void GestureHandleMTSyncEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleButtonPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleButtonReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleMotionEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);
void GestureHandleKeyPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device);

//Gesture recognizer helper
static Bool PointInBorderSize(WindowPtr pWin, int x, int y);
static WindowPtr GestureWindowOnXY(int x, int y);
Bool GestureHasFingerEventMask(int eventType, int num_finger);
#ifdef _F_SUPPORT_BEZEL_FLICK_
static int get_distance(int x1, int y1, int x2, int y2);
#endif
static double get_angle(int x1, int y1, int x2, int y2);

//Gesture recognizer and handlers
void GestureRecognize_GroupPinchRotation(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_GroupFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx);
void GestureRecognize_GroupPan(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_GroupTap(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_GroupTapNHold(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_GroupHold(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
#ifdef _F_SUPPORT_BEZEL_FLICK_
int GestureBezelAngleRecognize(int type, int distance, double angle);
#endif
void GestureHandleGesture_Flick(int num_of_fingers, int distance, Time duration, int direction);
void GestureHandleGesture_Tap(int num_finger, int tap_repeat, int cx, int cy);
void GestureHandleGesture_PinchRotation(int num_of_fingers, double zoom, double angle, int distance, int cx, int cy, int kinds);
void GestureHandleGesture_Hold(int num_fingers, int cx, int cy, Time holdtime, int kinds);
void GestureHandleGesture_TapNHold(int num_fingers, int cx, int cy, Time interval, Time holdtime, int kinds);
void GestureHandleGesture_Pan(int num_fingers, short int dx, short int dy, int direction, int distance, Time duration, int kinds);
static void GestureHoldDetector(int type, InternalEvent *ev, DeviceIntPtr device);
void GestureRecognize(int type, InternalEvent *ev, DeviceIntPtr device);
ErrorStatus GestureFlushOrDrop(void);

static int GesturePalmGetHorizIndexWithX(int x, int type);
static int GesturePalmGetVertiIndexWithY(int y, int type);
static void GesturePalmRecognize_Hold(int type, int idx, int timer_expired);
static void GesturePalmRecognize_FlickHorizen(int type, int idx);
static void GesturePalmRecognize_FlickVertical(int type,int idx);
static int GesturePalmGetScreenInfo();
static int GesturePalmGetAbsAxisInfo(DeviceIntPtr dev);
static void GesturePalmDataUpdate(int idx, int type, InternalEvent *ev, DeviceIntPtr device);
static void GesturePalmUpdateAreaInfo(int type, int idx);
void GesturePalmRecognize(int type, InternalEvent *ev, DeviceIntPtr device);

//#define __PALM_GESTURE_LOG__
//#define __PALM_DETAIL_LOG__
//#define __DETAIL_DEBUG__
//#define __BEZEL_DEBUG__
//#define __DEBUG_EVENT_HANDLER__

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
		XDBG_DEBUG(MGEST, "TRUE !! Has grabMask\n");
#endif//__DETAIL_DEBUG__
		return TRUE;
	}

	if( g_pGesture->eventMask & eventmask )
	{
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "TRUE !! Has eventMask\n");
#endif//__DETAIL_DEBUG__
		return TRUE;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "FALSE !! eventType=%d, num_finger=%d\n", eventType, num_finger);
#endif//__DETAIL_DEBUG__

	return ret;
}

#ifdef _F_SUPPORT_BEZEL_FLICK_
static int
get_distance(int _x1, int _y1, int _x2, int _y2)
{
	int xx, yy;
	xx = ABS(_x2 - _x1);
	yy = ABS(_y2 - _y1);

	if(xx && yy)
	{
		return (int)sqrt(pow(xx, 2) + pow(yy, 2));
	}
	else if(xx)
	{
		return yy;
	}
	else if(yy)
	{
		return xx;
	}
	else
	{
		return 0;
	}
}
#endif//_F_SUPPORT_BEZEL_FLICK_

static double
get_angle(int _x1, int _y1, int _x2, int _y2)
{
   double a, xx, yy;
   xx = fabs(_x2 - _x1);
   yy = fabs(_y2 - _y1);

   if (((int) xx) && ((int) yy))
     {
        a = atan(yy / xx);
        if (_x1 < _x2)
          {
             if (_y1 < _y2)
               {
                  return (RAD_360DEG - a);
               }
             else
               {
                  return (a);
               }
          }
        else
          {
             if (_y1 < _y2)
               {
                  return (RAD_180DEG + a);
               }
             else
               {
                  return (RAD_180DEG - a);
               }
          }
     }

   if (((int) xx))
     {  /* Horizontal line */
        if (_x2 < _x1)
          {
             return (RAD_180DEG);
          }
        else
          {
             return (0.0);
          }
     }

   /* Vertical line */
   if (_y2 < _y1)
     {
        return (RAD_90DEG);
     }
   else
     {
        return (RAD_270DEG);
     }
}

void
GestureHandleGesture_Flick(int num_of_fingers, int distance, Time duration, int direction)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyFlickEvent fev;

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "num_fingers=%d, distance=%d, duration=%d, direction=%d\n",
		num_of_fingers, distance, duration, direction);
#endif//__DETAIL_DEBUG__

	if(num_of_fingers == 0)
	{
		if(direction == FLICK_EASTWARD || direction == FLICK_WESTWARD)
			g_pGesture->recognized_palm |= PalmFlickHorizFilterMask;
		if(direction == FLICK_NORTHWARD || direction == FLICK_SOUTHWARD)
			g_pGesture->recognized_palm |= PalmFlickVertiFilterMask;
	}

#ifdef _F_SUPPORT_BEZEL_FLICK_
	else if(num_of_fingers == 1)
	{
		g_pGesture->bezel_recognized_mask |= BezelFlickFilterMask;
	}
	else
#endif
		g_pGesture->recognized_gesture |= FlickFilterMask;

	memset(&fev, 0, sizeof(xGestureNotifyFlickEvent));
	fev.type = GestureNotifyFlick;
	fev.kind = GestureDone;
	fev.num_finger = num_of_fingers;
	fev.distance = distance;
	fev.duration = duration;
	fev.direction = direction;

	if(g_pGesture->GrabEvents)
	{
		target_win = g_pGesture->GrabEvents[GestureNotifyFlick].pGestureGrabWinInfo[num_of_fingers].window;
		target_pWin = g_pGesture->GrabEvents[GestureNotifyFlick].pGestureGrabWinInfo[num_of_fingers].pWin;
	}
	else
	{
		target_win = None;
		target_pWin = None;
	}

	if( g_pGesture->grabMask && (target_win != None) )
	{
		fev.window = target_win;
	}
	else
	{
		fev.window = g_pGesture->gestureWin;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "fev.window=0x%x, g_pGesture->grabMask=0x%x\n", fev.window, g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyFlick, GestureFlickMask, (xGestureCommonEvent *)&fev);
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
	XDBG_DEBUG(MGEST, "num_finger=%d, tap_repeat=%d, cx=%d, cy=%d\n",
		num_finger, tap_repeat, cx, cy);
#endif//__DETAIL_DEBUG__

	g_pGesture->recognized_gesture |= TapFilterMask;
	memset(&tev, 0, sizeof(xGestureNotifyTapEvent));
	tev.type = GestureNotifyTap;
	tev.kind = GestureDone;
	tev.num_finger = num_finger;
	tev.tap_repeat = tap_repeat;
	tev.interval = 0;
	tev.cx = cx;
	tev.cy = cy;

	if(g_pGesture->GrabEvents)
	{
		target_win = g_pGesture->GrabEvents[GestureNotifyTap].pGestureGrabWinInfo[num_finger].window;
		target_pWin = g_pGesture->GrabEvents[GestureNotifyTap].pGestureGrabWinInfo[num_finger].pWin;
	}
	else
	{
		target_win = None;
		target_pWin = None;
	}

	if( g_pGesture->grabMask && (target_win != None) )
	{
		tev.window = target_win;
	}
	else
	{
		tev.window = g_pGesture->gestureWin;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "tev.window=0x%x, g_pGesture->grabMask=0x%x\n", tev.window, g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyTap, GestureTapMask, (xGestureCommonEvent *)&tev);
}

void GestureHandleGesture_PinchRotation(int num_of_fingers, double zoom, double angle, int distance, int cx, int cy, int kinds)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyPinchRotationEvent prev;

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "num_fingers=%d, zoom=%.2f, angle=%.2f(deg=%.2f), distance=%d, cx=%d, cy=%d\n",
				num_of_fingers, zoom, angle, rad2degree(angle), distance, cx, cy);
#endif//__DETAIL_DEBUG__

	g_pGesture->recognized_gesture |= PinchRotationFilterMask;
	memset(&prev, 0, sizeof(xGestureNotifyPinchRotationEvent));
	prev.type = GestureNotifyPinchRotation;
	prev.kind = kinds;
	prev.num_finger = num_of_fingers;
	prev.zoom = XDoubleToFixed(zoom);
	prev.angle = XDoubleToFixed(angle);
	prev.distance = distance;
	prev.cx = cx;
	prev.cy = cy;

	if(g_pGesture->GrabEvents)
	{
		target_win = g_pGesture->GrabEvents[GestureNotifyPinchRotation].pGestureGrabWinInfo[num_of_fingers].window;
		target_pWin = g_pGesture->GrabEvents[GestureNotifyPinchRotation].pGestureGrabWinInfo[num_of_fingers].pWin;
	}
	else
	{
		target_win = None;
		target_pWin = None;
	}

	if( g_pGesture->grabMask && (target_win != None) )
	{
		prev.window = target_win;
	}
	else
	{
		prev.window = g_pGesture->gestureWin;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "prev.window=0x%x, g_pGesture->grabMask=0x%x\n", (unsigned int)prev.window, (unsigned int)g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyPinchRotation, GesturePinchRotationMask, (xGestureCommonEvent *)&prev);
}

void GestureHandleGesture_Hold(int num_fingers, int cx, int cy, Time holdtime, int kinds)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyHoldEvent hev;

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "num_fingers=%d, cx=%d, cy=%d, holdtime=%d, kinds=%d\n",
				num_fingers, cx, cy, holdtime, kinds);
#endif//__DETAIL_DEBUG__

	if(num_fingers == 0)
		g_pGesture->recognized_palm |= PalmHoldFilterMask;
	else
		g_pGesture->recognized_gesture |= HoldFilterMask;
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
	XDBG_DEBUG(MGEST, "hev.window=0x%x, g_pGesture->grabMask=0x%x\n", hev.window, g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyHold, GestureHoldMask, (xGestureCommonEvent *)&hev);
}

void GestureHandleGesture_TapNHold(int num_fingers, int cx, int cy, Time interval, Time holdtime, int kinds)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyTapNHoldEvent thev;

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "num_fingers=%d, cx=%d, cy=%d, interval=%d, holdtime=%d, kinds=%d\n",
				num_fingers, cx, cy, interval, holdtime, kinds);
#endif//__DETAIL_DEBUG__

	g_pGesture->recognized_gesture |= TapNHoldFilterMask;
	memset(&thev, 0, sizeof(xGestureNotifyTapNHoldEvent));
	thev.type = GestureNotifyTapNHold;
	thev.kind = kinds;
	thev.num_finger = num_fingers;
	thev.holdtime = holdtime;
	thev.cx = cx;
	thev.cy = cy;
	thev.interval = interval;

	if(g_pGesture->GrabEvents)
	{
		target_win = g_pGesture->GrabEvents[GestureNotifyTapNHold].pGestureGrabWinInfo[num_fingers].window;
		target_pWin = g_pGesture->GrabEvents[GestureNotifyTapNHold].pGestureGrabWinInfo[num_fingers].pWin;
	}
	else
	{
		target_win = None;
		target_pWin = None;
	}

	if( g_pGesture->grabMask && (target_win != None) )
	{
		thev.window = target_win;
	}
	else
	{
		thev.window = g_pGesture->gestureWin;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "thev.window=0x%x, g_pGesture->grabMask=0x%x\n", thev.window, g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyTapNHold, GestureTapNHoldMask, (xGestureCommonEvent *)&thev);
}

void GestureHandleGesture_Pan(int num_fingers, short int dx, short int dy, int direction, int distance, Time duration, int kinds)
{
	Window target_win;
	WindowPtr target_pWin;
	xGestureNotifyPanEvent pev;

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "num_fingers=%d, dx=%d, dy=%d, direction=%d, distance=%d, duration=%d, kinds=%d\n",
				num_fingers, dx, dy, direction, distance, duration, kinds);
#endif//__DETAIL_DEBUG__

	g_pGesture->recognized_gesture |= PanFilterMask;
	memset(&pev, 0, sizeof(xGestureNotifyPanEvent));
	pev.type = GestureNotifyPan;
	pev.kind = kinds;
	pev.num_finger = num_fingers;
	pev.direction = direction;
	pev.distance = distance;
	pev.duration = duration;
	pev.dx = dx;
	pev.dy = dy;

	if(g_pGesture->GrabEvents)
	{
		target_win = g_pGesture->GrabEvents[GestureNotifyPan].pGestureGrabWinInfo[num_fingers].window;
		target_pWin = g_pGesture->GrabEvents[GestureNotifyPan].pGestureGrabWinInfo[num_fingers].pWin;
	}
	else
	{
		target_win = None;
		target_pWin = None;
	}

	if( g_pGesture->grabMask && (target_win != None) )
	{
		pev.window = target_win;
	}
	else
	{
		pev.window = g_pGesture->gestureWin;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "pev.window=0x%x, g_pGesture->grabMask=0x%x\n", pev.window, g_pGesture->grabMask);
#endif//__DETAIL_DEBUG__

	GestureSendEvent(target_pWin, GestureNotifyPan, GesturePanMask, (xGestureCommonEvent *)&pev);
}

static void GestureHoldDetector(int type, InternalEvent *ev, DeviceIntPtr device)
{
	int i;
	int idx = -1;
	pixman_region16_t tarea1;
	static int num_pressed = 0;
	unsigned int hold_area_size;
	PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;

	if(!g_pGesture->hold_detector_activate)
	{
#ifdef __HOLD_DETECTOR_DEBUG__
		XDBG_DEBUG(MGEST, "g_pGesture->hold_detector_activate=0\n");
#endif//__HOLD_DETECTOR_DEBUG__
		return;
	}

	if(!g_pGesture->has_hold_grabmask)
	{
		Mask eventmask = (1L << GestureNotifyHold);

		if( (g_pGesture->grabMask & eventmask) &&
		(g_pGesture->GrabEvents[GestureNotifyHold].pGestureGrabWinInfo[0].window != None) )
		{
			g_pGesture->has_hold_grabmask = 1;

			//Initialize a set of variables
			num_pressed = 0;
			memset(&g_pGesture->cts, 0, sizeof(g_pGesture->cts));
			pixman_region_init(&g_pGesture->chold_area);
#ifdef __HOLD_DETECTOR_DEBUG__
			XDBG_DEBUG(MGEST, "[%d] Initialize...\n", __LINE__);
#endif//__HOLD_DETECTOR_DEBUG__
		}
		else
		{
			//reset local hold_grab_mask variable
			g_pGesture->has_hold_grabmask = 0;

			g_pGesture->hold_detector_activate = 0;
#ifdef __HOLD_DETECTOR_DEBUG__
			XDBG_DEBUG(MGEST, "has_hold_grabmask=0 and g_pGesture->hold_detector_activate=0\n");
#endif//__HOLD_DETECTOR_DEBUG__
			return;
		}		
	}

	if( IGNORE_EVENTS == g_pGesture->ehtype ||
		device->id < g_pGesture->first_fingerid )
	{
#ifdef __HOLD_DETECTOR_DEBUG__
		XDBG_DEBUG(MGEST, "Return (IGNORE_EVENTS or device->id:%d < first_fingerid:%d)\n", device->id, g_pGesture->first_fingerid);
#endif//__HOLD_DETECTOR_DEBUG__
		return;
	}

	for( i = 0 ; i < g_pGesture->num_mt_devices ; i++ )
	{
		if( device->id == g_pGesture->mt_devices[i]->id )
		{
			idx = i;
			break;
		}
	}
	if( (idx < 0) || ((MAX_MT_DEVICES-1) < idx )) return;

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->cts[idx].status = BTN_PRESSED;
			g_pGesture->cts[idx].cx = ev->device_event.root_x;
			g_pGesture->cts[idx].cy = ev->device_event.root_y;

			num_pressed++;
			if(num_pressed < 3) break;

			if( num_pressed > g_pGesture->num_mt_devices )
				num_pressed = g_pGesture->num_mt_devices;

			pixman_region_init(&tarea1);
			pixman_region_init(&g_pGesture->chold_area);
			pixman_region_init_rect(&tarea1, g_pGesture->cts[0].cx, g_pGesture->cts[0].cy, g_pGesture->cts[0].cx+1, g_pGesture->cts[0].cy+1);

			tarea1.extents.x1 = g_pGesture->cts[0].cx;
			tarea1.extents.x2 = g_pGesture->cts[0].cx+1;
			tarea1.extents.y1 = g_pGesture->cts[0].cy;
			tarea1.extents.y2 = g_pGesture->cts[0].cy+1;

			pixman_region_union(&g_pGesture->chold_area, &tarea1, &tarea1);

			for( i = 1 ; i < num_pressed ; i++ )
			{
					pixman_region_init_rect(&tarea1, g_pGesture->cts[i].cx, g_pGesture->cts[i].cy, g_pGesture->cts[i].cx + 1, g_pGesture->cts[i].cy + 1);

					tarea1.extents.x1 = g_pGesture->cts[i].cx;
					tarea1.extents.x2 = g_pGesture->cts[i].cx + 1;
					tarea1.extents.y1 = g_pGesture->cts[i].cy;
					tarea1.extents.y2 = g_pGesture->cts[i].cy + 1;

					pixman_region_union(&g_pGesture->chold_area, &g_pGesture->chold_area, &tarea1);
			}
			break;

		case ET_Motion:
			if(BTN_RELEASED == g_pGesture->cts[idx].status)
				return;

			g_pGesture->cts[idx].status = BTN_MOVING;
			g_pGesture->cts[idx].cx = ev->device_event.root_x;
			g_pGesture->cts[idx].cy = ev->device_event.root_y;

			if(num_pressed < 3) break;

			pixman_region_init(&tarea1);
			pixman_region_init(&g_pGesture->chold_area);
			pixman_region_init_rect(&tarea1, g_pGesture->cts[0].cx, g_pGesture->cts[0].cy, g_pGesture->cts[0].cx+1, g_pGesture->cts[0].cy+1);

			tarea1.extents.x1 = g_pGesture->cts[0].cx;
			tarea1.extents.x2 = g_pGesture->cts[0].cx+1;
			tarea1.extents.y1 = g_pGesture->cts[0].cy;
			tarea1.extents.y2 = g_pGesture->cts[0].cy+1;

			pixman_region_union(&g_pGesture->chold_area, &tarea1, &tarea1);

			for( i = 1 ; i < num_pressed ; i++ )
			{
					pixman_region_init_rect(&tarea1, g_pGesture->cts[i].cx, g_pGesture->cts[i].cy, g_pGesture->cts[i].cx + 1, g_pGesture->cts[i].cy + 1);

					tarea1.extents.x1 = g_pGesture->cts[i].cx;
					tarea1.extents.x2 = g_pGesture->cts[i].cx + 1;
					tarea1.extents.y1 = g_pGesture->cts[i].cy;
					tarea1.extents.y2 = g_pGesture->cts[i].cy + 1;

					pixman_region_union(&g_pGesture->chold_area, &g_pGesture->chold_area, &tarea1);
			}
			break;

		case ET_ButtonRelease:
			g_pGesture->cts[idx].status = BTN_RELEASED;
			g_pGesture->cts[idx].cx = ev->device_event.root_x;
			g_pGesture->cts[idx].cy = ev->device_event.root_y;

			num_pressed--;
			if(num_pressed <3)
			{
				pixman_region_init(&g_pGesture->chold_area);
			}
			break;
	}

	if(num_pressed >= 3)
	{
		hold_area_size = AREA_SIZE(&g_pGesture->chold_area.extents);

#ifdef __HOLD_DETECTOR_DEBUG__
		XDBG_DEBUG(MGEST, "hold_area_size=%d, pPalmMisc->half_scrn_area_size=%d\n", hold_area_size, pPalmMisc->half_scrn_area_size);
#endif//__HOLD_DETECTOR_DEBUG__

		if(pPalmMisc->half_scrn_area_size <= hold_area_size)
		{
			GestureHandleGesture_Hold(0, AREA_CENTER_X(&g_pGesture->chold_area.extents), AREA_CENTER_Y(&g_pGesture->chold_area.extents), PALM_HOLD_TIME_THRESHOLD, GestureBegin);
			GestureHandleGesture_Hold(0, AREA_CENTER_X(&g_pGesture->chold_area.extents), AREA_CENTER_Y(&g_pGesture->chold_area.extents), PALM_HOLD_TIME_THRESHOLD, GestureEnd);

			g_pGesture->hold_detector_activate = 0;
			g_pGesture->has_hold_grabmask = 0;
		}
	}
}

void
GestureRecognize_GroupPinchRotation(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired)
{
	static int cx, cy;

	static int num_pressed = 0;
	static int state = GestureEnd;
	static int event_type = GestureNotifyPinchRotation;
	static OsTimerPtr pinchrotation_event_timer = NULL;

	static pixman_region16_t base_area;
	static pixman_region16_t cur_area;

	static double base_distance = 0.0f;
	static double base_angle = 0.0f;

	static double prev_distance = 0.0f;
	static double prev_angle = 0.0f;

	static double cur_distance = 0.0f;
	static double cur_angle = 0.0f;

	double diff_distance = 0.0f;
	double diff_angle = 0.0f;

	static int has_event_mask = 0;

	static Time base_time = 0;
	Time current_time;

	if(g_pGesture->recognized_palm || g_pGesture->enqueue_fulled == 1
#ifdef _F_SUPPORT_BEZEL_FLICK_
			|| g_pGesture->bezel_recognized_mask)
#else
			)
#endif
		goto cleanup_pinchrotation;

	if( timer_expired )
	{
		if( state == GestureEnd )
		{
			current_time = GetTimeInMillis();
			if( (current_time - base_time) >= g_pGesture->pinchrotation_time_threshold )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[Timer] You must move farther than dist threshold(=%.2f) or angle threshold(=%2f) within time threshold(=%d) !\n", g_pGesture->pinchrotation_dist_threshold, g_pGesture->pinchrotation_angle_threshold, g_pGesture->pinchrotation_time_threshold);
#endif//__DETAIL_DEBUG__
				goto cleanup_pinchrotation;
			}
		}

		return;
	}

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags |= PressFlagPinchRotation;

			if( g_pGesture->num_pressed < 2 )
				return;

			if( g_pGesture->num_pressed < num_pressed && state != GestureEnd )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[P][cleanup] num_finger changed !(state: %d)  num_pressed=%d, g_pGesture->num_pressed=%d\n", state, num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_pinchrotation;
			}

			if( base_distance == 0.0f && g_pGesture->num_pressed == 2 )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[First Time !!!] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__

				base_time = GetTimeInMillis();
				pixman_region_init(&base_area);
				pixman_region_union(&base_area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[1]);

				prev_distance = base_distance = AREA_DIAG_LEN(&base_area.extents);

#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[P] x1=%d, x2=%d, y1=%d, y2=%d\n", g_pGesture->fingers[0].px, g_pGesture->fingers[1].px,
				g_pGesture->fingers[0].py, g_pGesture->fingers[1].py);
#endif//__DETAIL_DEBUG__

				prev_angle = base_angle = get_angle(g_pGesture->fingers[0].px, g_pGesture->fingers[0].py, g_pGesture->fingers[1].px, g_pGesture->fingers[1].py);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[P] base_angle=%.2f(deg=%.2f)\n", base_angle, rad2degree(base_angle));
#endif//__DETAIL_DEBUG__
				event_type = GestureNotifyPinchRotation;
				pinchrotation_event_timer = TimerSet(pinchrotation_event_timer, 0, g_pGesture->pinchrotation_time_threshold, GestureEventTimerHandler, (int *)&event_type);
			}
			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[P][num_pressed=%d] AREA_SIZE(base_area.extents)=%d\n", num_pressed, AREA_SIZE(&base_area.extents));
			XDBG_DEBUG(MGEST, "[P][num_pressed=%d] base_distance=%.2f, base_angle=%.2f(deg=%.2f)\n", num_pressed, base_distance, base_angle, rad2degree(base_angle));
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagPinchRotation) )
				break;

			if( (num_pressed != g_pGesture->num_pressed) && (state != GestureEnd) )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_pinchrotation;
			}

			if( num_pressed < 2 )
				return;

			if( g_pGesture->fingers[0].mx && g_pGesture->fingers[0].my && g_pGesture->fingers[1].mx && g_pGesture->fingers[1].my )
			{
				pixman_region_init(&cur_area);
				pixman_region_union(&cur_area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[1]);

				cur_distance = AREA_DIAG_LEN(&cur_area.extents);

#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M] x1=%d, x2=%d, y1=%d, y2=%d\n", g_pGesture->fingers[0].mx, g_pGesture->fingers[1].mx,
				g_pGesture->fingers[0].my, g_pGesture->fingers[1].my);
#endif//__DETAIL_DEBUG__

				cur_angle = get_angle(g_pGesture->fingers[0].mx, g_pGesture->fingers[0].my, g_pGesture->fingers[1].mx, g_pGesture->fingers[1].my);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M] cur_angle=%.2f(deg=%.2f)\n", cur_angle, rad2degree(cur_angle));
#endif//__DETAIL_DEBUG__

				diff_distance = prev_distance - cur_distance;
				diff_angle = prev_angle - cur_angle;

				cx = AREA_CENTER_X(&cur_area.extents);
				cy = AREA_CENTER_Y(&cur_area.extents);

#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][state=%d] cx=%d, cy=%d\n", state, cx, cy);
#endif//__DETAIL_DEBUG__

#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][num_pressed=%d] prev_distance=%.2f, cur_distance=%.2f, diff=%.2f\n", num_pressed, prev_distance, cur_distance, diff_distance);
				XDBG_DEBUG(MGEST, "[M][num_pressed=%d] prev_angle=%.2f(deg=%.2f), cur_angle=%.2f(deg=%.2f), diff=%.2f(deg=%.2f)\n", num_pressed, prev_angle, rad2degree(prev_angle), cur_angle, rad2degree(cur_angle), diff_angle, rad2degree(diff_angle));
#endif//__DETAIL_DEBUG__

				switch( state )
				{
					case GestureEnd:
						if( (ABS(diff_distance) >= g_pGesture->pinchrotation_dist_threshold) || (ABS(diff_angle) >= g_pGesture->pinchrotation_angle_threshold) )
						{
#ifdef __DETAIL_DEBUG__
							if( ABS(diff_distance) >= g_pGesture->pinchrotation_dist_threshold )
								XDBG_DEBUG(MGEST, "[M] zoom changed !\n");

							if( ABS(diff_angle) >= g_pGesture->pinchrotation_angle_threshold )
								XDBG_DEBUG(MGEST, "[M] angle changed !\n");
#endif//__DETAIL_DEBUG__

							TimerCancel(pinchrotation_event_timer);
							state = GestureBegin;
							goto gesture_begin_handle;
						}
						break;

					case GestureBegin:
gesture_begin_handle:
#ifdef __DETAIL_DEBUG__
						XDBG_DEBUG(MGEST, "PINCHROTATION Begin !cx=%d, cy=%d, state=%d\n", cx, cy, state);
#endif//__DETAIL_DEBUG__
						if( GestureHasFingerEventMask(GestureNotifyPinchRotation, num_pressed) )
						{
							GestureHandleGesture_PinchRotation(num_pressed, cur_distance / base_distance, (cur_angle > base_angle) ? (cur_angle-base_angle) : (RAD_360DEG + cur_angle - base_angle), cur_distance, cx, cy, GestureBegin);
							prev_distance = cur_distance;
							prev_angle = cur_angle;
							state = GestureUpdate;
							has_event_mask = 1;
						}
						else
						{
							has_event_mask = 0;
							goto cleanup_pinchrotation;
						}
						break;

					case GestureUpdate:
						//if( ABS(diff_distance) < g_pGesture->pinchrotation_dist_threshold && ABS(diff_angle) < g_pGesture->pinchrotation_angle_threshold )
						//	break;

#ifdef __DETAIL_DEBUG__
						if( ABS(diff_distance) >= g_pGesture->pinchrotation_dist_threshold )
							XDBG_DEBUG(MGEST, "[M] zoom changed !\n");

						if( ABS(diff_angle) >= g_pGesture->pinchrotation_angle_threshold )
							XDBG_DEBUG(MGEST, "[M] angle changed !\n");
#endif//__DETAIL_DEBUG__

#ifdef __DETAIL_DEBUG__
						XDBG_DEBUG(MGEST, "PINCHROTATION Update ! cx=%d, cy=%d, state=%d\n", cx, cy, state);
#endif//__DETAIL_DEBUG__
						GestureHandleGesture_PinchRotation(num_pressed, cur_distance / base_distance, (cur_angle > base_angle) ? (cur_angle-base_angle) : (RAD_360DEG + cur_angle - base_angle), cur_distance, cx, cy, GestureUpdate);
						prev_distance = cur_distance;
						prev_angle = cur_angle;
						break;

					case GestureDone:
					default:
						break;
				}
			}
			break;

		case ET_ButtonRelease:
			if( state != GestureEnd && num_pressed >= 2)
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_pinchrotation;
			}

			if( g_pGesture->num_pressed )
				break;

			goto cleanup_pinchrotation;
			break;
	}

	return;

cleanup_pinchrotation:
	g_pGesture->filter_mask |= PinchRotationFilterMask;
	if(  has_event_mask  && (state == GestureBegin || state == GestureUpdate) )
	{
		state = GestureEnd;
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "PINCHROTATION End ! cx=%d, cy=%d, state=%d\n", cx, cy, state);
#endif//__DETAIL_DEBUG__
		GestureHandleGesture_PinchRotation(num_pressed, cur_distance / base_distance, (cur_angle > base_angle) ? (cur_angle-base_angle) : (RAD_360DEG + cur_angle - base_angle), cur_distance, cx, cy, GestureEnd);
	}
	else if(g_pGesture->num_pressed > 1)
	{
		if(!(g_pGesture->filter_mask & PanFilterMask))
		{
			pixman_region_init(&base_area);
			pixman_region_union(&base_area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[1]);

			prev_distance = base_distance = AREA_DIAG_LEN(&base_area.extents);
			prev_angle = base_angle = get_angle(g_pGesture->fingers[0].px, g_pGesture->fingers[0].py, g_pGesture->fingers[1].px, g_pGesture->fingers[1].py);

			g_pGesture->filter_mask &= ~PinchRotationFilterMask;

			return;
		}
		g_pGesture->recognized_gesture &= ~PinchRotationFilterMask;
	}

	if( g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL )
	{
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "[cleanup] GestureFlushOrDrop() !\n");
#endif//__DETAIL_DEBUG__

		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
	}

	prev_distance = base_distance = 0.0f;
	prev_angle = base_angle = 0.0f;
	has_event_mask = num_pressed = 0;
	state = GestureEnd;
	cx = cy = 0;
	TimerCancel(pinchrotation_event_timer);
	return;
}

void
GestureRecognize_GroupFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx)
{
	static int num_pressed = 0;
	static int mbits = 0;
	static int base_area_size = 0;
	static Time base_time = 0;
	static int base_x, base_y;
	Time current_time;
	Time duration;
	int distx, disty;
	int distance, direction;
	int area_size;
	int flicked = 0;

	if(g_pGesture->recognized_palm || g_pGesture->enqueue_fulled == 1
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
		goto cleanup_flick;

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags |= PressFlagFlick;
			if( g_pGesture->num_pressed < 2 )
				return;

			if( !base_area_size || g_pGesture->num_pressed > num_pressed )
			{
				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				base_x = g_pGesture->area.extents.x1;
				base_y = g_pGesture->area.extents.y1;
				base_time = GetTimeInMillis();
			}
			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[P]][num_pressed=%d] AREA_SIZE(area.extents)=%d\n", num_pressed, base_area_size);
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagFlick ) )
				break;

#ifdef __DETAIL_DEBUG__
			if( num_pressed > g_pGesture->num_pressed )
			{
				XDBG_DEBUG(MGEST, "[M][cleanup] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
				//goto cleanup_flick;
			}
#endif//__DETAIL_DEBUG__

			if( num_pressed < 2 )
				return;

			mbits |= (1 << idx);
			if( mbits == (pow(2, num_pressed)-1) )
			{
				area_size = AREA_SIZE(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][num_pressed=%d] AREA_SIZE(area.extents)=%d\n", num_pressed, area_size);
#endif//__DETAIL_DEBUG__
				if( ABS(base_area_size - area_size) >= FLICK_AREA_THRESHOLD )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] diff between Area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, FLICK_AREA_THRESHOLD);
#endif//__DETAIL_DEBUG__
					goto cleanup_flick;
				}

				current_time = GetTimeInMillis();
				if( (current_time - base_time) >= FLICK_AREA_TIMEOUT )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] diff between current time(=%d) and base time(=%d) is bigger than threashold(=%d) !\n", current_time, base_time, FLICK_AREA_TIMEOUT);
#endif//__DETAIL_DEBUG__
					goto cleanup_flick;
				}
				mbits = 0;
			}
			break;

		case ET_ButtonRelease:
			if( g_pGesture->num_pressed )
				break;

			duration = GetTimeInMillis() - base_time;
			distx = g_pGesture->area.extents.x1 - base_x;
			disty = g_pGesture->area.extents.y1 - base_y;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "duration=%d, distx=%d, disty=%d\n", duration, distx, disty);
#endif//__DETAIL_DEBUG__

			if( duration <= 0 || duration >= FLICK_AREA_TIMEOUT )
				goto cleanup_flick;

			if( ABS(distx) >= FLICK_MOVE_THRESHOLD )
			{
				direction = (distx > 0) ? FLICK_EASTWARD : FLICK_WESTWARD;
				distance = ABS(distx);
				flicked++;
			}
			else if( ABS(disty) >= FLICK_MOVE_THRESHOLD )
			{
				direction = (disty > 0) ? FLICK_SOUTHWARD : FLICK_NORTHWARD;
				distance = ABS(disty);
				flicked++;
			}

			if( !flicked )
				goto cleanup_flick;

			if( GestureHasFingerEventMask(GestureNotifyFlick, num_pressed) )
				GestureHandleGesture_Flick(num_pressed, distance, duration, direction);
			goto cleanup_flick_recognized;
			break;
	}

	return;

cleanup_flick:

	g_pGesture->recognized_gesture &= ~FlickFilterMask;

cleanup_flick_recognized:

	g_pGesture->filter_mask |= FlickFilterMask;
	num_pressed = 0;
	base_area_size = 0;
	base_time = 0;
	mbits = 0;
	return;
}

void
GestureRecognize_GroupPan(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired)
{
	static int num_pressed = 0;
	static int mbits = 0;
	static int base_area_size = 0;
	static Time base_time = 0;
	static pixman_box16_t base_box_ext;
	static int base_cx;
	static int base_cy;
	static int prev_cx;
	static int prev_cy;
	static int cx = 0;
	static int cy = 0;
	int dx, dy;
	static Time prev_time = 0;
	Time current_time = 0;
	int distance = 0;
	int direction = 0;
	int area_size;
	static int time_checked = 0;
	static int state = GestureEnd;

	static OsTimerPtr pan_event_timer = NULL;
	static int event_type = GestureNotifyPan;

	if(g_pGesture->recognized_palm || g_pGesture->enqueue_fulled == 1
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
		goto cleanup_pan;

	if( timer_expired )
	{
		if( !time_checked )
		{
			current_time = GetTimeInMillis();
			if( (current_time - base_time) >= PAN_TIME_THRESHOLD )
			{
				if( (!cx && !cy) || INBOX(&base_box_ext, cx, cy) )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[Timer] You must move farther than move threshold(=%d) within time threshold(=%d) !\n", PAN_MOVE_THRESHOLD*2, PAN_TIME_THRESHOLD);
#endif//__DETAIL_DEBUG__
					goto cleanup_pan;
				}
				time_checked = 1;
			}
		}
		return;
	}

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags |= PressFlagPan;

			if( g_pGesture->num_pressed < 2 )
				return;

			if( !base_area_size || g_pGesture->num_pressed > num_pressed )
			{
				if( state != GestureEnd )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[P][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_pan;
				}
				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				prev_cx = base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
				prev_cy = base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
				prev_time = base_time = GetTimeInMillis();
				base_box_ext.x1 = base_cx-PAN_MOVE_THRESHOLD;
				base_box_ext.y1 = base_cy-PAN_MOVE_THRESHOLD;
				base_box_ext.x2 = base_cx+PAN_MOVE_THRESHOLD;
				base_box_ext.y2 = base_cy+PAN_MOVE_THRESHOLD;
				event_type = GestureNotifyPan;
				pan_event_timer = TimerSet(pan_event_timer, 0, PAN_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);
			}
			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[P][num_pressed=%d] AREA_SIZE(area.extents)=%d, base_cx=%d, base_cy=%d\n", num_pressed, base_area_size, base_cx, base_cy);
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagPan ) )
				break;

			if( num_pressed != g_pGesture->num_pressed )
			{
				if( state != GestureEnd )
				{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_pan;
				}
			}

			if( num_pressed < 2 )
				return;

			mbits |= (1 << idx);
			if( mbits == (pow(2, num_pressed)-1) )
			{
				area_size = AREA_SIZE(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
#endif//__DETAIL_DEBUG__

				if( (state != GestureUpdate) && (ABS(base_area_size - area_size) >= PAN_AREA_THRESHOLD) )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, PAN_AREA_THRESHOLD);
#endif//__DETAIL_DEBUG__
					goto cleanup_pan;
				}

				cx = AREA_CENTER_X(&g_pGesture->area.extents);
				cy = AREA_CENTER_Y(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M] cx=%d, prev_cx=%d, diff=%d\n", cx, prev_cx, ABS(cx-prev_cx));
				XDBG_DEBUG(MGEST, "[M] cy=%d, prev_cy=%d, diff=%d\n", cy, prev_cy, ABS(cy-prev_cy));
#endif//__DETAIL_DEBUG__

				if( state <= GestureBegin )
				{
					if( !INBOX(&base_box_ext, cx, cy) )
					{
						TimerCancel(pan_event_timer);
						pan_event_timer = NULL;
						
						if( GestureHasFingerEventMask(GestureNotifyPan, num_pressed) )
						{
							GestureHandleGesture_Pan(num_pressed, prev_cx, prev_cy, direction, distance, current_time-prev_time, GestureBegin);
							state = GestureUpdate;
						}
						else
							goto cleanup_pan;
					}
				}
				else
				{
					dx = cx-prev_cx;
					dy = cy-prev_cy;

					//if( ABS(dx) >= PAN_UPDATE_MOVE_THRESHOLD || ABS(dy) >= PAN_UPDATE_MOVE_THRESHOLD )
					{
#ifdef __DETAIL_DEBUG__
						XDBG_DEBUG(MGEST, "PAN Update !dx=%d, dy=%d, state=%d\n", dx, dy, state);
#endif//__DETAIL_DEBUG__

						GestureHandleGesture_Pan(num_pressed, dx, dy, direction, distance, current_time-prev_time, GestureUpdate);
					}
				}

				prev_cx = cx;
				prev_cy = cy;
				mbits = 0;
			}
			break;

		case ET_ButtonRelease:
			if( state != GestureEnd && num_pressed >= 2)
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_pan;
			}

			if( g_pGesture->num_pressed )
				break;

			goto cleanup_pan;
			break;
	}

	return;

cleanup_pan:
	g_pGesture->filter_mask |= PanFilterMask;
	if( state == GestureBegin || state == GestureUpdate )
	{
		state = GestureEnd;
		if( GestureHasFingerEventMask(GestureNotifyPan, num_pressed) )
		{
			GestureHandleGesture_Pan(num_pressed, (short int)(cx-prev_cx), (short int)(cy-prev_cy), direction, distance, GetTimeInMillis()-prev_time, GestureEnd);
		}
	}
	else if(g_pGesture->num_pressed > 1)
	{
		if(!(g_pGesture->filter_mask & PinchRotationFilterMask))
		{
			base_area_size = AREA_SIZE(&g_pGesture->area.extents);
			prev_cx = base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
			prev_cy = base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
			prev_time = base_time = GetTimeInMillis();
			base_box_ext.x1 = base_cx-PAN_MOVE_THRESHOLD;
			base_box_ext.y1 = base_cy-PAN_MOVE_THRESHOLD;
			base_box_ext.x2 = base_cx+PAN_MOVE_THRESHOLD;
			base_box_ext.y2 = base_cy+PAN_MOVE_THRESHOLD;
			g_pGesture->filter_mask &= ~PanFilterMask;
			return;
		}
		g_pGesture->recognized_gesture &= ~PanFilterMask;
	}

	num_pressed = 0;
	base_area_size = 0;
	base_time = 0;
	mbits = 0;
	time_checked = 0;
	state = GestureEnd;
	cx = cy = 0;
	prev_time = 0;
	base_box_ext.x1 = base_box_ext.x2 = base_box_ext.y1 = base_box_ext.y2 = 0;
	if( pan_event_timer )
	{
		TimerCancel(pan_event_timer);
		pan_event_timer = NULL;
	}
	return;
}

void
GestureRecognize_GroupTap(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired)
{
	static int num_pressed = 0;
	static int base_area_size = 0;

	static Time base_time = 0;
	Time current_time;

	int cx, cy;
	int area_size;

	static int state = GestureEnd;
	static int mbits = 0;
	static int base_cx;
	static int base_cy;
	static pixman_box16_t base_box_ext;

	static int tap_repeat = 0;
	static int prev_tap_repeat = 0;
	static int prev_num_pressed = 0;

	static OsTimerPtr tap_event_timer = NULL;
	static int event_type = GestureNotifyTap;

	if(g_pGesture->recognized_palm || g_pGesture->enqueue_fulled == 1
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
		goto cleanup_tap;

	if( timer_expired )
	{
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "[Timer] state=%d\n", state);
#endif//__DETAIL_DEBUG__

		switch( state )
		{
			case GestureBegin://first tap initiation check
				if( num_pressed )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[Timer][state=1] Tap time expired !(num_pressed=%d, tap_repeat=%d)\n", tap_repeat, num_pressed, tap_repeat);
#endif//__DETAIL_DEBUG__
					state = GestureEnd;
					goto cleanup_tap;
				}
				break;

			case GestureUpdate:
				if( tap_repeat <= 1 )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[Timer][state=2] %d finger SINGLE TAP !(ignored)\n", prev_num_pressed);
#endif//__DETAIL_DEBUG__
					state = GestureEnd;
					goto cleanup_tap;
				}

#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[Timer][state=2]  tap_repeat=%d, prev_tap_repeat=%d, num_pressed=%d\n", tap_repeat, prev_tap_repeat, num_pressed);
#endif//__DETAIL_DEBUG__
				if( GestureHasFingerEventMask(GestureNotifyTap, prev_num_pressed) )
				{
					if(prev_num_pressed == 2 && tap_repeat == 2)
					{
						g_pGesture->zoom_enabled = (g_pGesture->zoom_enabled + 1)%2;
						if(g_pGesture->zoom_enabled == 1)
						{
							g_pGesture->recognized_gesture |= TapFilterMask;
						}
					}
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

			if( !prev_num_pressed && (!base_area_size || g_pGesture->num_pressed > num_pressed) )
			{
				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
				base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
				base_time = GetTimeInMillis();
				base_box_ext.x1 = base_cx-TAP_MOVE_THRESHOLD;
				base_box_ext.y1 = base_cy-TAP_MOVE_THRESHOLD;
				base_box_ext.x2 = base_cx+TAP_MOVE_THRESHOLD;
				base_box_ext.y2 = base_cy+TAP_MOVE_THRESHOLD;
				state = GestureBegin;
				TimerCancel(tap_event_timer);
				tap_event_timer = TimerSet(tap_event_timer, 0, g_pGesture->singletap_threshold, GestureEventTimerHandler, (int *)&event_type);
			}

			num_pressed = g_pGesture->num_pressed;

			current_time = GetTimeInMillis();

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[P][num_pressed=%d] AREA_SIZE(area.extents)=%d, base_cx=%d, base_cy=%d, base_time=%d, current_time=%d\n", num_pressed, base_area_size, base_cx, base_cy, base_time, current_time);
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagTap ) )
				break;

			if( num_pressed < 2 )
				return;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[M][num_pressed=%d] g_pGesture->num_pressed: %d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__

			if( num_pressed != g_pGesture->num_pressed )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][cleanup] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				//goto cleanup_tap;
				break;
			}

			mbits |= (1 << idx);
			if( mbits == (pow(2, num_pressed)-1) )
			{
				area_size = AREA_SIZE(&g_pGesture->area.extents);
				cx = AREA_CENTER_X(&g_pGesture->area.extents);
				cy = AREA_CENTER_Y(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
				XDBG_DEBUG(MGEST, "[M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
				XDBG_DEBUG(MGEST, "[M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
#endif//__DETAIL_DEBUG__

				if( ABS(base_area_size-area_size) >= TAP_AREA_THRESHOLD )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, ABS(base_area_size-area_size));
#endif//__DETAIL_DEBUG__
					goto cleanup_tap;
				}

				if( !INBOX(&base_box_ext, cx, cy) )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] current center coordinates is not in base coordinates box !\n");
#endif//__DETAIL_DEBUG__
					goto cleanup_tap;
				}
			}
			break;

		case ET_ButtonRelease:
			if( g_pGesture->num_pressed )
			{
				break;
			}

			if( !tap_repeat )
			{
				prev_num_pressed = num_pressed;
			}

			prev_tap_repeat = tap_repeat;
			tap_repeat++;
			g_pGesture->tap_repeated = tap_repeat;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[R] tap_repeat=%d, prev_tap_repeat=%d, num_pressed=%d, prev_num_pressed=%d\n", tap_repeat, prev_tap_repeat, num_pressed, prev_num_pressed);
#endif//__DETAIL_DEBUG__

			if( num_pressed != prev_num_pressed || !GestureHasFingerEventMask(GestureNotifyTap, num_pressed) )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R] num_pressed(=%d) != prev_num_pressed(=%d) OR %d finger tap event was not grabbed/selected !\n",
					num_pressed, prev_num_pressed, num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_tap;
			}

			if( tap_repeat < MAX_TAP_REPEATS )
			{
				state = GestureUpdate;
				TimerCancel(tap_event_timer);
				tap_event_timer = TimerSet(tap_event_timer, 0, g_pGesture->doubletap_threshold, GestureEventTimerHandler, (int *)&event_type);
				num_pressed = 0;
				break;
			}

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[R] %d finger %s\n", num_pressed, (tap_repeat==2) ? "DBL_TAP" : "TRIPLE_TAP");
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

	if( GestureEnd == state )
		g_pGesture->recognized_gesture &= ~TapFilterMask;
	g_pGesture->filter_mask |= TapFilterMask;

	if( g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL )
	{
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "[cleanup] GestureFlushOrDrop() !\n");
#endif//__DETAIL_DEBUG__

		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
	}

	num_pressed = 0;
	tap_repeat = 0;
	g_pGesture->tap_repeated = 0;
	prev_num_pressed = 0;
	mbits = 0;
	base_time = 0;
	state = GestureEnd;
	TimerCancel(tap_event_timer);
	return;
}

void
GestureRecognize_GroupTapNHold(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired)
{
	static int num_pressed = 0;
	static int base_area_size = 0;
	static Time base_time = 0;
	static int base_cx;
	static int base_cy;
	int cx, cy;
	static pixman_box16_t base_box_ext;
	int area_size;
	static int mbits = 0;

	static int tap_repeat = 0;
	static int prev_num_pressed = 0;

	static OsTimerPtr tapnhold_event_timer = NULL;
	static int event_type = GestureNotifyTapNHold;
	static int state = GestureEnd;

	Time interval = 0;
	Time holdtime = 0;

	if(g_pGesture->recognized_palm || g_pGesture->enqueue_fulled == 1
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
		goto cleanup_tapnhold;

	if( timer_expired )
	{
		if( (state == GestureEnd) && num_pressed )
		{
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[Timer][state=%d] Tap time expired !(num_pressed=%d, tap_repeat=%d)\n", GestureEnd, tap_repeat, num_pressed, tap_repeat);
#endif//__DETAIL_DEBUG__
			state = 0;
			goto cleanup_tapnhold;
		}

		if( state == GestureDone )
		{
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[Timer][state=%d] Interval between Tap and Hold is too long !\n");
#endif//__DETAIL_DEBUG__
			goto cleanup_tapnhold;
		}

#ifdef __DETAIL_DEBUG__
		switch( state )
		{
			case GestureBegin:
				XDBG_DEBUG(MGEST, "[Timer] TapNHold Begin !\n");
				break;

			case GestureUpdate:
				XDBG_DEBUG(MGEST, "[Timer] TapNHold Update !\n");
				break;
		}
#endif//__DETAIL_DEBUG__

		if( GestureHasFingerEventMask(GestureNotifyTapNHold, prev_num_pressed) )
		{
			GestureHandleGesture_TapNHold(prev_num_pressed, base_cx, base_cy, interval, holdtime, state);
			tapnhold_event_timer = TimerSet(tapnhold_event_timer, 0, TAPNHOLD_HOLD_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);
		}
		else
		{
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[Timer] %d finger TapNHold event was not grabbed/selected !\n", prev_num_pressed);
#endif//__DETAIL_DEBUG__
			goto cleanup_tapnhold;
		}

		if( state <= GestureBegin )
			state++;
		return;
	}

	switch( type )
	{
		case ET_ButtonPress:
			g_pGesture->fingers[idx].flags |= PressFlagTapNHold;

			if( g_pGesture->num_pressed < 2 )
				return;

			//if( !prev_num_pressed && (!base_area_size || g_pGesture->num_pressed > num_pressed) )
			if( !base_area_size || g_pGesture->num_pressed > num_pressed )
			{

				if( state == GestureUpdate )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[P][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_tapnhold;
				}

				if( state == GestureDone )
					state = GestureBegin;

				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
				base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
				base_time = GetTimeInMillis();
				base_box_ext.x1 = base_cx-TAPNHOLD_MOVE_THRESHOLD;
				base_box_ext.y1 = base_cy-TAPNHOLD_MOVE_THRESHOLD;
				base_box_ext.x2 = base_cx+TAPNHOLD_MOVE_THRESHOLD;
				base_box_ext.y2 = base_cy+TAPNHOLD_MOVE_THRESHOLD;
				if( state == GestureEnd )
					tapnhold_event_timer = TimerSet(tapnhold_event_timer, 0, TAPNHOLD_TAP_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);
				else
				{
					TimerCancel(tapnhold_event_timer);
					tapnhold_event_timer = TimerSet(tapnhold_event_timer, 0, TAPNHOLD_HOLD_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);
				}
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[P] Create Timer !(state=%d)\n", state);
#endif//__DETAIL_DEBUG__
			}

			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[P][num_pressed=%d] AREA_SIZE(area.extents)=%d, base_cx=%d, base_cy=%d, base_time=%d\n", num_pressed, base_area_size, base_cx, base_cy, base_time);
#endif//__DETAIL_DEBUG__
			break;

		case ET_Motion:
			if( !(g_pGesture->fingers[idx].flags & PressFlagTapNHold ) )
				break;

			if( num_pressed < 2 )
				return;

			if( num_pressed != g_pGesture->num_pressed )
			{
				if( state != GestureEnd )
				{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_tapnhold;
				}
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][cleanup] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				//goto cleanup_tapnhold;
			}

			mbits |= (1 << idx);
			if( mbits == (pow(2, num_pressed)-1) )
			{
				area_size = AREA_SIZE(&g_pGesture->area.extents);
				cx = AREA_CENTER_X(&g_pGesture->area.extents);
				cy = AREA_CENTER_Y(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
				XDBG_DEBUG(MGEST, "[M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
				XDBG_DEBUG(MGEST, "[M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
#endif//__DETAIL_DEBUG__

				if( ABS(base_area_size-area_size) >= TAPNHOLD_AREA_THRESHOLD )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, ABS(base_area_size-area_size));
#endif//__DETAIL_DEBUG__
					goto cleanup_tapnhold;
				}

				if( !INBOX(&base_box_ext, cx, cy) )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] current center coordinates is not in base coordinates box !\n");
#endif//__DETAIL_DEBUG__
					goto cleanup_tapnhold;
				}
			}
			break;

		case ET_ButtonRelease:
			if( state != GestureEnd && num_pressed >= 2)
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_tapnhold;
			}

			if( g_pGesture->num_pressed )
				break;

			if( !tap_repeat )
			{
				prev_num_pressed = num_pressed;
			}

			tap_repeat++;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[R] tap_repeat=%d, num_pressed=%d, prev_num_pressed=%d\n", tap_repeat, num_pressed, prev_num_pressed);
#endif//__DETAIL_DEBUG__

			if( num_pressed != prev_num_pressed || !GestureHasFingerEventMask(GestureNotifyTapNHold, num_pressed) )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R] num_pressed(=%d) != prev_num_pressed(=%d) OR %d finger tap event was not grabbed/selected !\n",
					num_pressed, prev_num_pressed, num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_tapnhold;
			}

			if( tap_repeat > 1 )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R] Tap events(tap_repeat=%d) were put twice or more !(ignored)\n", tap_repeat);
#endif//__DETAIL_DEBUG__
				goto cleanup_tapnhold;
			}

			prev_num_pressed = num_pressed;
			num_pressed = 0;
			state = GestureDone;

			TimerCancel(tapnhold_event_timer);
			tapnhold_event_timer = TimerSet(tapnhold_event_timer, 0, TAPNHOLD_INTV_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[R][Last] state=%d, tap_repeat=%d, num_pressed=%d, prev_num_pressed=%d\n", state,  tap_repeat, num_pressed, prev_num_pressed);
#endif//__DETAIL_DEBUG__
			break;
	}

	return;

cleanup_tapnhold:

	if( state == GestureUpdate )
	{
		state = GestureEnd;
		if( GestureHasFingerEventMask(GestureNotifyTapNHold, prev_num_pressed) )
		{
			GestureHandleGesture_TapNHold(prev_num_pressed, base_cx, base_cy, interval, holdtime, state);
		}
	}
	else
	{
		g_pGesture->recognized_gesture &= ~TapNHoldFilterMask;
	}

	g_pGesture->filter_mask |= TapNHoldFilterMask;
	if( g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL )
	{
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "[cleanup] GestureFlushOrDrop() !\n");
#endif//__DETAIL_DEBUG__

		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
	}

	TimerCancel(tapnhold_event_timer);
	num_pressed = 0;
	tap_repeat = 0;
	prev_num_pressed = 0;
	mbits = 0;
	base_time = 0;
	state = 0;

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

	if(g_pGesture->recognized_palm || g_pGesture->enqueue_fulled == 1
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
		goto cleanup_hold;

	if( timer_expired )
	{
		if( state <= GestureBegin )
			state++;

#ifdef __DETAIL_DEBUG__
		switch( state )
		{
			case GestureBegin:
				XDBG_DEBUG(MGEST, "HOLD Begin !\n");
				break;

			case GestureUpdate:
				XDBG_DEBUG(MGEST, "HOLD Update !\n");
				break;
		}
#endif//__DETAIL_DEBUG__

		if( GestureHasFingerEventMask(GestureNotifyHold, num_pressed) )
		{
			GestureHandleGesture_Hold(num_pressed, base_cx, base_cy, GetTimeInMillis()-base_time, state);
			hold_event_timer = TimerSet(hold_event_timer, 0, HOLD_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);
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
					XDBG_DEBUG(MGEST, "[P][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_hold;
				}

				base_area_size = AREA_SIZE(&g_pGesture->area.extents);
				base_cx = AREA_CENTER_X(&g_pGesture->area.extents);
				base_cy = AREA_CENTER_Y(&g_pGesture->area.extents);
				base_time = GetTimeInMillis();
				base_box_ext.x1 = base_cx-HOLD_MOVE_THRESHOLD;
				base_box_ext.y1 = base_cy-HOLD_MOVE_THRESHOLD;
				base_box_ext.x2 = base_cx+HOLD_MOVE_THRESHOLD;
				base_box_ext.y2 = base_cy+HOLD_MOVE_THRESHOLD;
				event_type = GestureNotifyHold;
				hold_event_timer = TimerSet(hold_event_timer, 0, HOLD_TIME_THRESHOLD, GestureEventTimerHandler, (int *)&event_type);
			}
			num_pressed = g_pGesture->num_pressed;

#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[P]][num_pressed=%d] AREA_SIZE(area.extents)=%d, base_cx=%d, base_cy=%d\n", num_pressed, base_area_size, base_cx, base_cy);
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
				XDBG_DEBUG(MGEST, "[M][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					goto cleanup_hold;
				}
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][cleanup] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				//goto cleanup_hold;
			}

			area_size = AREA_SIZE(&g_pGesture->area.extents);
			cx = AREA_CENTER_X(&g_pGesture->area.extents);
			cy = AREA_CENTER_Y(&g_pGesture->area.extents);
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "[M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
			XDBG_DEBUG(MGEST, "[M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
			XDBG_DEBUG(MGEST, "[M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
#endif//__DETAIL_DEBUG__

			if( ABS(base_area_size-area_size) >= HOLD_AREA_THRESHOLD )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, ABS(base_area_size-area_size));
#endif//__DETAIL_DEBUG__
				goto cleanup_hold;
			}

			if( !INBOX(&base_box_ext, cx, cy) )
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M] current center coordinates is not in base coordinates box !\n");
#endif//__DETAIL_DEBUG__
				goto cleanup_hold;
			}
			break;

		case ET_ButtonRelease:
			if( state != GestureEnd && num_pressed >= 2)
			{
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[R][cleanup] num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
				goto cleanup_hold;
			}

			//XDBG_DEBUG(MGEST, "[R] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
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
		g_pGesture->recognized_gesture &= ~HoldFilterMask;
	}

	g_pGesture->filter_mask |= HoldFilterMask;
	num_pressed = 0;
	base_area_size = 0;
	base_time = 0;
	base_cx = base_cy = 0;
	state = GestureEnd;
	base_box_ext.x1 = base_box_ext.x2 = base_box_ext.y1 = base_box_ext.y2 = 0;
	TimerCancel(hold_event_timer);
	return;
}

#ifdef _F_SUPPORT_BEZEL_FLICK_
int
GestureBezelAngleRecognize(int type, int distance, double angle)
{
	if (distance < g_pGesture->bezel.flick_distance)
	{
#ifdef __BEZEL_DEBUG__
		XDBG_DEBUG(MGEST, "distance(%d) < flick_distance(%d)\n", distance, g_pGesture->bezel.flick_distance);
#endif//__BEZEL_DEBUG__
		return 0;
	}
	switch(type)
	{
		case BEZEL_TOP_LEFT:
			break;
		case BEZEL_TOP_RIGHT:
			break;
		case BEZEL_BOTTOM_LEFT:
			if( (g_pGesture->bezel.min_rad< angle) && (angle < g_pGesture->bezel.max_rad) )
			{
#ifdef __BEZEL_DEBUG__
				XDBG_DEBUG(MGEST, "bottom_left bezel success....\n");
#endif//__BEZEL_DEBUG__
				return 1;
			}
			else
			{
#ifdef __BEZEL_DEBUG__
				XDBG_DEBUG(MGEST, "bottom_left bezel failed....\n");
#endif//__BEZEL_DEBUG__
				return 0;
			}
		case BEZEL_BOTTOM_RIGHT:
			if( (g_pGesture->bezel.min_180_rad< angle) && (angle < g_pGesture->bezel.max_180_rad))
			{
#ifdef __BEZEL_DEBUG__
				XDBG_DEBUG(MGEST, "bottom_right bezel success...\n");
#endif//__BEZEL_DEBUG__
				return 1;
			}
			else
			{
#ifdef __BEZEL_DEBUG__
				XDBG_DEBUG(MGEST, "bottom_right bezel failed....\n");
#endif//__BEZEL_DEBUG__
				return 0;
			}
		default:
			return 0;
	}
	return 0;
}
#endif

static inline void
GestureEnableDisable()
{
	if((g_pGesture->grabMask) || (g_pGesture->lastSelectedWin != None))
	{
		GestureEnable(1, FALSE, g_pGesture->this_device);
	}
	else
	{
		GestureEnable(0, FALSE, g_pGesture->this_device);
	}
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
		XDBG_DEBUG(MGEST, "pWin->drawable.id=0x%x\n", pWin->drawable.id);
#endif//__DETAIL_DEBUG__
		g_pGesture->gestureWin = pWin->drawable.id;
	}
	else
	{
#ifdef __DETAIL_DEBUG__
		XDBG_DEBUG(MGEST, "GestureWindowOnXY returns NULL !\n");
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
		XDBG_DEBUG(MGEST, "No grabbed events or no events were selected for window(0x%x) !\n", pWin->drawable.id);
#endif//__DETAIL_DEBUG__
		return NULL;
	}

nonempty_eventmask:

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "g_pGesture->eventMask=0x%x\n", g_pGesture->eventMask);
#endif//__DETAIL_DEBUG__

	mask = (GESTURE_FILTER_MASK_ALL & ~(g_pGesture->grabMask | g_pGesture->eventMask));

#ifdef __DETAIL_DEBUG__
#ifdef _F_SUPPORT_BEZEL_FLICK_
	XDBG_DEBUG(MGEST, "g_pGesture->filter_mask=0x%x, mask=0x%x, palm_filter_mask=0x%x bezel_filter_mask=0x%x\n", g_pGesture->filter_mask, mask, g_pGesture->palm_filter_mask, g_pGesture->bezel_filter_mask);
#else
	XDBG_DEBUG(MGEST, "g_pGesture->filter_mask=0x%x, mask=0x%x, palm_filter_mask=0x%x\n", g_pGesture->filter_mask, mask, g_pGesture->palm_filter_mask);
#endif
#endif//__DETAIL_DEBUG__
	g_pGesture->palm_filter_mask = 0;
	if(mask & HoldFilterMask)
		g_pGesture->palm_filter_mask |= PalmHoldFilterMask;
	if(mask & FlickFilterMask)
	{
		g_pGesture->palm_filter_mask |= PalmFlickHorizFilterMask;
		g_pGesture->palm_filter_mask |= PalmFlickVertiFilterMask;
#ifdef _F_SUPPORT_BEZEL_FLICK_
		g_pGesture->bezel_filter_mask |= BezelFlickFilterMask;
#endif
	}
	if(!(mask & FlickFilterMask))
	{
#ifdef _F_SUPPORT_BEZEL_FLICK_
		if(!(g_pGesture->GrabEvents[GestureNotifyFlick].pGestureGrabWinInfo[1].window))
		{
			g_pGesture->bezel_filter_mask |= BezelFlickFilterMask;
		}
		else
#endif
		if(!(g_pGesture->GrabEvents[GestureNotifyFlick].pGestureGrabWinInfo[0].window))
		{
			g_pGesture->palm_filter_mask |= PalmFlickHorizFilterMask;
			g_pGesture->palm_filter_mask |= PalmFlickVertiFilterMask;
		}
	}
	if(!g_pGesture->palm.palmflag)
	{
		if(!GestureHasFingersEvents(HoldFilterMask))
			mask |= HoldFilterMask;
		if(!GestureHasFingersEvents(FlickFilterMask))
			mask |= FlickFilterMask;
		g_pGesture->filter_mask = mask;
	}
#ifdef __DETAIL_DEBUG__
#ifdef _F_SUPPORT_BEZEL_FLICK_
	XDBG_DEBUG(MGEST, "g_pGesture->filter_mask=0x%x, palm_filter_mask: 0x%x, bezel_filter_mask=0x%x\n", g_pGesture->filter_mask, g_pGesture->palm_filter_mask, g_pGesture->bezel_filter_mask);
#else
	XDBG_DEBUG(MGEST, "g_pGesture->filter_mask=0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->palm_filter_mask);
#endif
#endif//__DETAIL_DEBUG__

	return pWin;
}

static Bool
GestureHasFingersEvents(int eventType)
{
	int i=0;
	Mask eventmask = (1L << eventType);
	for(i=2; i<MAX_MT_DEVICES; i++)
	{
		if( (g_pGesture->grabMask & eventmask) &&
		(g_pGesture->GrabEvents[eventType].pGestureGrabWinInfo[i].window != None) )
		{
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "TRUE !! Has grabMask\n");
#endif//__DETAIL_DEBUG__
			return TRUE;
		}
	}
	return FALSE;
}

static CARD32
GestureEventTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	int event_type = *(int *)arg;

	switch( event_type )
	{
		case GestureNotifyHold:
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "GestureNotifyHold (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupHold(event_type, NULL, NULL, 0, 1);
			break;

		case GestureNotifyPan:
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "GestureNotifyPan (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupPan(event_type, NULL, NULL, 0, 1);
			break;

		case GestureNotifyTap:
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "GestureNotifyTap (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupTap(event_type, NULL, NULL, 0, 1);
			break;

		case GestureNotifyTapNHold:
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "GestureNotifyTapNHold (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupTapNHold(event_type, NULL, NULL, 0, 1);
			break;

		case GestureNotifyPinchRotation:
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "GestureNotifyPinchRotation (event_type = %d)\n", event_type);
#endif//__DETAIL_DEBUG__
			GestureRecognize_GroupPinchRotation(event_type, NULL, NULL, 0, 1);
			break;

		default:
#ifdef __DETAIL_DEBUG__
			XDBG_DEBUG(MGEST, "unknown event_type (=%d)\n", event_type);
#endif//__DETAIL_DEBUG__
			if(timer)
				XDBG_INFO(MGEST, "timer=%x\n", (unsigned int)timer);
	}

	return 0;
}

static CARD32
GesturePalmEventTimerHandler(OsTimerPtr timer,CARD32 time,pointer arg)
{
	int event_type = *(int *)arg;

	switch( event_type )
	{
		case GestureNotifyHold:
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "GestureNotifyHold (event_type = %d)\n", event_type);
#endif//__PALM_DETAIL_LOG__
			GesturePalmRecognize_Hold(event_type, 0, 1);
			break;

		default:
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "unknown event_type (=%d)\n", event_type);
#endif//__PALM_DETAIL_LOG__
			break;
	}

	return 0;
}

#ifdef _F_SUPPORT_BEZEL_FLICK_
static CARD32
GestureBezelSingleFingerTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	BezelFlickStatusPtr pBezel = &g_pGesture->bezel;

	if( pBezel->is_active != BEZEL_NONE )
	{
		pBezel->is_active = BEZEL_END;
#ifdef __BEZEL_DEBUG__
		XDBG_DEBUG(MGEST, "end\n");
#endif//__BEZEL_DEBUG__
	}
	return 0;
}
#endif

static CARD32
GestureSingleFingerTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
	g_pGesture->recognized_gesture = 0;

	if( ERROR_INVALPTR == GestureFlushOrDrop() )
	{
		GestureControl(g_pGesture->this_device, DEVICE_OFF);
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "expired !\n");
#endif//__DETAIL_DEBUG__

	return 0;
}

static CARD32
GesturePalmSingleFingerTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	PalmStatusPtr pPalm = &g_pGesture->palm;
	if(pPalm->palmflag || (pPalm->biggest_tmajor >= PALM_FLICK_FINGER_MIN_TOUCH_MAJOR))
	{
		pPalm->single_timer_expired = 0;
		return 0;
	}

	pPalm->single_timer_expired = 1;
#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "palm expired !\n");
#endif//__DETAIL_DEBUG__

	return 0;
}

static CARD32
GesturePalmHoldRapidHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "[%d] rapid timer in.....\n", __LINE__);
#endif
	GesturePalmRecognize_Hold(0, 0, 0);
	return 0;
}


static int
GesturePalmGetHorizIndexWithX(int x, int type)
{
	int i;
	int ret_idx = -1;
	static int pressed_idx = -1;
	PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;

	for(i = 0 ; i < PALM_HORIZ_ARRAY_COUNT ; i++)
	{
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "pPalmMisc->horiz_coord[%d]=%d, x=%d\n", i, pPalmMisc->horiz_coord[i], x);
#endif//__PALM_DETAIL_LOG__
		if(x <= pPalmMisc->horiz_coord[i])
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "index=%d\n", i);
#endif//__PALM_DETAIL_LOG__
			ret_idx = i;
			goto index_check;
		}
	}
#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "Error ! Failed to get horiz coordinate index !\n");
#endif//__PALM_DETAIL_LOG__
	return ret_idx;

index_check:

	if(type == ET_ButtonPress)
	{
		pressed_idx = ret_idx;
	}
	else if(type == ET_ButtonRelease)
	{
		if((pressed_idx <= 1) && (ret_idx >= (PALM_HORIZ_ARRAY_COUNT-2)))
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[R] Valid !\n");
#else
			;
#endif//__PALM_DETAIL_LOG__
		}
		else if((pressed_idx >= (PALM_HORIZ_ARRAY_COUNT-2)) && (ret_idx <= 1))
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[R] Valid !\n");
#else
			;
#endif//__PALM_DETAIL_LOG__
		}
		else
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[R] Invalid !(pressed_idx=%d, released_idx=%d\n", pressed_idx, ret_idx);
#endif//__PALM_DETAIL_LOG__
			ret_idx = -1;
		}
	}

	return ret_idx;
}

static int
GesturePalmGetVertiIndexWithY(int y, int type)
{
	int i;
	int ret_idx = -1;
	static int pressed_idx = -1;
	PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;

	for(i = 0 ; i < PALM_VERTI_ARRAY_COUNT ; i++)
	{
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "pPalmMisc->horiz_coord[%d]=%d, x=%d\n", i, pPalmMisc->horiz_coord[i], y);
#endif//__PALM_DETAIL_LOG__
		if(y <= pPalmMisc->verti_coord[i])
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "index=%d\n", i);
#endif//__PALM_DETAIL_LOG__
			ret_idx = i;
			goto index_check;
		}
	}
#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "Error ! Failed to get verti coordinate index !\n");
#endif//__PALM_DETAIL_LOG__
	return ret_idx;

index_check:

	if(type == ET_ButtonPress)
	{
		if((ret_idx <= 1) || (ret_idx >=(PALM_VERTI_ARRAY_COUNT-2)))
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[P] Valid !\n");
#endif//__PALM_DETAIL_LOG__
			pressed_idx = ret_idx;
		}
		else
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[P] Invalid !(pressed_idx(=%d) must be between 0 and 1 or between 3 and 4\n", pressed_idx);
#endif//__PALM_DETAIL_LOG__
			ret_idx = -1;
		}

	}
	else if(type == ET_ButtonRelease)
	{
		if((pressed_idx <= 1) && (ret_idx >= (PALM_VERTI_ARRAY_COUNT-2)))
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[R] Valid !\n");
#else
			;
#endif//__PALM_DETAIL_LOG__
		}
		else if((pressed_idx >= (PALM_VERTI_ARRAY_COUNT-2)) && (ret_idx <= 1))
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[R] Valid !\n");
#else
			;
#endif//__PALM_DETAIL_LOG__
		}
		else
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[R] Invalid !(pressed_idx=%d, released_idx=%d\n", pressed_idx, ret_idx);
#endif//__PALM_DETAIL_LOG__
			ret_idx = -1;
		}
	}

	return ret_idx;
}

static void
GesturePalmRecognize_Hold(int type, int idx, int timer_expired)
{
	static int is_holding = 1;
	static int num_pressed = 0;
	static Time base_time = 0;
	static int cx, cy;
	static int base_width_size = 0;

	static int state = GestureEnd;
	static int false_base_width_size_count = 0;
	static int max_num_finger = 0;

	static OsTimerPtr palm_hold_event_timer = NULL;
	static int event_type = GestureNotifyHold;
	PalmStatusPtr pPalm = &g_pGesture->palm;
	static int hold_occured = 0;
	static int rapid_hold = 0;
	static OsTimerPtr palm_hold_rapid_timer = NULL;

#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "num_pressed: %d, cur_touched: %d, palm_flag: %d, is_holding: %d hold_occured %.f\n", num_pressed, pPalm->cur_touched, pPalm->palmflag, is_holding, hold_occured);
	XDBG_DEBUG(MGEST, "pPalm->biggest_wmajor: %.f, pPalm->bigger_wmajor: %.f, pPalm->biggest_tmajor: %.f\n", pPalm->biggest_wmajor, pPalm->bigger_wmajor, pPalm->biggest_tmajor);
#endif
	if(g_pGesture->enqueue_fulled == 1)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "EQ Event is full.... palm recognize drop..\n");
#endif
		goto hold_failed;
	}
	if(rapid_hold && type==0 && idx==0 && timer_expired == 0)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "rapid timer is released .....\n");
#endif
		rapid_hold = 0;
		goto release_hold;
	}
	if(pPalm->single_timer_expired && (pPalm->biggest_tmajor < PALM_HOLD_FINGER_MIN_TOUCH_MAJOR) && (max_num_finger < (PALM_HOLD_MIN_FINGER - 1)))
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "No Hold single finger...expired, biggest_tmajor: %.f\n", pPalm->biggest_tmajor);
#endif
		goto hold_failed;
	}

	if(g_pGesture->recognized_gesture || (g_pGesture->recognized_palm && !(g_pGesture->recognized_palm & PalmHoldFilterMask)) || (g_pGesture->palm_filter_mask & PalmHoldFilterMask)
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "zoom_enabled: %d\n", g_pGesture->zoom_enabled);
		XDBG_DEBUG(MGEST, "type(%d) recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", type, g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
		XDBG_DEBUG(MGEST, "type(%d) recognized_gesture= 0x%x, filter_mask= 0x%x\n", type, g_pGesture->recognized_gesture, g_pGesture->filter_mask);
#endif
		goto hold_failed;
	}
	if( timer_expired && !pPalm->palmflag)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "No Palm is comming \n");
#endif
		if( (pPalm->biggest_wmajor > PALM_HOLD_FINGER_MIN_WIDTH_MAJOR) && (max_num_finger >= PALM_HOLD_MIN_FINGER) )
		{
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "It seemed palm hold (biggest_wmajor: %.f max_num_finger: %d) \n", pPalm->biggest_wmajor, max_num_finger);
#endif
			pPalm->palmflag = 1;
		}
		else
		{
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "No Palm and No Hold \n");
#endif
			goto hold_failed;
		}
	}

	if( timer_expired && pPalm->palmflag)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "state: %d\n", state);
#endif
		if(max_num_finger < PALM_HOLD_MIN_FINGER)
		{
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "palm hold false : max_num_finger(%d) < %d\n", max_num_finger, PALM_HOLD_MIN_FINGER);
#endif
			goto hold_failed;
		}
		if( state <= GestureBegin )
			state++;

		switch( state )
		{
			case GestureBegin:
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "HOLD Begin !(state=%d)\n", state);
#endif
				break;

			case GestureUpdate:
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "HOLD Update !(state=%d)\n", state);
#endif
				break;
		}

		if(base_width_size < PALM_HOLD_MIN_BASE_WIDTH)
		{
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[P] No Hold base_width_size: %d < %d\n", base_width_size, PALM_HOLD_MIN_BASE_WIDTH);
#endif
			goto hold_failed;
		}

		if( GestureHasFingerEventMask(GestureNotifyHold, 0) )
		{
			if(state == GestureBegin)
			{
				hold_occured = 1;
			}
			GestureHandleGesture_Hold(0, cx, cy, GetTimeInMillis()-base_time, state);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[T] create hold timer @ timer expired\n");
#endif
			palm_hold_event_timer = TimerSet(palm_hold_event_timer, 0, PALM_HOLD_UPDATE_THRESHOLD, GesturePalmEventTimerHandler, (int *)&event_type);
		}
		return;
	}

	switch( type )
	{
		case ET_ButtonPress:
			if(!is_holding)
				break;
			if(!base_time)
			{
				if(!base_time)
					base_time = GetTimeInMillis();
				event_type = GestureNotifyHold;
				TimerCancel(palm_hold_event_timer);
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[P] create hold timer @ initial time\n");
#endif
				max_num_finger=0;
				palm_hold_event_timer = TimerSet(palm_hold_event_timer, 0, PALM_HOLD_TIME_THRESHOLD, GesturePalmEventTimerHandler, (int *)&event_type);
			}
			base_width_size = AREA_WIDTH(&pPalm->area.extents);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[P] Hold base_width_size: %d\n", base_width_size);
#endif
			false_base_width_size_count = 0;
			num_pressed++;
			if(max_num_finger < num_pressed)
				max_num_finger = num_pressed;
			break;

		case ET_Motion:
			if(!num_pressed || !is_holding)
				return;

			if(state < GestureBegin)
			{
				if( !(pPalm->palmflag) && pPalm->max_palm >= PALM_FLICK_MIN_PALM)
					pPalm->palmflag = 1;
				if(base_width_size < PALM_HOLD_MIN_BASE_WIDTH)
				{
					false_base_width_size_count++;
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[M] increase false_base_width_size_count: %d!, width: %.f\n", false_base_width_size_count, base_width_size);
#endif
					if(false_base_width_size_count > PALM_HOLD_FALSE_WIDTH)
					{
#ifdef __PALM_GESTURE_LOG__
						XDBG_DEBUG(MGEST, "[M] No hold width!\n");
#endif
						goto hold_failed;
					}
				}
			}

			cx = AREA_CENTER_X(&pPalm->area.extents);
			cy = AREA_CENTER_Y(&pPalm->area.extents);
			break;

		case ET_ButtonRelease:
			if(--num_pressed < 0)
				num_pressed = 0;

			if(pPalm->cur_touched || num_pressed)
				break;
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[R] All fingers were released ! state=%d, is_holding=%d, cur_touched=%d, num_pressed=%d\n",
				state, is_holding, pPalm->cur_touched, num_pressed);
#endif
			if(!is_holding)
				goto cleanup_hold;

			if (hold_occured == 0 && state == GestureEnd)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] Fast Release..\n");
#endif
				if( (pPalm->palmflag) && (base_width_size > PALM_HOLD_MIN_BASE_WIDTH))
				{
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[R] seemed hold palm is came, base_width_size: %d..\n", base_width_size);
#endif
					if( GestureHasFingerEventMask(GestureNotifyHold, 0) )
					{
						hold_occured = 1;
						state = GestureBegin;
						GestureHandleGesture_Hold(0, cx, cy, GetTimeInMillis()-base_time, state);
						rapid_hold = 1;
						palm_hold_rapid_timer = TimerSet(palm_hold_rapid_timer, 0, PALM_HOLD_TIME_THRESHOLD, GesturePalmHoldRapidHandler,  NULL);
#ifdef __PALM_GESTURE_LOG__
						XDBG_DEBUG(MGEST, "[R] rapid hold begin !!\n");
#endif
						goto hold_failed;
					}
				}
				else if( (pPalm->biggest_wmajor > PALM_HOLD_FINGER_MIN_WIDTH_MAJOR) && (max_num_finger >= PALM_HOLD_MIN_FINGER) )
				{
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[R] seemed hold.. biggest_wmajor: %.f, max_num_finger: %d", pPalm->biggest_wmajor, max_num_finger);
#endif
					if( GestureHasFingerEventMask(GestureNotifyHold, 0) )
					{
						hold_occured = 1;
						state = GestureBegin;
						GestureHandleGesture_Hold(0, cx, cy, GetTimeInMillis()-base_time, state);
						rapid_hold = 1;
						palm_hold_rapid_timer = TimerSet(palm_hold_rapid_timer, 0, PALM_HOLD_TIME_THRESHOLD, GesturePalmHoldRapidHandler,  NULL);
#ifdef __PALM_GESTURE_LOG__
						XDBG_DEBUG(MGEST, "[R] rapid hold begin !!\n");
#endif
						goto hold_failed;
					}
				}
			}
release_hold:
			if(hold_occured == 0 && num_pressed == 0)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] release all finger and No hold base_width_size: %d, biggest_wmajor: %.f, max_num_finger: %d\n", base_width_size, pPalm->biggest_wmajor, max_num_finger);
#endif
				goto hold_failed;
			}

			if( state == GestureBegin || state == GestureUpdate )
			{
				state = GestureEnd;
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] HOLD End !(state=%d)\n", state);
#endif
				hold_occured = 0;
				//g_pGesture->recognized_palm = GESTURE_PALM_FILTER_MASK_ALL;
				if( GestureHasFingerEventMask(GestureNotifyHold, 0) )
				{
					GestureHandleGesture_Hold(0, cx, cy, GetTimeInMillis()-base_time, state);
				}
				//else
					//XDBG_DEBUG(MGEST, "[END] \n");
			}
			else
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] Gesture Hold End is already occured\n");
#endif
				goto hold_failed;
			}
			goto cleanup_hold;
	}
	return;

hold_failed:
	is_holding = 0;
	TimerCancel(palm_hold_event_timer);
	g_pGesture->palm_filter_mask |= PalmHoldFilterMask;
	if( g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL )
	{
		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
	}
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "[F] recognized_palm= 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
#endif
	goto cleanup_hold;
	return;

cleanup_hold:
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "[F] HOLD Cleanup (state: %d)\n", state);
#endif
	TimerCancel(palm_hold_event_timer);
	if(state == GestureBegin || state == GestureUpdate)
	{
		if( GestureHasFingerEventMask(GestureNotifyHold, 0) )
		{
			GestureHandleGesture_Hold(0, cx, cy, GetTimeInMillis()-base_time, GestureEnd);
		}
	}
	is_holding = 1;
	num_pressed = 0;
	false_base_width_size_count = 0;
	base_time = 0;
	cx = cy = 0;
	max_num_finger = 0;
	state = GestureEnd;
	base_width_size = 0;
	hold_occured = 0;
	rapid_hold = 0;
	return;
}

static void
GesturePalmRecognize_FlickHorizen(int type, int idx)
{
	static int curTouched = 0;
	static int num_pressed = 0;
	static int base_width_size = 0;
	static Time base_time = 0;
	static int base_x;
	static pixman_box16_t base_box_ext;
#ifdef __PALM_GESTURE_LOG__
	int i;
#endif

	int line_idx;
	static int press_idx;
	static int prev_line_idx;
	static int horiz_line[PALM_HORIZ_ARRAY_COUNT];

	Time duration;
	int distx=0, disty=0;
	int distance, direction;

	int width_size;
	static int is_flicking = 1;
	//static int is_surface = 0;
	static int pass_count = 0;
	static int base_cx=0, base_cy=0;
	static int release_flag = 0;

	PalmStatusPtr pPalm = &g_pGesture->palm;
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "num_pressed: %d, cur_touched: %d palmflag: %d\n", num_pressed, pPalm->cur_touched, pPalm->palmflag);
	XDBG_DEBUG(MGEST, "idx: %d, cx: %d, cy: %d\n", idx, pPalm->cx, pPalm->cy);
#endif

	if(g_pGesture->enqueue_fulled == 1)
	{
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "EQ Event is full.... palm recognize drop..\n");
#endif
			goto flick_failed;
	}

	if(pPalm->single_timer_expired)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "No flick single finger...expired\n");
#endif
		goto flick_failed;
	}

	if(g_pGesture->recognized_gesture || (g_pGesture->recognized_palm && !(g_pGesture->recognized_palm & PalmFlickHorizFilterMask)) || (g_pGesture->palm_filter_mask & PalmFlickHorizFilterMask)
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "zoom_enabled: %d\n", g_pGesture->zoom_enabled);
		XDBG_DEBUG(MGEST, "type(%d) recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", type, g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
		XDBG_DEBUG(MGEST, "type(%d) recognized_gesture= 0x%x, filter_mask= 0x%x\n", type, g_pGesture->recognized_gesture, g_pGesture->filter_mask);
#endif
		goto flick_failed;
	}
	switch( type )
	{
		case ET_ButtonPress:
			if(!is_flicking)
				break;
			if(!base_width_size || pPalm->cur_touched > curTouched)
			{
				if(!base_time)
				{
					base_time = GetTimeInMillis();
					base_x = AREA_CENTER_X(&pPalm->area.extents);
					line_idx = GesturePalmGetHorizIndexWithX(base_x, type);
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[P] cx: %d, cy: %d, x1: %d, x2: %d, y1: %d, y2: %d, line_idx: %d\n", pPalm->cx, pPalm->cy, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2, line_idx);
#endif
					if(line_idx < 0)
					{
#ifdef __PALM_GESTURE_LOG__
						XDBG_DEBUG(MGEST, "[P] line_idx is invalid.. base_x: %d, line_idx: %d\n", base_x, line_idx);
#endif
						goto flick_failed;
					}

					horiz_line[line_idx]++;
					pass_count++;
					press_idx = prev_line_idx = line_idx;
					release_flag = 0;
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[P] Base_width=%d, base_x=%d, line_idx=%d\n", base_width_size, base_x, line_idx);
#endif
				}

				base_width_size = AREA_WIDTH(&pPalm->area.extents);
				if(base_width_size > PALM_FLICK_HORIZ_MAX_BASE_WIDTH)
				{
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[P] No flickBase_width=%d > MAX_WIDTH\n", base_width_size, PALM_FLICK_HORIZ_MAX_BASE_WIDTH);
#endif
					goto flick_failed;
				}
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[P] Base_width=%d, cur_touched=%d\n", base_width_size, pPalm->cur_touched);
#endif
				if(pPalm->max_touched == 1)
				{
					base_cx = AREA_CENTER_X(&pPalm->area.extents);
					base_cy = AREA_CENTER_Y(&pPalm->area.extents);
					base_box_ext.x1 = base_cx-HOLD_MOVE_THRESHOLD;
					base_box_ext.y1 = base_cy-HOLD_MOVE_THRESHOLD;
					base_box_ext.x2 = base_cx+HOLD_MOVE_THRESHOLD;
					base_box_ext.y2 = base_cy+HOLD_MOVE_THRESHOLD;
				}
			}
			curTouched = pPalm->cur_touched;
			num_pressed++;
			break;

		case ET_Motion:
			if(!num_pressed || !is_flicking)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] num_pressed: %d, is_flicking: %d\n", num_pressed, is_flicking);
#endif
				break;
			}

			distx = AREA_CENTER_X(&pPalm->area.extents);
			disty = AREA_CENTER_Y(&pPalm->area.extents);
			line_idx = GesturePalmGetHorizIndexWithX(distx, type);
#ifdef __PALM_GESTURE_LOG__
			for(i=0; i<PALM_HORIZ_ARRAY_COUNT; i++)
			{
				XDBG_DEBUG(MGEST, "M] %d: %d\n", i, horiz_line[i]);
			}
			XDBG_DEBUG(MGEST, "[M] distx: %d, line_idx: %d, prev_line_idx: %d! pass_count: %d\n", distx, line_idx, prev_line_idx, pass_count);
#endif

			if(line_idx < 0)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] line_idx is invalid.. base_x: %d, line_idx: %d\n", base_x, line_idx);
#endif
				goto flick_failed;
			}

			if(pPalm->max_touched == 1)
			{
				if(ABS(disty - base_cy) > PALM_FLICK_HORIZ_MAX_MOVE_Y)
				{
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[M] No flick ! (move too long toward y coordination %d(%d - %d) > %d\n", ABS(disty - base_cy), disty, base_cy, PALM_FLICK_HORIZ_MAX_MOVE_Y);
#endif
					goto flick_failed;
				}
			}

			if(prev_line_idx != line_idx)
			{
				horiz_line[line_idx]++;
				if(horiz_line[line_idx] > 2)
					goto flick_failed;
				pass_count++;
			}
			if(pass_count > 6)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(visit same place twice !)\n");
#endif
				goto flick_failed;
			}
#if 0
			if((prev_line_idx != line_idx) && horiz_line[line_idx] && !release_flag)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(visit same place twice !)\n");
#endif
				goto flick_failed;
			}
#endif
			prev_line_idx = line_idx;

			width_size = AREA_WIDTH(&pPalm->area.extents);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[M] Base_width=%d, Current_width=%d, diff=%d\n", base_width_size, width_size, ABS(width_size - base_width_size));
#endif
			duration = GetTimeInMillis() - base_time;
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[M] duration =%d !\n", duration);
#endif
			if(!pPalm->palmflag && (duration >= PALM_FLICK_INITIAL_TIMEOUT))
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(initial flick timeout : duration=%d)\n", duration);
#endif
				goto flick_failed;
			}
			if( (duration >= PALM_FLICK_INITIAL_TIMEOUT) && (INBOX(&base_box_ext, distx, disty)) )
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(move too short !in duration: %d)\n", duration);
#endif
				goto flick_failed;
			}
			if( (duration >= PALM_FLICK_FALSE_TIMEOUT) && (pPalm->biggest_tmajor < PALM_FLICK_TOUCH_MAJOR) )
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(press touch major(%.f < %.f) is little in duration(%d))\n", pPalm->biggest_tmajor, PALM_FLICK_TOUCH_MAJOR, duration);
#endif
				goto flick_failed;
			}

			if(duration >= PALM_FLICK_DETECT_TIMEOUT)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(flick detection timeout : duration=%d)\n", duration);
#endif
				goto flick_failed;
			}
			break;

		case ET_ButtonRelease:
			release_flag = 1;
			if(--num_pressed < 0)
				num_pressed = 0;
			base_width_size = AREA_WIDTH(&pPalm->area.extents);
			if(num_pressed)
				break;
			if(!pPalm->palmflag)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick (No palm comming and all finger release))\n");
#endif
				goto flick_failed;
			}

			if(!is_flicking)
				goto cleanup_flick;

			duration = GetTimeInMillis() - base_time;
			distx = AREA_CENTER_X(&pPalm->area.extents);
			line_idx = GesturePalmGetHorizIndexWithX(distx, type);

			if(line_idx < 0)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick (distx: %d, line_idx: %d))\n", distx, line_idx);
#endif
				goto flick_failed;
			}
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[R] duration=%d, distx=%d\n", duration, distx);
#endif
			if(duration >= PALM_FLICK_DETECT_TIMEOUT)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick !(flick detection timeout : duration=%d)\n", duration);
#endif
				goto flick_failed;
			}
			if(pass_count < PALM_HORIZ_ARRAY_COUNT - 1)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick !(flick distance is short!\n");
#endif
				goto flick_failed;
			}
			if(pPalm->biggest_tmajor < PALM_FLICK_TOUCH_MAJOR)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick !(touch major(%.f < %d) is small...)\n", pPalm->biggest_tmajor, PALM_FLICK_TOUCH_MAJOR);
#endif
				goto flick_failed;
			}

			direction = (line_idx <= 1) ? FLICK_EASTWARD : FLICK_WESTWARD;
			distance = ABS(distx - base_x);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[R] Palm Flick !!!, direction=%d, distance=%d\n", direction, distance);
#endif
			if( GestureHasFingerEventMask(GestureNotifyFlick, 0) )
				GestureHandleGesture_Flick(0, distance, duration, direction);
			goto cleanup_flick;
			break;
	}

	return;

flick_failed:
	is_flicking = 0;
	g_pGesture->recognized_palm &= ~PalmFlickHorizFilterMask;
	g_pGesture->palm_filter_mask |= PalmFlickHorizFilterMask;
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "[Failed] recognized_palm= 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
#endif
	goto cleanup_flick;
	return;

cleanup_flick:
	num_pressed = 0;
	is_flicking = 1;
	base_width_size = 0;
	//is_surface = 0;
	base_time = 0;
	curTouched = 0;
	pass_count = 0;
	prev_line_idx = 0;
	release_flag = 0;
	base_cx = base_cy = 0;
	base_box_ext.x1 = base_box_ext.x2 = base_box_ext.y1 = base_box_ext.y2 = 0;
	memset(&horiz_line, 0L, PALM_HORIZ_ARRAY_COUNT * sizeof(int));
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "[cleanup_flick] base_width_size=%d, curTouched=%d\n", base_width_size, curTouched);
#endif
	return;
}

static void
GesturePalmRecognize_FlickVertical(int type,int idx)
{
	static int curTouched = 0;
	static int num_pressed = 0;
	static int base_height_size = 0;
	static Time base_time = 0;
	static int base_y;
	static int base_cx, base_cy;
	static int pass_count = 0;
	static int release_flag = 0;
	static pixman_box16_t base_box_ext;

	int line_idx;
	static int press_idx;
	static int prev_line_idx;
	static int verti_line[PALM_VERTI_ARRAY_COUNT];

	Time duration;
	int disty;
	int distx;
	int distance, direction;

	int height_size;
	static int is_flicking = 1;
	static int false_base_height_size = 0;

	PalmStatusPtr pPalm = &g_pGesture->palm;
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "num_pressed: %d, cur_touched: %d, palm_flag: %d, single_timer_expired: %d\n", num_pressed, pPalm->cur_touched, pPalm->palmflag, pPalm->single_timer_expired);
#endif
	if(g_pGesture->enqueue_fulled == 1)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "EQ Event is full.... palm recognize drop..\n");
#endif
		goto flick_failed;
	}


	if(g_pGesture->recognized_gesture || (g_pGesture->recognized_palm && !(g_pGesture->recognized_palm & PalmFlickVertiFilterMask)) || (g_pGesture->palm_filter_mask & PalmFlickVertiFilterMask)
#ifdef _F_SUPPORT_BEZEL_FLICK_
		|| g_pGesture->bezel_recognized_mask)
#else
		)
#endif
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "zoom_enabled: %d\n", g_pGesture->zoom_enabled);
		XDBG_DEBUG(MGEST, "type(%d) recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", type, g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
		XDBG_DEBUG(MGEST, "type(%d) recognized_gesture= 0x%x, filter_mask= 0x%x\n", type, g_pGesture->recognized_gesture, g_pGesture->filter_mask);
#endif
		goto flick_failed;
	}

	if(pPalm->single_timer_expired)
	{
#ifdef __PALM_GESTURE_LOG__
		XDBG_DEBUG(MGEST, "No flick single finger...expired\n");
#endif
		goto flick_failed;
	}

	switch( type )
	{
		case ET_ButtonPress:
			if(!is_flicking)
				break;
			if(!base_height_size || pPalm->cur_touched > curTouched)
			{
				if(!base_time)
				{
					base_time = GetTimeInMillis();
					base_y = AREA_CENTER_Y(&pPalm->area.extents);
					line_idx = GesturePalmGetVertiIndexWithY(base_y, type);

#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[P] Base_height=%d, base_y=%d, line_idx=%d\n", base_height_size, base_y, line_idx);
#endif
					if(line_idx < 0)
						goto flick_failed;

					verti_line[line_idx] = 1;
					pass_count++;
					press_idx = prev_line_idx = line_idx;
					release_flag = 0;
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[P] Base_height=%d, base_y=%d, line_idx=%d\n", base_height_size, base_y, line_idx);
#endif
				}

				base_height_size = AREA_HEIGHT(&pPalm->area.extents);
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[P] Base_height=%d, cur_touched=%d\n", base_height_size, pPalm->cur_touched);
#endif
				if(base_height_size > PALM_FLICK_VERTI_MAX_BASE_WIDTH)
				{
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[P] No flickBase_width=%d > MAX_WIDTH\n", base_height_size, PALM_FLICK_VERTI_MAX_BASE_WIDTH);
#endif
					goto flick_failed;
				}
				false_base_height_size = 0;
				if(pPalm->max_touched == 1)
				{
					base_cx = AREA_CENTER_X(&pPalm->area.extents);
					base_cy = AREA_CENTER_Y(&pPalm->area.extents);
					base_box_ext.x1 = base_cx-HOLD_MOVE_THRESHOLD;
					base_box_ext.y1 = base_cy-HOLD_MOVE_THRESHOLD;
					base_box_ext.x2 = base_cx+HOLD_MOVE_THRESHOLD;
					base_box_ext.y2 = base_cy+HOLD_MOVE_THRESHOLD;
				}
			}

			curTouched = pPalm->cur_touched;
			num_pressed++;
			break;

		case ET_Motion:
			if(!num_pressed|| !is_flicking)
				break;

			distx = AREA_CENTER_X(&pPalm->area.extents);
			disty = AREA_CENTER_Y(&pPalm->area.extents);
			line_idx = GesturePalmGetVertiIndexWithY(disty, type);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[M] line_idx: %d\n", line_idx);
#endif

			if(line_idx < 0)
				goto flick_failed;
			if((prev_line_idx != line_idx) && verti_line[line_idx] && !release_flag)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(visit same place twice !)\n");
#endif
				goto flick_failed;
			}

			if(pPalm->max_touched == 1)
			{
				if(ABS(AREA_CENTER_X(&pPalm->area.extents) - base_cx) > PALM_FLICK_VERTI_MAX_MOVE_X)
				{
#ifdef __PALM_GESTURE_LOG__
					XDBG_DEBUG(MGEST, "[M] No flick ! (move too long toward x coordination %d(%d - %d) > PALM_FLICK_VERTI_MAX_MOVE_X\n", ABS(AREA_CENTER_X(&pPalm->area.extents) - base_cx), AREA_CENTER_X(&pPalm->area.extents) , base_cx, PALM_FLICK_VERTI_MAX_MOVE_X);
#endif
					goto flick_failed;
				}
			}

			verti_line[line_idx] = 1;
			if(prev_line_idx != line_idx)
				pass_count++;
			prev_line_idx = line_idx;

			height_size = AREA_HEIGHT(&pPalm->area.extents);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[M] Base_height=%d, Current_height=%d, diff=%d\n", base_height_size, height_size, ABS(height_size - base_height_size));
#endif

			duration = GetTimeInMillis() - base_time;
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[M] duration =%d !\n", duration);
#endif

			if(!pPalm->palmflag && (duration >= PALM_FLICK_INITIAL_TIMEOUT))
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(initial flick timeout : duration=%d)\n", duration);
#endif
				goto flick_failed;
			}
			if( (duration >= PALM_FLICK_INITIAL_TIMEOUT) && (INBOX(&base_box_ext, distx, disty)) )
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(move too short !in duration: %d)\n", duration);
#endif
				goto flick_failed;
			}

			if(duration >= PALM_FLICK_DETECT_TIMEOUT)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[M] No flick !(flick detection timeout : duration=%d)\n", duration);
#endif
				goto flick_failed;
			}
			break;

		case ET_ButtonRelease:
			release_flag = 1;
			if(pPalm->cur_touched)
				break;

			if(--num_pressed < 0)
				num_pressed = 0;
			if(num_pressed)
				break;
			if(!pPalm->palmflag)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] num_pressed is 0\n");
#endif
				goto flick_failed;
			}

			if(!is_flicking)
				goto cleanup_flick;

			duration = GetTimeInMillis() - base_time;
			disty = AREA_CENTER_Y(&pPalm->area.extents);
			line_idx = GesturePalmGetVertiIndexWithY(disty, type);
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[R] line_idx: %d\n", line_idx);
#endif

			if(line_idx < 0)
				goto flick_failed;
#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[R] duration=%d, disty=%d\n", duration, disty);
#endif

			if(duration >= PALM_FLICK_DETECT_TIMEOUT)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick !(flick detection timeout : duration=%d)\n", duration);
#endif
				goto flick_failed;
			}
			if(pass_count < PALM_VERTI_ARRAY_COUNT -1)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick !(flick distance is short!)\n");
#endif
				goto flick_failed;
			}
			if(pPalm->biggest_tmajor < PALM_FLICK_TOUCH_MAJOR)
			{
#ifdef __PALM_GESTURE_LOG__
				XDBG_DEBUG(MGEST, "[R] No flick !(flick touch major(%.f < %d) is small!)\n", pPalm->biggest_tmajor, PALM_FLICK_TOUCH_MAJOR);
#endif
				goto flick_failed;
			}
			direction = (line_idx <= 1) ? FLICK_SOUTHWARD : FLICK_NORTHWARD;
			distance = ABS(disty - base_y);

#ifdef __PALM_GESTURE_LOG__
			XDBG_DEBUG(MGEST, "[R] Palm Flick !!!, direction=%d, distance=%d\n", direction, distance);
#endif

			if( GestureHasFingerEventMask(GestureNotifyFlick, 0) )
				GestureHandleGesture_Flick(0, distance, duration, direction);
			goto cleanup_flick;
			break;
	}

	return;

flick_failed:
	is_flicking = 0;
	g_pGesture->recognized_palm &= ~PalmFlickVertiFilterMask;
	g_pGesture->palm_filter_mask |= PalmFlickVertiFilterMask;
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "[Fail] recognized_palm= 0x%x, palm_filter_mask= 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
#endif
	goto cleanup_flick;
	return;

cleanup_flick:
	num_pressed = 0;
	is_flicking = 1;
	base_height_size = 0;
	base_time = 0;
	curTouched = 0;
	pass_count = 0;
	false_base_height_size = 0;
	prev_line_idx = 0;
	release_flag = 0;
	base_cx = base_cy = 0;
	base_box_ext.x1 = base_box_ext.x2 = base_box_ext.y1 = base_box_ext.y2 = 0;
	memset(&verti_line, 0L, PALM_VERTI_ARRAY_COUNT * sizeof(int));
#ifdef __PALM_GESTURE_LOG__
	XDBG_DEBUG(MGEST, "[cleanup_flick] base_height_size=%d, curTouched=%d\n", base_height_size, curTouched);
#endif

	return;
}


static int
GesturePalmGetScreenInfo()
{
	int i;
	pixman_region16_t tarea;
	PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;
	ScreenPtr pScreen = miPointerCurrentScreen();

	if(!pScreen)
	{
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "Failed to get screen information !\n");
#endif
		pPalmMisc->scrn_width = pPalmMisc->scrn_height = 0;
		return 0;
	}

	pPalmMisc->scrn_width = pScreen->width;
	pPalmMisc->scrn_height = pScreen->height;
	pixman_region_init(&tarea);
	pixman_region_init_rect(&tarea, 0, 0, pPalmMisc->scrn_width, pPalmMisc->scrn_height);
	pPalmMisc->half_scrn_area_size = AREA_SIZE(&tarea.extents);
	pPalmMisc->half_scrn_area_size = (unsigned int)((double)pPalmMisc->half_scrn_area_size / 2);
#ifdef __HOLD_DETECTOR_DEBUG__
	XDBG_DEBUG(MGEST, "pPalmMisc->half_scrn_area_size = %d\n", pPalmMisc->half_scrn_area_size);
#endif//__HOLD_DETECTOR_DEBUG__

	for(i = 0 ; i < PALM_HORIZ_ARRAY_COUNT ; i++)
	{
		pPalmMisc->horiz_coord[i] = pPalmMisc->scrn_width * ((i+1)/(double)PALM_HORIZ_ARRAY_COUNT);
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "pPalmMisc->horiz_coord[%d]=%d, pPalmMisc->scrn_width=%d\n", i, pPalmMisc->horiz_coord[i], pPalmMisc->scrn_width);
#endif
	}
	for(i = 0 ; i < PALM_VERTI_ARRAY_COUNT ; i++)
	{
		pPalmMisc->verti_coord[i] = pPalmMisc->scrn_height * ((i+1)/(double)PALM_VERTI_ARRAY_COUNT);
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "pPalmMisc->verti_coord[%d]=%d, pPalmMisc->scrn_height=%d\n", i, pPalmMisc->verti_coord[i], pPalmMisc->scrn_height);
#endif
	}

	return 1;
}

static int
GesturePalmGetAbsAxisInfo(DeviceIntPtr dev)
{
	int i, found = 0;
	int numAxes;
	PalmStatusPtr pPalm;

	Atom atom_wmajor;
	Atom atom_tmajor;
	Atom atom_tminor;
	Atom atom_tangle;
	Atom atom_tpalm;
	Atom atom_mt_px;
	Atom atom_mt_py;

	Atom atom_px;
	Atom atom_py;
	Atom atom_mt_slot;
	Atom atom_tracking_id;
	Atom atom_distance;

	g_pGesture->wmajor_idx = -1;
	g_pGesture->tmajor_idx = -1;
	g_pGesture->tminor_idx = -1;
	g_pGesture->tangle_idx = -1;
	g_pGesture->tpalm_idx = -1;
	g_pGesture->mt_px_idx = -1;
	g_pGesture->mt_py_idx = -1;

	memset(&g_pGesture->palm, 0, sizeof(PalmStatus));

	if (!dev || !dev->valuator)
		goto out;

	numAxes = dev->valuator->numAxes;
	atom_mt_px = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_POSITION_X);
	atom_mt_py = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_POSITION_Y);
	atom_wmajor = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_WIDTH_MAJOR);
	atom_tmajor = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_TOUCH_MAJOR);
	atom_tminor = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_TOUCH_MINOR);
	atom_tangle = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_ANGLE);
	atom_tpalm = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_PALM);

	atom_px = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_X);
	atom_py = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_Y);
	atom_mt_slot = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_SLOT);
	atom_tracking_id = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_TRACKING_ID);
	atom_distance = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_DISTANCE);

	if (!numAxes || !atom_mt_px || !atom_mt_py || !atom_tmajor || !atom_tminor  || !atom_tangle || !atom_tpalm)
	{
		XDBG_WARNING(MGEST, "one or more axes is/are not supported!\n");
		goto out;
	}

	for( i = 0 ; i < numAxes ; i++ )
	{
		AxisInfoPtr axes = &dev->valuator->axes[i];

		if (!axes || (axes->mode != Absolute))
			continue;

		if ( axes->label == atom_mt_px )
		{
			g_pGesture->mt_px_idx = i;
			found += 1;
		}
		else if ( axes->label == atom_mt_py )
		{
			g_pGesture->mt_py_idx = i;
			found += 2;
		}
		else if ( axes->label == atom_wmajor )
		{
			g_pGesture->wmajor_idx = i;
			found += 3;
		}
		else if ( axes->label == atom_tmajor )
		{
			g_pGesture->tmajor_idx = i;
			found += 4;
		}
		else if ( axes->label == atom_tminor )
		{
			g_pGesture->tminor_idx = i;
			found += 5;
		}
		else if ( axes->label == atom_tangle )
		{
			g_pGesture->tangle_idx = i;
			found += 6;
		}
		else if ( axes->label == atom_tpalm )
		{
			g_pGesture->tpalm_idx = i;
			found += 7;
		}
	}

	if (found != 28)
	{
		XDBG_WARNING(MGEST, "Axes for palm recognization are not supported !\n");
		goto out;
	}

	pPalm = &g_pGesture->palm;
	pixman_region_init(&pPalm->area);

	for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
	{
		pixman_region_init_rect (&pPalm->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
	}

	g_pGesture->palm_misc.enabled = 1;
	XDBG_INFO(MGEST, "Axes for palm recognization are supported !\n");
	return 1;

out:
	g_pGesture->palm_misc.enabled = 0;
	XDBG_INFO(MGEST, "Palm recognization is not supported !\n");
	return 0;
}

static void
GesturePalmDataUpdate(int idx, int type, InternalEvent *ev, DeviceIntPtr device)
{
	int wmajor_idx = g_pGesture->wmajor_idx;
	int tmajor_idx = g_pGesture->tmajor_idx;
	int tminor_idx = g_pGesture->tminor_idx;
	int tangle_idx = g_pGesture->tangle_idx;
	int tpalm_idx = g_pGesture->tpalm_idx;
	int px_idx = g_pGesture->mt_px_idx;
	int py_idx= g_pGesture->mt_py_idx;

	double width_major = 0.0f;
	double touch_major = 0.0f;
	double touch_minor = 0.0f;
	double touch_angle = 0.0f;
	double touch_palm = 0.0f;
	double max_width = -1.0f;

	int i;
	int count;
	double meanX = 0.0f;
	double meanY = 0.0f;
	double tmpXp = 0.0f;
	double tmpYp = 0.0f;

	PalmStatusPtr pPalm = &g_pGesture->palm;
	DeviceEvent *de = &ev->device_event;
	pPalm->cx = 0;
	pPalm->cy = 0;

	if (!de || !de->valuators.data)
	{
		XDBG_WARNING(MGEST, "de or de->valuators.data are NULL !\n");
		return;
	}

	if ((wmajor_idx < 0) || (tmajor_idx < 0) || (tminor_idx < 0) || (tangle_idx < 0) || (tpalm_idx < 0) || (px_idx < 0) || (py_idx < 0))
	{
		XDBG_WARNING(MGEST, "One or more of axes are not supported !\n");
		return;
	}

	width_major = de->valuators.data[wmajor_idx];
	touch_major = de->valuators.data[tmajor_idx];
	touch_minor = de->valuators.data[tminor_idx];
	touch_angle = de->valuators.data[tangle_idx];
	touch_palm = de->valuators.data[tpalm_idx];
	if( !(g_pGesture->palm.palmflag) && pPalm->max_palm >= PALM_FLICK_MIN_PALM)
	{
		g_pGesture->palm.palmflag = 1;
		g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
	}

#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "[idx:%d, devid:%d, type:%d] width_major=%.f, touch_major=%.f, touch_minor=%.f, touch_palm=%.f \n", idx, de->deviceid, type, width_major, touch_major, touch_minor, touch_palm);
	XDBG_DEBUG(MGEST, "[%d]: touch_status: %d, x: %d, y: %d  (cur_touched: %d);\n", idx, pPalm->pti[idx].touch_status, pPalm->pti[idx].x, pPalm->pti[idx].y, pPalm->cur_touched);
#endif

	switch(type)
	{
		case ET_ButtonPress:
			if (!pPalm->pti[idx].touch_status)
			{
				pPalm->cur_touched++;
				pPalm->pti[idx].touch_status = 1;
			}

			pPalm->pti[idx].x = de->root_x;
			pPalm->pti[idx].y = de->root_y;
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[idx:%d(devid:%d)][PRESS] x=%d, y=%d, wmajor=%.f, tmajor=%.f, tminor=%.f, tangle=%.f, tpalm=%.f\n", idx, device->id, pPalm->pti[idx].x, pPalm->pti[idx].y, pPalm->pti[idx].wmajor, pPalm->pti[idx].tmajor, pPalm->pti[idx].tminor, pPalm->pti[idx].tangle, pPalm->pti[idx].tpalm);
#endif
			break;

		case ET_ButtonRelease:
			if (pPalm->pti[idx].touch_status)
			{
				--pPalm->cur_touched;
				if (pPalm->cur_touched < 0)
					pPalm->cur_touched = 0;
			}

			pPalm->pti[idx].touch_status = 2;

			pPalm->pti[idx].x = de->root_x;
			pPalm->pti[idx].y = de->root_y;

			pPalm->pti[idx].tangle = 0.0f;
			pPalm->pti[idx].wmajor = 0.0f;
			pPalm->pti[idx].tmajor = 0.0f;
			pPalm->pti[idx].tminor = 0.0f;
			pPalm->pti[idx].tpalm = 0.0f;
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[idx:%d(devid:%d)][RELEASE] x=%d, y=%d, wmajor=%.f, tmajor=%.f, tminor=%.f, tangle=%.f, tpalm=%.f\n", idx, device->id, pPalm->pti[idx].x, pPalm->pti[idx].y, pPalm->pti[idx].wmajor, pPalm->pti[idx].tmajor, pPalm->pti[idx].tminor, pPalm->pti[idx].tangle, pPalm->pti[idx].tpalm);
#endif
			break;

		case ET_Motion:
			pPalm->pti[idx].x = de->root_x;
			pPalm->pti[idx].y = de->root_y;

			pPalm->pti[idx].tmajor = touch_major;
			pPalm->pti[idx].tangle = touch_angle;
			pPalm->pti[idx].wmajor = width_major;
			pPalm->pti[idx].tminor = touch_minor;
			pPalm->pti[idx].tpalm = touch_palm;

			if (!pPalm->pti[idx].touch_status || (pPalm->pti[idx].tmajor == 0))
				return;
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[idx:%d(devid:%d)][MOVE] x=%d, y=%d, wmajor=%.f, tmajor=%.f, tminor=%.f, tangle=%.f, tpalm=%.f\n", idx, device->id, pPalm->pti[idx].x, pPalm->pti[idx].y, pPalm->pti[idx].wmajor, pPalm->pti[idx].tmajor, pPalm->pti[idx].tminor, pPalm->pti[idx].tangle, pPalm->pti[idx].tpalm);
#endif
			break;
	}

	pPalm->sum_size = 0.0f;
	pPalm->max_wmajor = -1.0f;
	pPalm->max_tmajor = -1.0f;
	pPalm->max_tminor = -1.0f;
	pPalm->max_size_idx = -1;
	pPalm->max_palm = -1.0f;
	max_width = -1.0f;

	for( count = 0, i = 0 ; i < g_pGesture->num_mt_devices ; i++ )
	{
		if (!pPalm->pti[i].touch_status)
			continue;
		if(pPalm->pti[i].touch_status == 2)
			pPalm->pti[i].touch_status =0;
		count++;
		meanX += pPalm->pti[i].x;
		meanY += pPalm->pti[i].y;
		pPalm->sum_size += pPalm->pti[i].wmajor;
		if(max_width < pPalm->pti[i].wmajor)
		{
			pPalm->max_size_idx = i;
		}
		if (pPalm->max_wmajor < pPalm->pti[i].wmajor)
		{
			pPalm->max_wmajor = pPalm->pti[i].wmajor;
		}
		if(pPalm->max_tmajor < pPalm->pti[i].tmajor)
			pPalm->max_tmajor = pPalm->pti[i].tmajor;
		if(pPalm->max_tminor < pPalm->pti[i].tminor)
			pPalm->max_tminor = pPalm->pti[i].tminor;
		if(pPalm->max_palm < pPalm->pti[i].tpalm)
		{
			pPalm->max_palm = (int)pPalm->pti[i].tpalm;
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "max_palm=%d pPalm->pti[%d].tpalm: %.f\n", pPalm->max_palm, i, pPalm->pti[i].tpalm);
#endif
		}
	}

	if (pPalm->max_size_idx < 0)
	{
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "Failed to get sum_size !\n");
#endif
		//meanX = 0;
		//meanY = 0;
		pPalm->dispersionX = 0.0f;
		pPalm->deviationX = 0.0f;
		pPalm->dispersionY= 0.0f;
		pPalm->deviationY = 0.0f;
		pPalm->max_eccen = 0.0f;
		pPalm->max_angle = 0.0f;
	}
	else
	{
		meanX /= count;
		meanY /= count;
		pPalm->cx = meanX;
		pPalm->cy = meanY;

		for( i = 0 ; i < g_pGesture->num_mt_devices ; i++ )
		{
			if (!pPalm->pti[i].touch_status)
				continue;

			tmpXp += (pPalm->pti[i].x - meanX)*(pPalm->pti[i].x - meanX);
			tmpYp += (pPalm->pti[i].y - meanY)*(pPalm->pti[i].y - meanY);
		}

		pPalm->dispersionX = tmpXp / count;
		pPalm->deviationX = sqrt(pPalm->dispersionX);
		pPalm->dispersionY = tmpYp / count;
		pPalm->deviationY = sqrt(pPalm->dispersionY);
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "meanX=%.f, meanY=%.f, count=%d, tmpXp=%.f, tmpYp=%.f\n", meanX, meanY, count, tmpXp, tmpYp);
#endif

		pPalm->max_eccen = pPalm->max_tmajor/ pPalm->max_tminor;
		pPalm->max_angle = pPalm->pti[pPalm->max_size_idx].tangle;
	}
	if(pPalm->palmflag)
	{
		TimerCancel(pPalm->palm_single_finger_timer);
		pPalm->single_timer_expired = 0;
	}
	if(pPalm->biggest_tmajor < pPalm->max_tmajor)
		pPalm->biggest_tmajor = pPalm->max_tmajor;
	if(pPalm->biggest_wmajor < pPalm->max_wmajor)
		pPalm->biggest_wmajor = pPalm->max_wmajor;
	if(pPalm->bigger_wmajor < pPalm->max_wmajor)
		pPalm->bigger_wmajor = pPalm->max_wmajor;
#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "[maxidx:%d] cur_touched=%d, palmflag=%d, sum_size=%.f, max_wmajor=%.f, max_eccen=%.1f, max_angle=%.f\n",	pPalm->max_size_idx, pPalm->cur_touched, pPalm->palmflag, pPalm->sum_size, pPalm->max_wmajor, pPalm->max_eccen, pPalm->max_angle);
	XDBG_DEBUG(MGEST, "sum_size=%.f, max_tmajor=%.f, dispersionX=%.f, deviationX=%.f, dispersionY=%.f, deviationY=%.f\n", pPalm->sum_size, pPalm->max_tmajor, pPalm->dispersionX, pPalm->deviationX, pPalm->dispersionY, pPalm->deviationY);
	XDBG_DEBUG(MGEST, "max_palm=%d\n", pPalm->max_palm);
#endif
}

static void
GesturePalmUpdateAreaInfo(int type, int idx)
{
	int i;
	PalmStatusPtr pPalm = &g_pGesture->palm;

	switch(type)
	{
		case ET_ButtonPress:
			pPalm->finger_rects[idx].extents.x1 = pPalm->pti[idx].x - FINGER_WIDTH;
			pPalm->finger_rects[idx].extents.x2 = pPalm->pti[idx].x + FINGER_WIDTH;
			pPalm->finger_rects[idx].extents.y1 = pPalm->pti[idx].y - FINGER_HEIGHT;
			pPalm->finger_rects[idx].extents.y2 = pPalm->pti[idx].y + FINGER_HEIGHT;
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[P] [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->finger_rects[idx].extents.x1, pPalm->finger_rects[idx].extents.x2, pPalm->finger_rects[idx].extents.y1, pPalm->finger_rects[idx].extents.y2);
			XDBG_DEBUG(MGEST, "[P] area [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif// __PALM_DETAIL_LOG__

		if(pPalm->cur_touched == 1)
			{
				pixman_region_union(&pPalm->area, &pPalm->finger_rects[idx], &pPalm->finger_rects[idx]);
#ifdef __PALM_DETAIL_LOG__
				XDBG_DEBUG(MGEST, "[P] cur:1 [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif//__PALM_DETAIL_LOG__
			}
			else
			{
				pixman_region_union(&pPalm->area, &pPalm->finger_rects[idx], &pPalm->finger_rects[idx]);
				for(i = 0 ; i < g_pGesture->num_mt_devices ; i++)
				{
					if(!pPalm->pti[i].touch_status)
						continue;

					pixman_region_union(&pPalm->area, &pPalm->area, &pPalm->finger_rects[i]);
#ifdef __PALM_DETAIL_LOG__
					XDBG_DEBUG(MGEST, "[P] cur:else [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", i, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif//__PALM_DETAIL_LOG__
				}
			}
			break;

		case ET_ButtonRelease:
			break;

		case ET_Motion:
			if (!pPalm->pti[idx].touch_status || (pPalm->pti[idx].tmajor == 0))
				return;
			pPalm->finger_rects[idx].extents.x1 = pPalm->pti[idx].x - FINGER_WIDTH;
			pPalm->finger_rects[idx].extents.x2 = pPalm->pti[idx].x + FINGER_WIDTH;
			pPalm->finger_rects[idx].extents.y1 = pPalm->pti[idx].y - FINGER_HEIGHT;
			pPalm->finger_rects[idx].extents.y2 = pPalm->pti[idx].y + FINGER_HEIGHT;
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[M] [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->finger_rects[idx].extents.x1, pPalm->finger_rects[idx].extents.x2, pPalm->finger_rects[idx].extents.y1, pPalm->finger_rects[idx].extents.y2);
			XDBG_DEBUG(MGEST, "[M] area [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif//__PALM_DETAIL_LOG__
			if(pPalm->cur_touched == 1)
		{
				pixman_region_union(&pPalm->area, &pPalm->finger_rects[idx], &pPalm->finger_rects[idx]);
#ifdef __PALM_DETAIL_LOG__
				XDBG_DEBUG(MGEST, "[M] cur:1 [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif//__PALM_DETAIL_LOG__
			}
			else
			{
			pixman_region_union(&pPalm->area, &pPalm->finger_rects[idx], &pPalm->finger_rects[idx]);
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "[M] cur:else:first [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", idx, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif//__PALM_DETAIL_LOG__

				for(i = 0 ; i < g_pGesture->num_mt_devices ; i++)
				{
					if(!pPalm->pti[i].touch_status)
						continue;
					pixman_region_union(&pPalm->area, &pPalm->area, &pPalm->finger_rects[i]);
#ifdef __PALM_DETAIL_LOG__
					XDBG_DEBUG(MGEST, "[M] cur:else [%d]: x1: %d, x2: %d, y1: %d, y2: %d\n", i, pPalm->area.extents.x1, pPalm->area.extents.x2, pPalm->area.extents.y1, pPalm->area.extents.y2);
#endif//__PALM_DETAIL_LOG__
				}
			}
			break;
	}
}

void
GesturePalmRecognize(int type, InternalEvent *ev, DeviceIntPtr device)
{
	int i;
	int idx = -1;
	PalmStatusPtr pPalm = &g_pGesture->palm;
	static int calc_touched = 0;

	if( device->id < g_pGesture->first_fingerid )
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

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "[g_pGesture->num_pressed=%d]\n", g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
	if(!g_pGesture->pTempPalmWin)
	{
		g_pGesture->pTempPalmWin = GestureGetEventsWindow();
		if(!g_pGesture->pTempPalmWin)
		{
#ifdef __PALM_DETAIL_LOG__
			XDBG_DEBUG(MGEST, "No events are grabbed/selected !\n");
#endif//__PALM_DETAIL_LOG__
			g_pGesture->palm_filter_mask = GESTURE_PALM_FILTER_MASK_ALL;
			goto flush_or_drop;
		}
	}
	GesturePalmDataUpdate(idx, type, ev, device);
	GesturePalmUpdateAreaInfo(type, idx);

	switch(type)
	{
		case ET_ButtonPress:
			pPalm->max_touched++;
			if( g_pGesture->num_pressed == 1 )
			{
				pPalm->palm_single_finger_timer = TimerSet(pPalm->palm_single_finger_timer, 0, 50, GesturePalmSingleFingerTimerHandler, NULL);
			}
			else if(g_pGesture->num_pressed > 1)
			{
				TimerCancel(pPalm->palm_single_finger_timer);
			}
			break;
		case ET_Motion:
			if(pPalm->cur_touched == 0)
			{
				break;
			}
			calc_touched++;
			if(calc_touched == pPalm->cur_touched)
			{
				calc_touched = 0;
			}
			break;
		case ET_ButtonRelease:
			if(calc_touched)
			{
				calc_touched--;
			}
			break;
	}

	if( GestureHasFingerEventMask(GestureNotifyFlick, 0) )
	{
		if(!(g_pGesture->palm_filter_mask & PalmFlickHorizFilterMask))
		{
			GesturePalmRecognize_FlickHorizen(type, idx);
		}
		if(!(g_pGesture->palm_filter_mask & PalmFlickVertiFilterMask))
		{
			GesturePalmRecognize_FlickVertical(type, idx);
		}
	}
	if( GestureHasFingerEventMask(GestureNotifyHold, 0) )
	{
		if(!(g_pGesture->palm_filter_mask & PalmHoldFilterMask))
		{
			GesturePalmRecognize_Hold(type, idx, 0);
		}
	}

	if(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL)
	{
		pixman_region_init(&pPalm->area);

		for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
		{
			pixman_region_init_rect (&pPalm->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
		}
		pPalm->palmflag = 0;
	}

	switch(type)
	{
		case ET_ButtonPress:
			break;

		case ET_ButtonRelease:
			if(( g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL ) && ( pPalm->cur_touched == 0))
				goto flush_or_drop;
			break;

		case ET_Motion:
			break;
	}
#ifdef __PALM_DETAIL_LOG__
	XDBG_DEBUG(MGEST, "recognized_palm: 0x%x, palm_filter_mask: 0x%x, ehtype: %d\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask, g_pGesture->ehtype);
#endif//__PALM_DETAIL_LOG__
	if( g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL )
	{
		if( !g_pGesture->recognized_palm )
			goto flush_or_drop;
	}

	if( g_pGesture->recognized_palm )
	{
		if( g_pGesture->ehtype == KEEP_EVENTS )
			GestureEventsDrop();
		g_pGesture->ehtype = IGNORE_EVENTS;
	}

	return;

flush_or_drop:
	calc_touched = 0;
#ifdef __PALM_DETAIL_LOG__
		XDBG_DEBUG(MGEST, "GestureFlushOrDrop() !\n");
#endif//__PALM_DETAIL_LOG__
		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}

}

#ifdef _F_SUPPORT_BEZEL_FLICK_
void
GestureBezelRecognize(int type, InternalEvent *ev, DeviceIntPtr device)
{
	static OsTimerPtr bezel_finger_timer = NULL;

	BezelFlickStatusPtr pBezel = &g_pGesture->bezel;
	int direction = 0;
	int distance;
	double angle;
	static Time base_time = 0;
	int idx = -1;
	int i;
	static int px=-1, py=-1;
	static int mx=-1, my=-1;
	static int rx=-1, ry=-1;
	static int event_count=0;

	if(g_pGesture->enqueue_fulled == 1)
	{
#ifdef __BEZEL_DEBUG__
		XDBG_DEBUG(MGEST, "EQ Event is full.... palm recognize drop..\n");
#endif//__BEZEL_DEBUG__
		goto bezel_failed;
	}

	if( (PROPAGATE_EVENTS == g_pGesture->ehtype) || (device->id != g_pGesture->first_fingerid) )
		return;

	for( i = 0 ; i < g_pGesture->num_mt_devices ; i++ )
	{
		if( device->id == g_pGesture->mt_devices[i]->id )
		{
			idx = i;
			break;
		}
	}
#ifdef __BEZEL_DEBUG__
	XDBG_DEBUG(MGEST, "[type: %d][g_pGesture->num_pressed=%d, x,y(%d, %d) ]\n", type, g_pGesture->num_pressed, ev->device_event.root_x, ev->device_event.root_x);
	XDBG_DEBUG(MGEST, "[filter_mask: 0x%x, bezel_filter_mask: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->bezel_filter_mask, g_pGesture->palm_filter_mask);
#endif//__BEZEL_DEBUG__

	if (idx < 0)
		return;
	if(g_pGesture->recognized_gesture || g_pGesture->recognized_palm)
		goto bezel_failed;
	if(g_pGesture->num_pressed > 1)
	{
#ifdef __BEZEL_DEBUG__
		XDBG_DEBUG(MGEST, "Not single finger g_pGesture->num_pressed: %d\n", g_pGesture->num_pressed);
#endif//__BEZEL_DEBUG__
		goto bezel_failed;
	}
	if(pBezel->is_active == BEZEL_END)
	{
#ifdef __BEZEL_DEBUG__
		XDBG_DEBUG(MGEST, "Bezel state is END pBezel->is_active: %d\n", pBezel->is_active);
		XDBG_DEBUG(MGEST, "[filter_mask: 0x%x, bezel_filter_mask: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->bezel_filter_mask, g_pGesture->palm_filter_mask);
#endif//__BEZEL_DEBUG__
		goto bezel_failed;
	}

	switch(type)
	{
		case ET_ButtonPress:
			base_time = GetTimeInMillis();
			px = ev->device_event.root_x;
			py = ev->device_event.root_y;
#ifdef __BEZEL_DEBUG__
			XDBG_DEBUG(MGEST, "[P] pBezel->is_active: %d, g_pGesture->num_pressed: %d, idx: %d\n", pBezel->is_active, g_pGesture->num_pressed, idx);
			XDBG_DEBUG(MGEST, "[P] g_pGesture->fingers[%d].p: (%d, %d)\n", idx, px,py);
#endif//__BEZEL_DEBUG__
			if( (pBezel->is_active == BEZEL_ON) && ((g_pGesture->num_pressed == 1) && (idx == 0)) )
			{
				if( ( px < pBezel->top_left.width) && ( py < pBezel->top_left.height) )
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[P] top_left\n");
#endif//__BEZEL_DEBUG__
					pBezel->is_active = BEZEL_START;
					pBezel->bezelStatus = BEZEL_TOP_LEFT;
				}
				else if( (px > (720 - pBezel->top_right.width)) && ( py < pBezel->top_right.height) )
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[P] top_right\n");
#endif//__BEZEL_DEBUG__
					pBezel->is_active = BEZEL_START;
					pBezel->bezelStatus = BEZEL_TOP_RIGHT;
				}
				else if( (px < pBezel->bottom_left.width) && ( py > (1280 - pBezel->bottom_left.height)) )
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[P] bottom_left\n");
#endif//__BEZEL_DEBUG__
					pBezel->is_active = BEZEL_START;
					pBezel->bezelStatus = BEZEL_BOTTOM_LEFT;
				}
				else if( (px > (720 - pBezel->bottom_right.width)) && ( py > (1280 - pBezel->bottom_right.height)) )
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[P] bottom_right\n");
#endif//__BEZEL_DEBUG__
					pBezel->is_active = BEZEL_START;
					pBezel->bezelStatus = BEZEL_BOTTOM_RIGHT;
				}
				else
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[P] None\n");
#endif//__BEZEL_DEBUG__
					pBezel->bezelStatus = NO_BEZEL;
					goto bezel_failed;
				}
			}
			if(pBezel->is_active == BEZEL_START)
			{
				bezel_finger_timer = TimerSet(bezel_finger_timer, 0, 500, GestureBezelSingleFingerTimerHandler, NULL);
			}
			else
			{
				TimerCancel(bezel_finger_timer);
			}
			break;
		case ET_Motion:
			if(px <0 || py < 0)
				return;
			mx = ev->device_event.root_x;
			my = ev->device_event.root_y;
			event_count++;
			if( (g_pGesture->bezel.bezel_angle_moving_check) && (event_count >= 10))
			{
				angle = get_angle(px, py, mx, my);
				event_count = 0;
				if(!GestureBezelAngleRecognize(pBezel->bezelStatus, pBezel->flick_distance, angle))
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[M] moving limit!\n");
#endif//__BEZEL_DEBUG__
					TimerCancel(bezel_finger_timer);
					goto bezel_failed;
				}
			}
			break;
		case ET_ButtonRelease:
			rx = ev->device_event.root_x;
			ry = ev->device_event.root_y;
			if( (g_pGesture->num_pressed == 0) && (g_pGesture->inc_num_pressed == 1) && (pBezel->is_active == BEZEL_START) )
			{
				angle = get_angle(px, py, rx, ry);
				distance = get_distance(px, py, rx, ry);
				Time duration = GetTimeInMillis() - base_time;
#ifdef __BEZEL_DEBUG__
				XDBG_DEBUG(MGEST, "[R] bezelStatus: %d, distance: %d, angle: %lf\n", pBezel->bezelStatus, distance, angle);
#endif//__BEZEL_DEBUG__
				int res = GestureBezelAngleRecognize(pBezel->bezelStatus, distance, angle);
				if(res)
				{
#ifdef __BEZEL_DEBUG__
					XDBG_DEBUG(MGEST, "[R] Bezel Success\n");
#endif//__BEZEL_DEBUG__
					pBezel->is_active = BEZEL_DONE;
					g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
					g_pGesture->palm_filter_mask = GESTURE_PALM_FILTER_MASK_ALL;
					TimerCancel(bezel_finger_timer);

					if( (0.0 < angle) && (angle < RAD_90DEG))
						direction = FLICK_NORTHEASTWARD;
					else if(angle < RAD_180DEG)
						direction = FLICK_NORTHWESTWARD;

					if( GestureHasFingerEventMask(GestureNotifyFlick, 1) )
						GestureHandleGesture_Flick(1, distance, duration, direction);
					goto bezel_cleanup;
				}
#ifdef __BEZEL_DEBUG__
				XDBG_DEBUG(MGEST, "[R] Bezel failed\n");
#endif//__BEZEL_DEBUG__
				goto bezel_failed;
			}
			break;
	}
	return;
bezel_failed:
#ifdef __BEZEL_DEBUG__
	XDBG_DEBUG(MGEST, "[F] Bezel failed\n");
#endif//__BEZEL_DEBUG__
	pBezel->is_active = BEZEL_END;
	g_pGesture->bezel_filter_mask |= BezelFlickFilterMask;
	goto bezel_cleanup;
	return;
bezel_cleanup:
#ifdef __BEZEL_DEBUG__
	XDBG_DEBUG(MGEST, "[F] Bezel cleanup\n");
#endif//__BEZEL_DEBUG__
	TimerCancel(bezel_finger_timer);
	if( ERROR_INVALPTR == GestureFlushOrDrop() )
	{
		GestureControl(g_pGesture->this_device, DEVICE_OFF);
	}
	bezel_finger_timer = NULL;
	base_time = 0;
	px=py=mx=my=rx=ry=-1;
	event_count = 0;
}
#endif

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
#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "[type: %d][g_pGesture->num_pressed=%d, x,y(%d, %d) ]\n", type, g_pGesture->num_pressed, ev->device_event.root_x, ev->device_event.root_x);
	XDBG_DEBUG(MGEST, "[inc_num_pressed: %d]\n", g_pGesture->inc_num_pressed);
#ifdef _F_SUPPORT_BEZEL_FLICK_
	XDBG_DEBUG(MGEST, "[filter_mask: 0x%x, bezel_filter_mask: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->bezel_filter_mask, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "[recognized_gesture: 0x%x, bezel_recognize_mask: 0x%x, recognized_palm: 0x%x\n", g_pGesture->recognized_gesture, g_pGesture->bezel_recognized_mask, g_pGesture->recognized_palm);
#else
	XDBG_DEBUG(MGEST, "[filter_mask: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "[recognized_gesture: 0x%x, recognized_palm: 0x%x\n", g_pGesture->recognized_gesture, g_pGesture->recognized_palm);
#endif
#endif//__DETAIL_DEBUG__


	switch( type )
	{
		case ET_ButtonPress:
			if( idx == 0 )
				g_pGesture->event_sum[0] = BTN_PRESSED;
			g_pGesture->fingers[idx].ptime = ev->any.time;
			g_pGesture->fingers[idx].px = ev->device_event.root_x;
			g_pGesture->fingers[idx].py = ev->device_event.root_y;

			if( g_pGesture->num_pressed == 1 )
			{
				single_finger_timer = TimerSet(single_finger_timer, 0, g_pGesture->singlefinger_threshold, GestureSingleFingerTimerHandler, NULL);
			}
			else
			{
				TimerCancel(single_finger_timer);
			}

			if( g_pGesture->num_pressed > g_pGesture->num_mt_devices )
				g_pGesture->num_pressed = g_pGesture->num_mt_devices;

			if( !g_pGesture->pTempWin || g_pGesture->num_pressed != g_pGesture->inc_num_pressed )
			{
				g_pGesture->pTempWin = GestureGetEventsWindow();

				if( NULL == g_pGesture->pTempWin )
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "[g_pGesture->num_pressed=%d] No events were selected !\n", g_pGesture->num_pressed);
#endif//__DETAIL_DEBUG__
					g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
					goto flush_or_drop;
				}
			}

			g_pGesture->inc_num_pressed = g_pGesture->num_pressed;

			g_pGesture->finger_rects[idx].extents.x1 = ev->device_event.root_x - FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.x2 = ev->device_event.root_x + FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.y1 =  ev->device_event.root_y - FINGER_HEIGHT;
			g_pGesture->finger_rects[idx].extents.y2 =  ev->device_event.root_y + FINGER_HEIGHT;

			if( g_pGesture->inc_num_pressed == 1 )
			{
				pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[P][g_pGesture->inc_num_pressed=1] AREA_SIZE(area.extents)=%d\n", AREA_SIZE(&g_pGesture->area.extents));
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
				XDBG_DEBUG(MGEST, "[P][g_pGesture->inc_num_pressed=%d] AREA_SIZE(area.extents)=%d\n", g_pGesture->inc_num_pressed, AREA_SIZE(&g_pGesture->area.extents));
#endif//__DETAIL_DEBUG__
			}

			break;

		case ET_Motion:
			if( !g_pGesture->fingers[idx].ptime )
				return;
			if( (g_pGesture->inc_num_pressed < 2) && (idx == 0))
			{
				g_pGesture->event_sum[0] += BTN_MOVING;
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "no seconds finger comming\n");
#endif//__DETAIL_DEBUG__

				// tolerate false motion events only when second double-finger press is applied
				if(!g_pGesture->tap_repeated)
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "single finger!\n");
#endif//__DETAIL_DEBUG__
					g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
					goto flush_or_drop;
				}
				else if(g_pGesture->event_sum[0] >= 7)
				{
#ifdef __DETAIL_DEBUG__
					XDBG_DEBUG(MGEST, "tap repeat Moving Limit Exceeded.\n");
#endif//__DETAIL_DEBUG__
					g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
					goto flush_or_drop;
				}
			}

			g_pGesture->fingers[idx].mx = ev->device_event.root_x;
			g_pGesture->fingers[idx].my = ev->device_event.root_y;

			g_pGesture->finger_rects[idx].extents.x1 = ev->device_event.root_x - FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.x2 = ev->device_event.root_x + FINGER_WIDTH;
			g_pGesture->finger_rects[idx].extents.y1 =  ev->device_event.root_y - FINGER_HEIGHT;
			g_pGesture->finger_rects[idx].extents.y2 =  ev->device_event.root_y + FINGER_HEIGHT;

			if( g_pGesture->inc_num_pressed == 1 )
			{
				pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
#ifdef __DETAIL_DEBUG__
				XDBG_DEBUG(MGEST, "[M][g_pGesture->inc_num_pressed=1] AREA_SIZE(area)=%d\n", AREA_SIZE(&g_pGesture->area.extents));
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
				XDBG_DEBUG(MGEST, "[M][g_pGesture->inc_num_pressed=%d] AREA_SIZE(area)=%d\n", g_pGesture->inc_num_pressed, AREA_SIZE(&g_pGesture->area.extents));
#endif//__DETAIL_DEBUG__
			}
			break;

		case ET_ButtonRelease:
			g_pGesture->fingers[idx].rtime = ev->any.time;
			g_pGesture->fingers[idx].rx = ev->device_event.root_x;
			g_pGesture->fingers[idx].ry = ev->device_event.root_y;

			if( g_pGesture->num_pressed <= 0 )
			{
#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "All fingers were released !\n");
#endif//__DETAIL_DEBUG__
				if( g_pGesture->inc_num_pressed == 1 )
					goto flush_or_drop;
			}
			break;
	}

	if( g_pGesture->filter_mask != GESTURE_FILTER_MASK_ALL )
	{
		if( !(g_pGesture->filter_mask & FlickFilterMask) )
		{
			GestureRecognize_GroupFlick(type, ev, device, idx);
		}
		if( !(g_pGesture->filter_mask & PanFilterMask) )
		{
			GestureRecognize_GroupPan(type, ev, device, idx, 0);
		}
		if( !(g_pGesture->filter_mask & PinchRotationFilterMask) )
		{
			GestureRecognize_GroupPinchRotation(type, ev, device, idx, 0);
		}
		if( !(g_pGesture->filter_mask & TapFilterMask) )
		{
			GestureRecognize_GroupTap(type, ev, device, idx, 0);
		}
		if( !(g_pGesture->filter_mask & TapNHoldFilterMask) )
		{
			GestureRecognize_GroupTapNHold(type, ev, device, idx, 0);
		}
		if( !(g_pGesture->filter_mask & HoldFilterMask) )
		{
			GestureRecognize_GroupHold(type, ev, device, idx, 0);
		}
	}

#ifdef __DETAIL_DEBUG__
#ifdef _F_SUPPORT_BEZEL_FLICK_
	XDBG_DEBUG(MGEST, "[filter_mask: 0x%x, bezel_filter_mask: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->bezel_filter_mask, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "[recognized_gesture: 0x%x, bezel_recognize_mask: 0x%x, recognized_palm: 0x%x\n", g_pGesture->recognized_gesture, g_pGesture->bezel_recognized_mask, g_pGesture->recognized_palm);
#else
	XDBG_DEBUG(MGEST, "[filter_mask: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->filter_mask, g_pGesture->palm_filter_mask);
	XDBG_DEBUG(MGEST, "[recognized_gesture: 0x%x, recognized_palm: 0x%x\n", g_pGesture->recognized_gesture, g_pGesture->recognized_palm);
#endif
#endif//__DETAIL_DEBUG__

	if( g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL )
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
	XDBG_DEBUG(MGEST, "GestureFlushOrDrop() !\n");
#endif//__DETAIL_DEBUG__
	if( ERROR_INVALPTR == GestureFlushOrDrop() )
	{
		GestureControl(g_pGesture->this_device, DEVICE_OFF);
	}
}


ErrorStatus GestureFlushOrDrop(void)
{
	ErrorStatus err = ERROR_NONE;
	PalmStatusPtr pPalm = &g_pGesture->palm;
	int i;
#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "recognized_gesture: 0x%x, filter_mask: 0x%x\n", g_pGesture->recognized_gesture, g_pGesture->filter_mask);
	XDBG_DEBUG(MGEST, "recognized_palm: 0x%x, palm_filter_mask: 0x%x\n", g_pGesture->recognized_palm, g_pGesture->palm_filter_mask);
#ifdef _F_SUPPORT_BEZEL_FLICK_
	XDBG_DEBUG(MGEST, "bezel_recognized_mask: 0x%x, bezel_filter_mask: 0x%x\n", g_pGesture->bezel_recognized_mask, g_pGesture->bezel_filter_mask);
#endif
#endif//__DETAIL_DEBUG__

	if(g_pGesture->recognized_gesture || g_pGesture->recognized_palm
#ifdef _F_SUPPORT_BEZEL_FLICK_
		 || g_pGesture->bezel_recognized_mask)
#else
		)
#endif
	{
		g_pGesture->ehtype = IGNORE_EVENTS;
		GestureEventsDrop();
		if(g_pGesture->recognized_palm)
			err = GestureRegionsReinit();
	//memset(pPalm->pti, 0, sizeof(pPalm->pti[MAX_MT_DEVICES]));
		for(i=0; i<MAX_MT_DEVICES; i++)
		{
			pPalm->pti[i].touch_status = 0;
			pPalm->pti[i].tangle = 0.0f;
			pPalm->pti[i].wmajor = 0.0f;
			pPalm->pti[i].tmajor = 0.0f;
			pPalm->pti[i].tminor = 0.0f;
			pPalm->pti[i].tpalm = 0.0f;
		}
		if( ERROR_NONE != err )
			return err;
	}
	else if((g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL) && (g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL)
#ifdef _F_SUPPORT_BEZEL_FLICK_
		&& (g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
#else
		)
#endif
	{
		g_pGesture->ehtype = PROPAGATE_EVENTS;
		err = GestureEventsFlush();
		if( ERROR_NONE != err )
			return err;

		err = GestureRegionsReinit();
		for(i=0; i<MAX_MT_DEVICES; i++)
		{
			pPalm->pti[i].touch_status = 0;
			pPalm->pti[i].tangle = 0.0f;
			pPalm->pti[i].wmajor = 0.0f;
			pPalm->pti[i].tmajor = 0.0f;
			pPalm->pti[i].tminor = 0.0f;
			pPalm->pti[i].tpalm = 0.0f;
		}
		if( ERROR_NONE != err )
			return err;

		g_pGesture->pTempWin = NULL;
		g_pGesture->inc_num_pressed = 0;
		g_pGesture->event_sum[0] = 0;
	}

	return ERROR_NONE;
}

void
GestureHandleMTSyncEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
	int i;
	PalmStatusPtr pPalm = &g_pGesture->palm;
#ifdef _F_SUPPORT_BEZEL_FLICK_
	BezelFlickStatusPtr pBezel = &g_pGesture->bezel;
#endif
#ifdef __DEBUG_EVENT_HANDLER__
	XDBG_DEBUG(MGEST, "(%d:%d) time:%d cur:%d\n",
			ev->any_event.deviceid, ev->any_event.sync, (int)ev->any.time, (int)GetTimeInMillis());
#endif//__DEBUG_EVENT_HANDLER__

	if( MTOUCH_FRAME_SYNC_BEGIN == ev->any_event.sync )
	{
#ifdef __DEBUG_EVENT_HANDLER__
		XDBG_DEBUG(MGEST, "SYNC_BEGIN\n");
#endif//__DEBUG_EVENT_HANDLER
		g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_BEGIN;

		if(g_pGesture->is_active)
		{
			g_pGesture->ehtype = KEEP_EVENTS;
			g_pGesture->filter_mask = 0;
			g_pGesture->recognized_gesture = 0;
			g_pGesture->num_pressed = 0;
			g_pGesture->palm_filter_mask = 0;
			g_pGesture->recognized_palm= 0;
			g_pGesture->hold_detector_activate = 1;
			g_pGesture->has_hold_grabmask = 0;
			pPalm->palmflag = 0;
			pPalm->single_timer_expired = 0;
			pPalm->biggest_tmajor = 0;
			pPalm->biggest_wmajor = 0;
			pPalm->bigger_wmajor = 0;
			g_pGesture->enqueue_fulled = 0;
#ifdef _F_SUPPORT_BEZEL_FLICK_
			pBezel->is_active = BEZEL_ON;
			g_pGesture->bezel_filter_mask = 0;
			g_pGesture->bezel_recognized_mask = 0;
#endif
			for( i=0 ; i < g_pGesture->num_mt_devices ; i++ )
				g_pGesture->fingers[i].ptime = 0;
		}
	}
	else if( MTOUCH_FRAME_SYNC_END == ev->any_event.sync )
	{
#ifdef __DEBUG_EVENT_HANDLER__
		XDBG_DEBUG(MGEST, "SYNC_END\n");
#endif//__DEBUG_EVENT_HANDLER
		g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_END;

		if(g_pGesture->is_active)
		{
			g_pGesture->ehtype = PROPAGATE_EVENTS;
			g_pGesture->palm_filter_mask = GESTURE_PALM_FILTER_MASK_ALL;
			pPalm->cur_touched = 0;
			pPalm->palmflag = 0;
			pPalm->max_palm = 0;
			g_pGesture->pTempPalmWin = NULL;
			if(pPalm->palm_single_finger_timer)
				TimerCancel(pPalm->palm_single_finger_timer);
			g_pGesture->pTempWin = NULL;
			g_pGesture->inc_num_pressed = g_pGesture->num_pressed = 0;
			g_pGesture->event_sum[0] = 0;
			pPalm->max_touched = 0;
#ifdef _F_SUPPORT_BEZEL_FLICK_
			pBezel->is_active = BEZEL_END;
			g_pGesture->bezel_filter_mask = BezelFlickFilterMask;
#endif
		}
	}
}

void
GestureHandleButtonPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	XDBG_DEBUG(MGEST, "mode: %d devid=%d time:%d cur: %d\n", g_pGesture->ehtype, device->id, ev->any.time, GetTimeInMillis());
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
			{
				if(!(device->id < g_pGesture->first_fingerid))
					g_pGesture->num_pressed++;
#ifdef _F_SUPPORT_BEZEL_FLICK_
				if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
					GestureBezelRecognize(ET_ButtonPress, ev, device);
#endif
				if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
					GestureRecognize(ET_ButtonPress, ev, device);
				if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
					GesturePalmRecognize(ET_ButtonPress, ev, device);
			}
			else
				device->public.processInputProc(ev, device);
			GestureHoldDetector(ET_ButtonPress, ev, device);
			break;

		case PROPAGATE_EVENTS:
			if(!(device->id < g_pGesture->first_fingerid))
				g_pGesture->num_pressed++;
			device->public.processInputProc(ev, device);
			GestureHoldDetector(ET_ButtonPress, ev, device);
			break;

		case IGNORE_EVENTS:
			if(!(device->id < g_pGesture->first_fingerid))
				g_pGesture->num_pressed++;
#ifdef _F_SUPPORT_BEZEL_FLICK_
			if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
				GestureBezelRecognize(ET_ButtonPress, ev, device);
#endif
			if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
				GestureRecognize(ET_ButtonPress, ev, device);
			if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
				GesturePalmRecognize(ET_ButtonPress, ev, device);
			break;

		default:
			break;
	}
}

void
GestureHandleMotionEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	XDBG_DEBUG(MGEST, "devid=%d time:%d cur: %d\n", device->id, ev->any.time, GetTimeInMillis());
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
			{
#ifdef _F_SUPPORT_BEZEL_FLICK_
				if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
					GestureBezelRecognize(ET_Motion, ev, device);
#endif
				if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
					GestureRecognize(ET_Motion, ev, device);
				if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
					GesturePalmRecognize(ET_Motion, ev, device);
			}
			else
				device->public.processInputProc(ev, device);
			GestureHoldDetector(ET_Motion, ev, device);
			break;

		case PROPAGATE_EVENTS:
			device->public.processInputProc(ev, device);
			GestureHoldDetector(ET_Motion, ev, device);
			break;

		case IGNORE_EVENTS:
#ifdef _F_SUPPORT_BEZEL_FLICK_
			if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
				GestureBezelRecognize(ET_Motion, ev, device);
#endif
			if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
				GestureRecognize(ET_Motion, ev, device);
			if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
				GesturePalmRecognize(ET_Motion, ev, device);
			break;

		default:
			break;
	}

}

void
GestureHandleButtonReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	XDBG_DEBUG(MGEST, "devid=%d time:%d cur: %d\n", device->id, ev->any.time, GetTimeInMillis());
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
			{
				if(!(device->id < g_pGesture->first_fingerid))
					g_pGesture->num_pressed--;
#ifdef _F_SUPPORT_BEZEL_FLICK_
				if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
					GestureBezelRecognize(ET_ButtonRelease, ev, device);
#endif
				if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
					GestureRecognize(ET_ButtonRelease, ev, device);
				if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
					GesturePalmRecognize(ET_ButtonRelease, ev, device);
			}
			else
				device->public.processInputProc(ev, device);
			GestureHoldDetector(ET_ButtonRelease, ev, device);
			break;

		case PROPAGATE_EVENTS:
			if(!(device->id < g_pGesture->first_fingerid))
				g_pGesture->num_pressed--;
			device->public.processInputProc(ev, device);
			GestureHoldDetector(ET_ButtonRelease, ev, device);
			break;

		case IGNORE_EVENTS:
			if(!(device->id < g_pGesture->first_fingerid))
				g_pGesture->num_pressed--;
#ifdef _F_SUPPORT_BEZEL_FLICK_
			if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
				GestureBezelRecognize(ET_ButtonRelease, ev, device);
#endif
			if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
				GestureRecognize(ET_ButtonRelease, ev, device);
			if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
				GesturePalmRecognize(ET_ButtonRelease, ev, device);
			break;

		default:
			break;
	}
}

void
GestureHandleKeyPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
	XDBG_DEBUG(MGEST, "devid=%d time:%d cur:%d\n", device->id, ev->any.time, GetTimeInMillis());
#endif//__DEBUG_EVENT_HANDLER__

	if(DPMSPowerLevel == DPMSModeOff)
	{
		XDBG_INFO(MGEST, "LCD status : Off\n");

		{
			int i;
			for(i = 0 ; i < NUM_PASSKEYS ; i++)
			{
				if(0 == g_pGesture->pass_keycodes[i])
					break;

				if(g_pGesture->pass_keycodes[i] == ev->device_event.detail.key)
				{
					XDBG_SECURE(MGEST, "Pass KeyPress (devid:%d, keycode:%d) during LCD Off!\n", device->id, ev->device_event.detail.key);
					goto handle_keypress;
				}
			}

			XDBG_SECURE(MGEST, "Ignore KeyPress (devid:%d, keycode:%d) during LCD Off!\n", device->id, ev->device_event.detail.key);
			return;
		}
	}

handle_keypress:

	if((g_pGesture->mtsync_status != MTOUCH_FRAME_SYNC_END) && (device->id == g_pGesture->touchkey_id))
	{
		XDBG_SECURE(MGEST, "Ignore TouchKey KeyPress (devid:%d, keycode:%d)\n", device->id, ev->device_event.detail.key);
		return;
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
		XDBG_ERROR(MGEST, "Failed on GestureInstallResourceStateHooks() !\n");
		return ERROR_ABNORMAL;
	}

	res = GestureSetMaxNumberOfFingers((int)MAX_MT_DEVICES);

	if( !res )
	{
		XDBG_ERROR(MGEST, "Failed on GestureSetMaxNumberOfFingers(%d) !\n", (int)MAX_MT_DEVICES);
		goto failed;
	}

	res = GestureRegisterCallbacks(GestureCbEventsGrabbed, GestureCbEventsSelected);

	if( !res )
	{
		XDBG_ERROR(MGEST, "Failed to register callbacks for GestureEventsGrabbed(), GestureEventsSelected() !\n");
		goto failed;
	}

	pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 5000, GestureTimerHandler, pInfo);

	if( !pGesture->device_setting_timer )
	{
		XDBG_ERROR(MGEST, "Failed to allocate memory for timer !\n");
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

	mieqSetHandler(ET_KeyPress, NULL);
	mieqSetHandler(ET_ButtonPress, NULL);
	mieqSetHandler(ET_ButtonRelease, NULL);
	mieqSetHandler(ET_Motion, NULL);
	mieqSetHandler(ET_MTSync, NULL);

	err = GestureFiniEQ();

	if( ERROR_INVALPTR == err )
	{
		XDBG_ERROR(MGEST, "EQ is invalid or was freed already !\n");
	}

	GestureRegisterCallbacks(NULL, NULL);
	GestureUninstallResourceStateHooks();

	return err;
}

static CARD32
GestureTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
	InputInfoPtr pInfo = (InputInfoPtr)arg;
	GestureDevicePtr pGesture = pInfo->private;

	int idx = 0;
	DeviceIntPtr dev;
	for( dev = inputInfo.pointer ; dev; dev = dev->next )
	{
		if(IsMaster(dev) && IsPointerDevice(dev))
		{
			pGesture->master_pointer = dev;
			XDBG_INFO(MGEST, "[id:%d] Master Pointer=%s\n", dev->id, pGesture->master_pointer->name);
			continue;
		}

		if(IsXTestDevice(dev, NULL) && IsPointerDevice(dev))
		{
			pGesture->xtest_pointer = dev;
			XDBG_INFO(MGEST, "[id:%d] XTest Pointer=%s\n", dev->id, pGesture->xtest_pointer->name);
			continue;
		}

		if(IsPointerDevice(dev))
		{
			if( idx >= MAX_MT_DEVICES )
			{
				XDBG_WARNING(MGEST, "Number of mt device is over MAX_MT_DEVICES(%d) !\n",
					MAX_MT_DEVICES);
				continue;
			}
			pGesture->mt_devices[idx] = dev;
			XDBG_INFO(MGEST, "[id:%d] MT device[%d] name=%s\n", dev->id, idx, pGesture->mt_devices[idx]->name);
			GesturePalmGetAbsAxisInfo(dev);
			idx++;
		}
	}

	for( dev = inputInfo.keyboard ; dev; dev = dev->next )
	{
		if(strcasestr(dev->name, "touchkey"))
		{
			g_pGesture->touchkey_id = dev->id;
			break;
		}
	}

	if( !pGesture->master_pointer || !pGesture->xtest_pointer )
	{
		XDBG_ERROR(MGEST, "Failed to get info of master pointer or XTest pointer !\n");
		pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 0, NULL, NULL);
		pGesture->num_mt_devices = 0;

		return 0;
	}

	pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 0, NULL, NULL);
	pGesture->num_mt_devices = idx;

	if( !pGesture->num_mt_devices )
	{
		XDBG_ERROR(MGEST, "Failed to mt device information !\n");
		pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 0, NULL, NULL);
		pGesture->num_mt_devices = 0;
    	pGesture->first_fingerid = -1;
		return 0;
	}

	pGesture->first_fingerid = pGesture->mt_devices[0]->id;
	memset(pGesture->fingers, 0, sizeof(TouchStatus)*pGesture->num_mt_devices);
	pGesture->pRootWin = RootWindow(pGesture->master_pointer);

	if(g_pGesture->palm_misc.enabled)
		GesturePalmGetScreenInfo();

	g_pGesture->pTempWin = NULL;
	g_pGesture->pTempPalmWin = NULL;
	g_pGesture->inc_num_pressed = 0;

	if( ERROR_NONE != GestureRegionsInit() || ERROR_NONE != GestureInitEQ() )
	{
		goto failed;
	}

	mieqSetHandler(ET_KeyPress, GestureHandleKeyPressEvent);
	mieqSetHandler(ET_ButtonPress, GestureHandleButtonPressEvent);
	mieqSetHandler(ET_ButtonRelease, GestureHandleButtonReleaseEvent);
	mieqSetHandler(ET_Motion, GestureHandleMotionEvent);
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
		g_pGesture->is_active = 0;
		XDBG_INFO(MGEST, "Disabled !\n");
	}
	else if((enable) && (!g_pGesture->is_active))
	{
		g_pGesture->is_active = 1;
		XDBG_INFO(MGEST, "Enabled !\n");
	}

	if(!prop)
		 XIChangeDeviceProperty(dev, prop_gesture_recognizer_onoff, XA_INTEGER, 32, PropModeReplace, 1, &g_pGesture->is_active, FALSE);
}

ErrorStatus
GestureRegionsInit(void)
{
	int i;
	PalmStatusPtr pPalm = &g_pGesture->palm;

	if( !g_pGesture )
		return ERROR_INVALPTR;

	pixman_region_init(&g_pGesture->area);
	pixman_region_init(&pPalm->area);

	for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
	{
		pixman_region_init_rect (&g_pGesture->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
		pixman_region_init_rect (&pPalm->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
	}

	return ERROR_NONE;
}

ErrorStatus
GestureRegionsReinit(void)
{
	PalmStatusPtr pPalm = &g_pGesture->palm;
	int i;
	if( !g_pGesture )
	{
		XDBG_ERROR(MGEST, "Invalid pointer access !\n");
		return ERROR_INVALPTR;
	}

	pixman_region_init(&g_pGesture->area);
	pixman_region_init(&pPalm->area);

	for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
	{
		pixman_region_init_rect (&pPalm->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
	}

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
		XDBG_ERROR(MGEST, "Failed to allocate memory for EQ !\n");
		return ERROR_ALLOCFAIL;
	}

	for( i = 0 ; i < GESTURE_EQ_SIZE ; i++ )
	{
		tmpEQ[i].event = (InternalEvent *)malloc(sizeof(InternalEvent));
		if( !tmpEQ[i].event )
		{
			XDBG_ERROR(MGEST, "Failed to allocation memory for each event buffer in EQ !\n");
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
		XDBG_ERROR(MGEST, "Invalid pointer access !\n");
		return ERROR_INVALPTR;
	}

	tail = g_pGesture->tailEQ;

	if( tail >= GESTURE_EQ_SIZE )
	{
		XDBG_WARNING(MGEST, "Gesture EQ is full !\n");
		printk("[X11][GestureEnqueueEvent] Gesture EQ is full...Force Gesture Flush !\n");
		g_pGesture->enqueue_fulled = 1;
		if(!(g_pGesture->filter_mask == GESTURE_FILTER_MASK_ALL))
		{
			if( !(g_pGesture->filter_mask & FlickFilterMask) )
			{
				GestureRecognize_GroupFlick(ev->any.type, ev, device, 0);
			}
			if( !(g_pGesture->filter_mask & PanFilterMask) )
			{
				GestureRecognize_GroupPan(ev->any.type, ev, device, 0, 0);
			}
			if( !(g_pGesture->filter_mask & PinchRotationFilterMask) )
			{
				GestureRecognize_GroupPinchRotation(ev->any.type, ev, device, 0, 0);
			}
			if( !(g_pGesture->filter_mask & TapFilterMask) )
			{
				GestureRecognize_GroupTap(ev->any.type, ev, device, 0, 0);
			}
			if( !(g_pGesture->filter_mask & TapNHoldFilterMask) )
			{
				GestureRecognize_GroupTapNHold(ev->any.type, ev, device, 0, 0);
			}
			if( !(g_pGesture->filter_mask & HoldFilterMask) )
			{
				GestureRecognize_GroupHold(ev->any.type, ev, device, 0, 0);
			}
		}
		if(!(g_pGesture->palm_filter_mask == GESTURE_PALM_FILTER_MASK_ALL))
		{
			if(!(g_pGesture->palm_filter_mask & PalmHoldFilterMask))
			{
				GesturePalmRecognize_Hold(ev->any.type, 0, 0);
			}
			if(!(g_pGesture->palm_filter_mask & PalmFlickHorizFilterMask))
			{
				GesturePalmRecognize_FlickHorizen(ev->any.type, 0);
			}
			if(!(g_pGesture->palm_filter_mask & PalmFlickHorizFilterMask))
			{
				GesturePalmRecognize_FlickVertical(ev->any.type, 0);
			}
		}
#ifdef _F_SUPPORT_BEZEL_FLICK_
		if(!(g_pGesture->bezel_filter_mask == BezelFlickFilterMask))
		{
			GestureBezelRecognize(ev->any.type, ev, device);
		}
#endif
		g_pGesture->filter_mask = GESTURE_FILTER_MASK_ALL;
		g_pGesture->palm_filter_mask = GESTURE_PALM_FILTER_MASK_ALL;
#ifdef _F_SUPPORT_BEZEL_FLICK_
		g_pGesture->bezel_filter_mask = BezelFlickFilterMask;
		g_pGesture->bezel_recognized_mask = 0;
#endif
		g_pGesture->recognized_gesture = 0;
		g_pGesture->recognized_palm = 0;
		if( ERROR_INVALPTR == GestureFlushOrDrop() )
		{
			GestureControl(g_pGesture->this_device, DEVICE_OFF);
		}
		return ERROR_EQFULL;
	}

#ifdef __DETAIL_DEBUG__
	switch( ev->any.type )
	{
		case ET_ButtonPress:
			XDBG_DEBUG(MGEST, "ET_ButtonPress (id:%d)\n", device->id);
			break;

		case ET_ButtonRelease:
			XDBG_DEBUG(MGEST, "ET_ButtonRelease (id:%d)\n", device->id);
			break;

		case ET_Motion:
			XDBG_DEBUG(MGEST, "ET_Motion (id:%d)\n", device->id);
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
	int i, j;
	DeviceIntPtr device;

	if( !g_pGesture->EQ )
	{
		XDBG_ERROR(MGEST, "Invalid pointer access !\n");
		return ERROR_INVALPTR;
	}

#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "\n");
#endif//__DETAIL_DEBUG__

	for( i = g_pGesture->headEQ ; i < g_pGesture->tailEQ ; i++)
	{
		device = g_pGesture->EQ[i].device;
#ifdef __DETAIL_DEBUG__
		if(g_pGesture->EQ[i].event->any.type != ET_Motion)
			XDBG_DEBUG(MGEST, "[%d] type: %d\n", device->id, g_pGesture->EQ[i].event->any.type);
#endif//__DETAIL_DEBUG__
		for(j = 0 ; j < MAX_MT_DEVICES+1 ; j++)
		{
			if(g_pGesture->palm.qti[j].devid == device->id)
			{
#ifdef __DETAIL_DEBUG__
				if(g_pGesture->EQ[i].event->any.type != ET_Motion)
					XDBG_DEBUG(MGEST, "[%d] type: %d(pressed: %d) time: %d\n", device->id, g_pGesture->EQ[i].event->any.type, g_pGesture->palm.qti[j].pressed, GetTimeInMillis());
#endif//__DETAIL_DEBUG__
				if( (g_pGesture->palm.qti[j].pressed == 0) && (g_pGesture->EQ[i].event->any.type == ET_ButtonRelease) )
				{
					XDBG_WARNING(MGEST, "Enqueued event..ButtonRelease with no ButtonPress !(devid: %d)\n", device->id);
					g_pGesture->EQ[i].event->any.type = ET_ButtonPress;
					device->public.processInputProc(g_pGesture->EQ[i].event, device);
					g_pGesture->EQ[i].event->any.type = ET_ButtonRelease;
					g_pGesture->palm.qti[j].pressed = 0;
				}
				else if(g_pGesture->EQ[i].event->any.type == ET_ButtonPress)
				{
					g_pGesture->palm.qti[j].pressed = 1;
				}
				else if( (g_pGesture->palm.qti[j].pressed == 1) && (g_pGesture->EQ[i].event->any.type == ET_ButtonRelease))
				{
					g_pGesture->palm.qti[j].pressed = 0;
				}
				break;
			}
			else if(g_pGesture->palm.qti[j].devid == 0)
			{
				g_pGesture->palm.qti[j].devid = device->id;
				j--;
			}
		}
#ifdef __DETAIL_DEBUG__
		if(g_pGesture->EQ[i].event->any.type != ET_Motion)
			XDBG_DEBUG(MGEST, "!!! [%d] type: %d\n", device->id, g_pGesture->EQ[i].event->any.type);
#endif
		device->public.processInputProc(g_pGesture->EQ[i].event, device);
	}
	memset(g_pGesture->palm.qti, 0, sizeof(g_pGesture->palm.qti[MAX_MT_DEVICES+1]));

	for( i = 0 ; i < MAX_MT_DEVICES ; i++ )
		g_pGesture->event_sum[i] = 0;

	g_pGesture->headEQ = g_pGesture->tailEQ = 0;//Free EQ

	return ERROR_NONE;
}


void
GestureEventsDrop(void)
{
#ifdef __DETAIL_DEBUG__
	XDBG_DEBUG(MGEST, "\n");
#endif//__DETAIL_DEBUG__

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
	InputInfoPtr pInfo;
	pInfo = device->public.devicePrivate;

#ifdef HAVE_PROPERTIES
	GestureInitProperty(device);
	XIRegisterPropertyHandler(device, GestureSetProperty, NULL, NULL);
#endif

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
#ifdef _F_SUPPORT_BEZEL_FLICK_
    BezelFlickStatusPtr pBezel;
#endif

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

    {
    	int i;
    	char tmp[10];

    	memset(&pGesture->pass_keycodes, 0, sizeof(pGesture->pass_keycodes));

    	for(i = 0 ; i < NUM_PASSKEYS ; i++)
    	{
    		snprintf(tmp, sizeof(tmp), "PassKey%d", i+1);
    		pGesture->pass_keycodes[i] = xf86SetIntOption(pInfo->options, tmp, 0);
    		XDBG_SECURE(MGEST, "pass_keycode[%d]=%d\n", i, pGesture->pass_keycodes[i]);
    	}
    }

    pGesture->gestureWin = None;
#ifdef _F_SUPPORT_BEZEL_FLICK_
    pBezel = &pGesture->bezel;
    pBezel->is_active = xf86SetIntOption(pInfo->options, "Bezel_Activate", 0);
    pBezel->top_left.width = xf86SetIntOption(pInfo->options, "Bezel_Top_Left_Width", 0);
    pBezel->top_left.height = xf86SetIntOption(pInfo->options, "Bezel_Top_Left_Height", 0);
    pBezel->top_right.width = xf86SetIntOption(pInfo->options, "Bezel_Top_Right_Width", 0);
    pBezel->top_right.height = xf86SetIntOption(pInfo->options, "Bezel_Top_Right_Height", 0);
    pBezel->bottom_left.width = xf86SetIntOption(pInfo->options, "Bezel_Bottom_Left_Width", 0);
    pBezel->bottom_left.height = xf86SetIntOption(pInfo->options, "Bezel_Bottom_Left_Height", 0);
    pBezel->bottom_right.width = xf86SetIntOption(pInfo->options, "Bezel_Bottom_Right_Width", 0);
    pBezel->bottom_right.height = xf86SetIntOption(pInfo->options, "Bezel_Bottom_Right_Height", 0);
    pBezel->flick_distance = xf86SetIntOption(pInfo->options, "Bezel_Flick_Distance", 0);
    pBezel->bezel_angle_ratio = xf86SetIntOption(pInfo->options, "Bezel_Flick_Angle_Ratio", 0);
    pBezel->bezel_angle_moving_check = xf86SetIntOption(pInfo->options, "Bezel_Flick_Angle_Moving_Check", 0);
#ifdef __BEZEL_DEBUG__
	XDBG_DEBUG(MGEST, "[BEZEL] top_left.width: %d, top_left.height: %d\n", pBezel->top_left.width, pBezel->top_left.height);
	XDBG_DEBUG(MGEST, "[BEZEL] top_right.width: %d, top_right.height: %d\n", pBezel->top_right.width, pBezel->top_right.height);
	XDBG_DEBUG(MGEST, "[BEZEL] bottom_left.width: %d, bottom_left.height: %d\n", pBezel->bottom_left.width, pBezel->bottom_left.height);
	XDBG_DEBUG(MGEST, "[BEZEL] bottom_right.width: %d, bottom_right.height: %d\n", pBezel->bottom_right.width, pBezel->bottom_right.height);
	XDBG_DEBUG(MGEST, "[BEZEL] flick_distance: %d, bezel_angle_ratio: %d, bezel_angle_moving_check: %d\n", pBezel->flick_distance, pBezel->bezel_angle_ratio, pBezel->bezel_angle_moving_check);
#endif//__BEZEL_DEBUG__
#endif

	pGesture->pinchrotation_time_threshold = xf86SetIntOption(pInfo->options, "PinchRotationTimeThresHold", PINCHROTATION_TIME_THRESHOLD);
	pGesture->pinchrotation_dist_threshold = xf86SetRealOption(pInfo->options, "PinchRotationDistThresHold", PINCHROTATION_DIST_THRESHOLD);
	pGesture->pinchrotation_angle_threshold = xf86SetRealOption(pInfo->options, "PinchRotationAngleThresHold", PINCHROTATION_ANGLE_THRESHOLD);
	pGesture->singlefinger_threshold = xf86SetIntOption(pInfo->options, "SingleFingerThresHold", SGL_FINGER_TIME_THRESHOLD);
	pGesture->singletap_threshold = xf86SetIntOption(pInfo->options, "SingleTapThresHold", SGL_TAP_TIME_THRESHOLD);
	pGesture->doubletap_threshold = xf86SetIntOption(pInfo->options, "DoubleTapThresHold", DBL_TAP_TIME_THRESHOLD);

	if (pGesture->is_active)
		pGesture->ehtype = KEEP_EVENTS;
	else
		pGesture->ehtype = PROPAGATE_EVENTS;
#ifdef _F_SUPPORT_BEZEL_FLICK_
	if(pBezel->bezel_angle_ratio > 0)
	{
		pBezel->min_rad = (RAD_90DEG / pBezel->bezel_angle_ratio);
		pBezel->max_rad = ((RAD_90DEG / pBezel->bezel_angle_ratio) * (pBezel->bezel_angle_ratio-1));
		pBezel->min_180_rad = (RAD_90DEG + pBezel->min_rad);
		pBezel->max_180_rad = (RAD_90DEG + pBezel->max_rad);
	}
	else
	{
		pBezel->min_rad = MIN_RAD;
		pBezel->max_rad = MAX_RAD;
		pBezel->min_180_rad = RAD_180DEG_MIN;
		pBezel->max_180_rad = RAD_180DEG_MAX;
	}
#endif
    pGesture->lastSelectedWin = None;
    pGesture->touchkey_id = 0;
    pGesture->mtsync_status = MTOUCH_FRAME_SYNC_END;
    g_pGesture->grabMask = g_pGesture->eventMask = 0;

    xf86Msg(X_INFO, "%s: Using device %s.\n", pInfo->name, pGesture->device);

    /* process generic options */
    xf86CollectInputOptions(pInfo, NULL);
    xf86ProcessCommonOptions(pInfo, pInfo->options);

    pInfo->fd = -1;

	g_pGesture->tap_repeated = 0;

	g_pGesture->palm.palmflag = 0;
	g_pGesture->palm.palm_single_finger_timer = NULL;
	g_pGesture->enqueue_fulled = 0;
	g_pGesture->zoom_enabled = 0;
	memset(g_pGesture->palm.qti, 0, sizeof(g_pGesture->palm.qti[MAX_MT_DEVICES+1]));

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

