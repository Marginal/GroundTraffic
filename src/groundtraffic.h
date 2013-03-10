/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 */

#ifdef _MSC_VER
#  define _USE_MATH_DEFINES
#  define _CRT_SECURE_NO_DEPRECATE
#  define inline __inline
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
#  define strcasecmp(s1, s2) _stricmp(s1, s2)
#  define strncasecmp(s1, s2, n) _strnicmp(s1, s2, n)
#endif

#if APL || LIN
#  include <dirent.h>
#  include <libgen.h>
#endif

#define XPLM210	/* Requires X-Plane 10.0 or later */
#define XPLM200
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"
#include "XPLMProcessing.h"
#include "XPLMScenery.h"
#include "XPLMUtilities.h"

/* Version of assert that suppresses "variable ... set but not used" if the variable only exists for the purpose of the asserted expression */
#ifdef	NDEBUG
#  undef assert
#  define assert(expr)	((void)(expr))
#endif

/* constants */
#define MAX_NAME 256		/* Arbitrary limit on object name lengths */
#define TILE_RANGE 1		/* How many tiles away from plane's tile to consider getting out of bed for */
#define ACTIVE_DISTANCE 6000.0	/* Distance [m] from tower location at which to actually get out of bed */
#define ACTIVE_HYSTERESIS (ACTIVE_DISTANCE*0.05)
#define DRAW_DISTANCE 3500.0	/* Distance [m] from object to draw it. Divided by LOD value. */
#define DEFAULT_LOD 2.25	/* Equivalent to "medium" world detail distance */
#define PROBE_INTERVAL 4	/* How often to probe ahead for altitude [s] */
#define TURN_TIME 2		/* Time [s] to execute a turn at a waypoint */
#define COLLISION_INTERVAL 4	/* How long to poll for crossing route path to become free */
#define COLLISION_TIMEOUT (60/COLLISION_INTERVAL)	/* How many times to poll before giving up to break deadlock */
#define RESET_TIME 15		/* If we're deactivated for longer than this then reset route timings */
#define MAX_VAR 10		/* How many var datarefs */

/* Published DataRefs */
#define REF_VAR			"marginal/groundtraffic/var"
#define REF_DISTANCE		"marginal/groundtraffic/distance"
#define REF_SPEED		"marginal/groundtraffic/speed"
#define REF_NODE_LAST		"marginal/groundtraffic/waypoint/last"
#define REF_NODE_LAST_DISTANCE	"marginal/groundtraffic/waypoint/last/distance"
#define REF_NODE_NEXT		"marginal/groundtraffic/waypoint/next"
#define REF_NODE_NEXT_DISTANCE	"marginal/groundtraffic/waypoint/next/distance"
typedef enum
{
    distance=0, speed, node_last, node_last_distance, node_next, node_next_distance,
    dataref_count
} dataref_t;

/* Geolocation */
#define INVALID_ALT FLT_MAX
typedef struct
{
    float lat, lon, alt;	/* drawing routines use float, so no point storing higher precision */
} loc_t;

/* OpenGL coordinate */
typedef struct
{
    float x, y,z;
} point_t;

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


/* User-defined DataRef */
typedef struct userref_t
{
    char *name;		/* NULL for standard var[n] datarefs */
    XPLMDataRef ref;
    float duration;
    float start1, start2;
    enum { rising, falling } slope;
    enum { linear, sine } curve;
    struct userref_t *next;
} userref_t;


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
        int set1 : 1;
        int set2 : 1;
        int backup : 1;		/* Just reverse to next node */
    } flags;
    struct collision_t *collisions;	/* Collisions with other routes */
    struct userref_t *userref;
} path_t;

typedef struct
{
    char name[MAX_NAME];
    float heading;		/* rotation applied before drawing */
    float offset;		/* offset applied after rotation before drawing. [m] */
    float lag;			/* time lag. [m] in train defn, [s] in route */
} objdef_t;

/* A route from routes.txt */
typedef struct route_t
{
    objdef_t object;
    XPLMObjectRef objref;
    path_t *path;
    int pathlen;
    struct
    {
        int frozen : 1;		/* Child whose parent is paused or waiting */
        int paused : 1;
        int waiting : 1;
        int collision : 1;
        int forwardsb : 1;	/* Waypoint before backing up */
        int backingup : 1;
        int forwardsa : 1;	/* Waypoint after backing up */
    } state;
    int direction;		/* Traversing path 1=forwards, -1=reverse */
    int last_node, next_node;	/* The last and next waypoints visited on the path */
    float last_time, next_time;	/* Time we left last_node, expected time to hit the next node */
    float speed;		/* [m/s] */
    float last_distance;	/* Cumulative distance travelled from first to last_node [m] */
    float next_distance;	/* Distance from last_node to next_node [m] */
    float distance;		/* Cumulative distance travelled from first node [m] */
    float next_heading;		/* Heading from last_node to next_node [m] */
    XPLMDrawInfo_t *drawinfo;	/* Where to draw - current OpenGL co-ordinates */
    float next_probe;		/* Time we should probe altitude again */
    float last_y, next_y;	/* OpenGL co-ordinates at last probe point */
    int deadlocked;		/* Counter used to break collision deadlock */
    struct userref_t varrefs[MAX_VAR];	/* Per-route var dataref */
    struct route_t *parent;	/* Points to head of a train */
    struct route_t *next;
} route_t;


/* A train of interconnected objects */
#define MAX_TRAIN 16
typedef struct train_t
{
    char name[MAX_NAME];
    objdef_t objects[MAX_TRAIN];
    struct train_t *next;
} train_t;


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
    enum { noconfig=0, inactive, active } state;
    char ICAO[5];
    loc_t tower;
    point_t p;
    route_t *routes;
    route_t *firstroute;
    train_t *trains;
    userref_t *userrefs;
    XPLMDrawInfo_t *drawinfo;	/* consolidated XPLMDrawInfo_t array for all routes/objects so they can be batched */
} airport_t;


/* prototypes */
int activate(airport_t *airport);
void deactivate(airport_t *airport);
void proberoutes(airport_t *airport);
void maproutes(airport_t *airport);
float userrefcallback(XPLMDataRef inRefcon);

int xplog(char *msg);
int readconfig(char *pkgpath, airport_t *airport);
void clearconfig(airport_t *airport);

int predrawcallback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon);
int drawcallback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon);


/* Globals */
extern char *pkgpath;
extern XPLMDataRef ref_plane_lat, ref_plane_lon, ref_view_x, ref_view_y, ref_view_z, ref_rentype, ref_night, ref_monotonic, ref_doy, ref_tod, ref_LOD;
extern XPLMDataRef ref_datarefs[dataref_count], ref_varref;
extern XPLMProbeRef ref_probe;
extern float draw_distance;
extern airport_t airport;
extern route_t *route;		/* Global so can be accessed in dataref callback */
extern int year;		/* Current year (in GMT tz) */

extern float last_frame;	/* Global so can be reset while disabled */
