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
static const char sep[]=" \t\r\n";
static int plane_count = 0;
static plane_ref_t plane_refs[MAX_PLANES] = {0};
static plane_acf_t plane_info[MAX_PLANES] = {0};

/* In this file */
static void read_v10_plane(FILE *h, int platform, plane_acf_t *info);
static void read_old_plane(FILE *h, int platform, plane_acf_t *info);

int setup_plane_refs()
{
    int i;
    plane_ref_t *plane_ref;
    char name[64], *c;

    /* User's aircraft */
    c = name + sprintf(name, "sim/flightmodel/position/");
    plane_ref = plane_refs + 0;

    strcpy(c, "local_x");     if (!(plane_ref->x   = XPLMFindDataRef(name))) return 0;
    strcpy(c, "local_y");     if (!(plane_ref->y   = XPLMFindDataRef(name))) return 0;
    strcpy(c, "local_z");     if (!(plane_ref->z   = XPLMFindDataRef(name))) return 0;
    strcpy(c, "local_vx");    if (!(plane_ref->vx  = XPLMFindDataRef(name))) return 0;
    strcpy(c, "local_vz");    if (!(plane_ref->vz  = XPLMFindDataRef(name))) return 0;
    strcpy(c, "psi");         if (!(plane_ref->hdg = XPLMFindDataRef(name))) return 0;
    if (!(plane_ref->gear= XPLMFindDataRef("sim/aircraft/parts/acf_gear_deploy"))) return 0;

    /* AI aircraft */
    for (i=1; i<MAX_PLANES; i++)
    {
        c = name + sprintf(name, "sim/multiplayer/position/plane%d_", i);
        plane_ref = plane_refs + i;

        strcpy(c, "x");           if (!(plane_ref->x   = XPLMFindDataRef(name))) return 0;
        strcpy(c, "y");           if (!(plane_ref->y   = XPLMFindDataRef(name))) return 0;
        strcpy(c, "z");           if (!(plane_ref->z   = XPLMFindDataRef(name))) return 0;
        strcpy(c, "v_x");         if (!(plane_ref->vx  = XPLMFindDataRef(name))) return 0;
        strcpy(c, "v_z");         if (!(plane_ref->vz  = XPLMFindDataRef(name))) return 0;
        strcpy(c, "psi");         if (!(plane_ref->hdg = XPLMFindDataRef(name))) return 0;
        strcpy(c, "gear_deploy"); if (!(plane_ref->gear= XPLMFindDataRef(name))) return 0;
    }

    return -1;
}

void reset_planes()
{
    plane_count = 0;
}

int count_planes()
{
    int i;
    XPLMPluginID controller;

    if (plane_count) return plane_count;

    XPLMCountAircraft(&plane_count, &i, &controller);	/* Use total, cos active may increase later */
    assert (plane_count > 0 && plane_count <= MAX_PLANES);
    if (plane_count > MAX_PLANES) plane_count = MAX_PLANES;

    for (i=0; i<plane_count; i++)
    {
        plane_acf_t *info = plane_info + i;
        int o;
        FILE *h;
        char path[MAX_ACF_PATH];

        /* If we've already analysed this ACF then re-use existing data */
        XPLMGetNthAircraftModel(i, info->name, path);
        for (o=0; o<i; o++)
            if (!strcmp(info->name, plane_info[o].name))
            {
                memcpy(info, plane_info+o, sizeof(plane_acf_t));
                break;
            }
        if (o<i) continue;

        /* Default values in case read fails - a 737-800 */
        info->length = 40;
        info->cgz = 18;
        info->semiwidth  = 18;
        info->refheight = 3.5;

        if (!(h = fopen(path, "r"))) continue;
        o = fgetc(h);
        if (o=='I' || o=='A')
            read_v10_plane(h, o, info);
        else if (o=='i' || o=='a')
            read_old_plane(h, o, info);
        fclose(h);
    }

    return plane_count;
}


static void read_v10_plane(FILE *h, int platform, plane_acf_t *info)
{
    char line[256], *c1, *c2;
    int eol1;
    int version;

    if (!fgets(line, sizeof(line), h)) return;
    if (!fgets(line, sizeof(line), h)) return;
    c1=strtok(line, sep);
    c2=strtok(NULL, sep);
    if (!c1 || !sscanf(c1, "%d%n", &version, &eol1) || c1[eol1] ||
        strcmp(c2, "version") ||
        version < 1004)
        return;	/* doesn't look like an ACF file */

    while (fgets(line, sizeof(line), h))
    {
        /* Assume for speed that fields are single-space separated */
        if (!strncmp(line, "P acf/_size_x ", sizeof("P acf/_size_x ")-1))
        {
            if (!sscanf(line+sizeof("P acf/_size_x ")-1, "%f", &info->semiwidth)) return;
            info->semiwidth *= 0.3048f;
        }
        else if (!strncmp(line, "P acf/_size_z ", sizeof("P acf/_size_z ")-1))
        {
            if (!sscanf(line+sizeof("P acf/_size_z ")-1, "%f", &info->length)) return;
            info->length *= 0.3048f;
        }
        else if (!strncmp(line, "P acf/_h_eqlbm ", sizeof("P acf/_h_eqlbm ")-1))
        {
            if (!sscanf(line+sizeof("P acf/_h_eqlbm ")-1, "%f", &info->refheight)) return;
            info->refheight *= 0.3048f;
        }
        else if (!strncmp(line, "P acf/_cgZ ", sizeof("P acf/_cgZ ")-1))
        {
            if (!sscanf(line+sizeof("P acf/_cgZ ")-1, "%f", &info->cgz)) return;
            info->cgz *= 0.3048f;
        }
    }
}

static size_t freadswap(void *ptr, size_t size, size_t nitems, FILE *stream)
{
    unsigned char *c = ptr;
    int i, b;

    for (i=0; i<nitems; i++, c+=size)
        for (b=size-1; b>=0; b--)
            if (!fread(c + b, 1, 1, stream)) return i;

    return nitems;
}

static void read_old_plane(FILE *h, int platform, plane_acf_t *info)
{
    int version = 0;
    size_t (*readfn)(void *, size_t, size_t, FILE *) = platform=='i' ? fread : freadswap;

    assert(sizeof(int) == 4);	/* Code below assumes this - could use int32_t */
    readfn(&version, 4, 1, h);

    /* WB_cgZ */
    if (version>=700 && version<=740)
    {
        if (fseek(h, 0x98a45, SEEK_SET)) return;
    }
    else if  ((version>=800 && version<=941) || version==8000)
    {
        if (fseek(h, 0x21489, SEEK_SET)) return;
    }
    else	/* unknown version */
    {
        return;
    }
    if (!readfn(&info->cgz, 4, 1, h)) return;
    info->cgz *= 0.3048f;

    /* AUTO_size_x, AUTO_size_z */
    if (version<=740)
    {
        if (fseek(h, 0x9bc2d, SEEK_SET)) return;
    }
    else
    {
        if (fseek(h, 0x21711, SEEK_SET)) return;
    }
    if (!readfn(&info->semiwidth, 4, 1, h)) return;
    info->semiwidth *= 0.3048f;
    if (!readfn(&info->length, 4, 1, h)) return;
    info->length *= 0.3048f;

    /* AUTO_h_eqlbm */
    if (version<=740)
    {
        if (fseek(h, 0x9bc3d, SEEK_SET)) return;
    }
    else
    {
        if (fseek(h, 0x2171d, SEEK_SET)) return;
    }
    if (!readfn(&info->refheight, 4, 1, h)) return;
    info->refheight *= 0.3048f;
}


plane_acf_t *get_plane_info(int planeno)
{
    return plane_info + planeno;
}


int get_plane_pos(plane_pos_t *pos, int planeno)
{
    plane_ref_t *plane_ref = plane_refs + planeno;
    float gear;

    assert(planeno < plane_count);
    if (!XPLMGetDatavf(plane_ref->gear, &gear, 0, 1) || gear!=1) return 0;	/* Not interested in airborne planes */

    pos->p.x = XPLMGetDataf(plane_ref->x);
    pos->p.y = XPLMGetDataf(plane_ref->y);
    pos->p.z = XPLMGetDataf(plane_ref->z);
    if (!(pos->p.x || pos->p.z)) return 0;	/* No position data ??? */

    pos->v.x = XPLMGetDataf(plane_ref->vx);
    pos->v.y = 0;				/* Don't care about vertical speed */
    pos->v.z = XPLMGetDataf(plane_ref->vz);
    pos->hdg = XPLMGetDataf(plane_ref->hdg);

    return -1;
}


/* Get a plane's ground footprint.
 * Returns NULL if the plane is airborne. Otherwise returns pointer to a statically allocated
 * array of 4 points, contents of which will be overwritten on next call. */
point_t *get_plane_footprint(int planeno, float time)
{
    static point_t p[4];	/* footprint rectangle */
    
    plane_pos_t pos;
    plane_acf_t *info = plane_info + planeno;
    float gndy, h, cosh, sinh;
    point_t proj, tail, semi;

    if (!get_plane_pos(&pos, planeno)) return NULL;

    gndy = pos.p.y - info->refheight;
    h = D2R(pos.hdg);
    cosh = cosf(h);
    sinh = sinf(h);
    if (pos.v.x || pos.v.z)
    {
        /* Add space in front of plane */
        proj.x = pos.p.x + sinh * 2 * info->cgz + time * pos.v.x;
        proj.z = pos.p.z - cosh * 2 * info->cgz + time * pos.v.z;
    }
    else
    {
        /* unless plane is *completely* static (i.e. brake on) */
        proj.x = pos.p.x + sinh * info->cgz;
        proj.z = pos.p.z - cosh * info->cgz;
    }
    tail.x = pos.p.x - sinh * (info->length - info->cgz);
    tail.z = pos.p.z + cosh * (info->length - info->cgz);
    semi.x = cosh * info->semiwidth;
    semi.z = sinh * info->semiwidth;

    p[0].x = proj.x - semi.x;  p[0].y = gndy;  p[0].z = proj.z - semi.z;
    p[1].x = proj.x + semi.x;  p[1].y = gndy;  p[1].z = proj.z + semi.z;
    p[2].x = tail.x + semi.x;  p[2].y = gndy;  p[2].z = tail.z + semi.z;
    p[3].x = tail.x - semi.x;  p[3].y = gndy;  p[3].z = tail.z - semi.z;

    return p;
}
