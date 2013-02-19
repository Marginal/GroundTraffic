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

#if LIN
#  include <dirent.h>
#  include <libgen.h>
#endif

#define XPLM210	/* Requires X-Plane 10.0 or later */
#define XPLM200
#include "XPLMDataAccess.h"
#include "XPLMDisplay.h"
#include "XPLMGraphics.h"
#include "XPLMPlugin.h"
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

/* Geolocation */
#define INVALID_ALT DBL_MAX
typedef struct
{
    double lat, lon, alt;	/* we do want double precision to prevent jerkiness */
} loc_t;

/* Route path - locations or commands */
typedef struct
{
    loc_t waypoint;		/* World */
    float x, y, z;		/* Local OpenGL co-ordinates */
    int pausetime;
    short attime;		/* minutes past midnight */
    union
    {
        unsigned char bits;
        struct
        {
            int mon : 1;
            int tue : 1;
            int wed : 1;
            int thu : 1;
            int fri : 1;
            int sat : 1;
            int sun : 1;
        };
    } atdays;
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
