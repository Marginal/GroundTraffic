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
#define TILE_RANGE 1		/* How many tiles away from plane's tile to consider getting out of bed for */
#define DRAW_DISTANCE 5000.0	/* Distance [m] from tower location at which to actually get out of bed */
#define DRAW_HYSTERESIS (DRAW_DISTANCE*0.05)
#define PROBE_INTERVAL 4	/* How often to probe ahead for altitude [s] */

/* Published DataRefs */
#define REF_DISTANCE		"marginal.org.uk/groundtraffic/distance"
#define REF_SPEED		"marginal.org.uk/groundtraffic/speed"
#define REF_NODE_LAST		"marginal.org.uk/groundtraffic/waypoint/last"
#define REF_NODE_LAST_DISTANCE	"marginal.org.uk/groundtraffic/waypoint/last/distance"
#define REF_NODE_NEXT		"marginal.org.uk/groundtraffic/waypoint/next"
#define REF_NODE_NEXT_DISTANCE	"marginal.org.uk/groundtraffic/waypoint/next/distance"
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

#define DAY_MON 1
#define DAY_TUE 2
#define DAY_WED 4
#define DAY_THU 8
#define DAY_FRI 16
#define DAY_SAT 32
#define DAY_SUN 64
#define MAX_ATTIMES 24		/* Number of times allowed in an At command */
#define INVALID_AT -1

/* Route path - locations or commands */
typedef struct
{
    loc_t waypoint;		/* World */
    float x, y, z;		/* Local OpenGL co-ordinates */
    int pausetime;
    short attime[MAX_ATTIMES];	/* minutes past midnight */
    unsigned char atdays;
    unsigned char reverse;
} path_t;
        
/* A route from routes.txt */
typedef struct route_t
{
    char objname[PATH_MAX];
    XPLMObjectRef objref;
    path_t *path;
    int pathlen;
    struct
    {
        int paused :1;
        int waiting : 1;
    } state;
    int direction;		/* Traversing path 1=forwards, -1=reverse */
    int last_node, next_node;	/* The last and next waypoints visited on the path */
    float last_time, next_time;	/* Time we left last_node, expected time to hit the next node */
    float heading;		/* rotation applied before drawing */
    float speed;		/* [m/s] */
    float last_distance;	/* Cumulative distance travelled from first to last_node [m] */
    float next_distance;	/* Distance from last_node to next_node [m] */
    float distance;		/* Cumulative distance travelled from first node [m] */
    XPLMDrawInfo_t drawinfo;	/* Where to draw - current OpenGL co-ordinates */
    float next_probe;		/* Time we should probe altitude again */
    float last_y, next_y;	/* OpenGL co-ordinates at last probe point */
    int objnum;			/* Only used while reading library object */
    struct route_t *next;
} route_t;

typedef enum
{
    noconfig=0, inactive, active
} state_t;

/* airport info from routes.txt */
typedef struct
{
    state_t state;
    char ICAO[5];
    loc_t tower;
    route_t *routes;
} airport_t;


/* prototypes */
int xplog(char *msg);
void deactivate(airport_t *airport);
int readconfig(char *pkgpath, airport_t *airport);
void clearconfig(airport_t *airport);
