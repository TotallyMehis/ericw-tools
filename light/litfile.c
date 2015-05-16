/*  Copyright (C) 2002-2006 Kevin Shanahan

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

#include <light/litfile.h>
#include <light/light.h>
#include <common/bspfile.h>
#include <common/cmdlib.h>

void
WriteLitFile(const bsp2_t *bsp, const char *filename, int version)
{
    FILE *litfile;
    char litname[1024];
    litheader_t header;

    snprintf(litname, sizeof(litname) - 4, "%s", filename);
    StripExtension(litname);
    if (version == 2)
	DefaultExtension(litname, ".lit2");
    else
	DefaultExtension(litname, ".lit");

    header.v1.ident[0] = 'Q';
    header.v1.ident[1] = 'L';
    header.v1.ident[2] = 'I';
    header.v1.ident[3] = 'T';
    header.v1.version = LittleLong(version);
    header.v2.numsurfs = LittleLong(bsp->numfaces);
    header.v2.lmsamples = LittleLong(bsp->lightdatasize);

    litfile = SafeOpenWrite(litname);
    SafeWrite(litfile, &header.v1, sizeof(header.v1));
    if (version == 2)
    {
        unsigned int i, j;
        unsigned int *offsets = malloc(bsp->numfaces * sizeof(*offsets));
        unsigned short *extents = malloc(2*bsp->numfaces * sizeof(*extents));
        unsigned char *styles = malloc(4*bsp->numfaces * sizeof(*styles));
        unsigned char *shifts = malloc(bsp->numfaces * sizeof(*shifts));
        for (i = 0; i < bsp->numfaces; i++)
        {
            offsets[i] = LittleLong(bsp->dfaces[i].lightofs);
            styles[i*4+0] = LittleShort(bsp->dfaces[i].styles[0]);
            styles[i*4+1] = LittleShort(bsp->dfaces[i].styles[1]);
            styles[i*4+2] = LittleShort(bsp->dfaces[i].styles[2]);
            styles[i*4+3] = LittleShort(bsp->dfaces[i].styles[3]);
            extents[i*2+0] = LittleShort(bsp->dfacesup[i].extent[0]);
            extents[i*2+1] = LittleShort(bsp->dfacesup[i].extent[1]);
            j = 0;
            while ((1u<<j) < bsp->dfacesup[i].lmscale)
                j++;
            shifts[i] = j;
        }
        SafeWrite(litfile, &header.v2, sizeof(header.v2));
        SafeWrite(litfile, offsets, bsp->numfaces * sizeof(*offsets));
        SafeWrite(litfile, extents, 2*bsp->numfaces * sizeof(*extents));
        SafeWrite(litfile, styles, 4*bsp->numfaces * sizeof(*styles));
        SafeWrite(litfile, shifts, bsp->numfaces * sizeof(*shifts));
	SafeWrite(litfile, lit_filebase, bsp->lightdatasize * 3);
        SafeWrite(litfile, lux_filebase, bsp->lightdatasize * 3);
    }
    else
        SafeWrite(litfile, lit_filebase, bsp->lightdatasize * 3);
    fclose(litfile);
}
