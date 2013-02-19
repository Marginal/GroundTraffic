/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 */

#include "groundtraffic.h"

#define N(c) (c?c:"")

static time_t mtime=-1;	/* control file modification time  */

void clearconfig(airport_t *airport)
{
    route_t *route=airport->routes;

    deactivate(airport);

    airport->ICAO[0]='\0';
    airport->tower.lat=airport->tower.lon=0;
    airport->tower.alt=INVALID_ALT;
    airport->state = noconfig;
    airport->routes=NULL;

    while (route)
    {
        route_t *next=route->next;
        free(route->path);
        free(route);
        route=next;
    }

    mtime=-1;		/* Don't cache */
}   

/* Convenience function */
static int failconfig(FILE *h, airport_t *airport, char *buffer, const char *format, ...)
{
    va_list ap;

    fclose(h);
    clearconfig(airport);
    va_start(ap, format);
    vsprintf(buffer, format, ap);
    va_end(ap);
    xplog(buffer);
    return 1;
}

/* 
 * Read our config file
 * Return: 0=config hasn't changed, !0=config has changed and airport->state is updated
 */
int readconfig(char *pkgpath, airport_t *airport)
{
    struct stat info;
    char buffer[PATH_MAX+128], line[PATH_MAX];
    FILE *h;
    int lineno=0;
    char sep[]=" \t\r\n";
    route_t *lastroute=NULL, *currentroute=NULL;

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
#if 0
    if (info.st_mtimespec.tv_sec==mtime) return 0;	/* file hasn't changed */
#else
    if (info.st_mtime==mtime) return 0;			/* file hasn't changed */
#endif
    clearconfig(airport);	/* Free old config */

    if (!(h=fopen(buffer, "r")))
    {
        sprintf(buffer, "Can't open %s/groundtraffic.txt", pkgpath);
        xplog(buffer);
        return 1;
    }
    while (fgets(line, PATH_MAX, h))
    {
        char *c1, *c2;
        int eol1, eol2;

        if (!lineno && !strncmp(line, "\xef\xbb\xbf", 3))	/* skip UTF-8 BOM */
            c1=strtok(line+3, sep);
        else
            c1=strtok(line, sep);
        lineno++;

        if (!c1)				/* Blank line = end of route */
        {
            if (currentroute && !currentroute->pathlen)
                return failconfig(h, airport, buffer, "Empty route at line %d", lineno);
            currentroute=NULL;
            continue;
        }
        else if (*c1=='#')			/* Skip comment lines */
        {
            continue;
        }
        else if (airport->state==noconfig)	/* Airport header */
        {
            if (strlen(c1)==4)
                strcpy(airport->ICAO, c1);
            else
                return failconfig(h, airport, buffer, "Expecting a 4 character airport ICAO code, found \"%s\" at line %d", c1, lineno);
            c1=strtok(NULL, sep);
            c2=strtok(NULL, sep);
            if (!c1 || !sscanf(c1, "%f%n", &airport->tower.lat, &eol1) || c1[eol1] ||
                !c2 || !sscanf(c2, "%f%n", &airport->tower.lon, &eol2) || c2[eol2])
                return failconfig(h, airport, buffer, "Expecting an airport \"lat lon\", found \"%s %s\" at line %d", N(c1), N(c2), lineno);

            airport->state=inactive;
        }
        else if (currentroute)			/* Existing route */
        {
            path_t *path;
            if (!(path=realloc(currentroute->path, (1+currentroute->pathlen) * sizeof(path_t))))
                return failconfig(h, airport, buffer, "Out of memory!");
            currentroute->path=path;

            /* Note: currentroute->pathlen hasn't been updated yet so points to the newly allocated path */
            if (!strcasecmp(c1, "pause"))
            {
                int pausetime;
                if (!currentroute->pathlen)
                    return failconfig(h, airport, buffer, "Route can't start with a pause, at line %d", lineno);

                c1=strtok(NULL, sep);
                if (!sscanf(c1, "%d%n", &pausetime, &eol1) || c1[eol1])
                    return failconfig(h, airport, buffer, "Expecting a pause time, found \"%s\" at line %d", c1, lineno);
                else if (pausetime <= 0 || pausetime >= 86400)
                    return failconfig(h, airport, buffer, "Pause time should be between 1 and 86399 seconds at line %d", lineno);

                path[currentroute->pathlen-1].pausetime = pausetime;
            }
            else if (!strcasecmp(c1, "at"))
            {
                int hour, minute, i=0;
                char daynames[7][10] = { "monday", "tuesday", "wednesday", "thursday", "friday", "saturday", "sunday" };
                int dayvals[7] = { DAY_MON, DAY_TUE, DAY_WED, DAY_THU, DAY_FRI, DAY_SAT, DAY_SUN };

                if (!currentroute->pathlen)
                    return failconfig(h, airport, buffer, "Route can't start with an \"at\", at line %d", lineno);

                while ((c1=strtok(NULL, sep)))
                {
                    if (!strcasecmp(c1, "on"))
                        break;
                    else if (i>=MAX_ATTIMES)
                        return failconfig(h, airport, buffer, "Exceeded %d times-of-day at line %d", MAX_ATTIMES, lineno);
                    else if (sscanf(c1, "%d:%d%n", &hour, &minute, &eol1)!=2 || c1[eol1] || hour<0 || hour>23 || minute<0 || minute>59)
                        return failconfig(h, airport, buffer, "Expecting a time-of-day \"HH:MM\" or \"on\", found \"%s\" at line %d", c1, lineno);
                    path[currentroute->pathlen-1].attime[i++] = hour*60+minute;
                }
                if (i<MAX_ATTIMES) path[currentroute->pathlen-1].attime[i] = INVALID_AT;	/* Terminate */

                while ((c1=strtok(NULL, sep)))
                {
                    for (i=0; i<7; i++)
                        if (!strncasecmp(c1, daynames[i], strlen(c1)))
                        {
                            path[currentroute->pathlen-1].atdays |= dayvals[i];
                            break;
                        }
                    if (i>=7)
                        return failconfig(h, airport, buffer, "Expecting a day name, found \"%s\" at line %d", c1, lineno);
                }
            }
            else if (!strcasecmp(c1, "reverse"))
            {
                if (!currentroute->pathlen)
                    return failconfig(h, airport, buffer, "Empty route at line %d", lineno);

                path[currentroute->pathlen-1].reverse=1;
                currentroute=NULL;		/* reverse terminates */
            }
            else				/* waypoint */
            {
                memset(path + currentroute->pathlen, 0, sizeof(path_t));
                path[currentroute->pathlen].waypoint.alt = INVALID_ALT;
                path[currentroute->pathlen].attime[0] = INVALID_AT;
                c2=strtok(NULL, sep);
                if (!c1 || !sscanf(c1, "%f%n", &path[currentroute->pathlen].waypoint.lat, &eol1) || c1[eol1] ||
                    !c2 || !sscanf(c2, "%f%n", &path[currentroute->pathlen].waypoint.lon, &eol2) || c2[eol2])
                    return failconfig(h, airport, buffer, "Expecting a waypoint \"lat lon\", found \"%s %s\" at line %d", N(c1), N(c2), lineno);

                currentroute->pathlen++;
            }
        }
        else if (!strcasecmp(c1, "route"))	/* New route */
        {
            route_t *newroute;
            if (!(newroute=malloc(sizeof(route_t))))
                return failconfig(h, airport, buffer, "Out of memory!");
            else if (lastroute)
                lastroute->next=newroute;
            else
                airport->routes=newroute;

            /* Initialise the route */
            memset(newroute, 0, sizeof(route_t));
            newroute->direction = 1;
            newroute->drawinfo.structSize = sizeof(XPLMDrawInfo_t);
            newroute->drawinfo.pitch = newroute->drawinfo.roll = 0;

            c1=strtok(NULL, sep);
            c2=strtok(NULL, sep);
            if (!c1 || !sscanf(c1, "%f%n", &newroute->speed, &eol1) || c1[eol1] ||
                !c2 || !sscanf(c2, "%255s%n", newroute->objname, &eol2) || c2[eol2])
                return failconfig(h, airport, buffer, "Expecting a route \"speed object [heading]\", found \"%s\" at line %d",  N(c1), N(c2), lineno);

            c1=strtok(NULL, sep);
            if (c1 && (!sscanf(c1, "%f%n", &newroute->heading, &eol1) || c1[eol1]))
                return failconfig(h, airport, buffer, "Expecting an object heading, found \"%s\" at line %d", c1, lineno);

            newroute->speed *= (1000.0 / (60*60));	/* convert km/h to m/s */
            currentroute=lastroute=newroute;
        }
        else
        {
            return failconfig(h, airport, buffer, "Expecting a route, found \"%s\" at line %d", c1, lineno);
        }

        if ((c1=strtok(NULL, sep)))
            return failconfig(h, airport, buffer, "Extraneous input \"%s\" at line %d", c1, lineno);
    }
    fclose(h);
    if (airport->state==noconfig)
    {
        xplog("Can't read groundtraffic.txt");
        return 1;
    }
#if 0
    mtime=info.st_mtimespec.tv_sec;
#else
    mtime=info.st_mtime;
#endif
    return 2;
}
