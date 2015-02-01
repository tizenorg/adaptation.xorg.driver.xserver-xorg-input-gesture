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

#ifndef ABS_CNT
#define ABS_CNT (ABS_MAX+1)
#endif

/**
 * If there's touch event in pointed window and there's no reponse, we just assume that client looks like deadlock.
 * In this case, we will make a popup window and terminate application.
 * To support this feature, we use SUPPORT_ANR_WITH_INPUT_EVENT flag.
 */
#define SUPPORT_ANR_WITH_INPUT_EVENT

#define NUM_PASSKEYS	20

#define SYSCALL(call) while (((call) == -1) && (errno == EINTR))
#define RootWindow(dev) dev->spriteInfo->sprite->spriteTrace[0]
#define CLIENT_BITS(id) ((id) & RESOURCE_CLIENT_MASK)
#define CLIENT_ID(id) ((int)(CLIENT_BITS(id) >> CLIENTOFFSET))

#define MAX_MT_DEVICES		10
#define GESTURE_EQ_SIZE	256

#define GESTURE_RECOGNIZER_ONOFF	"GESTURE_RECOGNIZER_ONOFF"
#define GESTURE_PALM_REJECTION_MODE	"GESTURE_PALM_REJECTION_MODE"
#define CHECK_APPLICATION_NOT_RESPONSE_IN_INPUT_EVENT "_CHECK_APPLICATION_NOT_RESPONSE_IN_INPUT_EVENT_"
#define ANR_EVENT_WINDOW "_ANR_EVENT_WINDOW_"

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

//pinch rotation
#define ZOOM_THRESHOLD			0.05f
#define ANGLE_THRESHOLD		0.1f

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

/* Gesture query devices infomation and register handlers
  * if a device_control function is called using DEVICE_READY */
#define DEVICE_READY 11

#define SCREEN_WIDTH				720
#define SCREEN_HEIGHT				1280

#define PAN_AREA_THRESHOLD			10000//=100pixel * 100pixel
#define PAN_MOVE_THRESHOLD			5//pixel
#define PAN_UPDATE_MOVE_THRESHOLD	3//pixel
#define PAN_TIME_THRESHOLD			300//ms

#define PINCHROTATION_TIME_THRESHOLD		500//ms
#define PINCHROTATION_INIT_DIST_THRESHOLD	25.0f
#define PINCHROTATION_INIT_ANGLE_THRESHOLD	0.2f
#define PINCHROTATION_DIST_THRESHOLD		25.0f
#define PINCHROTATION_ANGLE_THRESHOLD		0.2f

#define HOLD_AREA_THRESHOLD			2500//=50pixel * 50pixel
#define HOLD_MOVE_THRESHOLD			10//pixel
#define HOLD_TIME_THRESHOLD			500//ms

#define TAP_AREA_THRESHOLD			30000//= 300pixel * 100pixel
#define TAP_MOVE_THRESHOLD			300//pixel
#define SGL_FINGER_TIME_THRESHOLD	50//ms
#define SGL_TAP_TIME_THRESHOLD		200//ms
#define DBL_TAP_TIME_THRESHOLD		400//ms
#define MAX_TAP_REPEATS				2

#define TAPNHOLD_AREA_THRESHOLD			4900//= 70pixel * 70pixel
#define TAPNHOLD_MOVE_THRESHOLD			50//pixel
#define TAPNHOLD_TAP_TIME_THRESHOLD		200//ms
#define TAPNHOLD_INTV_TIME_THRESHOLD		200//ms
#define TAPNHOLD_HOLD_TIME_THRESHOLD	500//ms

#define FLICK_AREA_THRESHOLD			22500//=150pixel * 150pixel
#define FLICK_AREA_TIMEOUT				700//ms
#define FLICK_MOVE_THRESHOLD			100//pixel
#define FLICK_MOVE_TIMEOUT				1000//ms

#define RAD_90DEG  M_PI_2
#define RAD_180DEG M_PI
#define RAD_270DEG (M_PI_2 * 3)
#define RAD_360DEG (M_PI * 2)
#define MIN_RAD (RAD_90DEG / 4)
#define MAX_RAD ((RAD_90DEG / 4) * 3)
#define RAD_180DEG_MIN (RAD_90DEG + MIN_RAD)
#define RAD_180DEG_MAX (RAD_90DEG + MAX_RAD)

#define rad2degree(r) ((r) * 180/M_PI)

#define AXIS_LABEL_PROP_ABS_X           "Abs X"
#define AXIS_LABEL_PROP_ABS_Y           "Abs Y"
#define AXIS_LABEL_PROP_ABS_Z           "Abs Z"
#define AXIS_LABEL_PROP_ABS_RX          "Abs Rotary X"
#define AXIS_LABEL_PROP_ABS_RY          "Abs Rotary Y"
#define AXIS_LABEL_PROP_ABS_RZ          "Abs Rotary Z"
#define AXIS_LABEL_PROP_ABS_THROTTLE    "Abs Throttle"
#define AXIS_LABEL_PROP_ABS_RUDDER      "Abs Rudder"
#define AXIS_LABEL_PROP_ABS_WHEEL       "Abs Wheel"
#define AXIS_LABEL_PROP_ABS_GAS         "Abs Gas"
#define AXIS_LABEL_PROP_ABS_BRAKE       "Abs Brake"
#define AXIS_LABEL_PROP_ABS_HAT0X       "Abs Hat 0 X"
#define AXIS_LABEL_PROP_ABS_HAT0Y       "Abs Hat 0 Y"
#define AXIS_LABEL_PROP_ABS_HAT1X       "Abs Hat 1 X"
#define AXIS_LABEL_PROP_ABS_HAT1Y       "Abs Hat 1 Y"
#define AXIS_LABEL_PROP_ABS_HAT2X       "Abs Hat 2 X"
#define AXIS_LABEL_PROP_ABS_HAT2Y       "Abs Hat 2 Y"
#define AXIS_LABEL_PROP_ABS_HAT3X       "Abs Hat 3 X"
#define AXIS_LABEL_PROP_ABS_HAT3Y       "Abs Hat 3 Y"
#define AXIS_LABEL_PROP_ABS_PRESSURE    "Abs Pressure"
#define AXIS_LABEL_PROP_ABS_DISTANCE    "Abs Distance"
#define AXIS_LABEL_PROP_ABS_TILT_X      "Abs Tilt X"
#define AXIS_LABEL_PROP_ABS_TILT_Y      "Abs Tilt Y"
#define AXIS_LABEL_PROP_ABS_TOOL_WIDTH  "Abs Tool Width"
#define AXIS_LABEL_PROP_ABS_VOLUME      "Abs Volume"
#define AXIS_LABEL_PROP_ABS_MT_SLOT     "Abs MT Slot"
#define AXIS_LABEL_PROP_ABS_MT_TOUCH_MAJOR "Abs MT Touch Major"
#define AXIS_LABEL_PROP_ABS_MT_TOUCH_MINOR "Abs MT Touch Minor"
#define AXIS_LABEL_PROP_ABS_MT_WIDTH_MAJOR "Abs MT Width Major"
#define AXIS_LABEL_PROP_ABS_MT_WIDTH_MINOR "Abs MT Width Minor"
#define AXIS_LABEL_PROP_ABS_MT_ORIENTATION "Abs MT Orientation"
#define AXIS_LABEL_PROP_ABS_MT_POSITION_X  "Abs MT Position X"
#define AXIS_LABEL_PROP_ABS_MT_POSITION_Y  "Abs MT Position Y"
#define AXIS_LABEL_PROP_ABS_MT_TOOL_TYPE   "Abs MT Tool Type"
#define AXIS_LABEL_PROP_ABS_MT_BLOB_ID     "Abs MT Blob ID"
#define AXIS_LABEL_PROP_ABS_MT_TRACKING_ID "Abs MT Tracking ID"
#define AXIS_LABEL_PROP_ABS_MT_PRESSURE    "Abs MT Pressure"
#define AXIS_LABEL_PROP_ABS_MT_DISTANCE    "Abs MT Distance"
#define AXIS_LABEL_PROP_ABS_MT_ANGLE       "Abs MT Angle/MT Component"
#define AXIS_LABEL_PROP_ABS_MT_PALM        "Abs MT Palm/MT Sumsize"
#define AXIS_LABEL_PROP_ABS_MISC           "Abs Misc"


typedef enum _MTSyncType
{
	MTOUCH_FRAME_SYNC_END,
	MTOUCH_FRAME_SYNC_BEGIN
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

#ifdef _F_SUPPORT_BEZEL_FLICK_
enum
{
	BEZEL_NONE,
	BEZEL_ON,
	BEZEL_START,
	BEZEL_DONE,
	BEZEL_END
};

enum
{
	NO_BEZEL,
	BEZEL_TOP_LEFT,
	BEZEL_TOP_RIGHT,
	BEZEL_BOTTOM_LEFT,
	BEZEL_BOTTOM_RIGHT
};
#endif

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

#define PalmFlickHorizFilterMask		0x01//(1 << 0)
#define PalmFlickVertiFilterMask		0x02//(1 << 1)

#define GESTURE_PALM_FILTER_MASK_ALL	0x03//(PalmFlickHorizFilterMask | PalmFlickVertiFilterMask)

#ifdef _F_SUPPORT_BEZEL_FLICK_
#define BezelFlickFilterMask		0x01//(1 << 0)
#endif

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

//palm global
#define PALM_MIN_TOUCH_MAJOR				30
#define PALM_MIN_WIDTH_MAJOR				40
#define PALM_MIN_TOUCH_MAJOR_BEZEL			16
#define PALM_MIN_WIDTH_MAJOR_BEZEL			24
#define PALM_BEZEL							33

//palm flick
#define PALM_FLICK_INITIAL_TIMEOUT			300//ms
#define PALM_FLICK_FALSE_TIMEOUT			900//ms
#define PALM_FLICK_DETECT_TIMEOUT			2000//ms
#define PALM_HORIZ_ARRAY_COUNT				4
#define PALM_VERTI_ARRAY_COUNT				7
#define PALM_FLICK_MIN_PALM					1
#define PALM_FLICK_MIN_BASE_WIDTH			30
#define PALM_FLICK_HORIZ_MAX_BASE_WIDTH		400
#define PALM_FLICK_VERTI_MAX_BASE_WIDTH		300
#define PALM_FALSE_FLICK_BASE_WIDTH			8
#define PALM_FLICK_TOUCH_MAJOR				80
#define PALM_FLICK_FINGER_MIN_TOUCH_MAJOR	15
#define PALM_FLICK_HORIZ_MAX_MOVE_Y			400
#define PALM_FLICK_VERTI_MAX_MOVE_X			300

//palm tap
#define PALM_MIN_MAJOR					200
#define PALM_SGL_TAP_TIMEOUT			200//ms
#define PALM_DBL_TAP_TIMEOUT			300//ms
#define PALM_TAP_MIN_DEVIATION			100
#define PALM_TAP_FALSE_DEVIATION		20
#define PALM_TAP_FALSE_SIZE				3

//palm hold
#define PALM_HOLD_TIME_THRESHOLD			150

typedef struct _tagPalmTouchInfo
{
	int touch_status;//One of BTN_RELEASED, BTN_PRESSED and BTN_MOVING

	int x;
	int y;
	double wmajor;//Value of valuator ABS_MT_WIDTH_MAJOR
	double tmajor;//Value of valuator ABS_MT_TOUCH_MAJOR
	double tminor;//Value of valuator ABS_MT_TOUCH_MINOR
	double tangle;//Value of valuator ABS_MT_ANGLE
	double tpalm;//Value of valuator ABS_MT_PALM
} PalmTouchInfo, *PalmTouchInfoPtr;

typedef struct _tagQueuedTouchInfo
{
	int devid;
	int pressed;
}QueuedTouchInfo;

typedef struct _tagPalmStatus
{
	int palmflag;
	double sum_size;
	double max_eccen;
	double max_angle;
	double max_wmajor;
	double max_tmajor;
	double max_tminor;
	double biggest_tmajor;
	double biggest_wmajor;
	double bigger_wmajor;
	int max_size_idx;
	int max_touched;
	int cur_touched;
	double dispersionX;
	double deviationX;
	double dispersionY;
	double deviationY;
	int cx;
	int cy;
	int max_palm;
	int single_timer_expired;

	OsTimerPtr palm_single_finger_timer;
	PalmTouchInfo pti[MAX_MT_DEVICES];
	QueuedTouchInfo qti[MAX_MT_DEVICES+1];
	pixman_region16_t area;
	pixman_region16_t finger_rects[MAX_MT_DEVICES];
} PalmStatus, *PalmStatusPtr;

typedef struct _tagPalmDrvStatus
{
	int enabled;
	int scrn_width;
	int scrn_height;
	unsigned int half_scrn_area_size;
	int horiz_coord[PALM_HORIZ_ARRAY_COUNT];
	int verti_coord[PALM_VERTI_ARRAY_COUNT];
} PalmMiscInfo, *PalmMiscInfoPtr;

typedef struct _tagStylusStatus
{
	CurTouchStatus t_status[MAX_MT_DEVICES];
	int stylus_id;
	Bool pen_detected;
	Bool fake_events;
} StylusInfo, *StylusInfoPtr;

#ifdef _F_SUPPORT_BEZEL_FLICK_
typedef struct _tagBezelStatus
{
	int width;
	int height;
}BezelStatus, *BezelStatusPtr;
typedef struct _tagBezelFlickStatus
{
	int is_active;
	BezelStatus top_left;
	BezelStatus top_right;
	BezelStatus bottom_left;
	BezelStatus bottom_right;
	int flick_distance;
	int bezel_angle_ratio;
	double min_rad;
	double max_rad;
	double min_180_rad;
	double max_180_rad;
	int bezel_angle_moving_check;
	int bezelStatus;
}BezelFlickStatus, *BezelFlickStatusPtr;
#endif

typedef struct _GestureDeviceRec
{
	char *device;
	int version;        /* Driver version */
	OsTimerPtr device_setting_timer;

	int is_active;

	int screen_width;
	int screen_height;

	int pinchrotation_time_threshold;
	double pinchrotation_dist_threshold;
	double pinchrotation_angle_threshold;

	int singlefinger_threshold;
	int singletap_threshold;
	int doubletap_threshold;

	int palm_min_touch_major;
	int palm_min_width_major;
	int palm_min_touch_major_bezel;
	int palm_min_width_major_bezel;
	int palm_bezel;

	int touchkey_id;
	MTSyncType mtsync_status;
	StylusInfo stylusInfo;
	int palm_rejection_mode;
	Bool palm_detected;
	Bool no_palm_events;

	int pass_keycodes[NUM_PASSKEYS];

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

	PalmStatus palm;
	PalmMiscInfo palm_misc;
	int wmajor_idx;
	int tmajor_idx;
	int tminor_idx;
	int tangle_idx;
	int tpalm_idx;
	int mt_px_idx;
	int mt_py_idx;
	int mt_tool_idx;

	pixman_region16_t area;
	pixman_region16_t finger_rects[MAX_MT_DEVICES];

	WindowPtr pTempWin;
	WindowPtr pTempPalmWin;
	int inc_num_pressed;

	int first_fingerid;
	int num_pressed;
	int zoom_enabled;
	int enqueue_fulled;
	int tap_repeated;
	TouchStatus fingers[MAX_MT_DEVICES];

	int event_sum[MAX_MT_DEVICES];
	uint32_t recognized_gesture;
	uint32_t filter_mask;
	uint32_t palm_filter_mask;
	uint32_t recognized_palm;
#ifdef _F_SUPPORT_BEZEL_FLICK_
	uint32_t bezel_filter_mask;
	uint32_t bezel_recognized_mask;
#endif

	DeviceIntPtr this_device;
	DeviceIntPtr mt_devices[MAX_MT_DEVICES];
	DeviceIntPtr master_pointer;
	DeviceIntPtr xtest_pointer;
#ifdef _F_SUPPORT_BEZEL_FLICK_
	BezelFlickStatus bezel;
#endif
    WindowPtr anr_window;

    int stylus_able;
    int support_palm;
} GestureDeviceRec, *GestureDevicePtr ;

#endif//_GESTURE_H_
