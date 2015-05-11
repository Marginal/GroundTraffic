/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 * Licensed under GNU LGPL v2.1.
 */

#include "groundtraffic.h"
#include "bbox.h"

#define N(c) (c?c:"<nothing>")

/* Globals */
static time_t mtime=-1;	/* control file modification time  */
static const char sep[]=" \t\r\n";

/* In this file */
static setcmd_t *readsetcmd(airport_t *airport, route_t *currentroute, path_t *node, char *buffer, int lineno);
static route_t *expandtrain(airport_t *airport, route_t *currentroute);

const glColor3f_t colors[16] = { { 0.0, 1.0, 0.0 }, // lime (match DRE color)
                                 { 1.0, 0.0, 0.0 }, // red
                                 { 1.0, 1.0, 0.0 }, // yellow
                                 { 0.0, 0.0, 1.0 }, // blue
                                 { 0.0, 1.0, 1.0 }, // aqua
                                 { 1.0, 0.0, 1.0 }, // fuchsia
                                 { 1.0,0.65, 1.0 }, // orange
                                 { 0.5, 0.5, 0.5 }, // gray
                                 { 0.5, 0.0, 0.0 }, // maroon
                                 { 0.5, 0.5, 0.0 }, // olive
                                 { 0.0, 0.5, 0.0 }, // green
                                 { 0.0, 0.5, 0.5 }, // teal
                                 { 0.0, 0.0, 0.5 }, // navy
                                 { 0.5, 0.0, 0.5 }, // purple
                                 {0.75,0.75,0.75 }, // silver
                                 { 0.0, 0.0, 0.0 }, // black
};

void clearconfig(airport_t *airport)
{
    route_t *route;
    train_t *train;
    userref_t *userref;
    extref_t *extref;

    deactivate(airport);

    airport->tower.lat=airport->tower.lon=0;
    airport->tower.alt = (double) INVALID_ALT;
    airport->state = noconfig;
    airport->done_first_activation = 0;
    airport->new_airport = -1;	/* Reloaded config causes synchronous load */
    airport->drawroutes = 0;
    airport->reflections = 0;
    airport->active_distance = ACTIVE_DISTANCE;

    route = airport->routes;
    while (route)
    {
        route_t *nextroute = route->next;

        if (!route->parent)	/* Paths and highways are shared with parent */
        {
            int i;
            for (i=0; i<route->pathlen; i++)
            {
                collision_t *collision = route->path[i].collisions;
                setcmd_t    *setcmd    = route->path[i].setcmds;
                whenref_t   *whenref   = route->path[i].whenrefs;

                while (collision)
                {
                    collision_t *next = collision->next;
                    free (collision);
                    collision = next;
                }
                while (setcmd)
                {
                    setcmd_t *next = setcmd->next;
                    free (setcmd);
                    setcmd = next;
                }
                while (whenref)
                {
                    whenref_t *next = whenref->next;
                    free (whenref);
                    whenref = next;
                }
            }
            free(route->path);
            free(route->varrefs);
            if (route->highway)
                for (i=0; i<MAX_HIGHWAY; free(route->highway->objects[i++].name));
            free(route->highway);
        }
        free(route->object.name);
        free(route->object.physical_name);
        free(route);
        route = nextroute;
    }
    airport->routes = airport->firstroute = NULL;

    train = airport->trains;
    while (train)
    {
        int i;
        train_t *next = train->next;
        free(train->name);
        for (i=0; i<MAX_TRAIN; free(train->objects[i++].name));
        free(train);
        train = next;
    }
    airport->trains = NULL;

    userref = airport->userrefs;
    while (userref)
    {
        userref_t *next = userref->next;
        if (userref->ref)
            XPLMUnregisterDataAccessor(userref->ref);
        free(userref->name);
        free(userref);
        userref = next;
    }
    airport->userrefs = NULL;

    extref = airport->extrefs;
    while (extref)
    {
        extref_t *next = extref->next;
        free(extref->name);
        free(extref);
        extref = next;
    }
    airport->extrefs = NULL;

    free(airport->drawinfo);
    airport->drawinfo = NULL;

    free(labeltbl);
    labeltbl = NULL;
    mtime=-1;		/* Don't cache */
}   

/* Convenience function */
static int failconfig(FILE *h, airport_t *airport, char *buffer, const char *format, ...)
{
    va_list ap;

    va_start(ap, format);
    vsprintf(buffer, format, ap);
    va_end(ap);
    xplog(buffer);
    clearconfig(airport);
    fclose(h);
    return 1;
}

/* 
 * Read our config file
 * Return: 0=config hasn't changed, !0=config has changed and airport->state is updated
 */
int readconfig(char *pkgpath, airport_t *airport)
{
    struct stat info;
    char buffer[MAX_NAME+128], line[MAX_NAME+64];
    FILE *h;
    int lineno=0, count=0, water=0, maxpathlen=0, doneprologue=0;
    route_t *currentroute=NULL;
    train_t *currenttrain=NULL;
    userref_t *userref;
    bbox_t bounds;
#ifdef DO_BENCHMARK
    struct timeval t1, t2;
    gettimeofday(&t1, NULL);		/* start */
#endif

#if APL || LIN		/* Might be a case sensitive file system */
    DIR *dir;
    struct dirent *ent;

    *buffer='\0';
    if (!(dir=opendir(pkgpath)))
    {
        clearconfig(airport);
        xplog("Can't find my scenery folder");
        return 1;
    }
    while ((ent=readdir(dir)))
        if (!strcasecmp(ent->d_name, "groundtraffic.txt"))
        {
            strcpy(buffer, pkgpath);
            strcat(buffer, "/");
            strcat(buffer, ent->d_name);
            break;
        }
    closedir(dir);
    if (!*buffer)
    {
        clearconfig(airport);
        sprintf(buffer, "Can't find groundtraffic.txt in %s", pkgpath);
        xplog(buffer);
        return 1;
    }
#else	/* Windows uses a case folding file system */
    strcpy(buffer, pkgpath);
    strcat(buffer, "/groundtraffic.txt");
#endif

    if (stat(buffer, &info))
    {
        clearconfig(airport);
        sprintf(buffer, "Can't find groundtraffic.txt in %s", pkgpath);
        xplog(buffer);
        return 1;
    }
    if (info.st_mtime==mtime) return 0;		/* File hasn't changed */
    clearconfig(airport);			/* File has changed - free old config */
    bbox_init(&bounds);

    if (!(h=fopen(buffer, "r")))
    {
        sprintf(buffer, "Can't open %s/groundtraffic.txt", pkgpath);
        xplog(buffer);
        return 1;
    }
    while (fgets(line, sizeof(line), h))
    {
        char *c1, *c2=NULL, *c3;
        int eol1, eol2, eol3;
        if (!lineno && !strncmp(line, "\xef\xbb\xbf", 3))	/* skip UTF-8 BOM */
            c1=strtok(line+3, sep);
        else
            c1=strtok(line, sep);
        lineno++;

        if (!c1)						/* Blank line = end of route or train */
        {
            if (currentroute && !currentroute->pathlen)
                return failconfig(h, airport, buffer, currentroute->highway ? "Empty highway at line %d" : "Empty route at line %d", lineno);
            currentroute = NULL;
            if (currenttrain && !currenttrain->objects[0].name)
                return failconfig(h, airport, buffer, "Empty train at line %d", lineno);
            currenttrain = NULL;
            continue;
        }
        else if (*c1=='#')					/* Skip comment lines */
        {
            continue;
        }
        else if (currentroute && currentroute->highway)		/* Existing highway */
        {
            highway_t *highway = currentroute->highway;
            int n;	/* Object count */
            char *c4;

            c2=strtok(NULL, sep);
            for (c3 = c2+strlen(c2)+1; isspace(*c3); c3++);			/* ltrim */
            for (c4 = c3+strlen(c3)-1; c4>=c3 && isspace(*c4); *(c4--) = '\0');	/* rtrim */
            if (!*c3)
            {
                /* Waypoint */
                if (!highway->objects[0].name)	/* Expect at least one car */
                    return failconfig(h, airport, buffer, "Expecting a car \"offset heading object\" at line %d", lineno);
                /* Fall through for waypoint */
            }
            else
            {
                /* Car */
                if (currentroute->pathlen)	/* Once we've had the first waypoint, we only expect waypoints */
                    return failconfig(h, airport, buffer, "Expecting a waypoint \"lat lon\" or a blank line at line %d", lineno);

                for (n=0; n<MAX_HIGHWAY && highway->objects[n].name; n++);
                if (n>=MAX_HIGHWAY)
                    return failconfig(h, airport, buffer, "Exceeded %d objects in a highway at line %d", MAX_HIGHWAY, lineno);
                else if (!c1 || !sscanf(c1, "%f%n", &highway->objects[n].offset, &eol1) || c1[eol1] ||
                         !c2 || !sscanf(c2, "%f%n", &highway->objects[n].offset, &eol2) || c2[eol2])
                    return failconfig(h, airport, buffer, "Expecting a car \"offset heading\", found \"%s %s\" at line %d", N(c1), N(c2), lineno);
                else if (*c3 == '.' || *c3 == '/' || *c3 == '\\')
                    return failconfig(h, airport, buffer, "Object name cannot start with a \"%c\" at line %d", *c3, lineno);
                else if (strlen(c3) >= MAX_NAME)
                    return failconfig(h, airport, buffer, "Object name exceeds %d characters at line %d", MAX_NAME-1, lineno);
                else if (!(highway->objects[n].name = strdup(c3)))
                    return failconfig(h, airport, buffer, "Out of memory!");
                continue;
            }
        }

        if (currentroute)			/* Existing route */
        {
            path_t *node = currentroute->pathlen ? currentroute->path + (currentroute->pathlen - 1) : NULL;

            if (!currentroute->highway && !strcasecmp(c1, "pause"))
            {
                int pausetime;
                if (!node)
                    return failconfig(h, airport, buffer, "Route can't start with a \"pause\" command at line %d", lineno);
                else if (currentroute->pathlen>1 && currentroute->path[currentroute->pathlen-2].flags.backup && currentroute->path[currentroute->pathlen-2].pausetime)
                    return failconfig(h, airport, buffer, "Can't pause both before and after a \"backup\" command at line %d", lineno);

                c1=strtok(NULL, sep);
                if (!c1 || !sscanf(c1, "%d%n", &pausetime, &eol1) || c1[eol1])
                    return failconfig(h, airport, buffer, "Expecting a pause time, found \"%s\" at line %d", N(c1), lineno);
                else if (pausetime <= 0 || pausetime >= 86400)
                    return failconfig(h, airport, buffer, "Pause time should be between 1 and 86399 seconds at line %d", lineno);
                node->pausetime += pausetime;	/* Multiple pauses stack */

                if ((c1=strtok(NULL, sep)))
                {
                    setcmd_t *setcmd;

                    if (strcasecmp(c1, "set"))
                        return failconfig(h, airport, buffer, "Expecting \"set\" or nothing, found \"%s\" at line %d", c1, lineno);
                    else if ((setcmd = readsetcmd(airport, currentroute, node, buffer, lineno)))
                        setcmd->flags.set2=1;
                    else
                    {
                        fclose(h);
                        clearconfig(airport);
                        xplog(buffer);
                        return 1;
                    }
                }
            }
            else if (!currentroute->highway && !strcasecmp(c1, "at"))
            {
                int hour, minute, i=0;
                char daynames[7][10] = { "sunday", "monday", "tuesday", "wednesday", "thursday", "friday", "saturday" };
                int dayvals[7] = { DAY_SUN, DAY_MON, DAY_TUE, DAY_WED, DAY_THU, DAY_FRI, DAY_SAT };

                if (!node)
                    return failconfig(h, airport, buffer, "Route can't start with an \"at\" command at line %d", lineno);
                else if (node->attime[0] != INVALID_AT)
                    return failconfig(h, airport, buffer, "Waypoint can't have more than one \"at\" command at line %d", lineno);
                while ((c1=strtok(NULL, sep)))
                {
                    if (!strcasecmp(c1, "on"))
                        break;
                    else if (i>=MAX_ATTIMES)
                        return failconfig(h, airport, buffer, "Exceeded %d times-of-day at line %d", MAX_ATTIMES, lineno);
                    else if (sscanf(c1, "%d:%d%n", &hour, &minute, &eol1)!=2 || c1[eol1] || hour<0 || hour>23 || minute<0 || minute>59)
                        return failconfig(h, airport, buffer, "Expecting a time-of-day \"HH:MM\" or \"on\", found \"%s\" at line %d", c1, lineno);
                    node->attime[i++] = hour*60+minute;
                }
                if (i<MAX_ATTIMES) node->attime[i] = INVALID_AT;	/* Terminate */

                while ((c1=strtok(NULL, sep)))
                {
                    for (i=0; i<7; i++)
                        if (!strncasecmp(c1, daynames[i], strlen(c1)))
                        {
                            node->atdays |= dayvals[i];
                            break;
                        }
                    if (i>=7)
                        return failconfig(h, airport, buffer, "Expecting a day name, found \"%s\" at line %d", c1, lineno);
                }
                if (!node->atdays) node->atdays = DAY_ALL;
            }
            else if (!currentroute->highway && (!strcasecmp(c1, "when") || !strcasecmp(c1, "and")))
            {
                whenref_t *whenref;
                extref_t *extref;

                if (!strcasecmp(c1, "when"))
                {
                    if (!node)
                        return failconfig(h, airport, buffer, "Route can't start with a \"when\" command at line %d", lineno);
                    else if (node->whenrefs)
                        return failconfig(h, airport, buffer, "Waypoint can't have more than one \"when\" command, consider using an \"and\" command at line %d", lineno);
                }
                else	// "and"
                {
                    if (!node)
                        return failconfig(h, airport, buffer, "Route can't start with an \"and\" command at line %d", lineno);
                    else if (!node->whenrefs)
                        return failconfig(h, airport, buffer, "Waypoint can't have an \"and\" command without a preceding \"when\" command at line %d", lineno);
                }

                if (!(whenref = calloc(1, sizeof(whenref_t))))
                    return failconfig(h, airport, buffer, "Out of memory!");
                whenref->next = node->whenrefs;
                node->whenrefs = whenref;

                if (!(c2=strtok(NULL, sep)))
                    return failconfig(h, airport, buffer, "Expecting a DataRef name at line %d", lineno);

                if (!strncasecmp(c2, "var[", 4) || !strncasecmp(c2, REF_BASE, sizeof(REF_BASE)-1))
                {
                    c3 = c1;
                    while ((*c3 = tolower(*c3))) c3++;
                    return failconfig(h, airport, buffer, "Can't use a per-route DataRef in a \"%s\" command at line %d", c1, lineno);
                }

                if ((c3 = strchr(c2, '[')))
                {
                    *(c3++) = '\0';	/* Strip index for lookup */
                    if (!sscanf(c3, "%d%n", &whenref->idx, &eol3) || eol3!=strlen(c3)-1 || c3[eol3]!=']')
                        return failconfig(h, airport, buffer, "Expecting a DataRef index \"[n]\", found \"[%s\" at line %d", N(c3), lineno);
                    else if (whenref->idx < 0)
                        return failconfig(h, airport, buffer, "DataRef index cannot be negative at line %d", lineno);
                }
                else
                    whenref->idx = -1;

                for (extref = airport->extrefs; extref && strcmp(c2, extref->name); extref=extref->next);
                if (!extref)
                {
                    /* new */
                    if (!(extref = calloc(1, sizeof(extref_t))) || !(extref->name = strdup(c2)))
                        return failconfig(h, airport, buffer, "Out of memory!");
                    /* Defer lookup to activation, after other plugins have Enabled */
                    extref->next = airport->extrefs;
                    airport->extrefs = extref;
                }
                node->whenrefs->extref = extref;

                c1=strtok(NULL, sep);
                c2=strtok(NULL, sep);
                if (!c1 || !sscanf(c1, "%f%n", &whenref->from, &eol1) || c1[eol1] ||
                    !c2 || !sscanf(c2, "%f%n", &whenref->to,   &eol2) || c2[eol2])
                    return failconfig(h, airport, buffer, "Expecting a range \"from to\", found \"%s %s\" at line %d", N(c1), N(c2), lineno);
                if (whenref->from > whenref->to)
                {
                    float foo = whenref->from;
                    whenref->from = whenref->to;
                    whenref->to = foo;
                }
            }
            else if (!currentroute->highway && !strcasecmp(c1, "backup"))
            {
                if (!node)
                    return failconfig(h, airport, buffer, "Route can't start with a \"backup\" command at line %d", lineno);
                else if (currentroute->pathlen>1 && currentroute->path[currentroute->pathlen-2].flags.backup)
                    return failconfig(h, airport, buffer, "Can't backup from two waypoints in sequence at line %d", lineno);

                node->flags.backup=1;
            }
            else if (!currentroute->highway && !strcasecmp(c1, "reverse"))
            {
                int i;
                if (!node)
                    return failconfig(h, airport, buffer, "Empty route at line %d", lineno);
                for (i=0; i<currentroute->pathlen; i++)
                    if (currentroute->path[i].flags.backup)
                        return failconfig(h, airport, buffer, "Can't use \"backup\" and \"reverse\" in the same route at line %d", lineno);
                node->flags.reverse=1;
                currentroute=NULL;		/* reverse terminates */
            }
            else if (!currentroute->highway && !strcasecmp(c1, "set"))
            {
                setcmd_t *setcmd;

                if (!node)
                    return failconfig(h, airport, buffer, "Route can't start with a \"set\" command at line %d", lineno);
                else if ((setcmd = readsetcmd(airport, currentroute, node, buffer, lineno)))
                    setcmd->flags.set1=1;
                else
                {
                    fclose(h);
                    clearconfig(airport);
                    xplog(buffer);
                    return 1;
                }
            }
            else				/* waypoint */
            {
                path_t *path, *last;
                float slat, slon, aa;

                if (!(path = realloc(currentroute->path, (1+currentroute->pathlen) * sizeof(path_t))))
                    return failconfig(h, airport, buffer, "Out of memory!");
                currentroute->path = path;

                node = path + currentroute->pathlen;
                last = node - 1;
                memset(node, 0, sizeof(path_t));
                node->attime[0] = INVALID_AT;
                if (!currentroute->highway) c2=strtok(NULL, sep);	/* done above for highways */
                if (!c1 || !sscanf(c1, "%f%n", &node->waypoint.lat, &eol1) || c1[eol1] ||
                    !c2 || !sscanf(c2, "%f%n", &node->waypoint.lon, &eol2) || c2[eol2])
                    return failconfig(h, airport, buffer, currentroute->pathlen ? (currentroute->highway ? "Expecting a waypoint \"lat lon\" or a blank line, found \"%s %s\" at line %d" : "Expecting a waypoint \"lat lon\", a command or a blank line, found \"%s %s\" at line %d") : "Expecting a waypoint \"lat lon\", found \"%s %s\" at line %d", N(c1), N(c2), lineno);
                else if (currentroute->pathlen && node->waypoint.lat==last->waypoint.lat && node->waypoint.lon==last->waypoint.lon)
                {
                    /* Duplicate nodes screw up cornering and collision avoidance, but KJFK contains loads so we will just skip them for now */
                    // return failconfig(h, airport, buffer, "Duplicate waypoint at line %d", lineno);
                    sprintf(buffer, "Note: Ignoring duplicate waypoint at line %d", lineno);
                    xplog(buffer);
                    continue;
                }
                bbox_add(&currentroute->bbox, node->waypoint.lat, node->waypoint.lon);

                /* determine activation radius using Haversine formula. http://mathforum.org/library/drmath/view/51879.html */
                bbox_add(&bounds, node->waypoint.lat, node->waypoint.lon);
                airport->tower.lat = (bounds.minlat + bounds.maxlat) / 2;
                airport->tower.lon = (bounds.minlon + bounds.maxlon) / 2;
                slat = sinf((bounds.maxlat-bounds.minlat) * (float) (M_PI/360));
                slon = sinf((bounds.maxlon-bounds.minlon) * (float) (M_PI/360));
                aa = slat*slat + cosf(bounds.minlat * (float) (M_PI/180)) * cosf(bounds.maxlat * (float) (M_PI/180)) * slon*slon;
                if ((airport->active_distance = RADIUS * atan2f(sqrtf(aa), sqrtf(1-aa))) > MAX_RADIUS)
                    return failconfig(h, airport, buffer, "Waypoint too far away at line %d", lineno);

                if (++(currentroute->pathlen) > maxpathlen) maxpathlen = currentroute->pathlen;
            }
            if ((c1=strtok(NULL, sep)))
                return failconfig(h, airport, buffer, "Extraneous input \"%s\" at line %d", c1, lineno);
        }

        else if (currenttrain)			/* Existing train */
        {
            int n;	/* Train length */

            for (n=0; n<MAX_TRAIN && currenttrain->objects[n].name; n++);
            if (n>=MAX_TRAIN)
                return failconfig(h, airport, buffer, "Exceeded %d objects in a train at line %d", MAX_TRAIN, lineno);

            c2=strtok(NULL, sep);
            c3=strtok(NULL, sep);
            if (!c1 || !sscanf(c1, "%f%n", &currenttrain->objects[n].lag, &eol1) || c1[eol1] ||
                !c2 || !sscanf(c2, "%f%n", &currenttrain->objects[n].offset, &eol2) || c2[eol2] ||
                !c3 || !sscanf(c3, "%f%n", &currenttrain->objects[n].heading, &eol3) || c3[eol3])
                return failconfig(h, airport, buffer, n ? "Expecting a car \"lag offset heading\" or a blank line, found \"%s %s %s\" at line %d" : "Expecting a car \"lag offset heading\", found \"%s %s %s\" at line %d", N(c1), N(c2), N(c3), lineno);
            else if (!n && currenttrain->objects[n].lag < 0)
                return failconfig(h, airport, buffer, "Train car lag must be greater or equal to 0 at line %d", lineno);
            else if (n && currenttrain->objects[n].lag < currenttrain->objects[n-1].lag)
                return failconfig(h, airport, buffer, "Train car lag must be greater than previous car's lag at line %d", lineno);

            for (c1 = c3+strlen(c3)+1; isspace(*c1); c1++);			/* ltrim */
            for (c2 = c1+strlen(c1)-1; c2>=c1 && isspace(*c2); *(c2--) = '\0');	/* rtrim */
            if (!*c1)
                return failconfig(h, airport, buffer, "Expecting an object name at line %d", lineno);
            else if (*c1 == '.' || *c1 == '/' || *c1 == '\\')
                return failconfig(h, airport, buffer, "Object name cannot start with a \"%c\" at line %d", *c1, lineno);
            else if (strlen(c1) >= MAX_NAME)
                return failconfig(h, airport, buffer, "Object name exceeds %d characters at line %d", MAX_NAME-1, lineno);
            else if (!(currenttrain->objects[n].name = strdup(c1)))
                return failconfig(h, airport, buffer, "Out of memory!");
        }

        else if (!strcasecmp(c1, "route"))	/* New route */
        {
            if (!(currentroute = calloc(1, sizeof(route_t))) || !(currentroute->varrefs = calloc(MAX_VAR, sizeof(userref_t))))
                return failconfig(h, airport, buffer, "Out of memory!");

            currentroute->next = airport->routes;
            airport->routes = currentroute;
            if (!airport->firstroute) airport->firstroute = currentroute;	/* Save for DRE */

            /* Initialise the route */
            currentroute->lineno = lineno;
            bbox_init(&currentroute->bbox);
            currentroute->direction = 1;
            if (count<16)
            {
                currentroute->drawcolor = colors[count++];
            }
            else
            {
                int r = rand();	/* Use the lower 15bits, which is all you get on Windows */
                currentroute->drawcolor.r = ((float) (0x001 + (r & 0x001F))) / 0x0020;
                currentroute->drawcolor.g = ((float) (0x020 + (r & 0x03E0))) / 0x0400;
                currentroute->drawcolor.b = ((float) (0x400 + (r & 0x7C00))) / 0x8000;
            }

            c1=strtok(NULL, sep);
            c2=strtok(NULL, sep);
            c3=strtok(NULL, sep);
            if (!c1 || !sscanf(c1, "%f%n", &currentroute->speed, &eol1) || c1[eol1] ||
                !c2 || !sscanf(c2, "%f%n", &currentroute->object.offset, &eol2) || c2[eol2] ||
                !c3 || !sscanf(c3, "%f%n", &currentroute->object.heading, &eol3) || c3[eol3])
                return failconfig(h, airport, buffer, "Expecting a route \"speed offset heading\", found \"%s %s %s\" at line %d",  N(c1), N(c2), N(c3), lineno);
            else if (currentroute->speed <= 0)
                return failconfig(h, airport, buffer, "Route speed must be greater than 0 at line %d", lineno);

            for (c1 = c3+strlen(c3)+1; isspace(*c1); c1++);			/* ltrim */
            for (c2 = c1+strlen(c1)-1; c2>=c1 && isspace(*c2); *(c2--) = '\0');	/* rtrim */
            if (!*c1)
                return failconfig(h, airport, buffer, "Expecting an object name at line %d", lineno);
            else if (*c1 == '.' || *c1 == '/' || *c1 == '\\')
                return failconfig(h, airport, buffer, "Object name cannot start with a \"%c\" at line %d", *c1, lineno);
            else if (strlen(c1) >= MAX_NAME)
                return failconfig(h, airport, buffer, "Object name exceeds %d characters at line %d", MAX_NAME-1, lineno);
            else if (!(currentroute->object.name = strdup(c1)))
                return failconfig(h, airport, buffer, "Out of memory!");

            currentroute->speed *= (float) (1000.0 / (60*60));	/* convert km/h to m/s */
        }
        else if (!strcasecmp(c1, "train"))	/* New train */
        {
            for (c1 = c1+strlen(c1)+1; isspace(*c1); c1++);			/* ltrim */
            for (c2 = c1+strlen(c1)-1; c2>=c1 && isspace(*c2); *(c2--) = '\0');	/* rtrim */
            if (!*c1)
                return failconfig(h, airport, buffer, "Expecting a train name at line %d", lineno);
            else if (*c1 == '.' || *c1 == '/' || *c1 == '\\')
                return failconfig(h, airport, buffer, "Train name cannot start with a \"%c\" at line %d", *c1, lineno);
            else if (strlen(c1) >= MAX_NAME)
                return failconfig(h, airport, buffer, "Train name exceeds %d characters at line %d", MAX_NAME-1, lineno);
            for (currenttrain=airport->trains; currenttrain; currenttrain=currenttrain->next)
                if (!strcmp(currenttrain->name, c1))
                    return failconfig(h, airport, buffer, "Can't re-define train \"%s\" at line %d", c1, lineno);

            if (!(currenttrain = calloc(1, sizeof(train_t))) || !(currenttrain->name = strdup(c1)))
                return failconfig(h, airport, buffer, "Out of memory!");

            currenttrain->next = airport->trains;
            airport->trains = currenttrain;
        }
        else if (!strcasecmp(c1, "highway"))	/* New highway */
        {
            highway_t *highway;

            if (!(currentroute = calloc(1, sizeof(route_t))) || !(highway = calloc(1, sizeof(highway_t))))
                return failconfig(h, airport, buffer, "Out of memory!");

            c1=strtok(NULL, sep);
            c2=strtok(NULL, sep);
            if (!c1 || !sscanf(c1, "%f%n", &currentroute->speed, &eol1) || c1[eol1] ||
                !c2 || !sscanf(c2, "%f%n", &highway->spacing, &eol2) || c2[eol2])
                return failconfig(h, airport, buffer, "Expecting a highway \"speed spacing\", found \"%s %s\" at line %d", N(c1), N(c2), lineno);
            else if (currentroute->speed <= 0)
                return failconfig(h, airport, buffer, "Highway speed must be greater than 0 at line %d", lineno);
            else if (highway->spacing <= 0)
                return failconfig(h, airport, buffer, "Highway spacing must be greater than 0 at line %d", lineno);
            if ((c3=strtok(NULL, sep))) return failconfig(h, airport, buffer, "Extraneous input \"%s\" at line %d", c3, lineno);

            currentroute->next = airport->routes;
            airport->routes = currentroute;
            if (!airport->firstroute) airport->firstroute = currentroute;	/* Save for DRE */

            /* Initialise the route */
            currentroute->lineno = lineno;
            bbox_init(&currentroute->bbox);
            currentroute->direction = 1;
            if (count<16)
            {
                currentroute->drawcolor = colors[count++];
            }
            else
            {
                int r = rand();	/* Use the lower 15bits, which is all you get on Windows */
                currentroute->drawcolor.r = ((float) (0x001 + (r & 0x001F))) / 0x0020;
                currentroute->drawcolor.g = ((float) (0x020 + (r & 0x03E0))) / 0x0400;
                currentroute->drawcolor.b = ((float) (0x400 + (r & 0x7C00))) / 0x8000;
            }
            currentroute->speed *= (float) (1000.0 / (60*60));	/* convert km/h to m/s */
            currentroute->highway = highway;
        }
        else if (!strcasecmp(c1, "water"))
        {
            airport->reflections = -1;
            water = -1;
            if ((c1=strtok(NULL, sep))) return failconfig(h, airport, buffer, "Extraneous input \"%s\" at line %d", c1, lineno);
        }
        else if (!strcasecmp(c1, "debug"))
        {
            airport->drawroutes = -1;
            if ((c1=strtok(NULL, sep))) return failconfig(h, airport, buffer, "Extraneous input \"%s\" at line %d", c1, lineno);
        }
        else if (!doneprologue)	/* Used to be airport header ICAO lat lon */
        {
            /* Silently skip input if in valid old format */
            c2=strtok(NULL, sep);
            c3=strtok(NULL, sep);
            if (strlen(c1)!=4 ||
                !c2 || !sscanf(c2, "%lf%n", &airport->tower.lat, &eol2) || c2[eol2] ||
                !c3 || !sscanf(c3, "%lf%n", &airport->tower.lon, &eol3) || c3[eol3] ||
                strtok(NULL, sep))
                return failconfig(h, airport, buffer, "Expecting a route or train, found \"%s\" at line %d", c1, lineno);
        }
        else
        {
            return failconfig(h, airport, buffer, "Expecting a route or train, found \"%s\" at line %d", c1, lineno);
        }
        doneprologue = -1;
    }

    /* Turn train routes into multiple individual routes */
    currentroute = airport->routes;
    while (currentroute)
    {
        if (!(currentroute = expandtrain(airport, currentroute)))
            return failconfig(h, airport, buffer, "Out of memory!");
        currentroute = currentroute->next;
    }

    /* Register user's DataRefs.
     * Have to do this early rather than during activate() because objects in DSF are loaded while we're still inactive */
    userref = airport->userrefs;
    while (userref)
    {
        if (XPLMFindDataRef(userref->name))
            return failconfig(h, airport, buffer, "Another plugin has already registered custom DataRef \"%s\"", userref->name);
        userref->ref = XPLMRegisterDataAccessor(userref->name, xplmType_Float, 0,
                                                NULL, NULL, userrefcallback, NULL, NULL, NULL,
                                                NULL, NULL, NULL, NULL, NULL, NULL, userref, NULL);
        userref = userref->next;
    }

    if (!airport->routes)
        return failconfig(h, airport, buffer, "No routes defined!");

    /* Finishing up */
#ifdef DEBUG
    sprintf(buffer, "Tower=%.9lf,%.9lf r=%d", airport->tower.lat, airport->tower.lon, (int) airport->active_distance);
    xplog(buffer);
#endif
    airport->state = inactive;
    airport->active_distance += (water ? ACTIVE_WATER : ACTIVE_DISTANCE);

    if (airport->drawroutes)
    {
        /* build route label lookup table for speed */
        int i;

        if (!(labeltbl = malloc(maxpathlen*5)))
            return failconfig(h, airport, buffer, "Out of memory!");
        for (i=0; i<maxpathlen; i++)
            sprintf(labeltbl+5*i, "%d", i);
    }

    fclose(h);
    mtime=info.st_mtime;
#ifdef DO_BENCHMARK
    gettimeofday(&t2, NULL);		/* stop */
    sprintf(buffer, "%d us in readconfig", (int) ((t2.tv_sec-t1.tv_sec) * 1000000 + t2.tv_usec - t1.tv_usec));
    xplog(buffer);
#endif
    return 2;
}

/* Read standalone or pause "set" command. Returns NULL on failure, and leaves error message in buffer. */
static setcmd_t *readsetcmd(airport_t *airport, route_t *currentroute, path_t *node, char *buffer, int lineno)
{
    setcmd_t *setcmd;
    userref_t *userref;
    char *c1;
    int eol1;

    if (!(c1=strtok(NULL, sep)))
    {
        sprintf(buffer, "Expecting a DataRef name at line %d", lineno);
        return 0;
    }
    else if (strlen(c1) >= MAX_NAME)
    {
        sprintf(buffer, "DataRef name exceeds %d characters at line %d", MAX_NAME-1, lineno);
        return 0;
    }

    if ((!strncasecmp(c1, "var[", 4) || !strncasecmp(c1, REF_VAR "[", sizeof(REF_VAR "[")-1)) && c1[strlen(c1)-1]==']')
    {
        /* Standard DataRef = route-specific */
        int i;
        c1 = strchr(c1, '[') + 1;
        if (!sscanf(c1, "%d%n", &i, &eol1) || eol1!=strlen(c1)-1)
        {
            sprintf(buffer, "Expecting DataRef name \"var[n]\", found \"%s\" at line %d", N(c1), lineno);
            return 0;
        }
        else if (i<0 || i>=MAX_VAR)
        {
            sprintf(buffer, "var DataRef index outside the range 0 to %d at line %d", MAX_VAR-1, lineno);
            return 0;
        }
        userref = *currentroute->varrefs + i;
    }
    else
    {
        /* User DataRef = global */
        for(userref = airport->userrefs; userref && strcmp(c1, userref->name); userref=userref->next);
        if (!userref)
        {
            /* new */
            if (!strncasecmp(c1, "sim/", 4))
            {
                sprintf(buffer, "Custom DataRef name can't start with \"sim/\" at line %d", lineno);
                return 0;
            }
            else if (!strncasecmp(c1, "marginal/", 9))
            {
                sprintf(buffer, "Custom DataRef name can't start with \"marginal/\", invent your own name! at line %d", lineno);
                return 0;
            }
            else if (!(userref = calloc(1, sizeof(userref_t))) || !(userref->name = malloc(strlen(c1)+1)))
            {
                strcpy(buffer, "Out of memory!");
                return 0;
            }
            strcpy(userref->name, c1);
            userref->next = airport->userrefs;
            airport->userrefs = userref;
        }
    }

    if (!(setcmd = calloc(1, sizeof(setcmd_t))))
    {
        strcpy(buffer, "Out of memory!");
        return 0;
    }
    setcmd->next = node->setcmds;
    node->setcmds = setcmd;

    setcmd->userref = userref;
    c1=strtok(NULL, sep);
    if (c1 && !strcasecmp(c1, "rise"))
        setcmd->flags.slope = rising;
    else if (c1 && !strcasecmp(c1, "fall"))
        setcmd->flags.slope = falling;
    else
    {
        sprintf(buffer, "Expecting a slope \"rise\" or \"fall\", found \"%s\" at line %d", N(c1), lineno);
        return 0;
    }

    c1=strtok(NULL, sep);
    if (c1 && !strcasecmp(c1, "linear"))
        setcmd->flags.curve = linear;
    else if (c1 && !strcasecmp(c1, "sine"))
        setcmd->flags.curve = sine;
    else
    {
        sprintf(buffer, "Expecting a curve \"linear\" or \"sine\", found \"%s\" at line %d", N(c1), lineno);
        return 0;
    }

    c1=strtok(NULL, sep);
    if (!c1 || !sscanf(c1, "%f%n", &setcmd->duration, &eol1) || c1[eol1])
    {
        sprintf(buffer, "Expecting a duration, found \"%s\" at line %d", N(c1), lineno);
        return 0;
    }

    return setcmd;
}


/* Check if this route names a train; if so replicate into multiple routes, and return pointer to last */
static route_t *expandtrain(airport_t *airport, route_t *currentroute)
{
    int i;
    train_t *train = airport->trains;
    route_t *route = currentroute;

    assert (currentroute);
    if (!currentroute) return NULL;
    if (currentroute->highway) return currentroute;	/* Highways don't have an object name */

    while (train)
    {
        if (!strcmp(currentroute->object.name, train->name)) break;
        train = train->next;
    }
    if (!train) return currentroute;

    /* It's a train */
    free(route->object.name);
    for (i=0; i<MAX_TRAIN; i++)
    {
        if (!train->objects[i].name) break;
        if (i)
        {
            /* Duplicate original route */
            route_t *newroute;
            if (!(newroute=malloc(sizeof(route_t)))) return NULL; /* OOM */
            memcpy(newroute, currentroute, sizeof(route_t));
            newroute->next = route->next;
            route->next = newroute;
            route = newroute;
            route->parent = currentroute;
        }
        /* Assign carriage to its route */
        if (!(route->object.name = strdup(train->objects[i].name))) return NULL;	/* OOM */
        route->object.lag = train->objects[i].lag / route->speed;	/* Convert distance to time lag */
        route->object.offset = train->objects[i].offset;
        route->object.heading = train->objects[i].heading;
        route->next_time = -route->object.lag;				/* Force recalc on first draw */
    }

    return route;
}
