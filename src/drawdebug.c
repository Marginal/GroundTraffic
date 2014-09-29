/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2014
 *
 * Licensed under GNU LGPL v2.1.
 */

#include "groundtraffic.h"

#define CIRCLEDIV 72


/* Draw route paths in 3d drawing phase */
void drawdebug3d(int drawnodes, GLint view[4])
{
    GLdouble model[16], proj[16];
    int i;
    route_t *route;

    /* This is slow! */
    glGetDoublev(GL_MODELVIEW_MATRIX, model);
    glGetDoublev(GL_PROJECTION_MATRIX, proj);

    for(route=airport.routes; route; route=route->next)
        if (!route->parent)
        {
            GLdouble winX, winY, winZ;

            gluProject(route->drawinfo->x, route->drawinfo->y, route->drawinfo->z, model, proj, view, &winX, &winY, &winZ);
            if (winZ<=1 && winX>=0 && winX<(view[0]+view[2]) && winY>=0 && winY<(view[1]+view[3]))	/* on screen and not behind us */
            {
                route->drawX = winX;
                route->drawY = winY;
            }
            else
                route->drawX = route->drawY = 0;

            glColor3fv(&route->drawcolor.r);
            glBegin((route->highway || route->path[route->pathlen-1].flags.reverse) ? GL_LINE_STRIP : GL_LINE_LOOP);
            for (i=0; i<route->pathlen; i++)
            {
                path_t *node = route->path + i;

                glVertex3fv(&node->p.x);

                if (drawnodes)
                {
                    gluProject(node->p.x, node->p.y, node->p.z, model, proj, view, &winX, &winY, &winZ);
                    if (winZ<=1 && winX>=0 && winX<(view[0]+view[2]) && winY>=0 && winY<(view[1]+view[3]))	/* on screen and not behind us */
                    {
                        node->drawX = winX;
                        node->drawY = winY;
                    }
                    else
                        node->drawX = node->drawY = 0;
                }
                else
                    node->drawX = node->drawY = 0;
            }
            glEnd();
        }
}


/* Draw route labels in 2d drawing phase - uses screen co-ordinates calculated in 3d phase */
void drawdebug2d()
{
    float waycolor[] = { 1, 1, 1 }, routecolor[] = { 0.5f, 1, 1 };
    int i;
    route_t *route;

    for (route=airport.routes; route; route=route->next)
        if (!route->parent)
            for (i=0; i<route->pathlen; i++)
            {
                path_t *node = route->path + i;
                if (node->drawX && node->drawY)
                {
                    // XPLMDrawTranslucentDarkBox(node->drawX-font_width, node->drawY+font_semiheight-2, node->drawX+(strlen(labeltbl+5*i)-1)*font_width+1, node->drawY-font_semiheight-2);
                    XPLMDrawString(waycolor, node->drawX-font_width, node->drawY-font_semiheight, labeltbl+5*i, NULL, xplmFont_Basic);
                }
            }

    for (route=airport.routes; route; route=route->next)
        if (!route->parent)
            if (route->drawX && route->drawY)
            {
                char buf[32];
                int off;

                sprintf(buf, "%d", route->lineno);
                off = strlen(buf);

                /* Test state flags in same order as drawcallback */
                if (route->state.waiting)
                    sprintf(buf+off, " %d\xE2\x96\xAA" "At", route->last_node);	/* BLACK SMALL SQUARE */
                else if (route->state.dataref)
                    sprintf(buf+off, " %d\xE2\x96\xAA" "When", route->last_node);
                else if (route->state.paused)
                    sprintf(buf+off, " %d\xE2\x96\xAA" "Pause", route->last_node);
                else if (route->state.collision == (collision_t*) -1)
                    sprintf(buf+off, " %d\xE2\xA8\xAF" "aircraft", route->last_node);	/* VECTOR OR CROSS PRODUCT */
                else if (route->state.collision)
                    sprintf(buf+off, " %d\xE2\xA8\xAF" "%d", route->last_node, route->state.collision->route->lineno);
                else
                    sprintf(buf+off, " %d\xE2\x9E\xA1" "%d", route->last_node, route->next_node);	/* BLACK RIGHTWARDS ARROW */
                XPLMDrawTranslucentDarkBox(route->drawX-off*font_width, route->drawY+3*font_semiheight, route->drawX+(strlen(buf)-2-off)*font_width+1, route->drawY+font_semiheight);
                XPLMDrawString(routecolor, route->drawX-off*font_width, route->drawY+font_semiheight+2, buf, NULL, xplmFont_Basic);
                XPLMDrawString(&(route->drawcolor.r), route->drawX, route->drawY+font_semiheight+3, "\xE2\x96\xAE", NULL, xplmFont_Basic);	/* BLACK VERTICAL RECTANGLE */
            }
}


int drawmap3d(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon)
{
    int i;

    if (!airport.drawroutes || airport.state == noconfig || !intilerange(airport.tower) || airport.tower.alt == (double) INVALID_ALT)
        return 1;

    XPLMSetGraphicsState(0, 0, 0,   0, 1,   0, 0);
    glLineWidth(1);

    /* draw activation range */
    glColor3f(0, 0, 0);
    glBegin(GL_LINE_LOOP);
    for (i=0; i<CIRCLEDIV; i++)
    {
        float a = (M_PI * 2 / CIRCLEDIV) * i;
        glVertex3f((float) airport.p.x + sinf(a) * airport.active_distance, (float) airport.p.y, (float) airport.p.z + cosf(a) * airport.active_distance);
    }
    glEnd();

    glColor3f(0.25, 0.25, 0.25);
    glBegin(GL_LINE_LOOP);
    for (i=0; i<CIRCLEDIV; i++)
    {
        float a = (M_PI * 2 / CIRCLEDIV) * i;
        glVertex3f((float) airport.p.x + sinf(a) * (airport.active_distance+ACTIVE_HYSTERESIS), (float) airport.p.y, (float) airport.p.z + cosf(a) * (airport.active_distance+ACTIVE_HYSTERESIS));
    }
    glEnd();

    /* draw route paths */
    if (airport.state == active)
    {
        GLint view[4];
        glGetIntegerv(GL_VIEWPORT, view);
        drawdebug3d(0, view);	/* Don't draw node numbers - too slow */
    }

    return 1;
}


int drawmap2d(XPLMDrawingPhase inPhase, int inIsBefore, void *inRefcon)
{
    if (airport.drawroutes && airport.state == active)
        drawdebug2d();

    return 1;
}
