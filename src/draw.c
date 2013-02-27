/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 */

#include "groundtraffic.h"

/* Globals */
float last_frame=0;	/* last time we recalculated */

/* In this file */
static void bez(XPLMDrawInfo_t *drawinfo, point_t *p1, point_t *p2, point_t *p3, float mu);


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

    double x, y, z;
    float view_x, view_y, view_z;
    float now;
    int tod=-1;
    unsigned int dow=0;
    float airport_x, airport_y, airport_z;
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

    if (airport.tower.alt==INVALID_ALT)
    {
        /* First time we've encountered our airport. Determine elevations. */
        proberoutes(&airport);
    }

    XPLMWorldToLocal(airport.tower.lat, airport.tower.lon, airport.tower.alt, &x, &y, &z);
    airport_x=x;  airport_y=y;  airport_z=z;
    view_x=XPLMGetDataf(ref_view_x);
    view_y=XPLMGetDataf(ref_view_y);
    view_z=XPLMGetDataf(ref_view_z);

    if (airport.state==active)
    {
        if (!indrawrange(airport_x-view_x, airport_y-view_y, airport_z-view_z, ACTIVE_DISTANCE+ACTIVE_HYSTERESIS))
        {
            deactivate(&airport);
            return 1;
        }
    }
    else
    {
        if (!indrawrange(airport_x-view_x, airport_y-view_y, airport_z-view_z, ACTIVE_DISTANCE)) return 1;	/* stay inactive */

        /* Going active */
        if (!activate(&airport))
        {
            clearconfig(&airport);
            return 1;
        }
    }

    if (airport_x!=airport.p.x || airport_y!=airport.p.y || airport_z!=airport.p.z)
    {
        /* OpenGL projection has shifted */
        airport.p.x=airport_x;  airport.p.y=airport_y;  airport.p.z=airport_z;
        maproutes(&airport);
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
        double progress;				/* double to force type promotion for accuracy */
        float route_now = now - route->object.lag;	/* Train objects are drawn in the past */

        if (route_now >= route->next_time && !route->state.frozen)
        {
            if (route->state.waiting)
            {
                /* We don't get notified when time-of-day changes in the sim, so poll once a minute */
                int i;
                if (!dow)
                {
                    /* Get current day-of-week. FIXME: This is in user's timezone, not the airport's. */
                    struct tm tm = { 0, 0, 12, XPLMGetDatai(ref_doy)+1, 0, year };
                    dow = (mktime(&tm) == -1) ? DAY_SUN : 1 << tm.tm_wday;
                }
                if (tod < 0) tod = (int) (XPLMGetDataf(ref_tod)/60);
                for (i=0; i<MAX_ATTIMES; i++)
                {
                    if (route->path[route->last_node].attime[i] == INVALID_AT)
                        break;
                    else if ((route->path[route->last_node].attime[i] == tod) &&
                             (route->path[route->last_node].atdays & dow))
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
                        route->state.waiting = 1;
                    if (route->path[route->last_node].pausetime)
                        route->state.paused = 1;
                }
                if (route->path[route->last_node].reverse)
                {
                    route->direction = -1;
                    route->next_node = route->pathlen-2;
                }
            }
            
            /* calculate time and heading to next node */
            last_node = route->path + route->last_node;
            next_node = route->path + route->next_node;

            /* Assume distances are too small to care about earth curvature so just calculate using OpenGL coords */
            route->next_heading = atan2(next_node->p.x - last_node->p.x, last_node->p.z - next_node->p.z) * 180.0*M_1_PI;
            route->next_distance = sqrtf((next_node->p.x - last_node->p.x) * (next_node->p.x - last_node->p.x) +
                                         (next_node->p.z - last_node->p.z) * (next_node->p.z - last_node->p.z));

            /* Maintain speed/progress unless there's been a large gap in draw callbacks because we were deactivated / disabled */
            if (route->last_time && route_now - route->next_time < RESET_TIME)
                route->last_time = route->next_time;
            else
            {
                route->last_time = now;			/* reset */
                route_now = route->last_time - route->object.lag;
            }

            if (route->state.waiting)
                route->next_time = route->last_time + 60;	/* poll every 60 seconds */
            else if (route->state.paused)
                route->next_time = route->last_time + route->path[route->last_node].pausetime;
            else
                route->next_time = route->last_time + route->next_distance / route->speed;

            /* Force re-probe since we've changed direction */
            route->next_probe = route_now;
            route->next_y = last_node->p.y;
        }

        /* Parent controls state of children */
        if (route->parent)
        {
            if ((route->parent->state.waiting|route->parent->state.paused) && !route->state.frozen)
            {
                /* Parent has just paused. Necessary to re-sync. */
                route->state.frozen = 1;
                route->direction = route->parent->direction;
                route->distance = route->parent->distance - route->object.lag * route->speed;	/* Negative at first node */
                if (route->path[route->pathlen-1].reverse && (!route->parent->last_node || route->parent->last_node==route->pathlen-1))
                {
                    /* Parent is at end of a reversible route - line up back in time from it */
                    route->last_node = route->parent->last_node;
                    route->next_node = route->parent->next_node;
                    route->last_distance = route->parent->last_distance;
                    route->next_distance = route->parent->next_distance;
                    route->next_heading = route->parent->next_heading;
                    route->last_time = route->parent->last_time;
                    route->next_time = route->last_time - route->next_distance / route->speed;
                }
                else
                {
                    /* Line up towards parent */
                    route->next_node = route->parent->last_node;
                    route->last_node = (route->next_node - route->direction + route->pathlen) % route->pathlen;
                    next_node = route->path + route->next_node;
                    last_node = route->path + route->last_node;
                    route->next_distance = sqrtf((next_node->p.x - last_node->p.x) * (next_node->p.x - last_node->p.x) +
                                                 (next_node->p.z - last_node->p.z) * (next_node->p.z - last_node->p.z));
                    route->last_distance = route->parent->last_distance - route->next_distance;
                    route->next_heading = atan2(next_node->p.x - last_node->p.x, last_node->p.z - next_node->p.z) * 180.0*M_1_PI;
                    route->next_time = route->parent->last_time;
                    route->last_time = route->last_time - route->next_distance / route->speed;
                }
                route_now = route->parent->last_time - route->object.lag;
            }
            else if (!(route->parent->state.waiting|route->parent->state.paused) && route->state.frozen)
            {
                /* Parent has just unpaused - maintain spacing */
                route->state.frozen = 0;
                if (route->last_node==route->parent->last_node)
                {
                    route->last_time = route->parent->last_time;
                    route->next_time = route->last_time + route->next_distance / route->speed;
                }
                else
                {
                    route->next_time = route->parent->last_time;
                    route->last_time = route->next_time - route->next_distance / route->speed;
                }
                route_now = route->parent->last_time - route->object.lag;
            }
        }

        /* Calculate drawing position */
        last_node = route->path + route->last_node;
        next_node = route->path + route->next_node;
        if (!(route->state.paused||route->state.frozen||route->state.waiting))
        {
            if (now < route->last_time)
                /* We must be in replay, and we've gone back in time beyond the last decision. Just show at the last node. */
                route_now = route->last_time - route->object.lag;	/* Train objects are drawn in the past */

            if (route_now >= route->next_probe)
            {
                /* Probe four seconds in the future */
                route->next_probe = route_now + PROBE_INTERVAL;
                route->last_y = route->next_y;
                progress = (route->next_probe - route->last_time) / (route->next_time - route->last_time);
                XPLMProbeTerrainXYZ(ref_probe, last_node->p.x + progress * (next_node->p.x - last_node->p.x), route->last_y, last_node->p.z + progress * (next_node->p.z - last_node->p.z), &probeinfo);
                route->next_y = probeinfo.locationY;
            }

            progress = (route_now - route->last_time) / (route->next_time - route->last_time);
            route->drawinfo.y = route->next_y + (route->last_y - route->next_y) * (route->next_probe - route_now) / PROBE_INTERVAL;
            if (!route->object.heading)
                route->drawinfo.pitch = sinf((route->next_y - route->last_y) / (PROBE_INTERVAL * route->speed)) * 180.0*M_1_PI;
            else if (route->object.heading == 180)
                route->drawinfo.pitch = sinf((route->last_y - route->next_y) / (PROBE_INTERVAL * route->speed)) * 180.0*M_1_PI;
            route->distance = route->last_distance + progress * route->next_distance;
        }
        else
        {
            /* Paused: Fake up last and next times for drawing code below */
            // FIXME: Do this once at start of pause and the wouldn't have to recalc times here
            if (!route->parent)
            {
                progress = - (route->object.lag * route->speed) / route->next_distance;
                route->last_time = now;
                // route->next_time = now + route->next_distance / route->speed;	/* cant do this for parents */
            }
            else if (route->last_node==route->parent->last_node)
            {
                progress = - (route->object.lag * route->speed) / route->next_distance;
                route->last_time = now;
                route->next_time = now + route->next_distance / route->speed;
            }
            else
            {
                progress = 1 - (route->object.lag * route->speed) / route->next_distance;
                route->next_time = now;
                route->last_time = now - route->next_distance / route->speed;
            }
            route_now = now - route->object.lag;
            route->drawinfo.y = last_node->p.y;
            route->drawinfo.pitch = 0;	/* Since we're not probing */
        }

        /* Finally do the drawing */
        if ((progress>=0.5 && route->next_time - route_now < TURN_TIME/2) && (next_node->p1.x || next_node->p1.z))
        {
            /* Approaching a waypoint */
            if (route->speed * 2 <= route->next_distance)
                bez(&route->drawinfo, &next_node->p1, &next_node->p, &next_node->p3, 0.5+(route_now - route->next_time)/TURN_TIME);
            else	/* Short edge */
                bez(&route->drawinfo, &next_node->p1, &next_node->p, &next_node->p3, progress - 0.5);
        }
        else if ((route_now - route->last_time < TURN_TIME/2) && (last_node->p3.x || last_node->p3.z))
        {
            /* Leaving a waypoint (may be from a negative direction if a paused child) */
            if (route->speed * 2 <= route->next_distance)
                bez(&route->drawinfo, &last_node->p1, &last_node->p, &last_node->p3, 0.5+(route_now - route->last_time)/TURN_TIME);
            else	/* Short edge */
                bez(&route->drawinfo, &last_node->p1, &last_node->p, &last_node->p3, progress + 0.5);
        }
        else
        {
            route->drawinfo.x = last_node->p.x + progress * (next_node->p.x - last_node->p.x);
            route->drawinfo.z = last_node->p.z + progress * (next_node->p.z - last_node->p.z);
            route->drawinfo.heading = route->next_heading;
        }
        if (route->object.offset)
        {
            double h = route->drawinfo.heading*M_PI/180;
            route->drawinfo.x += sin(h) * route->object.offset;
            route->drawinfo.z -= cos(h) * route->object.offset;
        }
        route->drawinfo.heading += route->object.heading;

        /* All the preceeding mucking about is pretty inexpensive. But drawing is expensive so test for range */
        if (indrawrange(route->drawinfo.x-view_x, route->drawinfo.y-view_y, route->drawinfo.z-view_z, draw_distance))
            XPLMDrawObjects(route->objref, 1, &(route->drawinfo), is_night, 1);
    }
    route=airport.routes;	/* Leave at the first route for DataRefEditor */

    return 1;
}


static void bez(XPLMDrawInfo_t *drawinfo, point_t *p1, point_t *p2, point_t *p3, float mu)
{
    double mum1, mum12, mu2;
    double tx, tz;

    // assert (mu>=0 && mu<=1);	// Trains may go negative at start or in replay

    mu2 = mu * mu;
    mum1 = 1 - mu;
    mum12 = mum1 * mum1;
    drawinfo->x = p1->x * mum12 + 2 * p2->x * mum1 * mu + p3->x * mu2;
    drawinfo->z = p1->z * mum12 + 2 * p2->z * mum1 * mu + p3->z * mu2;

    tx = 2 * mum1 * (p2->x - p1->x) + 2 * mu * (p3->x - p2->x);
    tz =-2 * mum1 * (p2->z - p1->z) - 2 * mu * (p3->z - p2->z);
    drawinfo->heading = atan2(tx, tz) * 180.0*M_1_PI;
}
