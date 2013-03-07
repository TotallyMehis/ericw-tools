/*
    Copyright (C) 1996-1997  Id Software, Inc.
    Copyright (C) 1997       Greg Lewis

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/
// csg4.c

#include "qbsp.h"

/*

NOTES
-----
Brushes that touch still need to be split at the cut point to make a tjunction

*/

face_t **validfaces;

static face_t *inside, *outside;
static int brushfaces;
static int csgfaces;
int csgmergefaces;

/*
==================
NewFaceFromFace

Duplicates the non point information of a face, used by SplitFace and
MergeFace.
==================
*/
face_t *
NewFaceFromFace(face_t *in)
{
    face_t *newf;

    newf = AllocMem(FACE, 1, true);

    newf->planenum = in->planenum;
    newf->texinfo = in->texinfo;
    newf->planeside = in->planeside;
    newf->original = in->original;
    newf->contents[0] = in->contents[0];
    newf->contents[1] = in->contents[1];
    newf->cflags[0] = in->cflags[0];
    newf->cflags[1] = in->cflags[1];

    VectorCopy(in->origin, newf->origin);
    newf->radius = in->radius;

    return newf;
}

void
UpdateFaceSphere(face_t *in)
{
    int i;
    vec3_t radius;
    vec_t lensq;

    MidpointWinding(&in->w, in->origin);
    in->radius = 0;
    for (i = 0; i < in->w.numpoints; i++) {
	VectorSubtract(in->w.points[i], in->origin, radius);
	lensq = VectorLengthSq(radius);
	if (lensq > in->radius)
	    in->radius = lensq;
    }
    in->radius = sqrt(in->radius);
}


/*
==================
SplitFace
==================
*/
void
SplitFace(face_t *in, const plane_t *split, face_t **front, face_t **back)
{
    vec_t dists[MAXEDGES + 1];
    int sides[MAXEDGES + 1];
    int counts[3];
    vec_t dot;
    int i, j;
    face_t *newf, *new2;
    vec_t *p1, *p2;
    vec3_t mid;

    if (in->w.numpoints < 0)
	Error(errFreedFace);

    /* Fast test */
    dot = DotProduct(in->origin, split->normal) - split->dist;
    if (dot > in->radius) {
	counts[SIDE_FRONT] = 1;
	counts[SIDE_BACK] = 0;
    } else if (dot < -in->radius) {
	counts[SIDE_FRONT] = 0;
	counts[SIDE_BACK] = 1;
    } else {
	CalcSides(&in->w, split, sides, dists, counts);
    }

    // Plane doesn't split this face after all
    if (!counts[SIDE_FRONT]) {
	*front = NULL;
	*back = in;
	return;
    }
    if (!counts[SIDE_BACK]) {
	*front = in;
	*back = NULL;
	return;
    }

    *back = newf = NewFaceFromFace(in);
    *front = new2 = NewFaceFromFace(in);

    // distribute the points and generate splits
    for (i = 0; i < in->w.numpoints; i++) {
	// Note: Possible for numpoints on newf or new2 to exceed MAXEDGES if
	// in->w.numpoints == MAXEDGES and it is a really devious split.

	p1 = in->w.points[i];

	if (sides[i] == SIDE_ON) {
	    VectorCopy(p1, newf->w.points[newf->w.numpoints]);
	    newf->w.numpoints++;
	    VectorCopy(p1, new2->w.points[new2->w.numpoints]);
	    new2->w.numpoints++;
	    continue;
	}

	if (sides[i] == SIDE_FRONT) {
	    VectorCopy(p1, new2->w.points[new2->w.numpoints]);
	    new2->w.numpoints++;
	} else {
	    VectorCopy(p1, newf->w.points[newf->w.numpoints]);
	    newf->w.numpoints++;
	}

	if (sides[i + 1] == SIDE_ON || sides[i + 1] == sides[i])
	    continue;

	// generate a split point
	p2 = in->w.points[(i + 1) % in->w.numpoints];

	dot = dists[i] / (dists[i] - dists[i + 1]);
	for (j = 0; j < 3; j++) {	// avoid round off error when possible
	    if (split->normal[j] == 1)
		mid[j] = split->dist;
	    else if (split->normal[j] == -1)
		mid[j] = -split->dist;
	    else
		mid[j] = p1[j] + dot * (p2[j] - p1[j]);
	}

	VectorCopy(mid, newf->w.points[newf->w.numpoints]);
	newf->w.numpoints++;
	VectorCopy(mid, new2->w.points[new2->w.numpoints]);
	new2->w.numpoints++;
    }

    if (newf->w.numpoints > MAXEDGES || new2->w.numpoints > MAXEDGES)
	Error(errLowSplitPointCount);

    // free the original face now that it is represented by the fragments
    FreeMem(in, FACE, 1);
}

/*
=================
RemoveOutsideFaces

Quick test before running ClipInside; move any faces that are completely
outside the brush to the outside list, without splitting them. This saves us
time in mergefaces later on (and sometimes a lot of memory)
=================
*/
static void
RemoveOutsideFaces(const brush_t *brush, face_t **inside, face_t **outside)
{
    plane_t clipplane;
    const face_t *clipface;
    face_t *face, *next;
    winding_t *w;

    face = *inside;
    *inside = NULL;
    while (face) {
	next = face->next;
	w = CopyWinding(&face->w);
	for (clipface = brush->faces; clipface; clipface = clipface->next) {
	    clipplane = map.planes[clipface->planenum];
	    if (!clipface->planeside) {
		VectorSubtract(vec3_origin, clipplane.normal, clipplane.normal);
		clipplane.dist = -clipplane.dist;
	    }
	    w = ClipWinding(w, &clipplane, true);
	    if (!w)
		break;
	}
	if (!w) {
	    /* The face is completely outside this brush */
	    face->next = *outside;
	    *outside = face;
	} else {
	    face->next = *inside;
	    *inside = face;
	    FreeMem(w, WINDING, 1);
	}
	face = next;
    }
}

/*
=================
ClipInside

Clips all of the faces in the inside list, possibly moving them to the
outside list or spliting it into a piece in each list.

Faces exactly on the plane will stay inside unless overdrawn by later brush
=================
*/
static void
ClipInside(const face_t *clipface, bool precedence,
	   face_t **inside, face_t **outside)
{
    face_t *face, *next, *frags[2];
    const plane_t *splitplane;

    splitplane = &map.planes[clipface->planenum];

    face = *inside;
    *inside = NULL;
    while (face) {
	next = face->next;

	/* Handle exactly on-plane faces */
	if (face->planenum == clipface->planenum) {
	    if (clipface->planeside != face->planeside || precedence) {
		/* always clip off opposite facing */
		frags[clipface->planeside] = NULL;
		frags[!clipface->planeside] = face;
	    } else {
		/* leave it on the outside */
		frags[clipface->planeside] = face;
		frags[!clipface->planeside] = NULL;
	    }
	} else {
	    /* proper split */
	    SplitFace(face, splitplane, &frags[0], &frags[1]);
	}

	if (frags[clipface->planeside]) {
	    frags[clipface->planeside]->next = *outside;
	    *outside = frags[clipface->planeside];
	}
	if (frags[!clipface->planeside]) {
	    frags[!clipface->planeside]->next = *inside;
	    *inside = frags[!clipface->planeside];
	}
	face = next;
    }
}


/*
==================
SaveOutside

Saves all of the faces in the outside list to the bsp plane list
==================
*/
static void
SaveOutside(bool mirror)
{
    face_t *f, *next, *newf;
    int i;
    int planenum;

    for (f = outside; f; f = next) {
	next = f->next;
	csgfaces++;
	planenum = f->planenum;

	if (mirror) {
	    newf = NewFaceFromFace(f);

	    newf->w.numpoints = f->w.numpoints;
	    newf->planeside = f->planeside ^ 1;	// reverse side
	    newf->contents[0] = f->contents[1];
	    newf->contents[1] = f->contents[0];
	    newf->cflags[0] = f->cflags[1];
	    newf->cflags[1] = f->cflags[0];

	    for (i = 0; i < f->w.numpoints; i++)	// add points backwards
		VectorCopy(f->w.points[f->w.numpoints - 1 - i], newf->w.points[i]);

	    validfaces[planenum] =
		MergeFaceToList(newf, validfaces[planenum]);
	}

	validfaces[planenum] = MergeFaceToList(f, validfaces[planenum]);
	validfaces[planenum] = FreeMergeListScraps(validfaces[planenum]);
    }
}

static void
FreeFaces(face_t *face)
{
    face_t *next;

    while (face) {
	next = face->next;
	FreeMem(face, FACE, 1);
	face = next;
    }
}

/*
==================
KeepInsideFaces

Keep the solid faces clipped inside a non-solid brush
==================
*/
static void
KeepInsideFaces(face_t *face, const brush_t *brush)
{
    face_t *next;

    while (face) {
	next = face->next;
	face->contents[0] = brush->contents;
	face->cflags[0] = brush->cflags;
	/*
	 * If the inside brush is empty space, inherit the outside contents.
	 * The only brushes with empty contents currently are hint brushes.
	 */
	if (face->contents[1] == CONTENTS_EMPTY)
	    face->contents[1] = brush->contents;
	face->next = outside;
	outside = face;
	face = next;
    }
}


//==========================================================================

/*
==================
BuildSurfaces

Returns a chain of all the external surfaces with one or more visible
faces.
==================
*/
surface_t *
BuildSurfaces(void)
{
    int i;
    face_t *face, **facelist;
    surface_t *surf, *surfhead;

    surfhead = NULL;

    facelist = validfaces;
    for (i = 0; i < map.numplanes; i++, facelist++) {
	if (!*facelist)
	    continue;

	/* create a new surface to hold the faces on this plane */
	surf = AllocMem(SURFACE, 1, true);
	surf->planenum = i;
	surf->next = surfhead;
	surfhead = surf;
	surf->faces = *facelist;
	for (face = surf->faces; face; face = face->next)
	    csgmergefaces++;

	/* Calculate bounding box and flags */
	CalcSurfaceInfo(surf);
    }

    return surfhead;
}

//==========================================================================

/*
==================
CopyFacesToOutside
==================
*/
static void
CopyFacesToOutside(const brush_t *brush)
{
    face_t *face, *newface;

    outside = NULL;

    for (face = brush->faces; face; face = face->next) {
	brushfaces++;
	newface = AllocMem(FACE, 1, true);
	*newface = *face;
	newface->next = outside;
	newface->contents[0] = CONTENTS_EMPTY;
	newface->contents[1] = brush->contents;
	newface->cflags[0] = 0;
	newface->cflags[1] = brush->cflags;
	outside = newface;
    }
}


/*
==================
CSGFaces

Returns a list of surfaces containing all of the faces
==================
*/
surface_t *
CSGFaces(const mapentity_t *entity)
{
    int i;
    const brush_t *b1, *b2;
    const face_t *clipface;
    bool overwrite;
    surface_t *surfaces;
    int progress = 0;

    Message(msgProgress, "CSGFaces");

    if (!validfaces)
	validfaces = AllocMem(OTHER, sizeof(face_t *) * map.maxplanes, false);

    memset(validfaces, 0, sizeof(face_t *) * map.maxplanes);
    csgfaces = brushfaces = csgmergefaces = 0;

    // do the solid faces
    for (b1 = entity->brushes; b1; b1 = b1->next) {
	// set outside to a copy of the brush's faces
	CopyFacesToOutside(b1);
	overwrite = false;
	for (b2 = entity->brushes; b2; b2 = b2->next) {
	    if (b1 == b2) {
		/* Brushes further down the list overried earlier ones */
		overwrite = true;
		continue;
	    }

	    /* check bounding box first */
	    for (i = 0; i < 3; i++)
		if (b1->mins[i] > b2->maxs[i] || b1->maxs[i] < b2->mins[i])
		    break;
	    if (i < 3)
		continue;

	    /*
	     * TODO - optimise by checking for opposing planes?
	     *  => brushes can't intersect
	     */

	    // divide faces by the planes of the new brush
	    inside = outside;
	    outside = NULL;

	    RemoveOutsideFaces(b2, &inside, &outside);
	    for (clipface = b2->faces; clipface; clipface = clipface->next)
		ClipInside(clipface, overwrite, &inside, &outside);

	    // these faces are continued in another brush, so get rid of them
	    if (b1->contents == CONTENTS_SOLID && b2->contents != CONTENTS_SOLID)
		KeepInsideFaces(inside, b2);
	    else
		FreeFaces(inside);
	}

	// all of the faces left in outside are real surface faces
	if (b1->contents != CONTENTS_SOLID)
	    SaveOutside(true);	// mirror faces for inside view
	else
	    SaveOutside(false);

	progress++;
	Message(msgPercent, progress, entity->numbrushes);
    }

    surfaces = BuildSurfaces();

    Message(msgStat, "%5i brushfaces", brushfaces);
    Message(msgStat, "%5i csgfaces", csgfaces);
    Message(msgStat, "%5i mergedfaces", csgmergefaces);

    return surfaces;
}
