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

#ifndef _GESTURE_H_
#define _GESTURE_H_

#include <xorg/events.h>
#include <xorg/gestureext.h>
#include <X11/extensions/gestureconst.h>
#include <pixman.h>

#if GET_ABI_MAJOR(ABI_XINPUT_VERSION) >= 3
#define HAVE_PROPERTIES 1
#endif

/**
 * If there's touch event in pointed window and there's no reponse, we just assume that client looks like deadlock.
 * In this case, we will make a popup window and terminate application.
 * To support this feature, we use SUPPORT_ANR_WITH_INPUT_EVENT flag.
 */
#define SUPPORT_ANR_WITH_INPUT_EVENT

#define GESTURE_RECOGNIZER_ONOFF	"GESTURE_RECOGNIZER_ONOFF"
#define GESTURE_PALM_REJECTION_MODE	"GESTURE_PALM_REJECTION_MODE"
#define CHECK_APPLICATION_NOT_RESPONSE_IN_INPUT_EVENT "_CHECK_APPLICATION_NOT_RESPONSE_IN_INPUT_EVENT_"
#define ANR_EVENT_WINDOW "_ANR_EVENT_WINDOW_"


#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))
#define RootWindow(dev) dev->spriteInfo->sprite->spriteTrace[0]
#define CLIENT_BITS(id) ((id) & RESOURCE_CLIENT_MASK)
#define CLIENT_ID(id) ((int)(CLIENT_BITS(id) >> CLIENTOFFSET))

#define MAX_MT_DEVICES		2
#define GESTURE_EQ_SIZE	256

#define GESTURE_RECOGNIZER_ONOFF	"GESTURE_RECOGNIZER_ONOFF"

#define FINGER_WIDTH		10
#define FINGER_HEIGHT		10
#define FINGER_WIDTH_2T	20
#define FINGER_HEIGHT_2T	20
#define AREA_CENTER_X(extents)	((extents)->x1 + (((extents)->x2-(extents)->x1)/2))
#define AREA_CENTER_Y(extents)	((extents)->y1 + (((extents)->y2-(extents)->y1)/2))
#define AREA_SIZE(extents)		(ABS((extents)->x2-(extents)->x1)*ABS((extents)->y2-(extents)->y1))
#define INBOX(r,x,y)				( ((r)->x2 >  x) && ((r)->x1 <= x) && ((r)->y2 >  y) && ((r)->y1 <= y) )
#define AREA_HEIGHT(extents)    (((extents)->y2)-((extents)->y1))
#define AREA_WIDTH(extents)	(((extents)->x2)-((extents)->x1))
#define AREA_DIAG_LEN(extents)  sqrt((AREA_WIDTH(extents)*AREA_WIDTH(extents))+(AREA_HEIGHT(extents)*AREA_HEIGHT(extents)))

//tap
#define TAP_THRESHOLD			100//in pixel
#define SINGLE_TAP_TIMEOUT		100//in msec
#define DOUBLE_TAP_TIMEOUT	250//in msec

//palm
#define PALM_HORIZ_ARRAY_COUNT 3
#define PALM_VERTI_ARRAY_COUNT 4

typedef int XFixed;
typedef double XDouble;
#define XDoubleToFixed(f)    ((XFixed) ((f) * 65536))
#define XFixedToDouble(f)    (((XDouble) (f)) / 65536)

#define MIN(x, y) (((x) < (y)) ? (x) : (y))
#define MAX(x, y) (((x) > (y)) ? (x) : (y))
#define ABS(x) (((x) < 0) ? -(x) : (x))

enum
{
	FLICK_NORTHWARD = 0,
	FLICK_NORTHEASTWARD,
	FLICK_EASTWARD,
	FLICK_SOUTHEASTWARD,
	FLICK_SOUTHWARD,
	FLICK_SOUTHWESTWARD,
	FLICK_WESTWARD,
	FLICK_NORTHWESTWARD
};

enum
{
	FLICK_POINT_NONE = 0,
	FLICK_POINT_UP,
	FLICK_POINT_RIGHT,
	FLICK_POINT_DOWN,
	FLICK_POINT_LEFT,
	FLICK_POINT_MAX
};

#define TAP_AREA_THRESHOLD			10000//= 100pixel * 100pixel
#define TAP_MOVE_THRESHOLD			35//pixel
#define SGL_TAP_TIME_THRESHOLD		300//ms
#define DBL_TAP_TIME_THRESHOLD		200//ms
#define MAX_TAP_REPEATS             3

#define FLICK_AREA_THRESHOLD			22500//=150pixel * 150pixel
#define FLICK_AREA_TIMEOUT				700//ms
#define FLICK_MOVE_THRESHOLD			100//pixel
#define FLICK_MOVE_TIMEOUT				1000//ms
#define FLICK_FALSE_Y_DIFF_COUNT		7
#define FLICK_FALSE_X_DIFF_COUNT		5

#define PALM_FLICK_DETECT_TIMEOUT			1000//ms
#define PALM_FLICK_MAX_TOUCH_MAJOR		130

#define AXIS_LABEL_PROP_ABS_MT_TRACKING_ID "Abs MT Tracking ID"
#define AXIS_LABEL_PROP_ABS_MT_SLOT     "Abs MT Slot"
#define AXIS_LABEL_PROP_ABS_MT_TOUCH_MAJOR "Abs MT Touch Major"
#define AXIS_LABEL_PROP_ABS_MT_TOUCH_MINOR "Abs MT Touch Minor"
#define AXIS_LABEL_PROP_ABS_MT_PALM        "Abs MT Palm/MT Sumsize"

typedef enum _MTSyncType
{
	MTOUCH_FRAME_SYNC_END,
	MTOUCH_FRAME_SYNC_BEGIN,
	MTOUCH_FRAME_SYNC_UPDATE
} MTSyncType;

typedef enum _EventHandleType
{
	PROPAGATE_EVENTS,
	KEEP_EVENTS,
	IGNORE_EVENTS
} EventHandleType;

typedef enum _ErrorStatus
{
	ERROR_NONE,
	ERROR_ABNORMAL,
	ERROR_INVALPTR,
	ERROR_EQFULL,
	ERROR_ALLOCFAIL
} ErrorStatus;

enum EventType
{
    ET_KeyPress = 2,
    ET_KeyRelease,
    ET_ButtonPress,
    ET_ButtonRelease,
    ET_Motion,
    /*
    ...
    */
    ET_MTSync = 0x7E,
    ET_Internal = 0xFF /* First byte */
};

struct _DeviceEvent {
    unsigned char header; /**< Always ET_Internal */
    enum EventType type;  /**< One of EventType */
    int length;           /**< Length in bytes */
    Time time;            /**< Time in ms */
    int deviceid;         /**< Device to post this event for */
    int sourceid;         /**< The physical source device */
    union {
        uint32_t button;  /**< Button number (also used in pointer emulating
                               touch events) */
        uint32_t key;     /**< Key code */
    } detail;
    uint32_t touchid;     /**< Touch ID (client_id) */
    int16_t root_x;       /**< Pos relative to root window in integral data */
    float root_x_frac;    /**< Pos relative to root window in frac part */
    int16_t root_y;       /**< Pos relative to root window in integral part */
    float root_y_frac;    /**< Pos relative to root window in frac part */
    uint8_t buttons[(MAX_BUTTONS + 7) / 8];  /**< Button mask */
    struct {
        uint8_t mask[(MAX_VALUATORS + 7) / 8];/**< Valuator mask */
        uint8_t mode[(MAX_VALUATORS + 7) / 8];/**< Valuator mode (Abs or Rel)*/
        double data[MAX_VALUATORS];           /**< Valuator data */
    } valuators;
    struct {
        uint32_t base;    /**< XKB base modifiers */
        uint32_t latched; /**< XKB latched modifiers */
        uint32_t locked;  /**< XKB locked modifiers */
        uint32_t effective;/**< XKB effective modifiers */
    } mods;
    struct {
        uint8_t base;    /**< XKB base group */
        uint8_t latched; /**< XKB latched group */
        uint8_t locked;  /**< XKB locked group */
        uint8_t effective;/**< XKB effective group */
    } group;
    Window root;      /**< Root window of the event */
    int corestate;    /**< Core key/button state BEFORE the event */
    int key_repeat;   /**< Internally-generated key repeat event */
    uint32_t flags;   /**< Flags to be copied into the generated event */
};

typedef struct _AnyEvent AnyEvent;
struct _AnyEvent
{
    unsigned char header; /**< Always ET_Internal */
    enum EventType type;  /**< One of EventType */
    int length;           /**< Length in bytes */
    Time time;            /**< Time in ms */
    int deviceid;
    MTSyncType sync;
    int x;
    int y;
};

union _InternalEvent {
	struct {
	    unsigned char header; /**< Always ET_Internal */
	    enum EventType type;  /**< One of ET_* */
	    int length;           /**< Length in bytes */
	    Time time;            /**< Time in ms. */
	} any;
	AnyEvent any_event;
	DeviceEvent device_event;
};

#define wUseDefault(w,field,def)	((w)->optional ? (w)->optional->field : def)
#define wBoundingShape(w)	wUseDefault(w, boundingShape, NULL)
#define wInputShape(w)          wUseDefault(w, inputShape, NULL)
#define wBorderWidth(w)		((int) (w)->borderWidth)

/* used as NULL-terminated list */
typedef struct _DevCursorNode {
    CursorPtr                   cursor;
    DeviceIntPtr                dev;
    struct _DevCursorNode*      next;
} DevCursNodeRec, *DevCursNodePtr, *DevCursorList;

typedef struct _WindowOpt {
    VisualID		visual;		   /* default: same as parent */
    CursorPtr		cursor;		   /* default: window.cursorNone */
    Colormap		colormap;	   /* default: same as parent */
    Mask		dontPropagateMask; /* default: window.dontPropagate */
    Mask		otherEventMasks;   /* default: 0 */
    struct _OtherClients *otherClients;	   /* default: NULL */
    struct _GrabRec	*passiveGrabs;	   /* default: NULL */
    PropertyPtr		userProps;	   /* default: NULL */
    unsigned long	backingBitPlanes;  /* default: ~0L */
    unsigned long	backingPixel;	   /* default: 0 */
    RegionPtr		boundingShape;	   /* default: NULL */
    RegionPtr		clipShape;	   /* default: NULL */
    RegionPtr		inputShape;	   /* default: NULL */
    struct _OtherInputMasks *inputMasks;   /* default: NULL */
    DevCursorList       deviceCursors;     /* default: NULL */
} WindowOptRec, *WindowOptPtr;

typedef struct _Window {
    DrawableRec		drawable;
    PrivateRec		*devPrivates;
    WindowPtr		parent;		/* ancestor chain */
    WindowPtr		nextSib;	/* next lower sibling */
    WindowPtr		prevSib;	/* next higher sibling */
    WindowPtr		firstChild;	/* top-most child */
    WindowPtr		lastChild;	/* bottom-most child */
    RegionRec		clipList;	/* clipping rectangle for output */
    RegionRec		borderClip;	/* NotClippedByChildren + border */
    union _Validate	*valdata;
    RegionRec		winSize;
    RegionRec		borderSize;
    DDXPointRec		origin;		/* position relative to parent */
    unsigned short	borderWidth;
    unsigned short	deliverableEvents; /* all masks from all clients */
    Mask		eventMask;      /* mask from the creating client */
    PixUnion		background;
    PixUnion		border;
    pointer		backStorage;	/* null when BS disabled */
    WindowOptPtr	optional;
    unsigned		backgroundState:2; /* None, Relative, Pixel, Pixmap */
    unsigned		borderIsPixel:1;
    unsigned		cursorIsNone:1;	/* else real cursor (might inherit) */
    unsigned		backingStore:2;
    unsigned		saveUnder:1;
    unsigned		DIXsaveUnder:1;
    unsigned		bitGravity:4;
    unsigned		winGravity:4;
    unsigned		overrideRedirect:1;
    unsigned		visibility:2;
    unsigned		mapped:1;
    unsigned		realized:1;	/* ancestors are all mapped */
    unsigned		viewable:1;	/* realized && InputOutput */
    unsigned		dontPropagate:3;/* index into DontPropagateMasks */
    unsigned		forcedBS:1;	/* system-supplied backingStore */
    unsigned		redirectDraw:2;	/* COMPOSITE rendering redirect */
    unsigned		forcedBG:1;	/* must have an opaque background */
#ifdef ROOTLESS
    unsigned		rootlessUnhittable:1;	/* doesn't hit-test */
#endif
} WindowRec;

typedef struct _IEvent {
	InternalEvent *event;
	int screen_num;
	DeviceIntPtr device;
} IEventRec, *IEventPtr;

enum
{
	BTN_RELEASED,
	BTN_PRESSED,
	BTN_MOVING
};

#define PressFlagFlick			0x01//(1 << 0)
#define PressFlagPan				0x02//(1 << 1)
#define PressFlagPinchRotation	0x04//(1 << 2)
#define PressFlagTap				0x08//(1 << 3)
#define PressFlagTapNHold		0x10//(1 << 4)
#define PressFlagHold			0x20//(1 << 5)

#define FlickFilterMask			0x01//(1 << 0)
#define PanFilterMask			0x02//(1 << 1)
#define PinchRotationFilterMask	0x04//(1 << 2)
#define TapFilterMask			0x08//(1 << 3)
#define TapNHoldFilterMask		0x10//(1 << 4)
#define HoldFilterMask			0x20//(1 << 5)

#define GESTURE_FILTER_MASK_ALL	0x3f//(FlickFilterMask | PanFilterMask | PinchRotationFilterMask | TapFilterMask |TapNHoldFilterMask | HoldFilterMask)

#define WFlickFilterMask				0x01//(1 << 0)
#define WTapFilterMask				0x02//(1 << 1)
#define WHoldFilterMask				0x04//(1 << 2)
#define WPalmFlickFilterMask			0x08//(1 << 3)

#define GESTURE_WATCH_FILTER_MASK_ALL	0x07//(WFlickFilterMask | WTapFilterMask | WHoldFilterMask )

#define PALM_HOLD_TIME_THRESHOLD 150

typedef struct _tagTouchStatus
{
	int status;//One of BTN_RELEASED, BTN_PRESSED and BTN_MOVING
	uint32_t flags;

	int px;		//press x
	int py;		//press y
	int mx;		//motion x
	int my;		//motion y
	int rx;		//release x
	int ry;		//release y
	Time ptime;	//press time
	Time mtime;	//motion time
	Time rtime;	//current/previous release time
} TouchStatus;

typedef struct _tagCurrentTouchStatus
{
	int status;//One of BTN_RELEASED, BTN_PRESSED and BTN_MOVING
	int cx;		//current x
	int cy;		//current y
} CurTouchStatus;

typedef struct _tagPalmDrvStatus
{
	int enabled;
	int scrn_width;
	int scrn_height;
	unsigned int half_scrn_area_size;
	int horiz_coord[PALM_HORIZ_ARRAY_COUNT];
	int verti_coord[PALM_VERTI_ARRAY_COUNT];
} PalmMiscInfo, *PalmMiscInfoPtr;


typedef struct _GestureDeviceRec
{
	char *device;
	int version;        /* Driver version */
	OsTimerPtr device_setting_timer;

	int is_active;
	int power_pressed;
	int flick_pressed_point;
	int flick_direction;

	int hwkeycode_flick_down;
	int hwkeycode_flick_up;
	int shutdown_keycode;
	int flick_press_area;
	int flick_press_area_left;
	int flick_press_area_left_right;
	int flick_minimum_height;
	int activate_flick_down;//up to down
	int activate_flick_up;//down to up
	int activate_flick_right;//left to right
	int screen_width;
	int screen_height;

	int singletap_threshold;
	int doubletap_threshold;
	int tripletap_threshold;

	int num_tap_repeated;

	double hold_area_threshold;
	int hold_move_threshold;
	int hold_time_threshold;

	int palm_flick_time_threshold;
	int palm_flick_max_tmajor_threshold;
	int palm_flick_min_tmajor_threshold;
	char *factory_cmdname;

	int max_mt_tmajor[MAX_MT_DEVICES];

	int hwkey_id;
	char *hwkey_name;
	DeviceIntPtr hwkey_dev;
	MTSyncType mtsync_status;
	int mtsync_total_count;

	DeviceIntPtr power_device;

	WindowPtr pRootWin;
	Window gestureWin;
	int num_mt_devices;

	Mask grabMask;
	Mask eventMask;
	GestureGrabEventPtr GrabEvents;
	Mask lastSelectedMask;
	Window lastSelectedWin;

	EventHandleType ehtype;
	IEventPtr	EQ;
	int headEQ;
	int tailEQ;

	int hold_detector_activate;
	int has_hold_grabmask;
	pixman_region16_t chold_area;
	CurTouchStatus cts[MAX_MT_DEVICES];
	Bool hold_detected;

	PalmMiscInfo palm_misc;
	int tmajor_idx;
	int tminor_idx;
	int tpalm_idx;
	int mt_px_idx;
	int mt_py_idx;

	pixman_region16_t area;
	pixman_region16_t finger_rects[MAX_MT_DEVICES];

	WindowPtr pTempWin;
	int inc_num_pressed;

	int first_fingerid;
	int num_pressed;
	TouchStatus fingers[MAX_MT_DEVICES];

	int event_sum[MAX_MT_DEVICES];
	uint32_t recognized_gesture;
	uint32_t filter_mask;

	DeviceIntPtr this_device;
	DeviceIntPtr mt_devices[MAX_MT_DEVICES];
	DeviceIntPtr master_pointer;
	DeviceIntPtr xtest_pointer;

	WindowPtr anr_window;
} GestureDeviceRec, *GestureDevicePtr ;

#endif//_GESTURE_H_
