/*
 * GroundTraffic
 *
 * (c) Jonathan Harris 2013
 *
 * Licensed under GNU LGPL v2.1.
 */

#ifndef	_BBOX_H_
#define	_BBOX_H_

typedef struct
{
    float minlat, maxlat, minlon, maxlon;
} bbox_t;

static inline void bbox_init(bbox_t *bbox)
{
    bbox->minlat =   90;
    bbox->maxlat =  -90;
    bbox->minlon =  180;
    bbox->maxlon = -180;
}

static inline void bbox_add(bbox_t *bbox, float lat, float lon)
{
    if (lat < bbox->minlat) bbox->minlat = lat;
    if (lat > bbox->maxlat) bbox->maxlat = lat;
    if (lon < bbox->minlon) bbox->minlon = lon;
    if (lon > bbox->maxlon) bbox->maxlon = lon;
}

static inline int bbox_intersect(bbox_t *a, bbox_t *b)
{
    return ((a->minlat <= b->maxlat) && (a->maxlat > b->minlat) &&
            (a->minlon <= b->maxlon) && (a->maxlon > b->minlon));
}

#endif /* _BBOX_H_ */
