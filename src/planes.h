/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 * Licensed under GNU LGPL v2.1.
 */

#ifdef _MSC_VER
typedef __int32 int32_t;
#else
#  include <stdint.h>
#endif

#include "groundtraffic.h"

#define MAX_PLANES 20		/* Seems to be a hard-coded limit in X-Plane, but maybe could increase in future */
#define MAX_ACF_NAME 256
#define MAX_ACF_PATH 512

typedef struct
{
    XPLMDataRef x, y, z, vx, vz, hdg, gear; 
} plane_ref_t;

typedef struct
{
    point_t p, v;		/* Position [m], speed [m/s] */
    float hdg;			/* [degrees] */
} plane_pos_t;

typedef struct
{
    char name[MAX_ACF_NAME];
    float length, semiwidth, refheight, cgz;	/* dimensions [m] */
} plane_acf_t;


/* prototypes */
int setup_plane_refs();
void reset_planes();
int count_planes();
plane_acf_t *get_plane_info(int planeno);
int get_plane_pos(plane_pos_t *pos, int planeno);
point_t *get_plane_footprint(int planeno, float time);
