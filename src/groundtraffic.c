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
static char *pkgpath;
static XPLMDataRef ref_plane_lat, ref_plane_lon, ref_view_x, ref_view_y, ref_view_z, ref_night, ref_monotonic, ref_tod;
static XPLMProbeRef ref_probe;
static airport_t airport = { 0 };


int xplog(char *msg)
{
    XPLMDebugString("GroundTraffic: ");
    XPLMDebugString(msg);
    XPLMDebugString("\n");
    return 0;
}


static inline int intilerange(loc_t tile, loc_t loc)
{
    return ((abs(tile.lat - floorf(loc.lat)) <= TILE_RANGE) &&
            (abs(tile.lon  - floorf(loc.lon)) <= TILE_RANGE));
}

static inline int indrawrange(float xdist, float ydist, float zdist, float range)
{
    return (xdist*xdist + ydist*ydist + zdist*zdist <= range*range);
}


/* Callback from XPLMLookupObjects to count library objects */
static void countlibraryobjs(const char *inFilePath, void *inRef)
{}	/* Don't need to do anything */


/* Callback from XPLMLookupObjects to load a library object */
static void loadlibraryobj(const char *inFilePath, void *inRef)
{
    route_t *route=inRef;
    if (!route->objnum)
        route->objref=XPLMLoadObject(inFilePath);	/* Load the nth object */
    (route->objnum)--;
}


/* Going active - load resources. Return non-zero if success */
int activate(airport_t *airport)
{
    route_t *route;
    char path[PATH_MAX];

    assert (airport->state!=noconfig);
    if (airport->state==active) return 1;

    for(route=airport->routes; route; route=route->next)
    {
        /* First try library object */
        int count=XPLMLookupObjects(route->objname, airport->tower.lat, airport->tower.lon, countlibraryobjs, NULL);
        if (count)
        {
            route->objnum = rand() % count;	/* Pick one at random. (rand() doesn't give an even distribution; I don't care) */
            XPLMLookupObjects(route->objname, airport->tower.lat, airport->tower.lon, loadlibraryobj, route);
        }
        else
        {
            /* Try local object */
            strcpy(path, pkgpath);
            strcat(path, "/");
            strncat(path, route->objname, PATH_MAX-strlen(pkgpath)-2);
            route->objref=XPLMLoadObject(path);
        }
        if (!(route->objref))
        {
            sprintf(path, "Can't load object \"%s\"", route->objname);
            xplog(path);
            return 0;
        }
        /* route->next_time=0;	If previously deactivated, just let it continue when are where it laeft off */
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
}


static int drawcallback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon)
{
    static float last_frame=0;	/* last time we recalculated */
    static int is_night=0;

    float view_x, view_y, view_z;
    float now;
    int tod=-1;
    double x, y, z, foo, alt;
    loc_t tile;
    route_t *route;
    XPLMProbeInfo_t probeinfo;
    probeinfo.structSize = sizeof(XPLMProbeInfo_t);

    if (airport.state==noconfig) return 1;
    
    tile.lat=floor(XPLMGetDatad(ref_plane_lat));
    tile.lon=floor(XPLMGetDatad(ref_plane_lon));
    if (!intilerange(tile, airport.tower))
    {
        deactivate(&airport);
        return 1;
    }
    else if (airport.tower.alt==INVALID_ALT)
    {
        /* First time we've encountered our airport. Determine elevation. Probe twice to correct for slant error, since our
         * airport might be up to two tiles away - http://forums.x-plane.org/index.php?showtopic=38688&page=3&#entry566469 */
        XPLMWorldToLocal(airport.tower.lat, airport.tower.lon, 0, &x, &y, &z);	// 1
        XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);			// 2
        XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);	// 3
        XPLMWorldToLocal(airport.tower.lat, airport.tower.lon, alt, &x, &y, &z);	// 4
        XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);			// 5
        XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
        airport.tower.alt=alt;
    }
    else
    {
        XPLMWorldToLocal(airport.tower.lat, airport.tower.lon, airport.tower.alt, &x, &y, &z);
    }

    view_x=XPLMGetDataf(ref_view_x);
    view_y=XPLMGetDataf(ref_view_y);
    view_z=XPLMGetDataf(ref_view_z);

    if (airport.state==active)
    {
        if (!indrawrange(x-view_x, y-view_y, z-view_z, DRAW_DISTANCE+DRAW_HYSTERESIS))
        {
            deactivate(&airport);
            return 1;
        }
    }
    else if (!indrawrange(x-view_x, y-view_y, z-view_z, DRAW_DISTANCE))
    {
        return 1;	/* stay inactive */
    }

    if (!activate(&airport))
    {
        clearconfig(&airport);
        return 1;
    }        

    /* We can be called multiple times per frame depending on shadow settings -
     * ("sim/graphics/view/world_render_type" = 0 if normal draw, 3 if shadow draw (which precedes normal))
     * So skip calculations and just draw if we've already run the calculations for this frame. */
    now = XPLMGetDataf(ref_monotonic);
    if (now == last_frame)
    {
        for(route=airport.routes; route; route=route->next)
            XPLMDrawObjects(route->objref, 1, &(route->drawinfo), is_night, 1);
        return 1;
    }
        
    /* Update and draw */
    last_frame=now;
    is_night = (int)(XPLMGetDataf(ref_night)+0.67);
    for(route=airport.routes; route; route=route->next)
    {
        path_t *last_node, *next_node;

        if (now >= route->next_time)
        {
            if (route->state.waiting)
            {
                /* We don't get notified when time-of-day changes in the sim, so poll once a minute */
                if (tod < 0) tod = (int) (XPLMGetDataf(ref_tod)/60);
                if (route->path[route->last_node].attime == tod)
                    route->state.waiting = 0;
                /* last and next were calculated when we first hit this waypoint */
            }
            else if (route->state.paused)
            {
                route->state.paused = 0;
                /* last and next were calculated when we first hit this waypoint */
            }
            else	/* next waypoint */
            {
                route->last_node = route->next_node;
                route->next_node += route->direction;

                if (route->next_node >= route->pathlen)
                {
                    /* At end of route - head to start */
                    route->next_node = 0;
                }
                else if (route->next_node < 0)
                {
                    /* Back at start of route - start again */
                    route->direction = 1;
                    route->next_node = 1;
                }

                if (route->path[route->last_node].attime >= 0)
                {
                    route->state.waiting = 1;
                }
                if (route->path[route->last_node].pausetime)
                {
                    route->state.paused = 1;
                }
                if (route->path[route->last_node].reverse)
                {
                    route->direction = -1;
                    route->next_node = route->pathlen-2;
                }
            }
            
            last_node = route->path + route->last_node;
            next_node = route->path + route->next_node;

            if (last_node->waypoint.alt==INVALID_ALT)
            {
                assert (!route->last_node);	/* Nodes other than the first should have been worked out below */
                /* Probe first node using tower location */
                XPLMWorldToLocal(last_node->waypoint.lat, last_node->waypoint.lon, airport.tower.alt, &x, &y, &z);
                XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);
                XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
                /* Probe twice since it might be some distance from the tower */
                XPLMWorldToLocal(route->path[0].waypoint.lat, route->path[0].waypoint.lon, alt, &x, &y, &z);
                XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);
                XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
                last_node->waypoint.alt=alt;
                last_node->x=probeinfo.locationX;  last_node->y=probeinfo.locationY;  last_node->z=probeinfo.locationZ;
            }
            else if (!last_node->x && !last_node->y && !last_node->z)
            {
                XPLMWorldToLocal(last_node->waypoint.lat, last_node->waypoint.lon, last_node->waypoint.alt, &x, &y, &z);
                last_node->x=x;  last_node->y=y;  last_node->z=z;
            }

            if (next_node->waypoint.alt==INVALID_ALT)
            {
                /* Assume this node is reasonably close to the last node so re-use its altitude to only probe once */
                XPLMWorldToLocal(next_node->waypoint.lat, next_node->waypoint.lon, route->path[route->last_node].waypoint.alt, &x, &y, &z);
                XPLMProbeTerrainXYZ(ref_probe, x, y, z, &probeinfo);
                XPLMLocalToWorld(probeinfo.locationX, probeinfo.locationY, probeinfo.locationZ, &foo, &foo, &alt);
                next_node->waypoint.alt=alt;
                next_node->x=probeinfo.locationX;  next_node->y=probeinfo.locationY;  next_node->z=probeinfo.locationZ;
            }
            else if (!next_node->x && !next_node->y && !next_node->z)
            {
                XPLMWorldToLocal(next_node->waypoint.lat, next_node->waypoint.lon, next_node->waypoint.alt, &x, &y, &z);
                next_node->x=x;  next_node->y=y;  next_node->z=z;
            }

            /* calculate time and heading to next node */

            /* Assume distances are too small to care about earth curvature so just calculate using OpenGL coords */
            route->drawinfo.heading = atan2(next_node->x - last_node->x, last_node->z - next_node->z) * 180.0*M_1_PI + route->heading;
            route->drawinfo.x = last_node->x;  route->drawinfo.y = last_node->y;  route->drawinfo.z = last_node->z;
            route->last_time = now;
            if (route->state.waiting)
            {
                route->next_time = now + 60;	/* poll every 60 seconds */
            }
            else if (route->state.paused)
            {
                route->next_time = now + route->path[route->last_node].pausetime;
            }
            else
            {
                route->next_time = now + sqrtf((next_node->x - last_node->x) * (next_node->x - last_node->x) +
                                               (next_node->z - last_node->z) * (next_node->z - last_node->z)) / route->speed;
                /* Force re-probe */
                route->next_probe = now;
                route->next_y = last_node->y;
            }
        }
        else	// (now >= route->next_time)
        {
            next_node = route->path + route->next_node;
            last_node = route->path + route->last_node;
        
            /* Re-cache waypoints' OpenGL co-ordinates if they were reset during a scenery shift */
            if (!last_node->x && !last_node->y && !last_node->z)
            {
                XPLMWorldToLocal(last_node->waypoint.lat, last_node->waypoint.lon, last_node->waypoint.alt, &x, &y, &z);
                last_node->x=x;  last_node->y=y;  last_node->z=z;
            }
            if (!next_node->x && !next_node->y && !next_node->z)
            {
                XPLMWorldToLocal(next_node->waypoint.lat, next_node->waypoint.lon, next_node->waypoint.alt, &x, &y, &z);
                next_node->x=x;  next_node->y=y;  next_node->z=z;
            }
        }

        /* Finally do the drawing */
        if (!(route->state.waiting||route->state.paused))
        {
            double progress;	/* double to force type promotion for accuracy */

            if (now >= route->next_probe)
            {
                /* Probe four seconds in the future */
                route->next_probe = now + PROBE_INTERVAL;
                route->last_y = route->next_y;
                progress = (route->next_probe - route->last_time) / (route->next_time - route->last_time);
                XPLMProbeTerrainXYZ(ref_probe, last_node->x + progress * (next_node->x - last_node->x), route->last_y, last_node->z + progress * (next_node->z - last_node->z), &probeinfo);
                route->next_y = probeinfo.locationY;
            }
            progress = (now - route->last_time) / (route->next_time - route->last_time);
            if (progress >= 0)
            {
                route->drawinfo.x = last_node->x + progress * (next_node->x - last_node->x);
                route->drawinfo.y = route->next_y + (route->last_y - route->next_y) * (route->next_probe - now) / PROBE_INTERVAL;
                route->drawinfo.z = last_node->z + progress * (next_node->z - last_node->z);
            }
            else
            {
                /* We must be in replay, and we've gone back in time beyond the last decision. Just show at the last node. */
                route->drawinfo.x = last_node->x;  route->drawinfo.y = last_node->y;  route->drawinfo.z = last_node->z;
            }
        }
        XPLMDrawObjects(route->objref, 1, &(route->drawinfo), is_night, 1);
    }

    return 1;
}



/**********************************************************************
 Plugin entry points
 **********************************************************************/

PLUGIN_API int XPluginStart(char *outName, char *outSignature, char *outDescription)
{
    char buffer[PATH_MAX], *c;

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
    ref_tod      =XPLMFindDataRef("sim/time/local_time_sec");
    ref_probe    =XPLMCreateProbe(xplm_ProbeY);
    if (!(ref_view_x && ref_view_y && ref_view_z && ref_night && ref_monotonic && ref_tod)) return xplog("Can't access X-Plane datarefs!");

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
    strcat(outName, " ");
    strcat(outName, c);
    strcat(outSignature, ".");
    strcat(outSignature, c);
    
    srand(time(NULL));	/* Seed rng */

    XPLMRegisterDrawCallback(drawcallback, xplm_Phase_Objects, 0, NULL);		/* After other 3D objects */

    return 1;
}

PLUGIN_API void XPluginStop(void)
{
    XPLMDestroyProbe(ref_probe);				/* Deallocate resources */
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
        /* May be a scenery shift - invalidate cached OpenGL locations */
        route_t *route;
        for(route=airport.routes; route; route=route->next)
        {
            int i;
            for (i=0; i<route->pathlen; i++)
                route->path[i].x = route->path[i].y = route->path[i].z = 0;
        }
    }
}
