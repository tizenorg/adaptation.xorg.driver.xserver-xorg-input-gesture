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

//#define __DETAIL_DEBUG__
//#define __DEBUG_EVENT_HANDLER__
//#define __PalmFlick_DEBUG__
//#define __HOLD_DETECTOR_DEBUG__

#ifdef __PalmFlick_DEBUG__
#define PalmFlickDebugPrint ErrorF
#else
#define PalmFlickDebugPrint(...)
#endif

#ifdef __HOLD_DETECTOR_DEBUG__
#define HoldDetectorDebugPrint ErrorF
#else
#define HoldDetectorDebugPrint(...)
#endif


#ifdef __DETAIL_DEBUG__
#define DetailDebugPrint ErrorF
#else
#define DetailDebugPrint(...)
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

#define LOG_TAG	"GESTURE"
#include "dlog.h"



char *strcasestr(const char *s, const char *find);
extern ScreenPtr miPointerCurrentScreen(void);
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
#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 21
static Bool PointInBorderSize(WindowPtr pWin, int x, int y);
#endif
static WindowPtr GestureWindowOnXY(int x, int y);
Bool GestureHasFingerEventMask(int eventType, int num_finger);

//Gesture recognizer and handlers
void GestureRecognize(int type, InternalEvent *ev, DeviceIntPtr device);
void GestureRecognize_GroupTap(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_GroupFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int start_point, int direction);
void GestureRecognize_GroupHold(int type, InternalEvent *ev, DeviceIntPtr device, int idx, int timer_expired);
void GestureRecognize_PalmFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx);
void GestureHandleGesture_Flick(int num_of_fingers, int distance, Time duration, int direction);
void GestureHandleGesture_Tap(int num_finger, int tap_repeat, int cx, int cy);
void GestureHandleGesture_Hold(int num_fingers, int cx, int cy, Time holdtime, int kinds);
ErrorStatus GestureFlushOrDrop(void);

static int GestureGetPalmValuator(InternalEvent *ev, DeviceIntPtr device);
static int GesturePalmGetAbsAxisInfo(DeviceIntPtr dev);
static void GestureHoldDetector(int type, InternalEvent *ev, DeviceIntPtr device);
static int GesturePalmGetScreenInfo();
static int GesturePalmGetHorizIndexWithX(int current_x, int idx, int type);

#ifdef HAVE_PROPERTIES
//function related property handling
static void GestureInitProperty(DeviceIntPtr dev);
static int GestureSetProperty(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr val, BOOL checkonly);
#endif

static Atom prop_gesture_recognizer_onoff = None;

#ifdef SUPPORT_ANR_WITH_INPUT_EVENT
static Atom prop_anr_in_input_event = None;
static Atom prop_anr_event_window = None;
static Window prop_anr_event_window_xid = None;
#endif


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

    if (!init && !fp)
    {
        fp = fopen("/dev/kmsg", "wt");
        init = 1;
    }

    if (!fp) return;

    va_start(argptr, fmt);
    vfprintf(fp, fmt, argptr);
    fflush(fp);
    va_end(argptr);
}

#ifdef SUPPORT_ANR_WITH_INPUT_EVENT
static WindowPtr
_GestureFindANRWindow(DeviceIntPtr device)
{
    WindowPtr root=NULL;
    WindowPtr anr_window=NULL;
    Window anr_xid=0;
    PropertyPtr pProp;
    int rc=0;

    root = RootWindow(device);

    if( prop_anr_event_window == None )
        prop_anr_event_window = MakeAtom(ANR_EVENT_WINDOW, strlen(ANR_EVENT_WINDOW), TRUE);

    rc = dixLookupProperty (&pProp, root, prop_anr_event_window, serverClient, DixReadAccess);
    if (rc == Success && pProp->data){
        anr_xid = *(int*)pProp->data;
    }

    if( anr_xid != 0 )
    {
        rc = dixLookupWindow(&anr_window, anr_xid, serverClient, DixSetPropAccess);
        if( rc == BadWindow )
        {
            ErrorF("Can't find ANR window !!\n");
            anr_window = NULL;
        }
        prop_anr_event_window_xid = anr_xid;
    }

    ErrorF("ANR Window is %#x. Ptr is %#x\n", anr_xid, anr_window);
    return anr_window;
}
#endif

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) < 21
static Bool
PointInBorderSize(WindowPtr pWin, int x, int y)
{
    BoxRec box;
    if (pixman_region_contains_point (&pWin->borderSize, x, y, &box))
    {
        return TRUE;
    }
    return FALSE;
}
#endif

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

    if ((g_pGesture->grabMask & eventmask) &&
            (g_pGesture->GrabEvents[eventType].pGestureGrabWinInfo[num_finger].window != None))
    {
        DetailDebugPrint("[GestureHasFingerEventMask] TRUE !! Has grabMask\n");
        return TRUE;
    }

    if (g_pGesture->eventMask & eventmask)
    {
        DetailDebugPrint("[GestureHasFingerEventMask] TRUE !! Has eventMask\n");
        return TRUE;
    }

    DetailDebugPrint("[GestureHasFingerEventMask] FALSE !! eventType=%d, num_finger=%d\n", eventType, num_finger);

    return ret;
}

static CARD32
GestureEventTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
    int event_type = *(int *)arg;

    switch (event_type)
    {
        case GestureNotifyHold:
            DetailDebugPrint("[GestureEventTimerHandler] GestureNotifyHold (event_type = %d)\n", event_type);
            GestureRecognize_GroupHold(event_type, NULL, NULL, 0, 1);
            break;
        case GestureNotifyTap:
            DetailDebugPrint("[GestureEventTimerHandler] GestureNotifyTap (event_type = %d)\n", event_type);
            GestureRecognize_GroupTap(event_type, NULL, NULL, 0, 1);
            break;
        default:
            DetailDebugPrint("[GestureEventTimerHandler] unknown event_type (=%d)\n", event_type);
            if (timer)
            {
                DetailDebugPrint("[GestureEventTimerHandler] timer=%x\n", (unsigned int)timer);
            }
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
    if (!tap_repeat || num_finger <= 1)
    {
        return;
    }

    DetailDebugPrint("[GestureHandleGesture_Tap] num_finger=%d, tap_repeat=%d, cx=%d, cy=%d\n", num_finger, tap_repeat, cx, cy);

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

    if (g_pGesture->grabMask && (target_win != None))
    {
        tev.window = target_win;
    }
    else
    {
        tev.window = g_pGesture->gestureWin;
    }

    DetailDebugPrint("[GestureHandleGesture_Tap] tev.window=0x%x, g_pGesture->grabMask=0x%x\n", (unsigned int)tev.window, (unsigned int)g_pGesture->grabMask);

    GestureSendEvent(target_pWin, GestureNotifyTap, GestureTapMask, (xGestureCommonEvent *)&tev);
    LOGI("GroupTap Event done. 2 fingers %d tap!", tap_repeat);
}

void
GestureHandleGesture_Flick(int num_of_fingers, int distance, Time duration, int direction)
{
    if (num_of_fingers == 0)
    {
        Window target_win;
        WindowPtr target_pWin;
        xGestureNotifyFlickEvent fev;

        DetailDebugPrint("[GestureHandleGesture_Flick] num_fingers=%d, distance=%d, duration=%d, direction=%d\n", num_of_fingers, distance, duration, direction);

        g_pGesture->recognized_gesture |= WPalmFlickFilterMask;

        memset(&fev, 0, sizeof(xGestureNotifyFlickEvent));
        fev.type = GestureNotifyFlick;
        fev.kind = GestureDone;
        fev.num_finger = num_of_fingers;
        fev.distance = distance;
        fev.duration = duration;
        fev.direction = direction;

        if (g_pGesture->GrabEvents)
        {
            target_win = g_pGesture->GrabEvents[GestureNotifyFlick].pGestureGrabWinInfo[num_of_fingers].window;
            target_pWin = g_pGesture->GrabEvents[GestureNotifyFlick].pGestureGrabWinInfo[num_of_fingers].pWin;
        }
        else
        {
            target_win = None;
            target_pWin = None;
        }

        if (g_pGesture->grabMask && (target_win != None))
        {
            fev.window = target_win;
        }
        else
        {
            fev.window = g_pGesture->gestureWin;
        }

        DetailDebugPrint("[GestureHandleGesture_Flick] fev.window=0x%x, g_pGesture->grabMask=0x%x\n", fev.window, g_pGesture->grabMask);

        GestureSendEvent(target_pWin, GestureNotifyFlick, GestureFlickMask, (xGestureCommonEvent *)&fev);
    }
    else
    {
        DetailDebugPrint("[GestureHandleGesture_Flick] num_fingers=%d, distance=%d, duration=%d, direction=%d\n", num_of_fingers, distance, duration, direction);

        switch (direction)
        {
            case FLICK_NORTHWARD:
                DetailDebugPrint("[GestureHandleGesture_Flick] Flick Down \n");
                GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_down);
                break;

            case FLICK_SOUTHWARD:
                DetailDebugPrint("[GestureHandleGesture_Flick] Flick Up \n");
                GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_up);
                break;
            case FLICK_WESTWARD:
                if (g_pGesture->power_pressed == 2)
                {
                    DetailDebugPrint("[GestureHandleGesture_Flick] Flick Right & power_pressed\n");
                    GestureEmulateHWKey(g_pGesture->hwkey_dev, 122);
                }
                break;
            default:
                break;
        }
        g_pGesture->recognized_gesture |= WFlickFilterMask;
    }
}

void GestureHandleGesture_Hold(int num_fingers, int cx, int cy, Time holdtime, int kinds)
{
    Window target_win;
    WindowPtr target_pWin;
    xGestureNotifyHoldEvent hev;

    DetailDebugPrint("[GestureHandleGesture_Hold] num_fingers=%d, cx=%d, cy=%d, holdtime=%d, kinds=%d\n", num_fingers, cx, cy, holdtime, kinds);

    if (num_fingers == 0)
    {
        g_pGesture->hold_detected = TRUE;
        LOGI("[PalmHold] PalmHold success !\n");
    }
    else
    {
        g_pGesture->recognized_gesture |= WHoldFilterMask;
    }

    memset(&hev, 0, sizeof(xGestureNotifyHoldEvent));
    hev.type = GestureNotifyHold;
    hev.kind = kinds;
    hev.num_finger = num_fingers;
    hev.holdtime = holdtime;
    hev.cx = cx;
    hev.cy = cy;

    if (g_pGesture->GrabEvents)
    {
        target_win = g_pGesture->GrabEvents[GestureNotifyHold].pGestureGrabWinInfo[num_fingers].window;
        target_pWin = g_pGesture->GrabEvents[GestureNotifyHold].pGestureGrabWinInfo[num_fingers].pWin;
    }
    else
    {
        target_win = None;
        target_pWin = None;
    }

    if (g_pGesture->grabMask && (target_win != None))
    {
        hev.window = target_win;
    }
    else
    {
        hev.window = g_pGesture->gestureWin;
    }

    DetailDebugPrint("[GestureHandleGesture_Hold] hev.window=0x%x, g_pGesture->grabMask=0x%x\n", hev.window, g_pGesture->grabMask);

    GestureSendEvent(target_pWin, GestureNotifyHold, GestureHoldMask, (xGestureCommonEvent *)&hev);
    LOGI("[GroupHold] GestureHold success !\n");
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

    if (timer_expired)
    {
        DetailDebugPrint("[GroupTap][Timer] state=%d, num_pressed=%d, tap_repeat=%d\n", state, num_pressed, tap_repeat);

        switch (state)
        {
            case 1://first tap initiation check
                if (num_pressed)
                {
                    DetailDebugPrint("[GroupTap][Timer][state=1] Tap time expired !(num_pressed=%d, tap_repeat=%d)\n", num_pressed, tap_repeat);
                    DetailDebugPrint("[GroupTap][F] 1\n");

                    state = 0;
                    goto cleanup_tap;
                }
                break;

            case 2:
                if (tap_repeat <= 1)
                {
                    state = 0;
                    DetailDebugPrint("[GroupTap][Timer][state=2] 2 finger %d tap\n", tap_repeat);
                    LOGI("[GroupTap][F] Second tap doesn't come up in 400ms after first tap.\n");
                    goto cleanup_tap;
                }

                if (GestureHasFingerEventMask(GestureNotifyTap, prev_num_pressed))
                {
                    DetailDebugPrint("[GroupTap] Success 1!! 2 finger %d tap\n", tap_repeat);
                    GestureHandleGesture_Tap(prev_num_pressed, tap_repeat, base_cx, base_cy);
                    goto cleanup_tap;
                }
                break;
        }
        return;
    }

    switch (type)
    {
        case ET_ButtonPress:
            g_pGesture->fingers[idx].flags |= PressFlagTap;

            if (g_pGesture->num_pressed < 2)
            {
                DetailDebugPrint("[GroupTap][P] num_pressed=%d, base_px=%d, base_py=%d. return \n", g_pGesture->num_pressed, g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
                return;
            }

            if ((!base_area_size || g_pGesture->num_pressed > num_pressed))
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

            DetailDebugPrint("[GroupTap][P] num_pressed=%d, area_size=%d, base_mx=%d, base_my=%d\n", num_pressed, base_area_size, g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
            break;

        case ET_Motion:
            if (!(g_pGesture->fingers[idx].flags & PressFlagTap))
            {
                break;
            }

            if (num_pressed < 2)
            {
                DetailDebugPrint("[GroupTap][M] num_pressed=%d, return \n", num_pressed);
                return;
            }

            if (num_pressed != g_pGesture->num_pressed)
            {
                DetailDebugPrint("[GroupTap][M] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
            }

            mbits |= (1 << idx);
            if (mbits == (pow(2, num_pressed)-1))
            {
                area_size = AREA_SIZE(&g_pGesture->area.extents);
                cx = AREA_CENTER_X(&g_pGesture->area.extents);
                cy = AREA_CENTER_Y(&g_pGesture->area.extents);

                DetailDebugPrint("[GroupTap][M][num_pressed=%d] area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
                DetailDebugPrint("[GroupTap][M] cx=%d, base_cx=%d, diff=%d\n", cx, g_pGesture->fingers[idx].mx, ABS(cx-base_cx));
                DetailDebugPrint("[GroupTap][M] cy=%d, base_cy=%d, diff=%d\n", cy, g_pGesture->fingers[idx].my, ABS(cy-base_cy));

                if (ABS(base_area_size-area_size) >= TAP_AREA_THRESHOLD)
                {
                    DetailDebugPrint("[GroupTap][M] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, ABS(base_area_size-area_size));
                    DetailDebugPrint("[GroupTap][F] 3\n");
                    LOGI("[GroupTap][F] diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%d)!\n", area_size, base_area_size, ABS(base_area_size-area_size));

                    goto cleanup_tap;
                }

                if (!INBOX(&base_box_ext, cx, cy))
                {
                    DetailDebugPrint("[GroupTap][M] current center coordinates is not in base coordinates box !\n");
                    DetailDebugPrint("[GroupTap][F] 4\n");
                    LOGI("[GroupTap][F] current center coordinates is not in base coordinates box !\n");

                    goto cleanup_tap;
                }
            }
            break;

        case ET_ButtonRelease:
            if (g_pGesture->num_pressed)
            {
                DetailDebugPrint("[GroupTap][R] Second finger doesn't come up. g_pGesture->num_pressed=%d\n", g_pGesture->num_pressed);
                break;
            }

            if (!tap_repeat)
            {
                prev_num_pressed = num_pressed;
            }

            tap_repeat++;
            g_pGesture->num_tap_repeated = tap_repeat;

            DetailDebugPrint("[GroupTap][R] tap_repeat=%d, num_pressed=%d, prev_num_pressed=%d\n", tap_repeat, num_pressed, prev_num_pressed);
            DetailDebugPrint("[GroupTap][R] base_rx=%d, base_ry=%d,\n", g_pGesture->fingers[idx].rx, g_pGesture->fingers[idx].ry);

            if ((num_pressed != prev_num_pressed) || (!GestureHasFingerEventMask(GestureNotifyTap, num_pressed)))
            {
                DetailDebugPrint("[GroupTap][R] num_pressed(=%d) != prev_num_pressed(=%d) OR %d finger tap event was not grabbed/selected !\n",
                        num_pressed, prev_num_pressed, num_pressed);
                DetailDebugPrint("[GroupTap][F] 5\n");
                LOGI("[GroupTap][F] num_pressed(=%d) != prev_num_pressed(=%d) OR %d finger tap event was not grabbed/selected !\n",
                        num_pressed, prev_num_pressed, num_pressed);
                goto cleanup_tap;
            }

            if (tap_repeat == 1)
            {
                DetailDebugPrint("[GroupTap][R] %d finger %d tap\n", num_pressed, tap_repeat);
                TimerCancel(tap_event_timer);
                tap_event_timer = TimerSet(tap_event_timer, 0, g_pGesture->doubletap_threshold, GestureEventTimerHandler, (int *)&event_type);
                state = 2;
                prev_num_pressed = num_pressed;
                num_pressed = 0;
                break;
            }

            else if (tap_repeat == 2)
            {
                DetailDebugPrint("[GroupTap][R] %d finger %d tap\n", num_pressed, tap_repeat);
                TimerCancel(tap_event_timer);
                tap_event_timer = TimerSet(tap_event_timer, 0, g_pGesture->tripletap_threshold, GestureEventTimerHandler, (int *)&event_type);
                state = 2;
                base_area_size = num_pressed = 0;
                break;
            }

            DetailDebugPrint("[GroupTap][R] %d finger %d tap\n", num_pressed, tap_repeat);

            if (tap_repeat == MAX_TAP_REPEATS)
            {
                if (GestureHasFingerEventMask(GestureNotifyTap, num_pressed))
                {
                    DetailDebugPrint("[GroupTap] Sucess 2!\n");
                    GestureHandleGesture_Tap(num_pressed, tap_repeat, base_cx, base_cy);
                }
                goto cleanup_tap;
            }

            if (tap_repeat >= MAX_TAP_REPEATS)
            {
                LOGI("[GroupTap][F] More than 3 taps. Ignore. \n");
                goto cleanup_tap;
            }

            prev_num_pressed = num_pressed;
            num_pressed = 0;
            break;
    }

    return;

cleanup_tap:

    DetailDebugPrint("[GroupTap][cleanup_tap]\n");

    if (0 == state)
    {
        g_pGesture->recognized_gesture &= ~WTapFilterMask;
    }

    g_pGesture->filter_mask |= WTapFilterMask;

    if (g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL)
    {
        DetailDebugPrint("[GroupTap][cleanup] GestureFlushOrDrop() !\n");

        if (ERROR_INVALPTR == GestureFlushOrDrop())
        {
            GestureControl(g_pGesture->this_device, DEVICE_OFF);
        }
    }

    num_pressed = 0;
    g_pGesture->num_tap_repeated = tap_repeat = 0;
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
    static int angle_base_x = 0, angle_base_y = 0;
    static int motion_count = 0;

    if (g_pGesture->num_pressed > 1)
    {
        DetailDebugPrint("[GroupFlick][F] 1\n");
        goto cleanup_flick;
    }

    if ((start_point <= FLICK_POINT_NONE) || (FLICK_POINT_MAX <= start_point))
    {
        DetailDebugPrint("[GroupFlick][F] 2\n");
        goto cleanup_flick;
    }

    switch (type)
    {
        case ET_ButtonPress:
            g_pGesture->fingers[idx].flags = PressFlagFlick;
            base_time = GetTimeInMillis();
            num_pressed = g_pGesture->num_pressed;
            switch (start_point)
            {
                case FLICK_POINT_UP:
                    if (g_pGesture->fingers[idx].py > g_pGesture->flick_press_area)
                    {
                        DetailDebugPrint("[GroupFlick][FlickDown][P] press coord is out of bound. (%d, %d)\n",
                                g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
                        DetailDebugPrint("[GroupFlick][F] 3\n");
                        LOGI("[BackKey][F] press coord is out of bound (40 pixel from upper vezel). press y=%d\n", g_pGesture->fingers[idx].py);
                        goto cleanup_flick;
                    }

                    angle_base_y = diff_base_coord = diff_current_coord = g_pGesture->fingers[idx].py;
                    angle_base_x = g_pGesture->fingers[idx].px;
                    DetailDebugPrint("[GroupFlick][FlickDown][P] px=%d, py=%d\n", g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
                    break;

                case FLICK_POINT_DOWN:
                    if (g_pGesture->fingers[idx].py < g_pGesture->screen_height - g_pGesture->flick_press_area)
                    {
                        DetailDebugPrint("[GroupFlick][FlickUp][P] press coord is out of bound. (%d, %d)\n",
                                g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
                        DetailDebugPrint("[GroupFlick][F] 4\n");
                        goto cleanup_flick;
                    }
                    angle_base_y = diff_base_coord = diff_current_coord = g_pGesture->fingers[idx].py;
                    angle_base_x = g_pGesture->fingers[idx].px;
                    break;

                case FLICK_POINT_LEFT:
                    if (g_pGesture->fingers[idx].px > g_pGesture->flick_press_area)
                    {
                        DetailDebugPrint("[GroupFlick][FlickLeft][P] press coord is out of bound. (%d, %d)\n",
                                g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);
                        DetailDebugPrint("[GroupFlick][F] 5\n");
                        goto cleanup_flick;
                    }
                    angle_base_y = diff_base_coord = diff_current_coord = g_pGesture->fingers[idx].py;
                    angle_base_x = g_pGesture->fingers[idx].px;
                    break;

                default:
                    DetailDebugPrint("[GroupFlick][F] 6\n");
                    goto cleanup_flick;
                    break;
            }

            break;

        case ET_Motion:

            motion_count++;

            if (motion_count > 15)
            {
                DetailDebugPrint("[GroupFlick][F] 6-1 motion_count=%d\n", motion_count);
                LOGI("[BackKey][F] More than 15 motion.\n");
                goto cleanup_flick;
            }

            if (!(g_pGesture->fingers[idx].flags & PressFlagFlick))
            {
                break;
            }

            switch (start_point)
            {
                case FLICK_POINT_UP:
                    diff_base_coord = diff_current_coord;
                    diff_current_coord = g_pGesture->fingers[idx].my;

                    if ((diff_current_coord - diff_base_coord) < 0)
                    {
                        DetailDebugPrint("[GroupFlick][FlickDown][M] false_diff\n");
                        false_diff_count++;
                    }

                    if (false_diff_count > FLICK_FALSE_Y_DIFF_COUNT)
                    {
                        DetailDebugPrint("[GroupFlick][FlickDown][M] false_diff_count: %d > %d\n",
                                false_diff_count, FLICK_FALSE_Y_DIFF_COUNT);
                        DetailDebugPrint("[GroupFlick][F] 7\n");
                        LOGI("[BackKey][F] Direction is wrong for 7 times.\n");
                        goto cleanup_flick;
                    }

                    if ((g_pGesture->fingers[idx].my < g_pGesture->flick_press_area) &&
                            (abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px) >(int)( g_pGesture->screen_width/2)))
                    {
                        DetailDebugPrint("[GroupFlick][FlickDown][M] move x: %d - %d, y coord: %d\n",
                                g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].my);
                        DetailDebugPrint("[GroupFlick][F] 8\n");
                        LOGI("[BackKey][F] From press point, moving x axis is more than half screen size.\n");
                        goto cleanup_flick;
                    }

                    if ((g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py) > g_pGesture->flick_minimum_height)
                    {
                        DetailDebugPrint("[GroupFlick][FlickDown][M] %d - %d < %d(min_size), angle_base_coord (%d, %d)\n",
                                g_pGesture->fingers[idx].my, g_pGesture->fingers[idx].py, g_pGesture->flick_minimum_height, angle_base_x, angle_base_y);

                        if (abs(g_pGesture->fingers[idx].mx - angle_base_x) == 0)
                        {
                            DetailDebugPrint("[GroupFlick][FlickDown][M] abs(%d - %d) = 0\n",
                                    g_pGesture->fingers[idx].mx, angle_base_x);
                            angle = 1.0f;
                        }
                        else
                        {
                            DetailDebugPrint("[GroupFlick][FlickDown][M] angle_base_x: %d, angle_base_y: %d\n",
                                    angle_base_x, angle_base_y);
                            int y_diff = abs(g_pGesture->fingers[idx].my - angle_base_y);
                            int x_diff = abs(g_pGesture->fingers[idx].mx - angle_base_x);
                            angle = (float)y_diff / (float)x_diff;
                        }

                        if (angle < 0.23f)
                        {
                            DetailDebugPrint("[GroupFlick][FlickDown][M][F] %d / %d = %f (angle)\n",
                                    abs(g_pGesture->fingers[idx].my - angle_base_y), abs(g_pGesture->fingers[idx].mx - angle_base_x), angle);
                            DetailDebugPrint("[GroupFlick][F] 9\n");
                            LOGI("[BackKey][F] angle is improper. %d < 0.23\n", angle);
                            goto cleanup_flick;
                        }

                        distance = g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py;
                        duration = GetTimeInMillis() - base_time;

                        GestureHandleGesture_Flick(num_pressed, distance, duration, direction);
                        DetailDebugPrint("[GroupFlick][FlickDown][M] FlickDown Done!!\n");
                        goto cleanup_flick_recognized;
                    }
                    else
                    {
                        if ((g_pGesture->fingers[idx].mx - diff_base_minor_coord) < 0)
                        {
                            false_minor_diff_count++;
                        }

                        if (false_minor_diff_count> FLICK_FALSE_X_DIFF_COUNT)
                        {
                            DetailDebugPrint("[GroupFlick][FlickDown][M] false_minor_diff_count: %d > %d\n",
                                    false_minor_diff_count, FLICK_FALSE_X_DIFF_COUNT);
                            DetailDebugPrint("[GroupFlick][F] 10\n");
                            goto cleanup_flick;
                        }
                    }

                    if (g_pGesture->fingers[idx].my < g_pGesture->flick_press_area)
                    {
                        angle_base_x = g_pGesture->fingers[idx].px;
                        angle_base_y = g_pGesture->fingers[idx].py;
                    }
                    DetailDebugPrint("[GroupFlick][FlickDown][M] mx=%d, my=%d, diff_x=%d, diff_y=%d\n",
                            g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].my, abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px), abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py));

                    break;

                case FLICK_POINT_DOWN:
                    diff_base_coord = diff_current_coord;
                    diff_current_coord = g_pGesture->fingers[idx].my;

                    if ((diff_base_coord - diff_current_coord) < 0)
                    {
                        false_diff_count++;
                    }

                    if (false_diff_count > FLICK_FALSE_Y_DIFF_COUNT)
                    {
                        DetailDebugPrint("[GroupFlick][FlickUp][M] false_diff_count: %d > %d\n",
                                false_diff_count, FLICK_FALSE_Y_DIFF_COUNT);
                        DetailDebugPrint("[GroupFlick][F] 11\n");
                        goto cleanup_flick;
                    }

                    if ((g_pGesture->fingers[idx].py - g_pGesture->fingers[idx].my) > g_pGesture->flick_minimum_height)
                    {
                        DetailDebugPrint("[GroupFlick][FlickUp][R] %d - %d < %d(min_size)\n",
                                g_pGesture->fingers[idx].my, g_pGesture->fingers[idx].py, g_pGesture->flick_minimum_height);
                        if (abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px) == 0)
                        {
                            DetailDebugPrint("[GroupFlick][FlickUp][R] abs(%d - %d) = 0\n",
                                    g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].px);
                            angle = 1.0f;
                        }
                        else
                        {
                            int y_diff = abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py);
                            int x_diff = abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px);
                            angle = (float)y_diff / (float)x_diff;
                        }

                        if (angle <0.5f)
                        {
                            DetailDebugPrint("[GroupFlick][FlickUp][R] %d / %d = %f (angle)\n",
                                    abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py), abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px), angle);
                            DetailDebugPrint("[GroupFlick][F] 12\n");
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

                    if ((diff_current_coord - diff_base_coord) < 0)
                    {
                        false_diff_count++;
                    }

                    if (false_diff_count > FLICK_FALSE_Y_DIFF_COUNT)
                    {
                        DetailDebugPrint("[GroupFlick][FlickLeft][M] false_diff_count: %d > %d\n",
                                false_diff_count, FLICK_FALSE_Y_DIFF_COUNT);
                        DetailDebugPrint("[GroupFlick][F] 13\n");
                        goto cleanup_flick;
                    }

                    if ((g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px) > g_pGesture->flick_minimum_height)
                    {
                        DetailDebugPrint("[GroupFlick][FlickLeft][M] %d - %d < %d(min_size)\n",
                                g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].px, g_pGesture->flick_minimum_height);

                        if (abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py) == 0)
                        {
                            DetailDebugPrint("[GroupFlick][FlickLeft][M] abs(%d - %d) = 0\n",
                                    g_pGesture->fingers[idx].my, g_pGesture->fingers[idx].py);
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
                            DetailDebugPrint("[GroupFlick][FlickLeft][M] %d / %d = %f (angle)\n",
                                    abs(g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px), abs(g_pGesture->fingers[idx].my - g_pGesture->fingers[idx].py), angle);
                            DetailDebugPrint("[GroupFlick][F] 14\n");
                            goto cleanup_flick;
                        }

                        distance = g_pGesture->fingers[idx].mx - g_pGesture->fingers[idx].px;
                        duration = GetTimeInMillis() - base_time;

                        GestureHandleGesture_Flick(num_pressed, distance, duration, direction);
                        goto cleanup_flick_recognized;
                    }

                    break;
                default:
                    DetailDebugPrint("[GroupFlick][F] 15\n");
                    goto cleanup_flick;
                    break;
            }
            break;

        case ET_ButtonRelease:
            DetailDebugPrint("[GroupFlick][R][F] 16\n");
            goto cleanup_flick;
            break;
    }

    return;

cleanup_flick:
    DetailDebugPrint("[GroupFlick] cleanup_flick \n");
    g_pGesture->recognized_gesture &= ~WFlickFilterMask;
    motion_count = 0;

cleanup_flick_recognized:
    DetailDebugPrint("[GroupFlick] Flick recognized !\n");
    g_pGesture->filter_mask |= WFlickFilterMask;
    num_pressed = 0;
    base_time = 0;
    false_diff_count = 0;
    diff_base_coord = 0;
    diff_current_coord = 0;
    angle = 0.0f;
    angle_base_x = angle_base_y = 0;
    motion_count = 0;
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

    if (timer_expired)
    {
        if (state <= GestureBegin)
        {
            state++;
        }

        switch (state)
        {
            case GestureBegin:
                DetailDebugPrint("[GroupHold] HOLD Begin !\n");
                break;

            case GestureUpdate:
                DetailDebugPrint("[GroupHold] HOLD Update !\n");
                break;
        }

        if (GestureHasFingerEventMask(GestureNotifyHold, num_pressed))
        {
            DetailDebugPrint("[GroupHold] Success 1! \n");
            GestureHandleGesture_Hold(num_pressed, base_cx, base_cy, GetTimeInMillis()-base_time, state);

            // one more time
            hold_event_timer = TimerSet(hold_event_timer, 0, g_pGesture->hold_time_threshold, GestureEventTimerHandler, (int *)&event_type);
        }
        return;
    }

    switch (type)
    {
        case ET_ButtonPress:
            g_pGesture->fingers[idx].flags |= PressFlagHold;

            if (g_pGesture->num_pressed < 2)
            {
                DetailDebugPrint("[GroupHold][P] No num_finger changed ! num_pressed=%d\n", num_pressed);
                DetailDebugPrint("[GroupHold][F] 0\n");
                return;
            }

            if (!base_area_size || g_pGesture->num_pressed > num_pressed)
            {
                if (state != GestureEnd)
                {
                    DetailDebugPrint("[GroupHold][P][cleanup] No num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
                    DetailDebugPrint("[GroupHold][F] 1\n");

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

            DetailDebugPrint("[GroupHold][P] num_pressed=%d area_size=%d, base_cx=%d, base_cy=%d\n", num_pressed, base_area_size, base_cx, base_cy);

            break;

        case ET_Motion:
            if (!(g_pGesture->fingers[idx].flags & PressFlagHold))
            {
                DetailDebugPrint("[GroupHold][M] No PressFlagHold\n");
                break;
            }

            if (num_pressed < 2)
            {
                DetailDebugPrint("[GroupHold][M] No num_finger changed ! num_pressed=%d\n", num_pressed);
                DetailDebugPrint("[GroupHold][F] 2\n");
                return;
            }

            if (num_pressed != g_pGesture->num_pressed)
            {
                if (state != GestureEnd)
                {
                    DetailDebugPrint("[GroupHold][M][cleanup] No num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
                    DetailDebugPrint("[GroupHold][F] 3\n");
                    goto cleanup_hold;
                }

                DetailDebugPrint("[GroupHold][M] num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
            }

            area_size = AREA_SIZE(&g_pGesture->area.extents);
            cx = AREA_CENTER_X(&g_pGesture->area.extents);
            cy = AREA_CENTER_Y(&g_pGesture->area.extents);

            DetailDebugPrint("[GroupHold][M] num_pressed=%d area_size=%d, base_area_size=%d, diff=%d\n", num_pressed, area_size, base_area_size, ABS(base_area_size - area_size));
            DetailDebugPrint("[GroupHold][M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
            DetailDebugPrint("[GroupHold][M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));

            if (area_size > 0 && base_area_size > 0)
            {
                if (((area_size > base_area_size) ? (double)area_size / (double)base_area_size : (double)base_area_size / (double) area_size) >= g_pGesture->hold_area_threshold)
                {
                    DetailDebugPrint("[GroupHold][M] No diff between area size(=%d) and base area size(=%d) is bigger than threshold(=%lf)!\n", area_size, base_area_size, ((area_size > base_area_size) ? (double)area_size / (double)base_area_size : (double)base_area_size / (double) area_size));
                    DetailDebugPrint("[GroupHold][F] 4\n");
                    goto cleanup_hold;
                }
            }

            if (!INBOX(&base_box_ext, cx, cy))
            {
                DetailDebugPrint("[GroupHold][M] No current center coordinates is not in base coordinates box !\n");
                DetailDebugPrint("[GroupHold][M] cx=%d, base_cx=%d, diff=%d\n", cx, base_cx, ABS(cx-base_cx));
                DetailDebugPrint("[GroupHold][M] cy=%d, base_cy=%d, diff=%d\n", cy, base_cy, ABS(cy-base_cy));
                DetailDebugPrint("[GroupHold][F] 5\n");
                goto cleanup_hold;
            }
            break;

        case ET_ButtonRelease:
            if (state != GestureEnd && num_pressed >= 2)
            {
                DetailDebugPrint("[GroupHold][R] No num_finger changed ! num_pressed=%d, g_pGesture->num_pressed=%d\n", num_pressed, g_pGesture->num_pressed);
                DetailDebugPrint("[GroupHold][F] 6\n");
                goto cleanup_hold;
            }

            if (g_pGesture->num_pressed)
            {
                DetailDebugPrint("[GroupHold][R] num_pressed=%d\n", num_pressed);
                DetailDebugPrint("[GroupHold][F] 7\n");
                break;
            }

            goto cleanup_hold;
            break;
    }

    return;

cleanup_hold:

    DetailDebugPrint("[GroupHold][cleanup_hold] enter!\n");

    if (state == GestureBegin || state == GestureUpdate)
    {
        state = GestureEnd;
        if (GestureHasFingerEventMask(GestureNotifyHold, num_pressed))
        {
            DetailDebugPrint("[GroupHold] Success 2!\n");
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

int
GestureGetMaxTmajor(InternalEvent *ev, int max_tmajor)
{
    int mt_tmajor_idx = g_pGesture->tmajor_idx;
    int mt_tmajor = 0;

    DeviceEvent *de = &ev->device_event;

    if (!de)
    {
        DetailDebugPrint("[GestureGetMaxTmajor] de is NULL !\n");
        return -1;
    }

    if (mt_tmajor_idx < 0)
    {
        DetailDebugPrint("[GestureGetMaxTmajor] One or more of axes are not supported !\n");
        return -1;
    }

    mt_tmajor = de->valuators.data[mt_tmajor_idx];

    DetailDebugPrint("[GestureGetMaxTmajor]mt_tmajor_idx=%d, mt_tmajor=%d, max_tmajor=%d\n", mt_tmajor_idx, mt_tmajor, max_tmajor);

    return ((mt_tmajor > max_tmajor) ? mt_tmajor : max_tmajor);

}

void
GestureRecognize_PalmFlick(int type, InternalEvent *ev, DeviceIntPtr device, int idx)
{
    //	static int num_pressed = 0;
    static int base_time = 0, current_time = 0;
    static int base_x[MAX_MT_DEVICES] = {0}, base_y[MAX_MT_DEVICES] = {0};
    static int update_x[MAX_MT_DEVICES] = {0}, update_y[MAX_MT_DEVICES] = {0};


    static int current_x[MAX_MT_DEVICES] = {0}, current_y[MAX_MT_DEVICES] = {0};
    static Bool press_status[MAX_MT_DEVICES] = {FALSE, FALSE};
    static Bool release_status[MAX_MT_DEVICES] = {FALSE, FALSE};

    static int line_idx[MAX_MT_DEVICES] = {0}, prev_line_idx[MAX_MT_DEVICES] = {0}, press_idx[MAX_MT_DEVICES] = {0};
    static Bool is_line_invalid[MAX_MT_DEVICES] = {TRUE, TRUE};

    static int max_tmajor[MAX_MT_DEVICES] = {0};
    static int total_max_tmajor = 0;
    static Bool is_tmajor_invalid[MAX_MT_DEVICES] = {TRUE, TRUE};

    static int mt_sync_count[MAX_MT_DEVICES] = {0};

    static Bool is_palm = FALSE;
    PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;

    static Bool is_retry = FALSE;

    int distance, direction, duration;

    if (g_pGesture->recognized_gesture && !(g_pGesture->recognized_gesture & WPalmFlickFilterMask))
    {
        PalmFlickDebugPrint("[PalmFlick] recognize other gesture already( g_pGesture->recognized_gesture: %d)\n", g_pGesture->recognized_gesture);
        PalmFlickDebugPrint("[PalmFlick][F] 1\n");
        goto flick_failed;
    }

    // only first finger recognize
    if (!(idx == 0 || idx == 1))
    {
        PalmFlickDebugPrint("[PalmFlick] idx=%d, least two fingers come on\n", idx);
        return;
    }

    switch (type)
    {
        case ET_ButtonPress:

            if (!is_palm)
            {
                if (idx == 0)
                {
                    base_time = GetTimeInMillis();
                }

                // press (x,y), touch major
                update_x[idx] = base_x[idx] = g_pGesture->fingers[idx].px;
                update_y[idx] = base_y[idx] = g_pGesture->fingers[idx].py;
                max_tmajor[idx] = g_pGesture->max_mt_tmajor[idx];
                press_status[idx] = TRUE;
                is_tmajor_invalid[idx] = FALSE;
                is_line_invalid[idx] = FALSE;

                total_max_tmajor = (total_max_tmajor > max_tmajor[idx]) ? total_max_tmajor : max_tmajor[idx];

                // press region
                press_idx[idx] = prev_line_idx[idx] = line_idx[idx] = GesturePalmGetHorizIndexWithX(base_x[idx], idx, type);

                PalmFlickDebugPrint("[PalmFlick][P] idx: %d, num_pressed: %d\n", idx, g_pGesture->num_pressed);
                PalmFlickDebugPrint("[PalmFlick][P] base_time: %d, base_x: %d, base_y: %d, line_idx: %d, touch_major=%d\n",
                        base_time, base_x[idx], base_y[idx], line_idx[idx], max_tmajor[idx]);

                // invalid region
                if (line_idx[idx] < 0 || line_idx[idx] >= PALM_HORIZ_ARRAY_COUNT)
                {
                    PalmFlickDebugPrint("[PalmFlick][P][F] No line_idx is invalid.. base_x: %d, line_idx: %d\n", base_x[idx], line_idx[idx]);
                    PalmFlickDebugPrint("[PalmFlick][F] 2\n");
                    goto flick_failed;
                }

                // check press point when there are two fingers
                if (idx == 1)
                {
                    if (press_idx[0] != press_idx[1])
                    {
                        PalmFlickDebugPrint("[PalmFlick] Press line_idx is the different between two fingers. 1st finger_line_idx=%d, 2nd finger_line_idx=%d\n",
                                press_idx[0], press_idx[1]);
                        PalmFlickDebugPrint("[PalmFlick][F] 2-1\n");
                        goto flick_failed;
                    }
                }
            }
            else
            {
                update_x[idx] = g_pGesture->fingers[idx].px;
                update_y[idx] = g_pGesture->fingers[idx].py;

                PalmFlickDebugPrint("[PalmFlick][P] Already palm flick success. base_x=%d, base_y=%d, update_x=%d, update_y=%d\n",
                        base_x[idx], base_y[idx], update_x[idx], update_y[idx]);
            }

            break;

        case ET_Motion:

            if (total_max_tmajor > g_pGesture->palm_flick_max_tmajor_threshold)
            {
                mt_sync_count[idx]++;
                is_palm = TRUE;
                DetailDebugPrint("[PalmFlick][M] Sufficient touch enough ! max_tmajor=%d\n", total_max_tmajor);
                break;
            }

            // motion information (touch major, x, y)
            current_x[idx] = g_pGesture->fingers[idx].mx;
            current_y[idx] = g_pGesture->fingers[idx].my;
            max_tmajor[idx] = g_pGesture->max_mt_tmajor[idx];
            mt_sync_count[idx]++;

            //int temp_total_max_tmajor = (idx == 0 ? max_tmajor[0] : max_tmajor[0] + max_tmajor[1]);
            int temp_total_max_tmajor = max_tmajor[idx];
            total_max_tmajor = (total_max_tmajor > temp_total_max_tmajor ? total_max_tmajor : temp_total_max_tmajor);

            PalmFlickDebugPrint("[PalmFlick][M] idx=%d, total_max_tmajor=%d, max_tmajor[0]=%d, max_tmajor[1]=%d, current current=(%d, %d)\n",
                    idx, total_max_tmajor, max_tmajor[0], max_tmajor[1], current_x[idx], current_y[idx]);

            // exception vezel end line motion
            if (current_x[idx] < 5 || current_x[idx] > 355)
            {
                if (total_max_tmajor >= g_pGesture->palm_flick_max_tmajor_threshold)
                {
                    PalmFlickDebugPrint("[PalmFlick][M][Vezel] Sufficient touch major was came(%d)\n", total_max_tmajor);
                    is_palm = TRUE;
                }
                else
                {
                    mt_sync_count[idx]--;
                    PalmFlickDebugPrint("[PalmFlick][M] Except vezel end line condition. x=%d, sync_count=%d \n",
                            current_x[idx], mt_sync_count[idx]);
                }
                break;
            }

            // get current position
            line_idx[idx] = GesturePalmGetHorizIndexWithX(current_x[idx], idx, type);

            PalmFlickDebugPrint("[PalmFlick][M] line_idx: %d, prev_line_idx: %d, sync_count: %d\n",
                    line_idx[idx], prev_line_idx[idx], mt_sync_count[idx]);

            //error check
            if (line_idx[idx] < 0 || line_idx[idx] >= PALM_HORIZ_ARRAY_COUNT)
            {
                PalmFlickDebugPrint("[PalmFlick][M][F] No line_idx is invalid.. base_x: %d, line_idx: %d\n", base_x[idx], line_idx[idx]);
                PalmFlickDebugPrint("[PalmFlick][F] 3\n");
                goto flick_failed;
            }

            // screen capture motion validation
            if (line_idx[idx] != prev_line_idx[idx])
            {
                if (base_x[idx] <= pPalmMisc->horiz_coord[0])
                {
                    if (line_idx[idx] < prev_line_idx[idx])
                    {
                        PalmFlickDebugPrint("[PalmFlick][M][F] Invalid line_idx.. line_idx: %d, prev_line_idx: %d, pPalmMisc->horiz_coord[0]: %d\n",
                                line_idx[idx], prev_line_idx[idx], pPalmMisc->horiz_coord[0]);

                        is_line_invalid[idx] = TRUE;

                        if (is_line_invalid[0] && is_line_invalid[1])
                        {
                            PalmFlickDebugPrint("[PalmFlick][F] 4\n");
                            goto flick_failed;
                        }
                    }
                }
                else if (base_x[idx] >= pPalmMisc->horiz_coord[PALM_HORIZ_ARRAY_COUNT-1])
                {
                    if (line_idx[idx] > prev_line_idx[idx])
                    {
                        PalmFlickDebugPrint("[PalmFlick][M][F] Invalid line_idx.. line_idx: %d, prev_line_idx: %d, pPalmMisc->horiz_coord[%d]: %d\n",
                                line_idx[idx], prev_line_idx[idx], PALM_HORIZ_ARRAY_COUNT-1, pPalmMisc->horiz_coord[PALM_HORIZ_ARRAY_COUNT-1]);

                        is_line_invalid[idx] = TRUE;

                        if (is_line_invalid[0] && is_line_invalid[1])
                        {
                            PalmFlickDebugPrint("[PalmFlick][F] 5\n");
                            goto flick_failed;
                        }
                    }
                }
                prev_line_idx[idx] = line_idx[idx];
            }

            if (is_palm == FALSE)
            {
                switch (mt_sync_count[idx])
                {
                    case 1:
                        if (total_max_tmajor <= g_pGesture->palm_flick_min_tmajor_threshold)
                        {
                            PalmFlickDebugPrint("[PalmFlick][M][F] mtsync_count: %d, max_tmajor: %d(%d) line_idx: %d\n",
                                    mt_sync_count[idx], total_max_tmajor, g_pGesture->palm_flick_min_tmajor_threshold, line_idx[idx]);
                            PalmFlickDebugPrint("[PalmFlick][F] 6\n");
                            is_tmajor_invalid[idx] = TRUE;
                            //goto flick_failed;
                        }
                        break;
                    case 2:
                        if (total_max_tmajor <= (g_pGesture->palm_flick_max_tmajor_threshold - 10))
                        {
                            PalmFlickDebugPrint("[PalmFlick][M][F] mtsync_count: %d, max_tmajor: %d(%d) line_idx: %d\n",
                                    mt_sync_count[idx], total_max_tmajor, g_pGesture->palm_flick_max_tmajor_threshold-10, line_idx[idx]);
                            PalmFlickDebugPrint("[PalmFlick][F] 7\n");
                            is_tmajor_invalid[idx] = TRUE;
                            //goto flick_failed;
                        }
                        break;
                    case 3:
                        if (total_max_tmajor < g_pGesture->palm_flick_max_tmajor_threshold)
                        {
                            PalmFlickDebugPrint("[PalmFlick][M][F] mtsync_count: %d, max_tmajor: %d(%d) line_idx: %d\n",
                                    mt_sync_count[idx], total_max_tmajor, g_pGesture->palm_flick_max_tmajor_threshold, line_idx[idx]);
                            PalmFlickDebugPrint("[PalmFlick][F] 8\n");
                            is_tmajor_invalid[idx] = TRUE;
                            //goto flick_failed;
                        }
                        break;
                    default:
                        PalmFlickDebugPrint("[PalmFlick][M] See more next motion...\n");
                        break;
                }
            }

            if (is_tmajor_invalid[0] && is_tmajor_invalid[1])
            {
                PalmFlickDebugPrint("[PalmFlick][M][F] max_tmajor=%d\n", total_max_tmajor);
                goto flick_failed;
            }

            current_time = GetTimeInMillis();

            if (current_time - base_time > g_pGesture->palm_flick_time_threshold)
            {
                PalmFlickDebugPrint("[PalmFlick][M][F] Release event were not came too long time (%d - %d > %d)\n", current_time, base_time, g_pGesture->palm_flick_time_threshold);
                PalmFlickDebugPrint("[PalmFlick][F] 10\n");
                goto flick_failed;
            }

            break;

        case ET_ButtonRelease:
            current_x[idx] = g_pGesture->fingers[idx].mx;
            current_y[idx] = g_pGesture->fingers[idx].my;
            release_status[idx] = TRUE;

            if ((update_x[idx] == current_x[idx]) && (update_y[idx] == current_y[idx]))
            {
                PalmFlickDebugPrint("[PalmFlick][R][F] Press point and release point are the same. base_x=%d, base_y=%d, current_x=%d, current_y=%d\n",
                        update_x[idx], update_y[idx], current_x[idx], current_y[idx]);
                PalmFlickDebugPrint("[PalmFlick][F] 10-1\n");
                break;
                //goto flick_failed;
            }

            if (!is_palm)
            {
                is_tmajor_invalid[idx] = TRUE;

                if (is_tmajor_invalid[0] && is_tmajor_invalid[1])
                {
                    PalmFlickDebugPrint("[PalmFlick][R][F] Insufficient touch major was came(%d)\n", total_max_tmajor);
                    PalmFlickDebugPrint("[PalmFlick][F] 11\n");
                    goto flick_failed;
                }
            }

            line_idx[idx] = GesturePalmGetHorizIndexWithX(current_x[idx], idx, type);

            if (is_palm && line_idx[idx] == 1)
            {
                PalmFlickDebugPrint("[PalmFlick][R] Enough major, but release. base_x=%d, base_y=%d, current_x=%d, current_y=%d\n",
                        base_x[idx], base_y[idx], current_x[idx], current_y[idx]);
                is_retry = TRUE;
                mt_sync_count[idx] = 0;
                break;
            }

            if (line_idx[idx] < 0 || line_idx[idx] > PALM_HORIZ_ARRAY_COUNT - 1)
            {
                is_line_invalid[idx] = TRUE;

                if (is_line_invalid[0] && is_line_invalid[1])
                {
                    PalmFlickDebugPrint("[PalmFlick][R][F] No line_idx is invalid.. base_x: %d, current_x: %d\n", base_x[idx], current_x[idx]);
                    PalmFlickDebugPrint("[PalmFlick][F] 12\n");
                    goto flick_failed;
                }
            }

            current_time = GetTimeInMillis();

            if (current_time - base_time > g_pGesture->palm_flick_time_threshold)
            {
                PalmFlickDebugPrint("[PalmFlick][R][F] Release event were came to have long delay (%d - %d > %d)\n",
                        current_time, base_time, g_pGesture->palm_flick_time_threshold);
                PalmFlickDebugPrint("[PalmFlick][F] 13\n");
                goto flick_failed;
            }

            direction = (line_idx[idx] <= 1) ? FLICK_EASTWARD : FLICK_WESTWARD;
            distance = ABS(current_x[idx] - base_x[idx]);
            duration = current_time - base_time;

            if (!is_retry)
            {
                if (GestureHasFingerEventMask(GestureNotifyFlick, 0))
                {
                    PalmFlickDebugPrint("[PalmFlick][R] Palm Flick1 !!!, direction=%d, distance=%d\n", direction, distance);
                    is_palm = FALSE;
                    GestureHandleGesture_Flick(0, distance, duration, direction);
                }
            }
            else
            {
                if (mt_sync_count[idx] < 25)
                {
                    PalmFlickDebugPrint("[PalmFlick][R][F] No enough motion=%d\n", mt_sync_count[idx]);
                    PalmFlickDebugPrint("[PalmFlick][F] 14\n");
                    goto flick_failed;
                }
                else
                {
                    if (GestureHasFingerEventMask(GestureNotifyFlick, 0))
                    {
                        PalmFlickDebugPrint("[PalmFlick][R] Palm Flick2 !!!, direction=%d, distance=%d\n", direction, distance);
                        is_palm = FALSE;
                        GestureHandleGesture_Flick(0, distance, duration, direction);
                    }
                }
            }

            g_pGesture->recognized_gesture |= WPalmFlickFilterMask;

            goto cleanup_flick;
            break;
    }

    return;

flick_failed:

    DetailDebugPrint("[PalmFlick][R] flick failed\n");

    g_pGesture->recognized_gesture &= ~WPalmFlickFilterMask;
    g_pGesture->filter_mask |= WPalmFlickFilterMask;
    goto cleanup_flick;

cleanup_flick:

    DetailDebugPrint("[PalmFlick][R] cleanup_flick\n");

    for (int i = 0; i < MAX_MT_DEVICES; i++)
    {
        base_x[i] = 0;
        base_y[i] = 0;
        update_x[i] = 0;
        update_y[i] = 0;
        current_x[i] = 0;
        current_y[i] = 0;
        line_idx[i] = 0;
        press_idx[i] = 0;
        prev_line_idx[i] = 0;
        max_tmajor[i] = 0;
        mt_sync_count[i] = 0;
        press_status[i] = FALSE;
        release_status[i] = FALSE;
        is_tmajor_invalid[i] = TRUE;
        is_line_invalid[i] = TRUE;
    }

    total_max_tmajor = 0;
    is_palm = FALSE;
    is_retry = FALSE;

    return;
}

static int
GesturePalmGetHorizIndexWithX(int current_x, int idx, int type)
{
    int i;
    int ret_idx = -1;
    static int pressed_idx[MAX_MT_DEVICES] = {-1, -1};
    PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;

    for (i = 0; i < PALM_HORIZ_ARRAY_COUNT; i++)
    {
        if (current_x <= pPalmMisc->horiz_coord[i])
        {
            DetailDebugPrint("[GesturePalmGetHorizIndexWithX] index=%d, pPalmMisc->horiz_coord[%d]=%d\n", i, i, pPalmMisc->horiz_coord[i]);

            ret_idx = i;
            goto index_check;
        }
    }

    DetailDebugPrint("[GesturePalmGetHorizIndexWithX]Error ! Failed to get horiz coordinate index !\n");
    return ret_idx;

index_check:

    if (type == ET_ButtonPress)
    {
        pressed_idx[idx] = ret_idx;

        // first press is center
        if (pressed_idx[idx] == PALM_HORIZ_ARRAY_COUNT -2)
        {
            DetailDebugPrint("[GesturePalmGetHorizIndexWithX][P] Invalid press area x=%d, idx=%d, pressed_idx=%d\n", current_x, idx, pressed_idx[idx]);
            ret_idx = -1;
        }

        DetailDebugPrint("[GesturePalmGetHorizIndexWithX][P] pressed_idx=%d\n", pressed_idx[idx]);
    }

    else if (type == ET_Motion)
    {
        DetailDebugPrint("[GesturePalmGetHorizIndexWithX][M] moving x=%d, idx=%d, pressed_idx=%d\n", current_x, idx, pressed_idx[idx]);
    }

    else if (type == ET_ButtonRelease)
    {
        if ((pressed_idx[idx] == 0) && (ret_idx == (PALM_HORIZ_ARRAY_COUNT - 1)))
        {
            DetailDebugPrint("[GesturePalmGetHorizIndexWithX][R] From the left to the right ! pressed_idx=%d, ret_idx=%d\n", pressed_idx[idx], ret_idx);
        }
        else if ((pressed_idx[idx] == (PALM_HORIZ_ARRAY_COUNT - 1)) && (ret_idx == 0))
        {
            DetailDebugPrint("[GesturePalmGetHorizIndexWithX][R] From the right to the left ! pressed_idx=%d, ret_idx=%d\n", pressed_idx[idx], ret_idx);
        }
        else if ((pressed_idx[idx] == ret_idx) && ret_idx != 1)
        {
            DetailDebugPrint("[GesturePalmGetHorizIndexWithX][R] Relased the same region ! pressed_idx=%d, ret_idx=%d\n", pressed_idx[idx], ret_idx);
            return 1;
        }
        else
        {
            DetailDebugPrint("[GesturePalmGetHorizIndexWithX][R] Invalid ! pressed_idx=%d, released_idx=%d\n", pressed_idx[idx], ret_idx);
            ret_idx = -1;
        }

        pressed_idx[idx] = -1;
    }

    return ret_idx;
}

static int
GesturePalmGetAbsAxisInfo(DeviceIntPtr dev)
{
    int i, found = 0;
    int numAxes;

    Atom atom_tpalm;
    Atom atom_mt_slot;
    Atom atom_tracking_id;
    Atom atom_tmajor;
    Atom atom_tminor;

    g_pGesture->tpalm_idx = -1;
    g_pGesture->tmajor_idx = -1;
    g_pGesture->tminor_idx = -1;

    if (!dev || !dev->valuator)
        goto out;

    numAxes = dev->valuator->numAxes;

    atom_tpalm = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_PALM);
    atom_mt_slot = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_SLOT);
    atom_tracking_id = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_TRACKING_ID);
    atom_tmajor = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_TOUCH_MAJOR);
    atom_tminor = XIGetKnownProperty(AXIS_LABEL_PROP_ABS_MT_TOUCH_MINOR);

    if (!numAxes || !atom_tpalm || !atom_tmajor || !atom_tminor)
    {
        ErrorF("one or more axes is/are not supported!\n");
        goto out;
    }

    for (i = 0; i < numAxes; i++)
    {
        AxisInfoPtr axes = &dev->valuator->axes[i];

        if (!axes || (axes->mode != Absolute))
            continue;

        if (axes->label == atom_tpalm)
        {
            g_pGesture->tpalm_idx = i;
            found += 1;
        }
        else if (axes->label == atom_tmajor)
        {
            g_pGesture->tmajor_idx = i;
            found += 2;
        }
        else if (axes->label == atom_tminor)
        {
            g_pGesture->tminor_idx = i;
            found += 4;
        }
    }

    if (found != 7)
    {
        ErrorF("Axes for palm recognization are not supported !\n");
        goto out;
    }

    g_pGesture->palm_misc.enabled = 1;
    ErrorF("Axes for palm recognization are supported !\n");
    return 1;

out:
    g_pGesture->palm_misc.enabled = 0;
    ErrorF("Palm recognization is not supported !\n");
    return 0;
}

static int
GestureGetPalmValuator(InternalEvent *ev, DeviceIntPtr device)
{
    int mt_palm_idx = g_pGesture->tpalm_idx;
    int mt_palm = 0;

    DeviceEvent *de = &ev->device_event;

    if (!de)
    {
        ErrorF("[GestureGetPalmValuator] de is NULL !\n");
        return -1;
    }

    if (mt_palm_idx < 0)
    {
        ErrorF("[GestureGetPalmValuator] One or more of axes are not supported !\n");
        return -1;
    }

    mt_palm = de->valuators.data[mt_palm_idx];

    HoldDetectorDebugPrint("[GestureGetPalmValuator] mt_palm:%d\n", mt_palm);

    return mt_palm;
}

static void GestureHoldDetector(int type, InternalEvent *ev, DeviceIntPtr device)
{
    int i;
    int idx = -1;
    pixman_region16_t tarea1;
    static int num_pressed = 0;
    unsigned int hold_area_size;
    PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;
    int palm_flag = 0;

    if (!g_pGesture->has_hold_grabmask)
    {
        HoldDetectorDebugPrint("[GestureHoldDetector] g_pGesture->has_hold_grabmask=%d\n", g_pGesture->has_hold_grabmask);

        Mask eventmask = (1L << GestureNotifyHold);

        if ((g_pGesture->grabMask & eventmask) &&
                (g_pGesture->GrabEvents[GestureNotifyHold].pGestureGrabWinInfo[0].window != None))
        {
            g_pGesture->has_hold_grabmask = 1;

            //Initialize a set of variables
            num_pressed = 0;
            memset(&g_pGesture->cts, 0, sizeof(g_pGesture->cts));
            pixman_region_init(&g_pGesture->chold_area);

            HoldDetectorDebugPrint("[GestureHoldDetector] Initialize...\n");
        }
        else
        {
            //reset local hold_grab_mask variable
            g_pGesture->has_hold_grabmask = 0;

            g_pGesture->hold_detector_activate = 0;
            HoldDetectorDebugPrint("[GestureHoldDetector] has_hold_grabmask=0 and g_pGesture->hold_detector_activate=0\n");
            return;
        }
    }

    if (IGNORE_EVENTS == g_pGesture->ehtype ||
            device->id < g_pGesture->first_fingerid)
    {
        HoldDetectorDebugPrint("[GestureHoldDetector] Return (IGNORE_EVENTS or device->id:%d < first_fingerid:%d)\n", device->id, g_pGesture->first_fingerid);
        return;
    }

    palm_flag = GestureGetPalmValuator(ev, device);

    if (palm_flag)
    {
        GestureHandleGesture_Hold(0, 0, 0, PALM_HOLD_TIME_THRESHOLD, GestureBegin);
        GestureHandleGesture_Hold(0, 0, 0, PALM_HOLD_TIME_THRESHOLD, GestureEnd);

        g_pGesture->hold_detector_activate = 0;
        g_pGesture->has_hold_grabmask = 0;
        HoldDetectorDebugPrint("[GestureHoldDetector] palm_flag:%d enable\n", palm_flag);
        return;
    }
    else
    {
        HoldDetectorDebugPrint("[GestureHoldDetector] palm_flag:%d disable\n", palm_flag);
        return;
    }

    HoldDetectorDebugPrint("[GestureHoldDetector] g_pGesture->num_mt_devices:%d\n", g_pGesture->num_mt_devices);

    for (i = 0; i < g_pGesture->num_mt_devices; i++)
    {
        if ( device->id == g_pGesture->mt_devices[i]->id)
        {
            idx = i;
            HoldDetectorDebugPrint("[GestureHoldDetector] idx:%d\n", idx);
            break;
        }
    }
    if ((idx < 0) || ((MAX_MT_DEVICES-1) < idx)) return;

    switch (type)
    {
        case ET_ButtonPress:
            g_pGesture->cts[idx].status = BTN_PRESSED;
            g_pGesture->cts[idx].cx = ev->device_event.root_x;
            g_pGesture->cts[idx].cy = ev->device_event.root_y;
            num_pressed++;
            HoldDetectorDebugPrint("[GestureHoldDetector][P] cx:%d, cy:%d, num_pressed:%d\n", g_pGesture->cts[idx].cx, g_pGesture->cts[idx].cy, num_pressed);

            if (num_pressed < 2)
            {
                HoldDetectorDebugPrint("[GestureHoldDetector][P] num_pressed:%d\n", num_pressed);
                break;
            }

            if (num_pressed > g_pGesture->num_mt_devices)
                num_pressed = g_pGesture->num_mt_devices;

            pixman_region_init(&tarea1);
            pixman_region_init(&g_pGesture->chold_area);
            pixman_region_init_rect(&tarea1, g_pGesture->cts[0].cx, g_pGesture->cts[0].cy, g_pGesture->cts[0].cx+1, g_pGesture->cts[0].cy+1);

            tarea1.extents.x1 = g_pGesture->cts[0].cx;
            tarea1.extents.x2 = g_pGesture->cts[0].cx+1;
            tarea1.extents.y1 = g_pGesture->cts[0].cy;
            tarea1.extents.y2 = g_pGesture->cts[0].cy+1;

            pixman_region_union(&g_pGesture->chold_area, &tarea1, &tarea1);

            for (i = 1; i < num_pressed; i++)
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
            if (BTN_RELEASED == g_pGesture->cts[idx].status)
                return;

            g_pGesture->cts[idx].status = BTN_MOVING;
            g_pGesture->cts[idx].cx = ev->device_event.root_x;
            g_pGesture->cts[idx].cy = ev->device_event.root_y;

            HoldDetectorDebugPrint("[GestureHoldDetector][M] cx:%d, cy:%d, num_pressed:%d\n", g_pGesture->cts[idx].cx, g_pGesture->cts[idx].cy, num_pressed);

            if (num_pressed < 2)
            {
                HoldDetectorDebugPrint("[GestureHoldDetector][M] num_pressed:%d\n", num_pressed);
                break;
            }

            pixman_region_init(&tarea1);
            pixman_region_init(&g_pGesture->chold_area);
            pixman_region_init_rect(&tarea1, g_pGesture->cts[0].cx, g_pGesture->cts[0].cy, g_pGesture->cts[0].cx+1, g_pGesture->cts[0].cy+1);

            tarea1.extents.x1 = g_pGesture->cts[0].cx;
            tarea1.extents.x2 = g_pGesture->cts[0].cx+1;
            tarea1.extents.y1 = g_pGesture->cts[0].cy;
            tarea1.extents.y2 = g_pGesture->cts[0].cy+1;

            pixman_region_union(&g_pGesture->chold_area, &tarea1, &tarea1);

            for (i = 1; i < num_pressed; i++)
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

            HoldDetectorDebugPrint("[GestureHoldDetector][R] cx:%d, cy:%d\n", g_pGesture->cts[idx].cx, g_pGesture->cts[idx].cy);

            num_pressed--;
            if (num_pressed <3)
            {
                pixman_region_init(&g_pGesture->chold_area);
            }
            break;
    }

    if (num_pressed >= 2)
    {
        hold_area_size = AREA_SIZE(&g_pGesture->chold_area.extents);

        HoldDetectorDebugPrint("[GestureHoldDetector] hold_area_size=%d, pPalmMisc->half_scrn_area_size=%d\n", hold_area_size, pPalmMisc->half_scrn_area_size);

        if (pPalmMisc->half_scrn_area_size <= hold_area_size)
        {
            GestureHandleGesture_Hold(0, AREA_CENTER_X(&g_pGesture->chold_area.extents), AREA_CENTER_Y(&g_pGesture->chold_area.extents), PALM_HOLD_TIME_THRESHOLD, GestureBegin);
            GestureHandleGesture_Hold(0, AREA_CENTER_X(&g_pGesture->chold_area.extents), AREA_CENTER_Y(&g_pGesture->chold_area.extents), PALM_HOLD_TIME_THRESHOLD, GestureEnd);

            g_pGesture->hold_detector_activate = 0;
            g_pGesture->has_hold_grabmask = 0;
        }
    }
    else
    {
        hold_area_size = AREA_SIZE(&g_pGesture->chold_area.extents);
        HoldDetectorDebugPrint("[GestureHoldDetector] num_pressed is under 2, hold_area_size=%d\n", hold_area_size);
    }
}


static int
GesturePalmGetScreenInfo()
{
    int i;
    pixman_region16_t tarea;
    PalmMiscInfoPtr pPalmMisc = &g_pGesture->palm_misc;
    ScreenPtr pScreen = miPointerCurrentScreen();

    if (!pScreen)
    {
        DetailDebugPrint("[X11][GesturePalmGetScreenInfo]Failed to get screen information !\n");

        pPalmMisc->scrn_width = pPalmMisc->scrn_height = 0;
        return 0;
    }

    pPalmMisc->scrn_width = pScreen->width;
    pPalmMisc->scrn_height = pScreen->height;
    pixman_region_init(&tarea);
    pixman_region_init_rect(&tarea, 0, 0, pPalmMisc->scrn_width, pPalmMisc->scrn_height);

    DetailDebugPrint("[X11][GesturePalmGetScreenInfo] x2:%d, x2:%d, y2:%d, y1:%d \n", tarea.extents.x2, tarea.extents.x1, tarea.extents.y2, tarea.extents.y1);
    pPalmMisc->half_scrn_area_size = AREA_SIZE(&tarea.extents);
    pPalmMisc->half_scrn_area_size = (unsigned int)((double)pPalmMisc->half_scrn_area_size / 4);

    DetailDebugPrint("[X11][GesturePalmGetScreenInfo] pPalmMisc->half_scrn_area_size = %d\n", pPalmMisc->half_scrn_area_size);

    for (i = 0; i < PALM_HORIZ_ARRAY_COUNT; i++)
    {
        pPalmMisc->horiz_coord[i] = pPalmMisc->scrn_width * ((i+1)/(double)PALM_HORIZ_ARRAY_COUNT);
        DetailDebugPrint("[X11][GesturePalmGetScreenInfo] pPalmMisc->horiz_coord[%d]=%d, pPalmMisc->scrn_width=%d\n", i, pPalmMisc->horiz_coord[i], pPalmMisc->scrn_width);
    }
    for (i = 0; i < PALM_VERTI_ARRAY_COUNT; i++)
    {
        pPalmMisc->verti_coord[i] = pPalmMisc->scrn_height * ((i+1)/(double)PALM_VERTI_ARRAY_COUNT);
        DetailDebugPrint("[X11][GesturePalmGetScreenInfo] pPalmMisc->verti_coord[%d]=%d, pPalmMisc->scrn_height=%d\n", i, pPalmMisc->verti_coord[i], pPalmMisc->scrn_height);
    }

    return 1;
}


static inline void
GestureEnableDisable()
{
    GestureEnable(1, FALSE, g_pGesture->this_device);
#if 0
    if ((g_pGesture->grabMask) || (g_pGesture->lastSelectedWin != None))
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

    if (pWin)
    {
        DetailDebugPrint("[GestureGetEventsWindow] pWin->drawable.id=0x%x\n", pWin->drawable.id);
        g_pGesture->gestureWin = pWin->drawable.id;
    }
    else
    {
        DetailDebugPrint("[GestureGetEventsWindow] GestureWindowOnXY returns NULL !\n");
        return NULL;
    }

    if (g_pGesture->gestureWin == g_pGesture->lastSelectedWin)
    {
        g_pGesture->eventMask = g_pGesture->lastSelectedMask;
        goto nonempty_eventmask;
    }

    //check selected event(s)
    if (!GestureHasSelectedEvents(pWin, &g_pGesture->eventMask))
    {
        g_pGesture->eventMask = 0;
    }
    else
    {
        g_pGesture->lastSelectedWin = g_pGesture->gestureWin;
        g_pGesture->lastSelectedMask = g_pGesture->eventMask;
    }

    if (!g_pGesture->eventMask && !g_pGesture->grabMask)
    {
        DetailDebugPrint("[X11][GestureGetEventsWindow] No grabbed events or no events were selected for window(0x%x) !\n", pWin->drawable.id);
        return NULL;
    }

nonempty_eventmask:

    DetailDebugPrint("[X11][GestureGetEventsWindow] g_pGesture->eventMask=0x%x\n", g_pGesture->eventMask);

    mask = (GESTURE_FILTER_MASK_ALL & ~(g_pGesture->grabMask | g_pGesture->eventMask));

    DetailDebugPrint("[X11][GestureGetEventsWindow] g_pGesture->filter_mask=0x%x, mask=0x%x\n", g_pGesture->filter_mask, mask);

    g_pGesture->filter_mask = mask;

    DetailDebugPrint("[X11][GestureGetEventsWindow] g_pGesture->filter_mask=0x%x\n", g_pGesture->filter_mask);

    return pWin;
}

static CARD32
GestureSingleFingerTimerHandler(OsTimerPtr timer, CARD32 time, pointer arg)
{
    g_pGesture->filter_mask |= WTapFilterMask;
    g_pGesture->filter_mask |= WHoldFilterMask;
    LOGI("[GroupTap][GroupHold] 50ms after 1st finger.\n");

    DetailDebugPrint("[GestureSingleFingerTimerHandler] TapFilterMask, HoldFilterMask \n");

    if ((g_pGesture->event_sum[0] == BTN_PRESSED) && ((g_pGesture->flick_pressed_point <= FLICK_POINT_NONE) && (FLICK_POINT_MAX <= g_pGesture->flick_pressed_point)))
    {
        DetailDebugPrint("[GestureSingleFingerTimerHandler] press_point: %d\n", g_pGesture->flick_pressed_point);
        DetailDebugPrint("[GestureSingleFingerTimerHandler] FlickFilterMask\n");
        g_pGesture->filter_mask |= WFlickFilterMask;
    }

    if (g_pGesture->flick_pressed_point == FLICK_POINT_DOWN && abs(g_pGesture->fingers[0].py - g_pGesture->fingers[0].my) < 3)
    {
        DetailDebugPrint("[GestureSingleFingerTimerHandler] py: %d, my: %d\n", g_pGesture->fingers[0].py, g_pGesture->fingers[0].my);
        DetailDebugPrint("[GestureSingleFingerTimerHandler] FlickFilterMask\n");
        g_pGesture->filter_mask |= WFlickFilterMask;
    }

    DetailDebugPrint("[GestureSingleFingerTimerHandler] expired !\n");

    if (g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL)
    {
        if ( ERROR_INVALPTR == GestureFlushOrDrop())
        {
            DetailDebugPrint("[GestureSingleFingerTimerHandler] AllFilterMask, Flush!\n");
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

    if (PROPAGATE_EVENTS == g_pGesture->ehtype || device->id < g_pGesture->first_fingerid)
    {
        return;
    }

    for (i = 0; i < g_pGesture->num_mt_devices; i++)
    {
        if (device->id == g_pGesture->mt_devices[i]->id)
        {
            idx = i;
            break;
        }
    }

    if (idx < 0)
        return;

    switch (type)
    {
        case ET_ButtonPress:
            if (idx == 0)
            {
                g_pGesture->event_sum[0] = BTN_PRESSED;
            }

            g_pGesture->max_mt_tmajor[idx] = GestureGetMaxTmajor(ev, g_pGesture->max_mt_tmajor[idx]);

            g_pGesture->fingers[idx].ptime = ev->any.time;
            g_pGesture->fingers[idx].px = ev->device_event.root_x;
            g_pGesture->fingers[idx].py = ev->device_event.root_y;

            g_pGesture->finger_rects[idx].extents.x1 = ev->device_event.root_x - FINGER_WIDTH;
            g_pGesture->finger_rects[idx].extents.x2 = ev->device_event.root_x + FINGER_WIDTH;
            g_pGesture->finger_rects[idx].extents.y1 =  ev->device_event.root_y - FINGER_HEIGHT;
            g_pGesture->finger_rects[idx].extents.y2 =  ev->device_event.root_y + FINGER_HEIGHT;

            g_pGesture->num_pressed++;
            g_pGesture->inc_num_pressed = g_pGesture->num_pressed;

            if (g_pGesture->inc_num_pressed == 1)
            {
                pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
            }
            else
            {
                pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);

                for (i = 1; i < g_pGesture->inc_num_pressed; i++)
                {
                    pixman_region_union(&g_pGesture->area, &g_pGesture->area, &g_pGesture->finger_rects[i]);
                }
            }

            DetailDebugPrint("[GestureRecognize][P] num_pressed=%d, area_size=%d, px=%d, py=%d\n",
                    g_pGesture->num_pressed, AREA_SIZE(&g_pGesture->area.extents), g_pGesture->fingers[idx].px, g_pGesture->fingers[idx].py);

            if (g_pGesture->num_pressed == 1)
            {
                single_finger_timer = TimerSet(single_finger_timer, 0, 50, GestureSingleFingerTimerHandler, NULL);

                if (g_pGesture->fingers[idx].py <= g_pGesture->flick_press_area)
                {
                    if ((!g_pGesture->activate_flick_down)
                            || (g_pGesture->fingers[idx].px <= (g_pGesture->flick_press_area_left_right))
                            || (g_pGesture->fingers[idx].px >= (g_pGesture->screen_width - g_pGesture->flick_press_area_left_right)))
                    {
                        DetailDebugPrint("[GestureRecognize][P] px=%d, flick_press_area_left_right=%d\n",
                            g_pGesture->fingers[idx].px, g_pGesture->flick_press_area_left_right);
                        g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
                        LOGI("[BackKey][F] Press touch within 40 pixel area from left or right vezel\n");
                    }
                    else
                    {
                        DetailDebugPrint("[GestureRecognize][P] FLICK_POINT_UP\n");
                        g_pGesture->flick_pressed_point = FLICK_POINT_UP;
                    }
                }
                else
                {
                    LOGI("[BackKey][F] Press touch outside 40 pixel area from upper vezel. \n");
                }

                if (g_pGesture->fingers[idx].py >= (g_pGesture->screen_height - g_pGesture->flick_press_area))
                {
                    if (!g_pGesture->activate_flick_up)
                    {
                        g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
                    }
                    else
                    {
                        DetailDebugPrint("[GestureRecognize][P] FLICK_POINT_DOWN\n");
                        g_pGesture->flick_pressed_point = FLICK_POINT_DOWN;
                    }
                }
                else if ( g_pGesture->fingers[idx].px <= g_pGesture->flick_press_area_left)
                {
                    if (!g_pGesture->activate_flick_right)
                    {
                        g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
                    }
                    else
                    {
                        DetailDebugPrint("[GestureRecognize][P] FLICK_POINT_LEFT\n");
                        g_pGesture->flick_pressed_point = FLICK_POINT_LEFT;
                    }
                }

                DetailDebugPrint("[GestureRecognize][P] flick_press_point: %d\n", g_pGesture->flick_pressed_point);

                if ((g_pGesture->flick_pressed_point <= FLICK_POINT_NONE) || (FLICK_POINT_MAX <= g_pGesture->flick_pressed_point))
                {
                    DetailDebugPrint("[GestureRecognize][P] FLICK_POINT_NONE\n");
                    g_pGesture->filter_mask |= WFlickFilterMask;
                    g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
                }
                else
                {
                    g_pGesture->flick_direction = (g_pGesture->flick_pressed_point - 1) * 2;
                    if ((g_pGesture->flick_direction == FLICK_WESTWARD) && (g_pGesture->power_pressed != 2))
                    {
                        DetailDebugPrint("[GestureRecognize][P] Flick WesWard is disable when power is not pressed\n");
                        g_pGesture->filter_mask |= WFlickFilterMask;
                        g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
                    }
                    if ((g_pGesture->flick_direction < FLICK_NORTHWARD) || (FLICK_NORTHWESTWARD < g_pGesture->flick_direction))
                    {
                        DetailDebugPrint("[GestureRecognize][P] Invalid flick direction\n");
                        g_pGesture->filter_mask |= WFlickFilterMask;
                        g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
                    }

                    DetailDebugPrint("[GestureRecognize][P] flick_direction: %d\n", g_pGesture->flick_direction);
                }
            }
            else
            {
                DetailDebugPrint("[GestureRecognize][P] Two more fingers come on!\n");
                TimerCancel(single_finger_timer);
                single_finger_timer = NULL;
            }
            break;

        case ET_Motion:

            if (!g_pGesture->fingers[idx].ptime)
            {
                DetailDebugPrint("[GestureRecognize][M] Start motion. idx=%d\n", idx);
                g_pGesture->max_mt_tmajor[idx] = GestureGetMaxTmajor(ev, g_pGesture->max_mt_tmajor[idx]);
                return;
            }

            g_pGesture->fingers[idx].mx = ev->device_event.root_x;
            g_pGesture->fingers[idx].my = ev->device_event.root_y;
            g_pGesture->max_mt_tmajor[idx] = GestureGetMaxTmajor(ev, g_pGesture->max_mt_tmajor[idx]);

            if (idx == 0)
            {
                g_pGesture->event_sum[0] += BTN_MOVING;
            }

            g_pGesture->finger_rects[idx].extents.x1 = ev->device_event.root_x - FINGER_WIDTH;
            g_pGesture->finger_rects[idx].extents.x2 = ev->device_event.root_x + FINGER_WIDTH;
            g_pGesture->finger_rects[idx].extents.y1 =  ev->device_event.root_y - FINGER_HEIGHT;
            g_pGesture->finger_rects[idx].extents.y2 =  ev->device_event.root_y + FINGER_HEIGHT;

            if (g_pGesture->inc_num_pressed == 1)
            {
                pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);
            }
            else
            {
                pixman_region_union(&g_pGesture->area, &g_pGesture->finger_rects[0], &g_pGesture->finger_rects[0]);

                for (i = 1; i < g_pGesture->inc_num_pressed; i++)
                {
                    pixman_region_union(&g_pGesture->area, &g_pGesture->area, &g_pGesture->finger_rects[i]);
                }
            }

            DetailDebugPrint("[GestureRecognize][M] num_pressed=%d, area_size=%d, mx=%d, my=%d\n",
                    g_pGesture->num_pressed, AREA_SIZE(&g_pGesture->area.extents), g_pGesture->fingers[idx].mx, g_pGesture->fingers[idx].my);

            break;

        case ET_ButtonRelease:
            g_pGesture->fingers[idx].rtime = ev->any.time;
            g_pGesture->fingers[idx].rx = ev->device_event.root_x;
            g_pGesture->fingers[idx].ry = ev->device_event.root_y;
            g_pGesture->max_mt_tmajor[idx] = GestureGetMaxTmajor(ev, g_pGesture->max_mt_tmajor[idx]);

            g_pGesture->num_pressed--;

            if (g_pGesture->num_pressed == 0)
            {
                DetailDebugPrint("[GestureRecognize] All fingers were released !\n");
            }
            else if (g_pGesture->num_pressed < 0)
            {
                DetailDebugPrint("[GestureRecognize] All fingers were released. But, num_pressed is under 0 !\n");
            }

            DetailDebugPrint("[GestureRecognize][R] num_pressed=%d, rx=%d, ry=%d\n",
                    g_pGesture->num_pressed, g_pGesture->fingers[idx].rx, g_pGesture->fingers[idx].ry);

            break;
    }

    if (!(g_pGesture->filter_mask & WFlickFilterMask))
    {
        DetailDebugPrint("[GestureRecognize] GestureRecognize_groupFlick !\n");
        GestureRecognize_GroupFlick(type, ev, device, idx, g_pGesture->flick_pressed_point, g_pGesture->flick_direction);
    }
    if (!(g_pGesture->filter_mask & WTapFilterMask))
    {
        DetailDebugPrint("[GestureRecognize] GestureRecognize_groupTap !\n");
        GestureRecognize_GroupTap(type, ev, device, idx, 0);
    }
    if (!(g_pGesture->filter_mask & WHoldFilterMask))
    {
        DetailDebugPrint("[GestureRecognize] GestureRecognize_groupHold !\n");
        GestureRecognize_GroupHold(type, ev, device, idx, 0);
    }
/*
    if (!(g_pGesture->filter_mask & WPalmFlickFilterMask))
    {
        DetailDebugPrint("[GestureRecognize] GestureRecognize_palmFlick !\n");
        GestureRecognize_PalmFlick(type, ev, device, idx);
    }
*/
    DetailDebugPrint("[GestureRecognize][N] g_pGesture->filter_mask = 0x%x, g_pGesture->GESTURE_WATCH_FILTER_MASK_ALL = 0x%x, g_pGesture->recognized_gesture=0x%x\n",
            g_pGesture->filter_mask, GESTURE_WATCH_FILTER_MASK_ALL, g_pGesture->recognized_gesture);

    if (g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL)
    {
        if (!g_pGesture->recognized_gesture)
        {
            DetailDebugPrint("[GestureRecognize][F] 1 !\n");
            goto flush_or_drop;
        }
        else if (!g_pGesture->num_pressed)
        {
            DetailDebugPrint("[GestureRecognize][F] 2 !\n");
            goto flush_or_drop;
        }
    }

    if (g_pGesture->recognized_gesture)
    {
        if (g_pGesture->ehtype == KEEP_EVENTS)
        {
            DetailDebugPrint("[GestureRecognize] Keep Event !\n");
            GestureEventsDrop();
        }
        g_pGesture->ehtype = IGNORE_EVENTS;
    }
    return;

flush_or_drop:

    DetailDebugPrint("[GestureRecognize] GestureFlushOrDrop() !\n");

    if (ERROR_INVALPTR == GestureFlushOrDrop())
    {
        GestureControl(g_pGesture->this_device, DEVICE_OFF);
    }
}

ErrorStatus GestureFlushOrDrop(void)
{
    ErrorStatus err;

    if (g_pGesture->recognized_gesture)
    {
        g_pGesture->ehtype = IGNORE_EVENTS;
        GestureEventsDrop();
        DetailDebugPrint("[GestureFlushOrDrop][Drop] IGNORE_EVENTS\n");
    }
    else
    {
        g_pGesture->ehtype = PROPAGATE_EVENTS;

        err = GestureEventsFlush();

        if (ERROR_NONE != err)
        {
            return err;
        }

        DetailDebugPrint("[GestureFlushOrDrop][Flush] PROPAGATE_EVENTS\n");
        DetailDebugPrint("[GestureFlushOrDrop][Flush] g_pGesture->filter_mask = 0x%x\n", g_pGesture->filter_mask);
        DetailDebugPrint("[GestureFlushOrDrop][Flush] g_pGesture->GESTURE_WATCH_FILTER_MASK_ALL = 0x%x\n", GESTURE_WATCH_FILTER_MASK_ALL);
        DetailDebugPrint("[GestureFlushOrDrop][Flush] g_pGesture->recognized_gesture=0x%x\n", g_pGesture->recognized_gesture);
    }

    err = GestureRegionsReinit();

    if (ERROR_NONE != err)
    {
        return err;
    }

    g_pGesture->pTempWin = NULL;
    g_pGesture->inc_num_pressed = g_pGesture->num_pressed = 0;
    g_pGesture->event_sum[0] = 0;

    return ERROR_NONE;
}

void
GestureHandleMTSyncEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
    int i;
    int idx;

#ifdef __DEBUG_EVENT_HANDLER__
    DetailDebugPrint("[GestureHandleMTSyncEvent] (%d:%d) time:%d cur:%d\n",
            ev->any_event.deviceid, ev->any_event.sync, (int)ev->any.time, (int)GetTimeInMillis());
#endif

    if (!g_pGesture->is_active)
    {
        g_pGesture->ehtype = PROPAGATE_EVENTS;
        DetailDebugPrint("[GestureHandleMTSyncEvent] PROPAGATE_EVENT\n");
        return;
    }

    if (MTOUCH_FRAME_SYNC_BEGIN == ev->any_event.sync)
    {
        DetailDebugPrint("[GestureHandleMTSyncEvent] SYNC_BEGIN\n");
        g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_BEGIN;
        g_pGesture->ehtype = KEEP_EVENTS;
        g_pGesture->filter_mask = 0;
        g_pGesture->recognized_gesture = 0;
        g_pGesture->hold_detector_activate = 1;
        g_pGesture->num_pressed = 0;
        g_pGesture->has_hold_grabmask = 0;
        g_pGesture->mtsync_total_count = 0;

        for (i=0; i < g_pGesture->num_mt_devices; i++)
        {
            g_pGesture->fingers[i].ptime = 0;
            g_pGesture->max_mt_tmajor[i] = 0;
        }
    }
    else if (MTOUCH_FRAME_SYNC_END == ev->any_event.sync)
    {
        DetailDebugPrint("[GestureHandleMTSyncEvent] SYNC_END\n");
        g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_END;
        g_pGesture->ehtype = PROPAGATE_EVENTS;
        g_pGesture->flick_pressed_point = FLICK_POINT_NONE;
        g_pGesture->pTempWin = NULL;
        g_pGesture->num_pressed = 0;
        g_pGesture->hold_detected = FALSE;
    }
    else if (MTOUCH_FRAME_SYNC_UPDATE == ev->any_event.sync)
    {
        g_pGesture->mtsync_status = MTOUCH_FRAME_SYNC_UPDATE;
        g_pGesture->mtsync_total_count++;

        DetailDebugPrint("[GestureHandleMTSyncEvent] SYNC_Update. mt_total_sync=%d\n", g_pGesture->mtsync_total_count);

        if ((g_pGesture->inc_num_pressed < 2) && (g_pGesture->filter_mask != GESTURE_WATCH_FILTER_MASK_ALL))
        {
            if (g_pGesture->num_tap_repeated == 1 || g_pGesture->num_tap_repeated == 2)
            {
                if (g_pGesture->mtsync_total_count >= 6)
                {
                    DetailDebugPrint("[GestureHandleMTSyncEvent] Moving Limit first tap repeated. tap_repeated: %d, mtsync_total_count: %d\n",
                            g_pGesture->num_tap_repeated, g_pGesture->mtsync_total_count);
                    g_pGesture->filter_mask |= WTapFilterMask;
                    g_pGesture->filter_mask |= WHoldFilterMask;
					LOGI("[GroupTap][GroupHold] Motions are more than 6 between 1st finger and 2nd finger.\n");
                }
            }

            if (g_pGesture->filter_mask == GESTURE_WATCH_FILTER_MASK_ALL)
            {
                DetailDebugPrint("[GestureHandleMTSyncEvent] Gesture filter mask all. GestureFlushOrDrop() !\n");

                if (ERROR_INVALPTR == GestureFlushOrDrop())
                {
                    GestureControl(g_pGesture->this_device, DEVICE_OFF);
                }
            }
        }
    }
}


void GestureEmulateHWKey(DeviceIntPtr dev, int keycode)
{
    if (dev)
    {
        DetailDebugPrint("[GestureEmulateHWKey] keycode=%d\n", keycode);
        xf86PostKeyboardEvent(dev, keycode, 1);
        xf86PostKeyboardEvent(dev, keycode, 0);
    }
}

void
GestureHandleButtonPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_HANDLER__
    DetailDebugPrint("[GestureHandleButtonPressEvent] devid=%d time:%d cur:%d (%d, %d)\n", device->id, ev->any.time, GetTimeInMillis(), ev->device_event.root_x, ev->device_event.root_y);
#endif//__DEBUG_EVENT_HANDLER__

    switch (g_pGesture->ehtype)
    {
        case KEEP_EVENTS:
            DetailDebugPrint("[GestureHandleButtonPressEvent] KEEP_EVENT\n");

            if (ERROR_INVALPTR == GestureEnqueueEvent(screen_num, ev, device))
            {
                GestureControl(g_pGesture->this_device, DEVICE_OFF);
                return;
            }

            if (g_pGesture->num_mt_devices)
            {
                GestureRecognize(ET_ButtonPress, ev, device);
            }
            else
            {
                device->public.processInputProc(ev, device);
            }

            GestureHoldDetector(ET_ButtonPress, ev, device);
            break;

        case PROPAGATE_EVENTS:
            DetailDebugPrint("[GestureHandleButtonPressEvent] PROPAGATE_EVENT\n");

            device->public.processInputProc(ev, device);
            GestureHoldDetector(ET_ButtonPress, ev, device);
            break;

        case IGNORE_EVENTS:
            DetailDebugPrint("[GestureHandleButtonPressEvent] IGNORE_EVENTS\n");

            GestureRecognize(ET_ButtonPress, ev, device);
            break;

        default:
            break;
    }
}

void
GestureHandleMotionEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
#ifdef __DEBUG_EVENT_MOTION_HANDLER__
    DetailDebugPrint("[GestureHandleMotionEvent] devid=%d time:%d cur:%d (%d, %d)\n", device->id, ev->any.time, GetTimeInMillis(), ev->device_event.root_x, ev->device_event.root_y);
#endif

    switch (g_pGesture->ehtype)
    {
        case KEEP_EVENTS:
            if (ERROR_INVALPTR == GestureEnqueueEvent(screen_num, ev, device))
            {
                GestureControl(g_pGesture->this_device, DEVICE_OFF);
                return;
            }

            if (g_pGesture->num_mt_devices)
            {
                GestureRecognize(ET_Motion, ev, device);
            }
            else
            {
                device->public.processInputProc(ev, device);
            }

            GestureHoldDetector(ET_Motion, ev, device);
            break;

        case PROPAGATE_EVENTS:
            device->public.processInputProc(ev, device);
            GestureHoldDetector(ET_Motion, ev, device);
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
    DetailDebugPrint("[GestureHandleButtonReleaseEvent] devid=%d time:%d cur:%d (%d, %d)\n", device->id, ev->any.time, GetTimeInMillis(), ev->device_event.root_x, ev->device_event.root_y);
#endif

    switch (g_pGesture->ehtype)
    {
        case KEEP_EVENTS:
            DetailDebugPrint("[GestureHandleButtonPressEvent] KEEP_EVENT\n");

            if (ERROR_INVALPTR == GestureEnqueueEvent(screen_num, ev,  device))
            {
                GestureControl(g_pGesture->this_device, DEVICE_OFF);
                return;
            }

            if (g_pGesture->num_mt_devices)
            {
                GestureRecognize(ET_ButtonRelease, ev, device);
            }
            else
            {
                device->public.processInputProc(ev, device);
            }

            GestureHoldDetector(ET_ButtonRelease, ev, device);
            break;

        case PROPAGATE_EVENTS:
            DetailDebugPrint("[GestureHandleButtonPressEvent] PROPAGATE_EVENTS\n");
#ifdef SUPPORT_ANR_WITH_INPUT_EVENT
                     if( IsMaster(device) && ev->any.type == ET_ButtonRelease )
                     {
                         if( g_pGesture->anr_window == NULL )
                         {
                             g_pGesture->anr_window = _GestureFindANRWindow(device);
                         }
                         Time current_time;

                         // Send event to the e17 process.
                         current_time = GetTimeInMillis();
                         if( g_pGesture->anr_window != NULL )
                         {
                             // Check anr_window validation.
                             if( dixLookupWindow(&g_pGesture->anr_window, prop_anr_event_window_xid, serverClient, DixSetPropAccess) != BadWindow )
                             {
                                 if( serverClient->devPrivates != NULL )
                                     dixChangeWindowProperty (serverClient, g_pGesture->anr_window, prop_anr_in_input_event,
                                                                               XA_CARDINAL, 32, PropModeReplace, 1, &current_time, TRUE);
                             }
                             else
                             {
                                 prop_anr_event_window_xid = 0;
                                 g_pGesture->anr_window = NULL;
                             }
                             DetailDebugPrint("Release TOUCH!! devid=%d time:%d cur: %d\n", device->id, ev->any.time, GetTimeInMillis());
                         }
                     }
#endif
            device->public.processInputProc(ev, device);
            GestureHoldDetector(ET_ButtonRelease, ev, device);
#if 0
            GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_down);
            GestureEmulateHWKey(g_pGesture->hwkey_dev, g_pGesture->hwkeycode_flick_up);
#endif
            break;

        case IGNORE_EVENTS:
            DetailDebugPrint("[GestureHandleButtonPressEvent] IGNORE_EVENTS\n");
            GestureRecognize(ET_ButtonRelease, ev, device);
            break;

        default:
            break;
    }
}

void
GestureHandleKeyPressEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
    if ((ev->device_event.detail.key == 124) && (g_pGesture->power_pressed != 0))
    {
        g_pGesture->power_pressed = 2;
        g_pGesture->power_device = device;

        DetailDebugPrint("[GestureHandleKeyPressEvent] power key pressed devid: %d, hwkey_id: %d\n", device->id, g_pGesture->hwkey_id);
        DetailDebugPrint("[GestureHandleKeyPressEvent] power_pressed: %d\n", g_pGesture->power_pressed);
    }
    device->public.processInputProc(ev, device);
}

void
GestureHandleKeyReleaseEvent(int screen_num, InternalEvent *ev, DeviceIntPtr device)
{
    if ((ev->device_event.detail.key == 124) && (g_pGesture->power_pressed != 0))
    {
        g_pGesture->power_pressed = 1;
        g_pGesture->power_device = device;

        DetailDebugPrint("[GestureHandleKeyReleaseEvent] power key released devid: %d, hwkey_id: %d\n", device->id, g_pGesture->hwkey_id);
        DetailDebugPrint("[GestureHandleKeyReleaseEvent] power_pressed: %d\n", g_pGesture->power_pressed);
    }
    device->public.processInputProc(ev, device);
}

static void
GestureHandleClientState (CallbackListPtr *list, pointer closure, pointer calldata)
{
    NewClientInfoRec *clientinfo = (NewClientInfoRec*)calldata;
    ClientPtr client = clientinfo->client;

    if (client->clientState != ClientStateGone)
    {
        return;
    }

    if (!g_pGesture->factory_cmdname)
    {
        return;
    }

    if (strncmp(client->clientIds->cmdname, g_pGesture->factory_cmdname, strlen(g_pGesture->factory_cmdname)))
    {
        return;
    }

    if (g_pGesture->is_active == 0)
    {
        int prop_val = 1;
        int rc = XIChangeDeviceProperty(g_pGesture->this_device, prop_gesture_recognizer_onoff, XA_INTEGER, 32, PropModeReplace, 1, &prop_val, FALSE);

        ErrorF("[GestureHandleClientState] %s is exited unintentionally\n", g_pGesture->factory_cmdname);

        if (rc != Success)
        {
            ErrorF("[GestureHandleClientState] Failed to Gesture Enable\n");
            return;
        }
    }
}

static ErrorStatus
GestureEnableEventHandler(InputInfoPtr pInfo)
{
    Bool res;
    GestureDevicePtr pGesture = pInfo->private;

    res = GestureInstallResourceStateHooks();

    if (!res)
    {
        ErrorF("[GestureEnableEventHandler] Failed on GestureInstallResourceStateHooks() !\n");
        return ERROR_ABNORMAL;
    }

    res = GestureSetMaxNumberOfFingers((int)MAX_MT_DEVICES);

    if (!res)
    {
        ErrorF("[GestureEnableEventHandler] Failed on GestureSetMaxNumberOfFingers(%d) !\n", (int)MAX_MT_DEVICES);
        goto failed;
    }

    res = GestureRegisterCallbacks(GestureCbEventsGrabbed, GestureCbEventsSelected);

    if (!res)
    {
        ErrorF("[GestureEnableEventHandler] Failed to register callbacks for GestureEventsGrabbed(), GestureEventsSelected() !\n");
        goto failed;
    }

    pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 5000, GestureTimerHandler, pInfo);

    if (!pGesture->device_setting_timer)
    {
        ErrorF("[GestureEnableEventHandler] Failed to set time for detecting devices !\n");
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

    if (ERROR_INVALPTR == err)
    {
        ErrorF("[GestureDisableEventHandler] EQ is invalid or was freed already !\n");
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

    if (!pInfo)
    {
        ErrorF("[GestureTimerHandler][%s] pInfo is NULL !\n");
        goto failed;
    }

    pGesture = pInfo->private;

    idx = 0;
    for (dev = inputInfo.pointer; dev; dev = dev->next)
    {
        if (IsMaster(dev) && IsPointerDevice(dev))
        {
            pGesture->master_pointer = dev;
            ErrorF("[GestureTimerHandler][id:%d] Master Pointer=%s\n", dev->id, pGesture->master_pointer->name);
            continue;
        }

        if (IsXTestDevice(dev, NULL) && IsPointerDevice(dev))
        {
            pGesture->xtest_pointer = dev;
            ErrorF("[GestureTimerHandler][id:%d] XTest Pointer=%s\n", dev->id, pGesture->xtest_pointer->name);
            continue;
        }

        if (IsPointerDevice(dev))
        {
            if (idx >= MAX_MT_DEVICES)
            {
                ErrorF("[GestureTimerHandler] Number of mt device is over MAX_MT_DEVICES(%d) !\n", MAX_MT_DEVICES);
                continue;
            }
            pGesture->mt_devices[idx] = dev;
            ErrorF("[GestureTimerHandler][id:%d] MT device[%d] name=%s\n", dev->id, idx, pGesture->mt_devices[idx]->name);
            GesturePalmGetAbsAxisInfo(dev);
            idx++;
        }
    }

    for (dev = inputInfo.keyboard ; dev; dev = dev->next)
    {
        if (g_pGesture->hwkey_name && !strncmp(dev->name, g_pGesture->hwkey_name, strlen(dev->name)))
        {
            g_pGesture->hwkey_id = dev->id;
            g_pGesture->hwkey_dev = dev;

            ErrorF("[GestureTimerHandler] hwkey_name has been found. hwkey_id=%d (hwkey_dev->name:%s)\n", g_pGesture->hwkey_id, g_pGesture->hwkey_dev->name);
            break;
        }
        else if (!strcasestr(dev->name, "keyboard") && strcasestr(dev->name, "key") && !IsXTestDevice(dev, NULL) && !IsMaster(dev))
        {
            g_pGesture->hwkey_id = dev->id;
            g_pGesture->hwkey_dev = dev;

            ErrorF("[GestureTimerHandler] hwkey has been found. hwkey_id=%d (hwkey_dev->name:%s)\n", g_pGesture->hwkey_id, g_pGesture->hwkey_dev->name);
            break;
        }
    }

    if (!g_pGesture->hwkey_id)
    {
        g_pGesture->hwkey_id = inputInfo.keyboard->id;
        g_pGesture->hwkey_dev = inputInfo.keyboard;

        ErrorF("[GestureTimerHandler] No hwkey has been found. Back key will go through VCK. hwkey_id=%d (hwkey_dev->name:%s)\n",
                g_pGesture->hwkey_id, g_pGesture->hwkey_dev->name);
    }

    if (!pGesture->master_pointer || !pGesture->xtest_pointer)
    {
        ErrorF("[GestureTimerHandler] Failed to get info of master pointer or XTest pointer !\n");
        pGesture->device_setting_timer = TimerSet(pGesture->device_setting_timer, 0, 0, NULL, NULL);
        pGesture->num_mt_devices = 0;

        return 0;
    }

    TimerCancel(pGesture->device_setting_timer);
    pGesture->device_setting_timer = NULL;
    pGesture->num_mt_devices = idx;

    if (!pGesture->num_mt_devices)
    {
        ErrorF("[GestureTimerHandler] Failed to mt device information !\n");
        TimerCancel(pGesture->device_setting_timer);
        pGesture->device_setting_timer = NULL;
        pGesture->num_mt_devices = 0;
        pGesture->first_fingerid = -1;
        return 0;
    }

    pGesture->first_fingerid = pGesture->mt_devices[0]->id;
    memset(pGesture->fingers, 0, sizeof(TouchStatus)*pGesture->num_mt_devices);
    pGesture->pRootWin = RootWindow(pGesture->master_pointer);

    if (g_pGesture->palm_misc.enabled)
    {
        GesturePalmGetScreenInfo();
    }

    g_pGesture->pTempWin = NULL;
    g_pGesture->inc_num_pressed = 0;

    if (ERROR_NONE != GestureRegionsInit() || ERROR_NONE != GestureInitEQ())
    {
        goto failed;
    }

    mieqSetHandler(ET_ButtonPress, GestureHandleButtonPressEvent);
    mieqSetHandler(ET_ButtonRelease, GestureHandleButtonReleaseEvent);
    mieqSetHandler(ET_Motion, GestureHandleMotionEvent);
    mieqSetHandler(ET_KeyPress, GestureHandleKeyPressEvent);
    mieqSetHandler(ET_KeyRelease, GestureHandleKeyReleaseEvent);

    //if ( pGesture->is_active)
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
    {
        return FALSE;
    }

    if (master)
    {
        return (dev->xtest_master_id == master->id);
    }

    return (dev->xtest_master_id != 0);
}

void
GestureEnable(int enable, Bool prop, DeviceIntPtr dev)
{
    if ((!enable) && (g_pGesture->is_active))
    {
        g_pGesture->ehtype = PROPAGATE_EVENTS;
        mieqSetHandler(ET_MTSync, NULL);
        g_pGesture->is_active = 0;
        ErrorF("[GestureEnable] Disabled !\n");
        int res = AddCallback (&ClientStateCallback, GestureHandleClientState, NULL);

        if (!res)
        {
            ErrorF("[GestureEnable] Failed to add callback for client state\n");
            return;
        }
    }

    else if ((enable) && (!g_pGesture->is_active))
    {
        g_pGesture->ehtype = KEEP_EVENTS;
        mieqSetHandler(ET_MTSync, GestureHandleMTSyncEvent);
        g_pGesture->is_active = 1;
        ErrorF("[GestureEnable] Enabled !\n");

        DeleteCallback (&ClientStateCallback, GestureHandleClientState, NULL);
    }

    if (!prop)
    {
        XIChangeDeviceProperty(dev, prop_gesture_recognizer_onoff, XA_INTEGER, 32, PropModeReplace, 1, &g_pGesture->is_active, FALSE);
    }
}

ErrorStatus
GestureRegionsInit(void)
{
    int i;

    if (!g_pGesture)
    {
        return ERROR_INVALPTR;
    }

    pixman_region_init(&g_pGesture->area);

    for (i = 0; i < MAX_MT_DEVICES; i++)
    {
        pixman_region_init_rect(&g_pGesture->finger_rects[i], 0, 0, FINGER_WIDTH_2T, FINGER_HEIGHT_2T);
    }

    return ERROR_NONE;
}

ErrorStatus
GestureRegionsReinit(void)
{
    if (!g_pGesture)
    {
        ErrorF("[GestureRegionsReinit] Invalid pointer access !\n");
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

    if (!tmpEQ)
    {
        ErrorF("[GestureInitEQ] Failed to allocate memory for EQ !\n");
        return ERROR_ALLOCFAIL;
    }

    for (i = 0; i < GESTURE_EQ_SIZE; i++)
    {
        tmpEQ[i].event = (InternalEvent *)malloc(sizeof(InternalEvent));
        if (!tmpEQ[i].event)
        {
            ErrorF("[GestureInitEQ] Failed to allocation memory for each event buffer in EQ !\n");
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

    if (!g_pGesture || !g_pGesture->EQ)
    {
        return ERROR_INVALPTR;
    }

    for (i = 0; i < GESTURE_EQ_SIZE; i++)
    {
        if (g_pGesture->EQ[i].event)
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

    if (!g_pGesture || !g_pGesture->EQ)
    {
        ErrorF("[GestureEnqueueEvent] Invalid pointer access !\n");
        return ERROR_INVALPTR;
    }

    tail = g_pGesture->tailEQ;

    if (tail >= GESTURE_EQ_SIZE)
    {
        ErrorF("[GestureEnqueueEvent] Gesture EQ is full !\n");
        printk("[GestureEnqueueEvent] Gesture EQ is full...Force Gesture Flush !\n");
        GestureEventsFlush();
        return ERROR_EQFULL;
    }

    switch (ev->any.type)
    {
        case ET_ButtonPress:
            DetailDebugPrint("[GestureEnqueueEvent] ET_ButtonPress (id:%d)\n", device->id);
            break;

        case ET_ButtonRelease:
            DetailDebugPrint("[GestureEnqueueEvent] ET_ButtonRelease (id:%d)\n", device->id);
            break;

        case ET_Motion:
            DetailDebugPrint("[GestureEnqueueEvent] ET_Motion (id:%d)\n", device->id);
            break;
    }

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

    if (!g_pGesture->EQ)
    {
        ErrorF("[GestureEventsFlush] Invalid pointer access !\n");
        return ERROR_INVALPTR;
    }

    DetailDebugPrint("[GestureEventsFlush]\n");

    for (i = g_pGesture->headEQ; i < g_pGesture->tailEQ; i++)
    {
        device = g_pGesture->EQ[i].device;
        device->public.processInputProc(g_pGesture->EQ[i].event, device);
    }

    for (i = 0; i < MAX_MT_DEVICES; i++)
    {
        g_pGesture->event_sum[i] = 0;
    }

    g_pGesture->headEQ = g_pGesture->tailEQ = 0;//Free EQ

    return ERROR_NONE;
}

void
GestureEventsDrop(void)
{
    DetailDebugPrint("[GestureEventsDrop]\n");
    g_pGesture->headEQ = g_pGesture->tailEQ = 0;//Free EQ
}

#ifdef HAVE_PROPERTIES
static void
GestureInitProperty(DeviceIntPtr dev)
{
    int rc;

#ifdef SUPPORT_ANR_WITH_INPUT_EVENT
    prop_anr_in_input_event = MakeAtom(CHECK_APPLICATION_NOT_RESPONSE_IN_INPUT_EVENT, strlen(CHECK_APPLICATION_NOT_RESPONSE_IN_INPUT_EVENT),  TRUE);
    prop_anr_event_window = MakeAtom(ANR_EVENT_WINDOW, strlen(ANR_EVENT_WINDOW), TRUE);
#endif

    prop_gesture_recognizer_onoff = MakeAtom(GESTURE_RECOGNIZER_ONOFF, strlen(GESTURE_RECOGNIZER_ONOFF),  TRUE);
    rc = XIChangeDeviceProperty(dev, prop_gesture_recognizer_onoff, XA_INTEGER, 32, PropModeReplace, 1, &g_pGesture->is_active, FALSE);

    if (rc != Success)
    {
        return;
    }

    XISetDevicePropertyDeletable(dev, prop_gesture_recognizer_onoff, FALSE);
}

static int
GestureSetProperty(DeviceIntPtr dev, Atom atom, XIPropertyValuePtr val,
        BOOL checkonly)
{
    if (prop_gesture_recognizer_onoff == atom)
    {
        int data;
        if (val->format != 32 || val->type != XA_INTEGER || val->size != 1)
            return BadMatch;

        if (!checkonly)
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
    GestureDevicePtr pGesture;

    pGesture = calloc(1, sizeof(GestureDeviceRec));

    if (!pGesture)
    {
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
    pGesture->num_tap_repeated = 0;
    pGesture->mtsync_total_count = 0;
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
    pGesture->tripletap_threshold= xf86SetIntOption(pInfo->options, "TripleTapThresHold", 0);
    pGesture->hold_area_threshold = xf86SetRealOption(pInfo->options, "HoldAreaThresHold", 0);
    pGesture->hold_move_threshold = xf86SetIntOption(pInfo->options, "HoldMoveThresHold", 0);
    pGesture->hold_time_threshold = xf86SetIntOption(pInfo->options, "HoldTimeThresHold", 0);
    pGesture->palm_flick_time_threshold = xf86SetIntOption(pInfo->options, "PalmFlickTimeThresHold", 0);
    pGesture->palm_flick_max_tmajor_threshold = xf86SetIntOption(pInfo->options, "PalmFlickMaxTouchMajorThresHold", 0);
    pGesture->palm_flick_min_tmajor_threshold = xf86SetIntOption(pInfo->options, "PalmFlickMinTouchMajorThresHold", 0);
    pGesture->activate_flick_down = xf86SetIntOption(pInfo->options, "ActivateFlickDown", 0);
    pGesture->activate_flick_up = xf86SetIntOption(pInfo->options, "ActivateFlickUp", 0);
    pGesture->activate_flick_right = xf86SetIntOption(pInfo->options, "ActivateFlickRight", 0);
    pGesture->factory_cmdname = xf86SetStrOption(pInfo->options, "FactoryCmdName", NULL);

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
    ErrorF("[X11][%s] palm_flick_time_threshold: %d, palm_flick_max_tmajor_threshold: %d, palm_flick_min_tmajor_threshold: %d\n", __FUNCTION__,
            pGesture->palm_flick_time_threshold, pGesture->palm_flick_max_tmajor_threshold, pGesture->palm_flick_min_tmajor_threshold);
    ErrorF("[X11][%s] activate_flick_down=%d, activate_flick_up=%d, activate_flick_right=%d\n", __FUNCTION__,
            pGesture->activate_flick_down, pGesture->activate_flick_up, pGesture->activate_flick_right);
    ErrorF("[X11][%s] factory cmd name: %s\n", __FUNCTION__, pGesture->factory_cmdname);
    ErrorF("[X11][%s] ###############################################################\n", __FUNCTION__);

    if (pGesture->hwkey_name)
    {
        ErrorF("[X11][%s] hwkey_name=%s\n", __FUNCTION__, pGesture->hwkey_name);
    }

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

    switch (what)
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
            if (ERROR_ABNORMAL == GestureEnableEventHandler(pInfo))
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



