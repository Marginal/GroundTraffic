/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 */

#include "groundtraffic.h"

#if IBM
#  include <windows.h>
BOOL APIENTRY DllMain(HANDLE hModule, DWORD ul_reason, LPVOID lpReserved)
{ return TRUE; }
#endif


/* Globals */
char *pkgpath;
XPLMDataRef ref_plane_lat, ref_plane_lon, ref_view_x, ref_view_y, ref_view_z, ref_night, ref_monotonic, ref_doy, ref_tod, ref_LOD;
XPLMProbeRef ref_probe;
float draw_distance = DRAW_DISTANCE/DEFAULT_LOD;
airport_t airport = { 0 };
route_t *route = NULL;	/* Global so can be accessed in dataref callback */
int year=113;		/* Current year (in GMT tz) since 1900 */

/* Published DataRefs */
const char datarefs[dataref_count][60] = { REF_DISTANCE, REF_SPEED, REF_NODE_LAST, REF_NODE_LAST_DISTANCE, REF_NODE_NEXT, REF_NODE_NEXT_DISTANCE };	/* Must be in same order as dataref_t */

/* In this file */
static float flightcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
static float floatrefcallback(XPLMDataRef inRefcon);
static int intrefcallback(XPLMDataRef inRefcon);


PLUGIN_API int XPluginStart(char *outName, char *outSignature, char *outDescription)
{
    char buffer[PATH_MAX], *c;
    time_t t;
    struct tm *tm;
    int i;

    sprintf(outName, "GroundTraffic v%.2f", VERSION);
    strcpy(outSignature, "Marginal.GroundTraffic");
    strcpy(outDescription, "Shows animated airport ground vehicle traffic");

    ref_plane_lat=XPLMFindDataRef("sim/flightmodel/position/latitude");
    ref_plane_lon=XPLMFindDataRef("sim/flightmodel/position/longitude");
    ref_view_x   =XPLMFindDataRef("sim/graphics/view/view_x");
    ref_view_y   =XPLMFindDataRef("sim/graphics/view/view_y");
    ref_view_z   =XPLMFindDataRef("sim/graphics/view/view_z");
    ref_night    =XPLMFindDataRef("sim/graphics/scenery/percent_lights_on");
    ref_monotonic=XPLMFindDataRef("sim/time/total_running_time_sec");
    ref_doy      =XPLMFindDataRef("sim/time/local_date_days");
    ref_tod      =XPLMFindDataRef("sim/time/local_time_sec");
    ref_LOD      =XPLMFindDataRef("sim/private/controls/reno/LOD_bias_rat");
    ref_probe    =XPLMCreateProbe(xplm_ProbeY);
    if (!(ref_view_x && ref_view_y && ref_view_z && ref_night && ref_monotonic && ref_doy && ref_tod)) return xplog("Can't access X-Plane datarefs!");

    XPLMEnableFeature("XPLM_WANTS_REFLECTIONS", 0);	/* Let's assume there aren't a lot of puddles around */
    XPLMEnableFeature("XPLM_USE_NATIVE_PATHS", 1);	/* Get paths in posix format */
    XPLMGetPluginInfo(XPLMGetMyID(), NULL, buffer, NULL, NULL);
#if APL
    strcat(buffer, "/../../../");
#elif _WIN64 || _LP64
    strcat(buffer, "/../../../../");	/* Windows and Linux 64bit plugins are another level down */
#else
    strcat(buffer, "/../../../");
#endif
#if IBM
    if (!(pkgpath=_fullpath(NULL, buffer, PATH_MAX))) return xplog("Can't find my scenery folder");
    if (pkgpath[strlen(pkgpath)-1]=='\\') pkgpath[strlen(pkgpath)-1]='\0';	/* trim trailing \ */
    for (c=pkgpath+strlen(pkgpath); *(c-1)!='\\' && c>pkgpath; c--);		/* basename */
#else
    if (!(pkgpath=realpath(buffer, NULL))) return xplog("Can't find my scenery folder");
    for (c=pkgpath+strlen(pkgpath); *(c-1)!='/' && c>pkgpath; c--);		/* basename */
#endif
    if (!strcasecmp(c, "Resources"))
        return xplog("This plugin should be installed in a scenery package folder!");	/* Fail */
    strcat(outName, " ");
    strcat(outName, c);
    strcat(outSignature, ".");
    strcat(outSignature, c);
    
    srand(time(NULL));	/* Seed rng */
    if (time(&t)!=-1 && (tm = localtime(&t)))	year=tm->tm_year;	/* What year is it? */

    for(i=0; i<dataref_count; i++)
        XPLMRegisterDataAccessor(datarefs[i], (i==node_last || i==node_next) ? xplmType_Int : xplmType_Float, 0,
                                 intrefcallback, NULL, floatrefcallback, NULL, NULL, NULL,
                                 NULL, NULL, NULL, NULL, NULL, NULL, (void*) ((intptr_t) i), NULL);
    XPLMRegisterFlightLoopCallback(flightcallback, -1, NULL);			/* Just for registering datarefs with DRE */
    XPLMRegisterDrawCallback(drawcallback, xplm_Phase_Objects, 0, NULL);	/* After other 3D objects */

    return 1;
}


PLUGIN_API void XPluginStop(void)
{
    XPLMUnregisterFlightLoopCallback(flightcallback, NULL);
    XPLMUnregisterDrawCallback(drawcallback, xplm_Phase_Objects, 0, NULL);
    XPLMDestroyProbe(ref_probe);
}

PLUGIN_API int XPluginEnable(void)
{
    readconfig(pkgpath, &airport);
    return 1;
}

PLUGIN_API void XPluginDisable(void)
{
    clearconfig(&airport);
}

PLUGIN_API void XPluginReceiveMessage(XPLMPluginID inFrom, long inMessage, void *inParam)
{
    if (inMessage==XPLM_MSG_AIRPORT_LOADED)
    {
        readconfig(pkgpath, &airport);
    }
    else if (inMessage==XPLM_MSG_SCENERY_LOADED)
    {
        float lod=0;

        /* Changing the world detail distance setting causes a reload */
        if (ref_LOD) lod=XPLMGetDataf(ref_LOD);
        draw_distance = DRAW_DISTANCE / (lod ? lod : DEFAULT_LOD);
    }
}


/* Log to Log.txt */
int xplog(char *msg)
{
    XPLMDebugString("GroundTraffic: ");
    XPLMDebugString(msg);
    XPLMDebugString("\n");
    return 0;
}


/* Flight loop callback for registering DataRefs with DRE */
static float flightcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon)
{
    int i;
    XPLMPluginID PluginID = XPLMFindPluginBySignature("xplanesdk.examples.DataRefEditor");
    if (PluginID != XPLM_NO_PLUGIN_ID)
        for(i=0; i<dataref_count; i++)
            XPLMSendMessageToPlugin(PluginID, 0x01000000, (void*) datarefs[i]);
    return 0;	/* Don't call again */
}


/* dataref accesor callback */
static float floatrefcallback(XPLMDataRef inRefcon)
{
    if (!route) return 0;

    switch ((dataref_t) ((intptr_t) inRefcon))
    {
    case distance:
        return route->distance;
    case speed:
        if (route->state.frozen||route->state.paused||route->state.waiting||route->state.collision)
            return 0;
        else if (route->state.backingup)
            return -route->speed;
        else
            return route->speed;
    case node_last_distance:
        return route->distance - route->last_distance;
    case node_next_distance:
        return route->next_distance - (route->distance - route->last_distance);
    default:
        return 0;
    }
}


/* dataref accesor callback */
static int intrefcallback(XPLMDataRef inRefcon)
{
    if (!route) return 0;

    switch ((dataref_t) ((intptr_t) inRefcon))
    {
    case node_last:
        return route->last_node;
    case node_next:
        return route->next_node;
    default:
        return 0;
    }
}


/* user-defined dataref accesor callback */
float userrefcallback(XPLMDataRef inRefcon)
{
    userref_t *userref = inRefcon;
    float now;

    assert (inRefcon);
    assert (airport.state==active);
    if (!userref || !userref->start1 || airport.state!=active) return 0;

    now = XPLMGetDataf(ref_monotonic);
    /* userref->duration may be zero so use equality tests to avoid divide by zero */
    if (now <= userref->start1 || now >= userref->start1 + userref->duration)
    {
        /* First out of range. Try second. */
        if (!userref->start2)
        {
            if (now <= userref->start1)
                return userref->slope==rising ? 0 : 1;
            else
                return userref->slope==rising ? 1 : 0;
        }
        else if (now <= userref->start2)
            return userref->slope==rising ? 1 : 0;
        else if (now >= userref->start2 + userref->duration)
            return userref->slope==rising ? 0 : 1;
        else if (userref->curve == linear)
            return userref->slope==rising ? 1 - (now - userref->start2)/userref->duration : (now - userref->start2)/userref->duration;
        else
            return userref->slope==rising ? (1 + cosf(M_PI * (now - userref->start2) / userref->duration))/2 : (1 - cosf(M_PI * (now - userref->start2) / userref->duration))/2;
    }
    else if (userref->curve == linear)
        return userref->slope==rising ? (now - userref->start1)/userref->duration : 1 - (now - userref->start1)/userref->duration;
    else
        return userref->slope==rising ? (1 - cosf(M_PI * (now - userref->start1) / userref->duration))/2 : (1 + cosf(M_PI * (now - userref->start1) / userref->duration))/2;
}


/* Callback from XPLMLookupObjects to count library objects */
static void countlibraryobjs(const char *inFilePath, void *inRef)
{}	/* Don't need to do anything */


/* Callback from XPLMLookupObjects to load a library object */
static void loadlibraryobj(const char *inFilePath, void *inRef)
{
    route_t *route=inRef;
    if (!route->deadlocked)
        route->objref=XPLMLoadObject(inFilePath);	/* Load the nth object */
    (route->deadlocked)--;
}


/* Going active - load resources. Return non-zero if success */
int activate(airport_t *airport)
{
    route_t *route, *other;
    char path[PATH_MAX];
    XPLMPluginID PluginID;

    assert (airport->state==inactive);

    /* Load resources */
    for(route=airport->routes; route; route=route->next)
    {
        /* First try library object */
        int count=XPLMLookupObjects(route->object.name, airport->tower.lat, airport->tower.lon, countlibraryobjs, NULL);
        if (count)
        {
            /* Pick one at random (temporarily abuse deadlock variable as a counter) */
            route->deadlocked = rand() % count;	/* rand() doesn't give an even distribution; I don't care */
            XPLMLookupObjects(route->object.name, airport->tower.lat, airport->tower.lon, loadlibraryobj, route);
        }
        else
        {
            /* Try local object */
            struct stat info;

            strcpy(path, pkgpath);
            strcat(path, "/");
            strncat(path, route->object.name, PATH_MAX-strlen(pkgpath)-2);
            if (!stat(path, &info))	/* stat it first to suppress misleading error in Log */
                route->objref=XPLMLoadObject(path);
        }
        if (!(route->objref))
        {
            sprintf(path, "Can't find object or train \"%s\"", route->object.name);
            xplog(path);
            return 0;
        }
        /* route->next_time=0;	If previously deactivated, just let it continue when are where it left off */
    }

    /* Check for collisions - O(n2) * O(m2) ! */
    for(route=airport->routes; route; route=route->next)
        if (!route->parent)			/* Skip child routes */
        {
            int rrev = route->path[route->pathlen-1].flags.reverse;

            for(other=airport->routes; other; other=other->next)
            {
                int orev = other->path[other->pathlen-1].flags.reverse;
                int r0, r1;

                if (other==route || other->parent) continue;	/* Skip child routes */

                for (r0=0; r0 < route->pathlen; r0++)
                {
                    int o0, o1;
                    loc_t *p0, *p1;

                    if ((r1 = r0+1) == route->pathlen)
                    {
                        if (rrev)
                            break;	/* Reversible routes don't circle back */
                        else
                            r1=0;
                    }
                    p0 = &route->path[r0].waypoint;
                    p1 = &route->path[r1].waypoint;

                    for (o0=0; o0 < other->pathlen; o0++)
                    {
                        loc_t *p2, *p3;
                        double s, t, d, s1_x, s1_y, s2_x, s2_y;

                        if ((o1 = o0+1) == other->pathlen)
                        {
                            if (orev)
                                break;	/* Reversible routes don't circle back */
                            else
                                o1=0;
                        }
                        p2 = &other->path[o0].waypoint;
                        p3 = &other->path[o1].waypoint;

                        /* http://stackoverflow.com/a/1968345 */
                        s1_x = p1->lon - p0->lon;  s1_y = p1->lat - p0->lat;
                        s2_x = p3->lon - p2->lon;  s2_y = p3->lat - p2->lat;
                        d = (-s2_x * s1_y + s1_x * s2_y);
                        s = (-s1_y * (p0->lon - p2->lon) + s1_x * (p0->lat - p2->lat)) / d;
                        t = ( s2_x * (p0->lat - p2->lat) - s2_y * (p0->lon - p2->lon)) / d;

                        if (s >= 0 && s <= 1 && t >= 0 && t <= 1)
                        {
                            /* Collision */
                            collision_t *newc;
                            if (!(newc=malloc(sizeof(collision_t))))
                                return xplog("Out of memory!");
                            newc->route = other;
                            newc->node = o0;
                            newc->next = route->path[r0].collisions;
                            route->path[r0].collisions = newc;
                        }
                    }
                }
            }
        }

    /*  Register user DataRefs with DRE. We don't do this at load to avoid cluttering up the place with unused DataRefs. */
    if ((PluginID = XPLMFindPluginBySignature("xplanesdk.examples.DataRefEditor")) != XPLM_NO_PLUGIN_ID)
    {
        userref_t *userref = airport->userrefs;
        while (userref)
        {
            if (userref->ref)
                XPLMSendMessageToPlugin(PluginID, 0x01000000, (void*) userref->name);
            userref = userref->next;
        }
    }

    airport->state=active;
    return 2;
}


/* No longer active - unload any resources */
void deactivate(airport_t *airport)
{
    route_t *route;

    if (airport->state!=active) return;

    for(route=airport->routes; route; route=route->next)
    {
        XPLMUnloadObject(route->objref);
        route->objref=0;
    }

    airport->state=inactive;
    last_frame = 0;
}


/* Probe out route paths */
void proberoutes(airport_t *airport)
{
    route_t *route = airport->routes;
    int i;
    double x, y, z, foo, alt;
    XPLMProbeInfo_t probeinfo;
    probeinfo.structSize = sizeof(XPLMProbeInfo_t);

    /* First find airport location. Probe twice to correct for slant error, since our
     * airport might be up to two tiles away - http://forums.x-plane.org/index.php?showtopic=38688&page=3&#entry566469 */
    XPLMWorldToLocal(airport->tower.lat, airport->tower.lon, 0, &x, &y, &z);	// 1
    XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);			// 2
    XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);	// 3
    XPLMWorldToLocal(airport->tower.lat, airport->tower.lon, alt, &x, &y, &z);	// 4
    XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);			// 5
    XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
    airport->tower.alt=alt;
    airport->p.x = airport->p.y = airport->p.z = 0;	/* We will need to run maproutes() */

    while (route)
    {
        if (!route->parent)	/* Children share parents' route paths, so already probed */
            for (i=0; i<route->pathlen; i++)
            {
                path_t *path=route->path+i;

                if (!i)
                {
                    /* Probe first node using tower location */
                    XPLMWorldToLocal(path->waypoint.lat, path->waypoint.lon, airport->tower.alt, &x, &y, &z);
                    XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);
                    XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
                    /* Probe twice since it might be some distance from the tower */
                    XPLMWorldToLocal(path->waypoint.lat, path->waypoint.lon, alt, &x, &y, &z);
                    XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);
                    XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
                    path->waypoint.alt=alt;
                }
                else
                {
                    /* Assume this node is reasonably close to the last node so re-use its altitude to only probe once */
                    XPLMWorldToLocal(path->waypoint.lat, path->waypoint.lon, route->path[i-1].waypoint.alt, &x, &y, &z);
                    XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);
                    XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
                    path->waypoint.alt=alt;
                }
            }
        route = route->next;
    }
}

/* Determine OpenGL co-ordinates of route paths */
void maproutes(airport_t *airport)
{
    route_t *route = airport->routes;

    while (route)
    {
        if (!route->parent)	/* Children share parents' route paths, so already mapped */
        {
            /* doesn't make sense to do bezier turns at start and end waypoints of a reversible route */
            int i;
            int reversible = route->path[route->pathlen-1].flags.reverse ? 1 : 0;

            for (i=0; i<route->pathlen; i++)
            {
                double x, y, z;
                path_t *path = route->path + i;

                XPLMWorldToLocal(path->waypoint.lat, path->waypoint.lon, path->waypoint.alt, &x, &y, &z);
                path->p.x=x;  path->p.y=y;  path->p.z=z;
            }

            /* Now do bezier turn points */
            for (i = reversible; i < route->pathlen - reversible; i++)
            {
                path_t *this = route->path + i;
                path_t *next = route->path + (i+1) % route->pathlen;
                path_t *last = route->path + (i-1+route->pathlen) % route->pathlen;	/* mod of negative is undefined */
                double dist, ratio;

                if (this->flags.backup) continue;	/* Want to be straight aligned at backing-up waypoint */

                /* back */
                dist = sqrtf((last->p.x - this->p.x) * (last->p.x - this->p.x) +
                             (last->p.z - this->p.z) * (last->p.z - this->p.z));
                if (dist < route->speed * TURN_TIME)
                    ratio = 0.5;	/* Node is too close - put control point halfway */
                else
                    ratio = route->speed * (TURN_TIME/2) / dist;
                this->p1.x = this->p.x + ratio * (last->p.x - this->p.x);
                this->p1.z = this->p.z + ratio * (last->p.z - this->p.z);

                /* fwd */
                dist = sqrtf((next->p.x - this->p.x) * (next->p.x - this->p.x) +
                             (next->p.z - this->p.z) * (next->p.z - this->p.z));
                if (dist < route->speed * TURN_TIME)
                    ratio = 0.5;	/* Node is too close - put control point halfway */
                else
                    ratio = route->speed * (TURN_TIME/2) / dist;
                this->p3.x = this->p.x + ratio * (next->p.x - this->p.x);
                this->p3.z = this->p.z + ratio * (next->p.z - this->p.z);
            }
        }
        route = route->next;
    }
}
