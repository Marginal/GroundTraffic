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
XPLMDataRef ref_plane_lat, ref_plane_lon, ref_view_x, ref_view_y, ref_view_z, ref_night, ref_monotonic, ref_tod, ref_LOD;
XPLMProbeRef ref_probe;
float draw_distance = DRAW_DISTANCE/DEFAULT_LOD;
airport_t airport = { 0 };
route_t *route = NULL;	/* Global so can be accessed in dataref callback */

/* Published DataRefs */
const char datarefs[dataref_count][60] = { REF_DISTANCE, REF_SPEED, REF_NODE_LAST, REF_NODE_LAST_DISTANCE, REF_NODE_NEXT, REF_NODE_NEXT_DISTANCE };	/* Must be in same order as dataref_t */

/* In this file */
static float flightcallback(float inElapsedSinceLastCall, float inElapsedTimeSinceLastFlightLoop, int inCounter, void *inRefcon);
static float floatrefcallback(XPLMDataRef inDataRef);
static int intrefcallback(XPLMDataRef inDataRef);


PLUGIN_API int XPluginStart(char *outName, char *outSignature, char *outDescription)
{
    char buffer[PATH_MAX], *c;
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
    ref_tod      =XPLMFindDataRef("sim/time/local_time_sec");
    ref_LOD      =XPLMFindDataRef("sim/private/controls/reno/LOD_bias_rat");
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
    if (!strcasecmp(c, "Resources"))
    {
        xplog("This plugin should be installed in a scenery package folder!");
        return 0;	/* Fail */
    }
    strcat(outName, " ");
    strcat(outName, c);
    strcat(outSignature, ".");
    strcat(outSignature, c);
    
    srand(time(NULL));	/* Seed rng */

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
        route_t *route;
        float lod=0;

        /* Changing the world detail distance setting causes a reload */
        if (ref_LOD) lod=XPLMGetDataf(ref_LOD);
        draw_distance = DRAW_DISTANCE / (lod ? lod : DEFAULT_LOD);

        /* May be a scenery shift - invalidate cached OpenGL locations */
        for(route=airport.routes; route; route=route->next)
        {
            int i;
            for (i=0; i<route->pathlen; i++)
                route->path[i].x = route->path[i].y = route->path[i].z = 0;
        }
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
static float floatrefcallback(XPLMDataRef inDataRef)
{
    if (!route) return 0;

    switch ((dataref_t) ((intptr_t) inDataRef))
    {
    case distance:
        return route->distance;
    case speed:
        return (route->state.waiting||route->state.paused) ? 0 : route->speed;
    case node_last_distance:
        return route->distance - route->last_distance;
    case node_next_distance:
        return route->next_distance - (route->distance - route->last_distance);
    default:
        return 0;
    }
}


/* dataref accesor callback */
static int intrefcallback(XPLMDataRef inDataRef)
{
    if (!route) return 0;

    switch ((dataref_t) ((intptr_t) inDataRef))
    {
    case node_last:
        return route->last_node;
    case node_next:
        return route->next_node;
    default:
        return 0;
    }
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
        int count=XPLMLookupObjects(route->object.name, airport->tower.lat, airport->tower.lon, countlibraryobjs, NULL);
        if (count)
        {
            route->objnum = rand() % count;	/* Pick one at random. (rand() doesn't give an even distribution; I don't care) */
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
