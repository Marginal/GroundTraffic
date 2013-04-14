/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 * Licensed under GNU LGPL v2.1.
 */

#include "groundtraffic.h"
#include "planes.h"

/* Globals */
route_t *drawroute = NULL;	/* Global so can be accessed in DataRef callback */
float last_frame=0;		/* last time we recalculated */
static int is_night=0;		/* was night last time we recalculated? */

/* In this file */
static void bez(XPLMDrawInfo_t *drawinfo, point_t *p1, point_t *p2, point_t *p3, float mu);


static int iscollision(route_t *route, int tryno)
{
    path_t *last_node = route->path + route->last_node;
    path_t *next_node = route->path + route->next_node;
    collision_t *c = tryno ? (route->direction>0 ? last_node->collisions : next_node->collisions) : NULL;
    int planeno;
    float t;

    /* Route collisions */
    while (c)
    {
        if ((c->route->direction>0 ? c->route->last_node : c->route->next_node) == c->node &&
            !(c->route->state.paused||c->route->state.waiting||c->route->state.dataref||c->route->state.collision) &&	/* No point waiting for a paused route. He'll re-check on exit from pause */
            c->route->last_node != c->route->next_node)			/* Check we're not just enabled/activated and deadlocked */
        {
            /* Collision */
            route->deadlocked = tryno;
            return -1;
        }
        c = c->next;
    }

    /* Plane collisions */
    t = route->next_distance / route->speed;	/* time to next waypoint */
    for (planeno=0; planeno<count_planes(); planeno++)
    {
        point_t *p;
        int i, j;

        if (!(p = get_plane_footprint(planeno, t))) continue;

        /* If our waypoint is inside the exclusion region then get our object out of the way! */
        if (inside(&last_node->p, p, 4)) return 0;

        for (i=0, j=3; i<4; j=i++)
            if (intersect(&last_node->p, &next_node->p, p+i, p+j))
                return -1;	/* Next edge intersects this plane's footprint */
    }

    return 0;
}


/* For drawing route nodes. Relies on the fact that the OpenGL view is not clipped to our window */
void labelcallback(XPLMWindowID inWindowID, void *inRefcon)
{
    float color[] = { 1, 1, 1 };
    int i;
    route_t *route;
    char buf[8];

    for (route=airport.routes; route; route=route->next)
        if (!route->parent)
            for (i=0; i<route->pathlen; i++)
            {
                path_t *node = route->path + i;
                if (node->drawX>0 && node->drawY>0)
                {
                    snprintf(buf, sizeof(buf), "%d", i);
                    XPLMDrawString(color, node->drawX, node->drawY, buf, NULL, xplmFont_Basic);
                }
            }
}


/* Actually do the drawing. Uses global drawroute so DataRef callbacks have access to the route being drawn.
 * Tries to batch concurrent routes that use the same XPLMObjectRef (note: not textual name since one name
 * might map to multiple library objects). Route linked list was sorted in XPLMObjectRef order during activate().
 * We can't batch if the object uses per-route DataRefs since we wouldn't know which route/object the accessor
 * callback was called for.
 * Note we don't know that an object uses per-route DataRefs until we draw it for the first time when the
 * accessor callback will set route->state.hasdataref.
 * If some objects are in range but others not then we issue one XPLMDrawObjects() call that spans all those
 * in range, since this seems to be cheaper than multiple calls even if more drawing results. */
static void drawroutes()
{
    float view_x, view_y, view_z;

    view_x=XPLMGetDataf(ref_view_x);
    view_y=XPLMGetDataf(ref_view_y);
    view_z=XPLMGetDataf(ref_view_z);

    drawroute=airport.routes;
    while (drawroute)
    {
        if (drawroute->state.hasdataref)
        {
            /* Have to check draw range every frame since "now" isn't updated while sim paused */
            if (indrawrange(drawroute->drawinfo->x-view_x, drawroute->drawinfo->y-view_y, drawroute->drawinfo->z-view_z, draw_distance))
                XPLMDrawObjects(drawroute->objref, 1, drawroute->drawinfo, is_night, 1);

            if (drawroute->next && drawroute->objref == drawroute->next->objref)
                drawroute->next->state.hasdataref = -1;	/* propagate flag to all routes using this objref */

            drawroute=drawroute->next;
        }
        else
        {
            route_t *route, *first = 0, *last = 0;

            for (route=drawroute; route && route->objref==drawroute->objref; route=route->next)
                /* Have to check draw range every frame since "now" isn't updated while sim paused */
                if (indrawrange(route->drawinfo->x-view_x, route->drawinfo->y-view_y, route->drawinfo->z-view_z, draw_distance))
                {
                    if (!first) first = route;
                    last = route;
                }

            if (first)
                XPLMDrawObjects(drawroute->objref, 1 + last->drawinfo - first->drawinfo, first->drawinfo, is_night, 1);

            drawroute=route;
        }
    }
}


/* Main update and draw loop */
int drawcallback(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon)
{
    double airport_x, airport_y, airport_z;
    float now;
    route_t *route;
    int tod=-1;
    unsigned int dow=0;
    XPLMProbeInfo_t probeinfo;
    probeinfo.structSize = sizeof(XPLMProbeInfo_t);

    assert (airport.state == active);

    XPLMWorldToLocal(airport.tower.lat, airport.tower.lon, airport.tower.alt, &airport_x, &airport_y, &airport_z);
    if (airport.p.x != airport_x || airport.p.y != airport_y || airport.p.z != airport_z)
    {
        /* OpenGL projection has shifted */
        airport.p.x=airport_x;  airport.p.y=airport_y;  airport.p.z=airport_z;
        maproutes(&airport);
    }

    /* draw route paths */
    if (airport.drawroutes && !XPLMGetDatai(ref_rentype))
    {
#ifdef DEBUG
        int planeno;
#endif
        GLdouble model[16], proj[16];
        GLint view[4] = { 0 };

        XPLMSetGraphicsState(0, 0, 0,   0, 1,   0, 0);
        glLineWidth(1.5);

        /* This is slow! */
        glGetDoublev(GL_MODELVIEW_MATRIX, model);
        glGetDoublev(GL_PROJECTION_MATRIX, proj);
        XPLMGetScreenSize(view+2, view+3);	/* Real viewport reported by GL_VIEWPORT will be larger than physical screen if FSAA enabled */

        for(route=airport.routes; route; route=route->next)
            if (!route->parent)
            {
                int i;
                glColor3fv(&route->drawcolor.r);
                glBegin(route->path[route->pathlen-1].flags.reverse ? GL_LINE_STRIP : GL_LINE_LOOP);
                for (i=0; i<route->pathlen; i++)
                {
                    path_t *node = route->path + i;
                    GLdouble winX, winY, winZ;

                    glVertex3fv(&node->p.x);
                    gluProject(node->p.x, node->p.y, node->p.z, model, proj, view, &winX, &winY, &winZ);
                    if (winZ <= 1)	/* not behind us */
                    {
                        node->drawX = winX;
                        node->drawY = winY;
                    }
                    else
                    {
                        node->drawX = node->drawY = -1;
                    }
                }
                glEnd();
            }

#ifdef DEBUG
        /* Draw AI plane positions */
        glColor4f(0,0,0,0.25f);
        glBegin(GL_QUADS);
        for (planeno=0; planeno<count_planes(); planeno++)
        {
            point_t *p;
            int i;

            if ((p = get_plane_footprint(planeno, 5.f)))	/* Display 5 seconds ahead */
                for (i=0; i<4; i++)
                    glVertex3fv(&(p[i].x));
        }
        glEnd();
#endif
    }

    /* We can be called multiple times per frame depending on shadow settings -
     * ("sim/graphics/view/world_render_type" = 0 if normal draw, 3 if shadow draw (which precedes normal))
     * So skip calculations and just draw if we've already run the calculations for this frame. */
    if ((now = XPLMGetDataf(ref_monotonic)) == last_frame)
    {
        drawroutes();
#ifdef DEBUG
        if (!XPLMGetDatai(ref_rentype)) last_frame = 0;	/* In DEBUG recalculate positions once per frame for easier debugging */
#endif
        return 1;
    }
    last_frame = now;
        
    /* Update and draw */
    is_night = (int) (XPLMGetDataf(ref_night) + 0.67f);
    for(route=airport.routes; route; route=route->next)
    {
        path_t *last_node, *next_node;
        float progress;
        float route_now = now - route->object.lag;	/* Train objects are drawn in the past */

        if (route_now >= route->next_time && !route->state.frozen)
        {
            int doset=0;

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
                        route->state.collision = iscollision(route, COLLISION_TIMEOUT);	/* Re-check for collision */
                        break;
                    }
                }
                /* last and next were calculated when we originally hit this waypoint */
            }
            else if (route->state.dataref)
            {
                whenref_t *whenref = route->path[route->last_node].whenrefs;

                while (whenref)
                {
                    float val;
                    extref_t *extref = whenref->extref;

                    if (extref->type == xplmType_Mine)
                    {
                        val = userrefcallback(extref->ref);
                    }
                    else if (whenref->idx < 0)
                    {
                        /* Not an array */
                        if (extref->type & xplmType_Float)
                            val = XPLMGetDataf(extref->ref);
                        else if (extref->type & xplmType_Double)
                            val = XPLMGetDatad(extref->ref);
                        else if (extref->type & xplmType_Int)
                            val = XPLMGetDatai(extref->ref);
                        else
                            val = 0;	/* Lookup failed or otherwise unusable */
                    }
                    else if (extref->type & xplmType_FloatArray)
                    {
                        XPLMGetDatavf(extref->ref, &val, whenref->idx, 1);
                    }
                    else if (extref->type & xplmType_IntArray)
                    {
                        int ival;
                        XPLMGetDatavi(extref->ref, &ival, whenref->idx, 1);
                        val = ival;
                    }
                    else
                    {
                        val = 0;	/* Lookup failed or otherwise unusable */
                    }

                    if ((val >= whenref->from) && (val <= whenref->to))
                        whenref = whenref->next;
                    else
                        break;		/* fail */
                }

                if (!whenref)
                {
                    /* All passed */
                    route->state.dataref = 0;
                    route->state.collision = iscollision(route, COLLISION_TIMEOUT);	/* Re-check for collision */
                    /* last and next were calculated when we originally hit this waypoint */
                }
            }
            else if (route->state.paused)
            {
                route->state.paused = 0;
                route->state.collision = iscollision(route, COLLISION_TIMEOUT);	/* Re-check for collision */
                /* last and next were calculated when we originally hit this waypoint */
            }
            else if (route->state.collision)
            {
                route->state.collision = iscollision(route, route->deadlocked-1);	/* Break deadlock on timeout */
                /* last and next were calculated when we originally hit this waypoint */
            }
            else	/* next waypoint */
            {
                route->last_node = route->next_node;
                route->next_node += route->direction;
                if (!route->last_node)
                    route->last_distance = 0;	/* reset distance travelled to prevent growing stupidly large */
                else if (route->state.backingup)
                    route->last_distance -= route->next_distance;
                else
                    route->last_distance += route->next_distance;
                route->distance = route->last_distance;

                if (route->path[route->last_node].flags.reverse)
                {
                    route->direction = -1;
                    route->next_node = route->pathlen-2;
                }
                else if (route->next_node >= route->pathlen)
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
                last_node = route->path + route->last_node;
                next_node = route->path + route->next_node;

                /* Assume distances are too small to care about earth curvature so just calculate using OpenGL coords */
                route->next_heading = R2D(atan2f(next_node->p.x - last_node->p.x, last_node->p.z - next_node->p.z));
                route->next_distance = sqrtf((next_node->p.x - last_node->p.x) * (next_node->p.x - last_node->p.x) +
                                             (next_node->p.z - last_node->p.z) * (next_node->p.z - last_node->p.z));

                if (!route->parent)
                {
                    if (last_node->whenrefs)
                        route->state.dataref = 1;
                    if (last_node->attime[0] != INVALID_AT)
                        route->state.waiting = 1;
                    if (last_node->pausetime)
                        route->state.paused = 1;
                    if (last_node->flags.set1 || last_node->flags.set2)
                        doset = 1;
                    if (last_node->flags.backup)
                    {
                        if (last_node->pausetime)	/* A */
                        {
                            /* Backing up after pause */
                            route->state.backingup = 1;
                            route->state.forwardsa = 1;
                        }
                        else						/* Y */
                        {
                            /* Backing up before pause */
                            route->state.forwardsb = 1;
                        }
                    }
                    else
                    {
                        if (!route->state.forwardsa)			/* !Q */
                        {
                            route->state.backingup = 0;
                            route->state.forwardsb = 0;
                        }
                        if (!route->state.backingup && !route->state.forwardsb)	/* !B */
                        {
                            route->state.forwardsa = 0;
                        }
                    }
                    route->state.collision = iscollision(route, COLLISION_TIMEOUT);
                }
            }
            
            last_node = route->path + route->last_node;
            next_node = route->path + route->next_node;

            /* Maintain speed/progress unless there's been a large gap in draw callbacks because we were deactivated / disabled */
            if (route->last_time && route_now - route->next_time < RESET_TIME)
                route->last_time = route->next_time;
            else
            {
                route->last_time = now;			/* reset */
                route_now = route->last_time - route->object.lag;
            }

            if (route->state.waiting)
                route->next_time = route->last_time + AT_INTERVAL;
            else if (route->state.dataref)
                route->next_time = route->last_time + WHEN_INTERVAL;
            else if (route->state.paused)
                route->next_time = route->last_time + last_node->pausetime;
            else if (route->state.collision)
                route->next_time = route->last_time + COLLISION_INTERVAL;
            else if (route->state.forwardsa && !last_node->flags.backup)			/* B */
            {
                route->next_distance += route->speed * TURN_TIME;	/* Allow for extra turning distance */
                route->next_time = route->last_time + route->next_distance / route->speed;
            }
            else if (route->state.forwardsb && last_node->flags.backup)	/* Y */
            {
                route->last_time += TURN_TIME;	/* Allow for extra turning distance */
                route->next_time = route->last_time + route->next_distance / route->speed;
            }
            else
                route->next_time = route->last_time + route->next_distance / route->speed;

            /* Set DataRefs. Need to do this after calculating last_time so use hacky flag */
            if (doset)
            {
                userref_t *userref = last_node->userref;

                userref->duration = last_node->userduration;
                userref->slope = last_node->flags.slope;
                userref->curve = last_node->flags.curve;
                if (last_node->flags.set2)
                {
                    userref->start1 = route->last_time;
                    userref->start2 = route->last_time + last_node->pausetime - userref->duration;
                }
                else if (last_node->flags.set1)
                {
                    userref->start1 = route->last_time;
                    userref->start2 = 0;
                }
            }

            /* Force re-probe since we've changed direction */
            route->next_probe = route_now;
            route->next_y = last_node->p.y;

        } // (route_now >= route->next_time && !route->state.frozen)

        /* Parent controls state of children */
        if (route->parent)
        {
            if ((route->parent->last_time == now) || (route->path[route->pathlen-1].flags.reverse && (!route->parent->last_node || route->parent->last_node==route->pathlen-1)))
            {
                /* Parent was reset or at end of a reversible route - line up back in time from it */
                route->direction = route->parent->direction;
                route->last_node = route->parent->last_node;
                route->next_node = route->parent->next_node;
                route->last_distance = route->parent->last_distance;
                route->next_distance = route->parent->next_distance;
                route->distance = route->parent->distance - route->object.lag * route->speed;	/* Negative at first node */
                route->next_heading = route->parent->next_heading;
                route->last_time = route->parent->last_time;
                route->next_time = route->last_time + route->next_distance / route->speed;
                route->state.frozen = 0;
            }

            if (route->parent->state.paused||route->parent->state.waiting||route->parent->state.dataref||route->parent->state.collision)
            {
                /* Parent is paused */
                if (!route->state.frozen)
                {
                    route->freeze_time = route->parent->last_time;	/* Save time parent started pause */
                    route->state.frozen = 1;
                }
                route_now = route->freeze_time - route->object.lag;
            }
            else if (route->state.frozen && !(route->parent->state.paused||route->parent->state.waiting||route->parent->state.dataref||route->parent->state.collision))
            {
                /* Parent has just unpaused - maintain spacing */
                route->last_time += (route->parent->last_time - route->freeze_time);
                route->next_time += (route->parent->last_time - route->freeze_time);
                route->state.frozen = 0;
            }
        }

        /* Calculate drawing position */
        last_node = route->path + route->last_node;
        next_node = route->path + route->next_node;
        if (!(route->state.paused||route->state.waiting||route->state.dataref||route->state.collision))
        {
            if (route->state.backingup && route->state.forwardsa && !last_node->flags.backup && route_now-route->last_time >= TURN_TIME/2)	/* C */
            {
                /* Reached mirror of p3. Fixup things so we're backwards in time on otherwise normal path */
                route->state.backingup = 0;
                route->last_time += TURN_TIME;
                route->next_distance -= route->speed * TURN_TIME;
            }
            if (route->state.forwardsb && !route->state.backingup && route->last_time - route_now <= TURN_TIME/2)	/* Z */
            {
                /* Reached mirror of p1. */
                route->state.backingup = 1;
            }
            else if (!(route->state.forwardsb || route->state.forwardsa) && now < route->last_time)
            {
                /* We must be in replay, and we've gone back in time beyond the last decision. Just show at the last node. */
                route_now = route->last_time - route->object.lag;	/* Train objects are drawn in the past */
            }

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
            route->drawinfo->y = route->next_y + (route->last_y - route->next_y) * (route->next_probe - route_now) / PROBE_INTERVAL;
            if (!route->object.heading)
                route->drawinfo->pitch = R2D(sinf((route->next_y - route->last_y) / (PROBE_INTERVAL * route->speed)));
            else if (route->object.heading == 180)
                route->drawinfo->pitch = R2D(sinf((route->last_y - route->next_y) / (PROBE_INTERVAL * route->speed)));
            if (route->state.backingup)
                route->distance = route->last_distance - progress * route->next_distance;
            else
                route->distance = route->last_distance + progress * route->next_distance;
            route->steer = 0;
        }
        else
        {
            /* Paused: Fake up times for drawing code below */
            progress = - (route->object.lag * route->speed) / route->next_distance;
            route_now = route->last_time - route->object.lag;
            route->drawinfo->y = last_node->p.y;
            route->drawinfo->pitch = 0;	/* Since we're not probing */
        }

#ifdef DEBUG
        {
            /* Show markers - which are only visible if shadows turned off! */
            path_t *node = progress < 0.5f ? last_node : next_node;
            XPLMSetGraphicsState(0, 0, 0,   0, 0,   0, 0);
            glLineWidth(3);
            glColor3f(1,0,0);
            glBegin(GL_LINE_STRIP);
            glVertex3f(node->p1.x, node->p.y,    node->p1.z);
            glVertex3f(node->p1.x, node->p.y+10, node->p1.z);
            glEnd();
            glColor3f(0,1,0);
            glBegin(GL_LINE_STRIP);
            glVertex3f(node->p.x,  node->p.y,    node->p.z);
            glVertex3f(node->p.x,  node->p.y+10, node->p.z);
            glEnd();
            glColor3f(0,0,1);
            glBegin(GL_LINE_STRIP);
            glVertex3f(node->p3.x, node->p.y,    node->p3.z);
            glVertex3f(node->p3.x, node->p.y+10, node->p3.z);
            glEnd();
        }
#endif

        /* Finally do the drawing */
        if (route->state.backingup)
        {
            point_t pr;	/* Mirror of p1/p3 */

            if (progress >= 0.5f)
            {
                /* Approaching a waypoint while backing up */
                if (next_node->flags.backup || route->next_time - route_now >= TURN_TIME/2 || !(next_node->p1.x || next_node->p1.z))
                {
                    /* No bezier points, or not in range, or approaching backup node */
                    route->drawinfo->x = last_node->p.x + progress * (next_node->p.x - last_node->p.x);
                    route->drawinfo->z = last_node->p.z + progress * (next_node->p.z - last_node->p.z);
                    route->drawinfo->heading = route->next_heading;
                }
                else
                {
                    assert(route->state.forwardsa);
                    pr.x = next_node->p.x + next_node->p.x - next_node->p3.x;
                    pr.z = next_node->p.z + next_node->p.z - next_node->p3.z;
                    if (route->speed * 2 <= route->next_distance)
                        bez(route->drawinfo, &next_node->p1, &next_node->p, &pr, 0.5f + (route_now - route->next_time)/TURN_TIME);
                    else	/* Short edge */
                        bez(route->drawinfo, &next_node->p1, &next_node->p, &pr, progress - 0.5f);
                    route->steer = route->next_heading - route->drawinfo->heading;
                }
            }
            else if (route->state.forwardsb && (route_now - route->last_time < TURN_TIME/2) && (last_node->p1.x || last_node->p1.z))
            {
                /* Leaving mirrored p1 waypoint while backing up */
                pr.x = last_node->p.x + last_node->p.x - last_node->p1.x;
                pr.z = last_node->p.z + last_node->p.z - last_node->p1.z;
                if (progress <0 || route->speed * 2 <= route->next_distance)
                    bez(route->drawinfo, &pr, &last_node->p, &last_node->p3, 0.5f + (route_now - route->last_time)/TURN_TIME);
                else	/* Short edge */
                    bez(route->drawinfo, &pr, &last_node->p, &last_node->p3, progress + 0.5f);
                if (progress < 0)
                    route->steer = 180 - route->drawinfo->heading + R2D(atan2f(pr.x - last_node->p.x, last_node->p.z - pr.z));	/* Don't have a route->last_heading */
                else
                    route->steer = route->drawinfo->heading - route->next_heading;
            }
            else if (route->state.forwardsa && (route_now - route->last_time < TURN_TIME/2) && (last_node->p3.x || last_node->p3.z))
            {
                /* Leaving a waypoint while backing up */
                pr.x = last_node->p.x + last_node->p.x - last_node->p3.x;
                pr.z = last_node->p.z + last_node->p.z - last_node->p3.z;
                if (route->speed * 2 <= route->next_distance)
                    bez(route->drawinfo, &last_node->p1, &last_node->p, &pr, 0.5f + (route_now - route->last_time)/TURN_TIME);
                else	/* Short edge */
                    bez(route->drawinfo, &last_node->p1, &last_node->p, &pr, progress + 0.5f);
                route->steer = 180 - route->next_heading + route->drawinfo->heading;
            }
            else
            {
                route->drawinfo->x = last_node->p.x + progress * (next_node->p.x - last_node->p.x);
                route->drawinfo->z = last_node->p.z + progress * (next_node->p.z - last_node->p.z);
                route->drawinfo->heading = route->next_heading;
            }
            route->drawinfo->heading += route->object.heading - 180;
            route->drawinfo->pitch = -route->drawinfo->pitch;
        }
        else if (route->state.forwardsb && (last_node->p1.x || last_node->p1.z))
        {
            /* Backing up to pause, keep going to mirror of p1 */
            progress = 2 - (route->last_time - route_now) / (TURN_TIME/2);
            route->drawinfo->x = last_node->p.x + progress * (last_node->p.x - last_node->p1.x);
            route->drawinfo->z = last_node->p.z + progress * (last_node->p.z - last_node->p1.z);
            /* Keep last heading */
        }
        else if (progress >= 0.5f)
        {
            /* Approaching a waypoint */
            if (next_node->flags.backup || (route->next_time - route_now >= TURN_TIME/2) || !(next_node->p1.x || next_node->p1.z))
            {
                /* No bezier points, or not in range, or approaching backup node */
                route->drawinfo->x = last_node->p.x + progress * (next_node->p.x - last_node->p.x);
                route->drawinfo->z = last_node->p.z + progress * (next_node->p.z - last_node->p.z);
                route->drawinfo->heading = route->next_heading;
            }
            else if (route->direction > 0)
            {
                if (route->speed * 2 <= route->next_distance)
                    bez(route->drawinfo, &next_node->p1, &next_node->p, &next_node->p3, 0.5f + (route_now - route->next_time)/TURN_TIME);
                else	/* Short edge */
                    bez(route->drawinfo, &next_node->p1, &next_node->p, &next_node->p3, progress - 0.5f);
                route->steer = route->drawinfo->heading - route->next_heading;
            }
            else
            {
                if (route->speed * 2 <= route->next_distance)
                    bez(route->drawinfo, &next_node->p3, &next_node->p, &next_node->p1, 0.5f + (route_now - route->next_time)/TURN_TIME);
                else	/* Short edge */
                    bez(route->drawinfo, &next_node->p3, &next_node->p, &next_node->p1, progress - 0.5f);
                route->steer = route->drawinfo->heading - route->next_heading;
            }
        }
        else if (route->state.forwardsa && progress<0 && (last_node->p3.x || last_node->p3.z))
        {
            /* Leaving mirror of p3. Special handling to deal with short paths. */
            progress = (route->last_time - route_now) / (TURN_TIME/2);
            route->drawinfo->x = last_node->p.x + progress * (last_node->p.x - last_node->p3.x);
            route->drawinfo->z = last_node->p.z + progress * (last_node->p.z - last_node->p3.z);
            route->drawinfo->heading = route->next_heading;
        }
        else if (!route->state.forwardsa && (route_now - route->last_time < TURN_TIME/2) && (last_node->p3.x || last_node->p3.z))
        {
            /* Leaving a waypoint (may be from a negative direction if a paused child) */
            if (route->direction > 0)
            {
                if ((progress < 0) || (route->speed * 2 <= route->next_distance))
                    bez(route->drawinfo, &last_node->p1, &last_node->p, &last_node->p3, 0.5f + (route_now - route->last_time)/TURN_TIME);
                else	/* Short edge */
                    bez(route->drawinfo, &last_node->p1, &last_node->p, &last_node->p3, progress + 0.5f);
            }
            else
            {
                if ((progress < 0) || (route->speed * 2 <= route->next_distance))
                    bez(route->drawinfo, &last_node->p3, &last_node->p, &last_node->p1, 0.5f + (route_now - route->last_time)/TURN_TIME);
                else	/* Short edge */
                    bez(route->drawinfo, &last_node->p3, &last_node->p, &last_node->p1, progress + 0.5f);
            }
            route->steer = route->next_heading - route->drawinfo->heading;
        }
        else
        {
            route->drawinfo->x = last_node->p.x + progress * (next_node->p.x - last_node->p.x);
            route->drawinfo->z = last_node->p.z + progress * (next_node->p.z - last_node->p.z);
            route->drawinfo->heading = route->next_heading;
        }
        if (route->object.offset)
        {
            float h = D2R(route->drawinfo->heading);
            route->drawinfo->x += sinf(h) * route->object.offset;
            route->drawinfo->z -= cosf(h) * route->object.offset;
        }
        if (route->steer)
            route->steer = fmodf(route->steer + 540, 360) - 180;	/* to range -180..180 */
        route->drawinfo->heading += route->object.heading;
    }

    drawroutes();
    return 1;
}


static void bez(XPLMDrawInfo_t *drawinfo, point_t *p1, point_t *p2, point_t *p3, float mu)
{
    float mum1, mum12, mu2;
    float tx, tz;

    // assert (mu>=0 && mu<=1);	// Trains may go negative at start or in replay

    mu2 = mu * mu;
    mum1 = 1 - mu;
    mum12 = mum1 * mum1;
    drawinfo->x = p1->x * mum12 + 2 * p2->x * mum1 * mu + p3->x * mu2;
    drawinfo->z = p1->z * mum12 + 2 * p2->z * mum1 * mu + p3->z * mu2;

    tx = 2 * mum1 * (p2->x - p1->x) + 2 * mu * (p3->x - p2->x);
    tz =-2 * mum1 * (p2->z - p1->z) - 2 * mu * (p3->z - p2->z);
    drawinfo->heading = R2D(atan2f(tx, tz));
}
