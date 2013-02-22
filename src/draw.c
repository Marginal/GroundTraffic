/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 */

#include "groundtraffic.h"

/* Globals */
float last_frame=0;	/* last time we recalculated */


static inline int intilerange(loc_t tile, loc_t loc)
{
    return ((abs(tile.lat - floorf(loc.lat)) <= TILE_RANGE) &&
            (abs(tile.lon  - floorf(loc.lon)) <= TILE_RANGE));
}

static inline int indrawrange(float xdist, float ydist, float zdist, float range)
{
    float dist2 = xdist*xdist + ydist*ydist + zdist*zdist;
    return (dist2 <= range*range);
}


int drawcallback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon)
{
    static int is_night=0;

    float view_x, view_y, view_z;
    float now;
    int tod=-1;
    double x, y, z, foo, alt;
    loc_t tile;
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
        if (!indrawrange(x-view_x, y-view_y, z-view_z, ACTIVE_DISTANCE+ACTIVE_HYSTERESIS))
        {
            deactivate(&airport);
            return 1;
        }
    }
    else if (!indrawrange(x-view_x, y-view_y, z-view_z, ACTIVE_DISTANCE))
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
            /* Have to check draw range every frame since "now" isn't updated while sim paused */
            if (indrawrange(route->drawinfo.x-view_x, route->drawinfo.y-view_y, route->drawinfo.z-view_z, draw_distance))
                XPLMDrawObjects(route->objref, 1, &(route->drawinfo), is_night, 1);
        route=airport.routes;	/* Leave at the first route for DataRefEditor */
        return 1;
    }
    last_frame=now;
        
    /* Update and draw */
    is_night = (int)(XPLMGetDataf(ref_night)+0.67);
    for(route=airport.routes; route; route=route->next)
    {
        path_t *last_node, *next_node;
        float route_now = now - route->object.offset;	/* Train objects are drawn in the past */

        if (route_now >= route->next_time)
        {
            if (route->state.waiting)
            {
                /* We don't get notified when time-of-day changes in the sim, so poll once a minute */
                int i;
                if (tod < 0) tod = (int) (XPLMGetDataf(ref_tod)/60);
                for (i=0; i<MAX_ATTIMES; i++)
                {
                    if (route->path[route->last_node].attime[i] == INVALID_AT)
                        break;
                    else if (route->path[route->last_node].attime[i] == tod)
                    {
                        route->state.waiting = 0;
                        break;
                    }
                }
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
                if (!route->last_node)
                    route->last_distance = 0;	/* reset distance travelled to prevent growing stupidly large */
                else
                    route->last_distance += route->next_distance;
                route->distance = route->last_distance;

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

                if (!route->parent)
                {
                    if (route->path[route->last_node].attime[0] != INVALID_AT)
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
            route->drawinfo.heading = atan2(next_node->x - last_node->x, last_node->z - next_node->z) * 180.0*M_1_PI + route->object.heading;
            route->drawinfo.x = last_node->x;  route->drawinfo.y = last_node->y;  route->drawinfo.z = last_node->z;
            route->next_distance = sqrtf((next_node->x - last_node->x) * (next_node->x - last_node->x) +
                                         (next_node->z - last_node->z) * (next_node->z - last_node->z));

            if (route->parent)
            {
                /* Parent controls our state */
                route->last_time = route->parent->last_time;	/* Maintain spacing from head of train */
                route->next_time = route->last_time + route->next_distance / route->speed;
            }
            else
            {
                route->last_time = now;

                if (route->state.waiting)
                    route->next_time = route->last_time + 60;	/* poll every 60 seconds */
                else if (route->state.paused)
                    route->next_time = route->last_time + route->path[route->last_node].pausetime;
                else
                    route->next_time = route->last_time + route->next_distance / route->speed;
            }
            
            if (!(route->state.waiting||route->state.paused))
            {
                /* Force re-probe */
                route->next_probe = route_now;
                route->next_y = last_node->y;
            }
        }
        else	// (route_now < route->next_time)
        {
            next_node = route->path + route->next_node;
            last_node = route->path + route->last_node;

            if (route->parent)
            {
                /* Parent controls our state */
                if ((route->parent->state.waiting|route->parent->state.paused) && !route->state.paused)
                {
                    /* Parent has just paused */
                    route->state.paused = 1;
                    route->next_time = FLT_MAX;
                }
                else if (!(route->parent->state.waiting|route->parent->state.paused) && route->state.paused)
                {
                    /* Parent has just unpaused */
                    route->state.paused = 0;
                    route->next_time = route->parent->last_time;	/* Maintain spacing from head of train */
                    route->last_time = route->next_time - route->next_distance / route->speed;
                }
            }
        
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

            if (route_now >= route->next_probe)
            {
                /* Probe four seconds in the future */
                route->next_probe = route_now + PROBE_INTERVAL;
                route->last_y = route->next_y;
                progress = (route->next_probe - route->last_time) / (route->next_time - route->last_time);
                XPLMProbeTerrainXYZ(ref_probe, last_node->x + progress * (next_node->x - last_node->x), route->last_y, last_node->z + progress * (next_node->z - last_node->z), &probeinfo);
                route->next_y = probeinfo.locationY;
            }
            if (now >= route->last_time)
            {
                progress = (route_now - route->last_time) / (route->next_time - route->last_time);
                route->drawinfo.x = last_node->x + progress * (next_node->x - last_node->x);
                route->drawinfo.y = route->next_y + (route->last_y - route->next_y) * (route->next_probe - route_now) / PROBE_INTERVAL;
                route->drawinfo.z = last_node->z + progress * (next_node->z - last_node->z);
                route->distance = route->last_distance + progress * route->next_distance;
            }
            else
            {
                /* We must be in replay, and we've gone back in time beyond the last decision. Just show at the last node. */
                route->drawinfo.x = last_node->x;  route->drawinfo.y = last_node->y;  route->drawinfo.z = last_node->z;
                route->distance = route->last_distance;
            }
        }
        /* All the preceeding mucking about is pretty inexpensive. But drawing is expensive so test for range */
        if (indrawrange(route->drawinfo.x-view_x, route->drawinfo.y-view_y, route->drawinfo.z-view_z, draw_distance))
            XPLMDrawObjects(route->objref, 1, &(route->drawinfo), is_night, 1);
    }
    route=airport.routes;	/* Leave at the first route for DataRefEditor */

    return 1;
}


