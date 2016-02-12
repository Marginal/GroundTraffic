/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013-2014
 *
 * Licensed under GNU LGPL v2.1.
 */

#ifndef	_GROUNDTRAFFIC_H_
#define	_GROUNDTRAFFIC_H_

#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#  define _CRT_SECURE_NO_DEPRECATE
#  define inline __forceinline
#endif

#include <assert.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <sys/stat.h>

#ifdef _MSC_VER
#  define PATH_MAX MAX_PATH
#  define snprintf _snprintf
#  define hypotf _hypotf
#  define strcasecmp(s1, s2) _stricmp(s1, s2)
#  define strncasecmp(s1, s2, n) _strnicmp(s1, s2, n)
#endif

#if IBM		/* http://msdn.microsoft.com/en-us/library/windows/desktop/ms686355%28v=vs.85%29.aspx */
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#else
#  include <dirent.h>
#  include <libgen.h>
#  include <sys/time.h>
#  include <pthread.h>
#  if APL	/* https://developer.apple.com/library/mac/documentation/cocoa/Conceptual/Multithreading/ThreadSafety/ThreadSafety.html */
#    include <libkern/OSAtomic.h>
#    define MemoryBarrier OSMemoryBarrier
#  elif LIN	/* http://gcc.gnu.org/onlinedocs/gcc-4.6.3/gcc/Atomic-Builtins.html */
#    define MemoryBarrier __sync_synchronize
#  endif
#endif

#if APL
#  include <OpenGL/gl.h>
#  include <OpenGL/glu.h>
#else
#  include <GL/gl.h>
#  include <GL/glu.h>
#endif

#define XPLM210	/* Requires X-Plane 10.0 or later */
#define XPLM200
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlanes.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMScenery.h"
#include "XPLMUtilities.h"

#include "bbox.h"

/* Version of assert that suppresses "variable ... set but not used" if the variable only exists for the purpose of the asserted expression */
#ifdef NDEBUG
#  undef assert
#  define assert(expr)	((void)(expr))
#elif !IBM
#  include <signal.h>
#  undef assert
#  define assert(expr)	{ if (!(expr)) raise(SIGTRAP); };
#endif

/* constants */
#define MAX_NAME 256		/* Arbitrary limit on object name lengths */
#define TILE_RANGE 1		/* How many tiles away from plane's tile to consider getting out of bed for */
#define ACTIVE_POLL 16		/* Poll to see if we've come into range every n frames */
#define ACTIVE_DISTANCE 5000.f	/* Distance [m] from tower location at which to actually get out of bed */
#define ACTIVE_WATER 20000.f	/* As above when "water" flag is set (you can see a long way on water) */
#define ACTIVE_HYSTERESIS (ACTIVE_DISTANCE*0.05f)
#define MAX_RADIUS 4000.f	/* Arbitrary limit on size of routes' bounding box */
#define RADIUS 6378145.f	/* from sim/physics/earth_radius_m [m] */
#define DEFAULT_DRAWLOD 2.f	/* Equivalent to an object 3m high */
#define DEFAULT_LOD 2.25f	/* Equivalent to "medium" world detail distance */
#define DEFAULT_DRAWCARS 3.f	/* Equivalent to "Chicago Suburbs" world detail distance */
#define PROBE_ALT_FIRST -100	/* Arbitrary depth below tower for probe of first waypoint */
#define PROBE_ALT_NEXT -25	/* Arbitrary depth below previous waypoint */
#define PROBE_INTERVAL 0.5f	/* How often to probe ahead for altitude [s] */
#define PROBE_GRADIENT 0.25f	/* Max gradient that vehicles will follow = 1:4 */
#define TURN_TIME 2.f		/* Time [s] to execute a turn at a waypoint */
#define AT_INTERVAL 60.f	/* How often [s] to poll for At times */
#define WHEN_INTERVAL 1.f	/* How often [s] to poll for When DataRef values */
#define COLLISION_INTERVAL 2.f	/* How long [s] to poll for crossing route path to become free. Also minimum spacing on overlapping segments */
#define COLLISION_TIMEOUT ((int) 60/COLLISION_INTERVAL)	/* How many times to poll before giving up to break deadlock */
#define COLLISION_ALT 3.f	/* Objects won't collide if their altitude differs by more than this [m] */
#define RESET_TIME 15.f		/* If we're deactivated for longer than this then reset route timings */
#define MAX_VAR 10		/* How many var datarefs */
#define HIGHWAY_VARIANCE 0.25f	/* How much to vary spacing of objects on a highway */

/* Options */
#undef  DO_BENCHMARK
#undef  DO_MARKERS

/* Published DataRefs */
#define REF_BASE		"marginal/groundtraffic/"
#define REF_VAR			REF_BASE "var"
#define REF_DISTANCE		REF_BASE "distance"
#define REF_SPEED		REF_BASE "speed"
#define REF_STEER		REF_BASE "steer"
#define REF_NODE_LAST		REF_BASE "waypoint/last"
#define REF_NODE_LAST_DISTANCE	REF_BASE "waypoint/last/distance"
#define REF_NODE_NEXT		REF_BASE "waypoint/next"
#define REF_NODE_NEXT_DISTANCE	REF_BASE "waypoint/next/distance"
#define REF_LOD			REF_BASE "lod"
#define REF_RANGE		REF_BASE "range"
#define REF_DRAWTIME		REF_BASE "drawtime"

typedef enum
{
    distance=0, speed, steer, node_last, node_last_distance, node_next, node_next_distance,
#ifdef DEBUG
    lod, range,
#endif
#ifdef DO_BENCHMARK
    drawtime,
#endif
    dataref_count
} dataref_t;

/* Geolocation */
typedef struct
{
    float lat, lon, alt;	/* drawing routines use float, so no point storing higher precision */
} loc_t;

#define INVALID_ALT FLT_MAX
typedef struct
{
    double lat, lon, alt;	/* but XPLMWorldToLocal uses double, so prevent type conversions */
} dloc_t;

/* OpenGL coordinate */
typedef struct
{
    float x, y, z;
} point_t;

typedef struct
{
    double x, y, z;
} dpoint_t;

/* Days in same order as tm_wday in struct tm, such that 2**tm_wday==DAY_X */
#define DAY_SUN 1
#define DAY_MON 2
#define DAY_TUE 4
#define DAY_WED 8
#define DAY_THU 16
#define DAY_FRI 32
#define DAY_SAT 64
#define DAY_ALL (DAY_SUN|DAY_MON|DAY_TUE|DAY_WED|DAY_THU|DAY_FRI|DAY_SAT)
#define MAX_ATTIMES 24		/* Number of times allowed in an At command */
#define INVALID_AT -1


/* User-defined published DataRef or per-route var[n] */
typedef enum { rising, falling } slope_t;
typedef enum { linear, sine } curve_t;
typedef struct userref_t
{
    char *name;			/* NULL for per-route var[n] datarefs */
    XPLMDataRef ref;
    float duration;
    float start1, start2;
    slope_t slope;
    curve_t curve;
    struct userref_t *next;	/* NULL for per-route var[n] datarefs */
} userref_t;

/* Set command */
typedef struct setcmd_t
{
    userref_t *userref;
    float duration;
    struct {
        int set1 : 1;		/* set command */
        int set2 : 1;		/* pause ... set command */
        slope_t slope : 1;
        curve_t curve : 1;
    } flags;
    struct setcmd_t *next;	/* Next setcmd at a waypoint */
} setcmd_t;


/* DataRef referenced in When or And command */
#define xplmType_Mine -1
typedef struct extref_t
{
    char *name;
    XPLMDataRef ref;		/* ID, or pointer if type == xplmType_Mine */
    XPLMDataTypeID type;
    struct extref_t *next;
} extref_t;


/* When & And command */
typedef struct whenref_t
{
    extref_t *extref;
    int idx;
    float from, to;
    struct whenref_t *next;	/* Next whenref at a waypoint */
} whenref_t;


/* Route path - locations or commands */
struct collision_t;
typedef struct
{
    loc_t waypoint;		/* World */
    point_t p;			/* Local OpenGL co-ordinates */
    point_t p1, p3;		/* Bezier points for turn */
    int pausetime;
    short attime[MAX_ATTIMES];	/* minutes past midnight */
    unsigned char atdays;
    struct {
        int reverse : 1;	/* Reverse whole route */
        int backup : 1;		/* Just reverse to next node */
    } flags;
    struct collision_t *collisions;	/* Collisions with other routes */
    setcmd_t *setcmds;
    whenref_t *whenrefs;
    int drawX, drawY;		/* For labeling nodes */
} path_t;

typedef struct
{
    GLfloat r, g, b;
} glColor3f_t;

typedef struct
{
    char *name;
    char *physical_name;
    XPLMObjectRef objref;
    float drawlod;		/* Multiply by lod_factor to get draw distance */
    float lag;			/* time lag. [m] in train defn, [s] in route */
    float offset;		/* offset applied after rotation before drawing. [m] */
    float heading;		/* rotation applied before drawing */
} objdef_t;

/* A route from routes.txt */
struct collision_t;
struct highway_t;
typedef struct route_t
{
    int lineno;			/* Source line in GroundTraffic.txt */
    objdef_t object;
    path_t *path;
    int pathlen;
    bbox_t bbox;		/* Bounding box of path */
    struct
    {
        int frozen : 1;		/* Child whose parent is waiting */
        int paused : 1;		/* Waiting for pause duration */
        int waiting : 1;	/* Waiting for At time */
        int dataref : 1;	/* Waiting for DataRef value */
        int forwardsb : 1;	/* Waypoint before backing up */
        int backingup : 1;
        int forwardsa : 1;	/* Waypoint after backing up */
        int hasdataref: 1;	/* Does the object on this route have DataRef callbacks? */
        struct collision_t *collision;	/* Waiting for this collision to resolve */
    } state;
    int direction;		/* Traversing path 1=forwards, -1=reverse */
    int last_node, next_node;	/* The last and next waypoints visited on the path */
    float last_time, next_time;	/* Time we left last_node, expected time to hit the next node */
    float freeze_time;		/* For children: Time when parent started pause */
    float speed;		/* [m/s] */
    float last_distance;	/* Cumulative distance travelled from first to last_node [m] */
    float next_distance;	/* Distance from last_node to next_node [m] */
    float distance;		/* Cumulative distance travelled from first node [m] */
    float next_heading;		/* Heading from last_node to next_node [m] */
    float steer;		/* Approximate steer angle (degrees) while turning */
    glColor3f_t drawcolor;	/* debug path color */
    int drawX, drawY;		/* debug label position */
    XPLMDrawInfo_t *drawinfo;	/* Where to draw - current OpenGL co-ordinates */
    float last_probe, next_probe;	/* Time of last altitude probe and when we should probe again */
    float last_y, next_y;	/* OpenGL co-ordinates at last and next probe points */
    int deadlocked;		/* Counter used to break collision deadlock */
    float highway_offset;	/* For highway children: Starting offset from start of route */
    struct highway_t *highway;	/* Is a highway */
    userref_t (*varrefs)[MAX_VAR];	/* Per-route var dataref */
    struct route_t *parent;	/* Points to head of a train */
    struct route_t *next;
} route_t;


/* A train of interconnected objects */
#define MAX_TRAIN 16
typedef struct train_t
{
    char *name;
    objdef_t objects[MAX_TRAIN];
    struct train_t *next;
} train_t;


/* A highway */
#define MAX_HIGHWAY 16
typedef struct highway_t
{
    objdef_t objects[MAX_HIGHWAY];
    objdef_t *expanded;	/* Physical objects */
    int obj_count;	/* Physical object count */
    float spacing;
    struct highway_t *next;
} highway_t;


/* Collision between routes */
typedef struct collision_t
{
    route_t *route;	/* Other route */
    int node;		/* Other node (assuming forwards direction) */
    struct collision_t *next;
} collision_t;


/* airport info from routes.txt */
typedef struct
{
    enum { noconfig=0, inactive, activating, active } state;
    int case_folding;		/* Whether our package is on a case-sensitive file system (i.e. Linux) */
    int done_first_activation;	/* Whether we've calculated collisions and expanded highways */
    int new_airport;		/* Whether we've moved to a new airport, so activation should be immediate */
    dloc_t tower;
    dpoint_t p;			/* Remember OpenGL location of tower to detect scenery shift */
    int drawroutes;
    int reflections;
    float active_distance;
    route_t *routes;
    route_t *firstroute;
    train_t *trains;
    userref_t *userrefs;
    extref_t *extrefs;
    XPLMDrawInfo_t *drawinfo;	/* consolidated XPLMDrawInfo_t array for all routes/objects so they can be batched */
} airport_t;


/* Worker thread */
/* Align to cache-line - http://software.intel.com/en-us/articles/avoiding-and-identifying-false-sharing-among-threads */
#if IBM
typedef __declspec(align(64)) struct
#else
typedef struct __attribute__((aligned(64)))
#endif
{
#if IBM
    HANDLE thread;
#else
    pthread_t thread;
#endif
    int die_please;
    int finished;
} worker_t;


/* prototypes */
int activate(airport_t *airport);
void deactivate(airport_t *airport);
void proberoutes(airport_t *airport);
void maproutes(airport_t *airport);
float userrefcallback(XPLMDataRef inRefcon);

int xplog(char *msg);
int readconfig(char *pkgpath, airport_t *airport);
void clearconfig(airport_t *airport);

void labelcallback(XPLMWindowID inWindowID, void *inRefcon);
int drawcallback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon);

void drawdebug3d(int drawnodes, GLint view[4]);
void drawdebug2d();
int drawmap3d(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon);
int drawmap2d(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon);


/* Globals */
extern char *pkgpath;
extern XPLMDataRef ref_plane_lat, ref_plane_lon, ref_view_x, ref_view_y, ref_view_z, ref_rentype, ref_night, ref_monotonic, ref_doy, ref_tod, ref_LOD;
extern XPLMDataRef ref_datarefs[dataref_count], ref_varref;
extern XPLMProbeRef ref_probe;
extern float lod_bias;
extern airport_t airport;
extern route_t *drawroute;	/* Global so can be accessed in dataref callback */
extern int year;		/* Current year (in GMT tz) */
#ifdef DO_BENCHMARK
extern int drawcumul;
extern int drawframes;
#endif

extern float last_frame;	/* Global so can be reset while disabled */
extern float lod_factor;
extern char *labeltbl;
extern int font_width, font_semiheight;


/* inlines */

/* naive UTF8-aware strlen */
static inline int utf8_strlen(const char *s)
{
    int len = 0;
    while (*s)
        if (((*s++) & 0xC0) != 0x80) len++;	/* Not a continuation byte */
    return len;
}


static inline int intilerange(dloc_t loc)
{
    double tile_lat, tile_lon;
    tile_lat = floor(XPLMGetDatad(ref_plane_lat));
    tile_lon = floor(XPLMGetDatad(ref_plane_lon));
    return ((fabs(tile_lat - floor(loc.lat)) <= TILE_RANGE) && (fabs(tile_lon - floor(loc.lon)) <= TILE_RANGE));
}


static inline int indrawrange(float xdist, float ydist, float zdist, float range)
{
    assert (airport.tower.alt != (double) INVALID_ALT);	/* If altitude is invalid then arguments to this function will be too */
    return (xdist*xdist + ydist*ydist + zdist*zdist <= range*range);
}

static inline float R2D(float r)
{
    return r * ((float) (180*M_1_PI));
}

static inline float D2R(float d)
{
    return d * ((float) (M_PI/180));
}


/* Operations on point_t */

static inline float angleto(point_t *from, point_t *to)
{
    return atan2f(to->x-from->x, to->z-from->z);
}

/* 2D is point inside polygon? */
static inline int inside(point_t *p, point_t *poly, int npoints)
{
    /* http://paulbourke.net/geometry/polygonmesh/ "Determining if a point lies on the interior of a polygon" */
    int i, j, c=0;
    for (i=0, j=npoints-1; i<npoints; j=i++)
        if ((((poly[i].z <= p->z) && (p->z < poly[j].z)) || ((poly[j].z <= p->z) && (p->z < poly[i].z))) &&
            (p->x < (poly[j].x - poly[i].x) * (p->z - poly[i].z) / (poly[j].z - poly[i].z) + poly[i].x))
            c = !c;
    return c;
}

/* 2D does line p0->p1 intersect p2->p3 */
static inline int intersect(point_t *p0, point_t *p1, point_t *p2, point_t *p3)
{
    /* http://stackoverflow.com/a/1968345 */
    float s, t, d, s1_x, s1_z, s2_x, s2_z;

    s1_x = p1->x - p0->x;  s1_z = p1->z - p0->z;
    s2_x = p3->x - p2->x;  s2_z = p3->z - p2->z;
    d = -s2_x * s1_z + s1_x * s2_z;
    if (d==0) return 0;	/* Precisely parallel or collinear - ignore in either case */

    s = (-s1_z * (p0->x - p2->x) + s1_x * (p0->z - p2->z)) / d;
    t = ( s2_x * (p0->z - p2->z) - s2_z * (p0->x - p2->x)) / d;

    /* use strict comparison because only interested in significant intersections */
    return s > 0 && s < 1 && t > 0 && t < 1;
}


/* Operations on loc_t */

/* 2D does line p0->p1 intersect p2->p3 */
static inline int loc_intersect(loc_t *p0, loc_t *p1, loc_t *p2, loc_t *p3)
{
    /* http://stackoverflow.com/a/1968345 */
    float s, t, d, s1_x, s1_y, s2_x, s2_y;

    s1_x = p1->lon - p0->lon;  s1_y = p1->lat - p0->lat;
    s2_x = p3->lon - p2->lon;  s2_y = p3->lat - p2->lat;
    d = (-s2_x * s1_y + s1_x * s2_y);
    if (d==0) return 0;	/* Precisely parallel or collinear - ignore in either case */

    s = (-s1_y * (p0->lon - p2->lon) + s1_x * (p0->lat - p2->lat)) / d;
    t = ( s2_x * (p0->lat - p2->lat) - s2_y * (p0->lon - p2->lon)) / d;

    /* use strict comparison because path segments don't count as colliding if they just share a starting node */
    return s > 0 && s < 1 && t > 0 && t < 1;
}


/* quick and dirty and not very accurate gettimeofday implementation ignoring timezone */
#if defined(_MSC_VER) && defined(DO_BENCHMARK)
# include <winsock2.h>	/* for timeval */
static inline int gettimeofday(struct timeval *tp, void *tzp)
{
    LARGE_INTEGER frequency;        // ticks per second
    LARGE_INTEGER counter;

    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    counter.QuadPart = (counter.QuadPart * 1000000) / frequency.QuadPart;	/* In microseconds */
    tp->tv_sec = counter.QuadPart / 1000000;
    tp->tv_usec = counter.QuadPart - tp->tv_sec * 1000000;
    return 0;
}
#endif


/* Operations on worker_t */

static inline int worker_start(worker_t *worker, void *(*start_routine)(void *))
{
    worker->die_please = worker->finished = 0;
    MemoryBarrier();
#if IBM
    if (!(worker->thread = CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) start_routine, NULL, 0, NULL)))
#else
    if (pthread_create(&worker->thread, NULL, start_routine, NULL))
#endif
    {
        return xplog("Internal error: Can't create worker thread");
    }
    return -1;
}

/* Wait for worker to stop */
static inline void worker_wait(worker_t *worker)
{
    if (worker->thread)
    {
#if IBM
        WaitForSingleObject(worker->thread, INFINITE);
        CloseHandle(worker->thread);
#else
        pthread_join(worker->thread, NULL);
#endif
        worker->thread =  0;
    }
}

/* Signal worker and wait for it to stop */
static inline void worker_stop(worker_t *worker)
{
    if (worker->thread)
    {
        worker->die_please = -1;
        MemoryBarrier();
        worker_wait(worker);
    }
}

/* Check whether worker is finished */
static inline int worker_is_finished(worker_t *worker)
{
    if (worker->thread)
    {
        MemoryBarrier();
        if (worker->finished)
        {
            worker_wait(worker);
            return -1;
        }
        else
            return 0;
    }
    else
        return -1;
}

/* Called from worker thread to check for early termination */
#define worker_check_stop(worker) { MemoryBarrier(); if ((*(worker)).die_please) return NULL; }

/* Called from worker thread to indicate completion */
#define worker_has_finished(worker) { MemoryBarrier(); (*(worker)).finished = -1; }


#endif /* _GROUNDTRAFFIC_H_ */
