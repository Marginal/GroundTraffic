/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 */

#include "groundtraffic.h"

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

/* 
 * Read our config file
 * Return: 0=config hasn't changed, !0=config has changed and airport->state is updated
 */
int readconfig(char *pkgpath, airport_t *airport)
{
    struct stat info;
    char buffer[PATH_MAX+128], line[PATH_MAX], *c;
    FILE *h;
    int lineno=0;
    route_t *lastroute=NULL, *currentroute=NULL;

#if LIN		/* Assume a case sensitive file system */
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
#else	/* Assume Mac and Windows using a case folding file system */
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
        int eol;
        lineno++;

        for (c=line+strlen(line)-1; isspace(*c); *(c--)='\0');	/* rtrim */
        if (lineno==1 && !strncmp(line, "\xef\xbb\xbf", 3)) c=line+3; else c=line;	/* skip UTF-8 BOM */
        while (isspace(*c)) { c++; }	/* ltrim */
        if (*c=='#')			/* Skip comment lines */
        {
        }
        else if (!*c)			/* Blank line = end of route */
        {
            if (currentroute && !currentroute->pathlen)
            {
                clearconfig(airport);
                sprintf(buffer, "Empty route at line %d", lineno);
                xplog(buffer);
                return 1;
            }
            currentroute=NULL;
        }
        else if (airport->state==noconfig)	/* Airport header */
        {
            if (sscanf(c, "%4c %lf%lf%n", airport->ICAO, &(airport->tower.lat), &(airport->tower.lon), &eol)!=3 || c[eol])
            {
                sprintf(buffer, "Expecting an airport \"ICAO lat lon\", found \"%s\" at line %d", line, lineno);
                xplog(buffer);
                return 1;
            }
            airport->ICAO[4]='\0';
            airport->state=inactive;
        }
        else if (currentroute)		/* Existing route */
        {
            path_t *path;
            if (!(path=realloc(currentroute->path, (1+currentroute->pathlen) * sizeof(path_t))))
            {
                clearconfig(airport);
                xplog("Out of memory!");
                return 1;
            }
            currentroute->path=path;

            /* Note: currentroute->pathlen hasn't been updated yet so points to the newly allocated path */
            sscanf(c, "%10s", buffer);
            if (!strcasecmp(buffer, "pause"))
            {
                int pausetime;
                c+=5;
                if (!currentroute->pathlen)
                {
                    clearconfig(airport);
                    sprintf(buffer, "Route can't start with a pause, at line %d", lineno);
                    xplog(buffer);
                    return 1;
                }
                else if (!sscanf(c, "%d%n", &pausetime, &eol) || c[eol])
                {
                    clearconfig(airport);
                    sprintf(buffer, "Expecting a pause time, found \"%s\" at line %d", c, lineno);
                    xplog(buffer);
                    return 1;
                }
                else if (pausetime <= 0 || pausetime >= 86400)
                {
                    clearconfig(airport);
                    sprintf(buffer, "Pause time should be between 1 and 86399 seconds at line %d", lineno);
                    xplog(buffer);
                    return 1;
                }
                path[currentroute->pathlen-1].pausetime = pausetime;
            }
            else if (!strcasecmp(buffer, "at"))
            {
                int hour, minute;
                c+=2;
                if (sscanf(c, "%d:%d", &hour, &minute)!=2 || hour<0 || hour>23 || minute<0 || minute>59)
                {
                    clearconfig(airport);
                    sprintf(buffer, "Expecting a time-of-day \"HH:MM\", found \"%s\" at line %d", c, lineno);
                    xplog(buffer);
                    return 1;
                }
                path[currentroute->pathlen-1].attime = hour*60+minute;
                path[currentroute->pathlen-1].atdays.bits = 0x7f;
            }
            else if (!strcasecmp(buffer, "reverse"))
            {
                if (!currentroute->pathlen)
                {
                    clearconfig(airport);
                    sprintf(buffer, "Empty route at line %d", lineno);
                    xplog(buffer);
                    return 1;
                }
                path[currentroute->pathlen-1].reverse=1;
                currentroute=NULL;	/* reverse terminates */
            }
            else	/* waypoint */
            {
                memset(path + currentroute->pathlen, 0, sizeof(path_t));
                path[currentroute->pathlen].waypoint.alt=INVALID_ALT;
                if (sscanf(c, "%lf%lf%n", &path[currentroute->pathlen].waypoint.lat, &path[currentroute->pathlen].waypoint.lon, &eol)<2 || c[eol])
                {
                    clearconfig(airport);
                    sprintf(buffer, "Expecting a waypoint \"lat lon\" or a command, found \"%s\" at line %d", c, lineno);
                    xplog(buffer);
                    return 1;
                }
                else
                {
                    path[currentroute->pathlen].attime=-1;
                }
                currentroute->pathlen++;
            }
        }
        else	/* New route */
        {
            route_t *newroute;
            if (!(newroute=malloc(sizeof(route_t))))
            {
                clearconfig(airport);
                xplog("Out of memory!");
                return 1;
            }
            else if (lastroute)
            {
                lastroute->next=newroute;
            }
            else
            {
                airport->routes=newroute;
            }

            /* Initialise the route */
            memset(newroute, 0, sizeof(route_t));
            newroute->direction = 1;
            newroute->drawinfo.structSize = sizeof(XPLMDrawInfo_t);
            newroute->drawinfo.pitch = newroute->drawinfo.roll = 0;

            if (sscanf(c, "%5s%f%255s%f", buffer, &(newroute->speed), newroute->objname, &(newroute->heading))<2 ||
                strcasecmp(buffer, "route"))
            {
                clearconfig(airport);
                sprintf(buffer, "Expecting a route \"ROUTE speed object [heading]\", found \"%s\" at line %d", line, lineno);
                xplog(buffer);
                return 1;
            }
            newroute->speed *= (1000.0 / (60*60));	/* convert km/h to m/s */
            currentroute=lastroute=newroute;
        }
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
