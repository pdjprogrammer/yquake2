/*
 * Copyright (C) 1997-2001 Id Software, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * =======================================================================
 *
 * Surface generation and drawing
 *
 * =======================================================================
 */

#include <assert.h>

#include "header/local.h"

int c_visible_lightmaps;
int c_visible_textures;
static vec3_t modelorg; /* relative to viewpoint */
msurface_t *r_alpha_surfaces;

gllightmapstate_t gl_lms;

void LM_InitBlock(void);
void LM_UploadBlock(qboolean dynamic);
qboolean LM_AllocBlock(int w, int h, int *x, int *y);

void R_SetCacheState(msurface_t *surf);
void R_BuildLightMap(msurface_t *surf, byte *dest, int stride);

static void
R_DrawGLPoly(glpoly_t *p)
{
	float *v;

	v = p->verts[0];

    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_TEXTURE_COORD_ARRAY );

    glVertexPointer( 3, GL_FLOAT, VERTEXSIZE*sizeof(GLfloat), v );
    glTexCoordPointer( 2, GL_FLOAT, VERTEXSIZE*sizeof(GLfloat), v+3 );
    glDrawArrays( GL_TRIANGLE_FAN, 0, p->numverts );

    glDisableClientState( GL_VERTEX_ARRAY );
    glDisableClientState( GL_TEXTURE_COORD_ARRAY );
}

static void
R_DrawGLFlowingPoly(msurface_t *fa)
{
	int i;
	float *v;
	glpoly_t *p;
	float scroll;

	p = fa->polys;

	scroll = -64 * ((r_newrefdef.time / 40.0) - (int)(r_newrefdef.time / 40.0));

	if (scroll == 0.0)
	{
		scroll = -64.0;
	}

    YQ2_VLA(GLfloat, tex, 2*p->numverts);
    unsigned int index_tex = 0;

    v = p->verts [ 0 ];

	for ( i = 0; i < p->numverts; i++, v += VERTEXSIZE )
    {
        tex[index_tex++] = v [ 3 ] + scroll;
        tex[index_tex++] = v [ 4 ];
    }
    v = p->verts [ 0 ];

    glEnableClientState( GL_VERTEX_ARRAY );
    glEnableClientState( GL_TEXTURE_COORD_ARRAY );

    glVertexPointer( 3, GL_FLOAT, VERTEXSIZE*sizeof(GLfloat), v );
    glTexCoordPointer( 2, GL_FLOAT, 0, tex );
    glDrawArrays( GL_TRIANGLE_FAN, 0, p->numverts );

    glDisableClientState( GL_VERTEX_ARRAY );
    glDisableClientState( GL_TEXTURE_COORD_ARRAY );

	YQ2_VLAFREE(tex);
}

static void
R_DrawTriangleOutlines(void)
{
	int i, j;
	glpoly_t *p;

	if (!gl_showtris->value)
	{
		return;
	}

	glDisable(GL_TEXTURE_2D);
	glDisable(GL_DEPTH_TEST);
	glColor4f(1, 1, 1, 1);

	for (i = 0; i < gl_state.max_lightmaps; i++)
	{
		msurface_t *surf;

		for (surf = gl_lms.lightmap_surfaces[i];
			 surf != 0;
			 surf = surf->lightmapchain)
		{
			p = surf->polys;

			for ( ; p; p = p->chain)
			{
				for (j = 2; j < p->numverts; j++)
				{
                    GLfloat vtx[12];
                    unsigned int k;

                    for (k=0; k<3; k++)
                    {
                       vtx[0+k] = p->verts [ 0 ][ k ];
                        vtx[3+k] = p->verts [ j - 1 ][ k ];
                        vtx[6+k] = p->verts [ j ][ k ];
                        vtx[9+k] = p->verts [ 0 ][ k ];
                    }

                    glEnableClientState( GL_VERTEX_ARRAY );

                    glVertexPointer( 3, GL_FLOAT, 0, vtx );
                    glDrawArrays( GL_LINE_STRIP, 0, 4 );

                    glDisableClientState( GL_VERTEX_ARRAY );
				}
			}
		}
	}

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);
}

static void
R_DrawGLPolyChain(glpoly_t *p, float soffset, float toffset)
{
	if ((soffset == 0) && (toffset == 0))
	{
		for ( ; p != 0; p = p->chain)
		{
			float *v;

			v = p->verts[0];

            glEnableClientState( GL_VERTEX_ARRAY );
            glEnableClientState( GL_TEXTURE_COORD_ARRAY );

            glVertexPointer( 3, GL_FLOAT, VERTEXSIZE*sizeof(GLfloat), v );
            glTexCoordPointer( 2, GL_FLOAT, VERTEXSIZE*sizeof(GLfloat), v+5 );
            glDrawArrays( GL_TRIANGLE_FAN, 0, p->numverts );

            glDisableClientState( GL_VERTEX_ARRAY );
            glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}
	}
	else
	{
		// workaround for lack of VLAs (=> our workaround uses alloca() which is bad in loops)
#ifdef _MSC_VER
		int maxNumVerts = 0;
		for (glpoly_t* tmp = p; tmp; tmp = tmp->chain)
		{
			if ( tmp->numverts > maxNumVerts )
				maxNumVerts = tmp->numverts;
		}

		YQ2_VLA( GLfloat, tex, 2 * maxNumVerts );
#endif

		for ( ; p != 0; p = p->chain)
		{
			float *v;
			int j;

			v = p->verts[0];
#ifndef _MSC_VER // we have real VLAs, so it's safe to use one in this loop
            YQ2_VLA(GLfloat, tex, 2*p->numverts);
#endif

            unsigned int index_tex = 0;

			for ( j = 0; j < p->numverts; j++, v += VERTEXSIZE )
			{
			    tex[index_tex++] = v [ 5 ] - soffset;
			    tex[index_tex++] = v [ 6 ] - toffset;
			}

			v = p->verts [ 0 ];

            glEnableClientState( GL_VERTEX_ARRAY );
            glEnableClientState( GL_TEXTURE_COORD_ARRAY );

            glVertexPointer( 3, GL_FLOAT, VERTEXSIZE*sizeof(GLfloat), v );
            glTexCoordPointer( 2, GL_FLOAT, 0, tex );
            glDrawArrays( GL_TRIANGLE_FAN, 0, p->numverts );

            glDisableClientState( GL_VERTEX_ARRAY );
            glDisableClientState( GL_TEXTURE_COORD_ARRAY );
		}

		YQ2_VLAFREE( tex );
	}
}

/*
 * This routine takes all the given light mapped surfaces
 * in the world and blends them into the framebuffer.
 */
static void
R_BlendLightmaps(const model_t *currentmodel)
{
	int i;
	msurface_t *surf, *newdrawsurf = 0;

	/* don't bother if we're set to fullbright or multitexture is enabled */
	if (gl_config.multitexture || r_fullbright->value || !r_worldmodel->lightdata)
	{
		return;
	}

	/* don't bother writing Z */
	glDepthMask(GL_FALSE);

	/* set the appropriate blending mode unless
	   we're only looking at the lightmaps. */
	if (!gl_lightmap->value)
	{
		glEnable(GL_BLEND);

		if (gl1_saturatelighting->value)
		{
			glBlendFunc(GL_ONE, GL_ONE);
		}
		else
		{
			glBlendFunc(GL_ZERO, GL_SRC_COLOR);
		}
	}

	if (currentmodel == r_worldmodel)
	{
		c_visible_lightmaps = 0;
	}

	/* render static lightmaps first */
	for (i = 1; i < gl_state.max_lightmaps; i++)
	{
		if (gl_lms.lightmap_surfaces[i])
		{
			if (currentmodel == r_worldmodel)
			{
				c_visible_lightmaps++;
			}

			R_Bind(gl_state.lightmap_textures + i);

			for (surf = gl_lms.lightmap_surfaces[i];
				 surf != 0;
				 surf = surf->lightmapchain)
			{
				if (surf->polys)
				{
					// Apply overbright bits to the static lightmaps
					if (gl1_overbrightbits->value)
					{
						R_TexEnv(GL_COMBINE);
						glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, gl1_overbrightbits->value);
					}

					R_DrawGLPolyChain(surf->polys, 0, 0);
				}
			}
		}
	}

	/* render dynamic lightmaps */
	if (gl1_dynamic->value)
	{
		LM_InitBlock();

		R_Bind(gl_state.lightmap_textures + 0);

		if (currentmodel == r_worldmodel)
		{
			c_visible_lightmaps++;
		}

		newdrawsurf = gl_lms.lightmap_surfaces[0];

		for (surf = gl_lms.lightmap_surfaces[0];
			 surf != 0;
			 surf = surf->lightmapchain)
		{
			int smax, tmax;
			byte *base;

			smax = (surf->extents[0] >> 4) + 1;
			tmax = (surf->extents[1] >> 4) + 1;

			if (LM_AllocBlock(smax, tmax, &surf->dlight_s, &surf->dlight_t))
			{
				base = gl_lms.lightmap_buffer[0];
				base += (surf->dlight_t * gl_state.block_width +
						surf->dlight_s) * LIGHTMAP_BYTES;

				R_BuildLightMap(surf, base, gl_state.block_width * LIGHTMAP_BYTES);
			}
			else
			{
				msurface_t *drawsurf;

				/* upload what we have so far */
				LM_UploadBlock(true);

				/* draw all surfaces that use this lightmap */
				for (drawsurf = newdrawsurf;
					 drawsurf != surf;
					 drawsurf = drawsurf->lightmapchain)
				{
					if (drawsurf->polys)
					{
						// Apply overbright bits to the dynamic lightmaps
						if (gl1_overbrightbits->value)
						{
							R_TexEnv(GL_COMBINE);
							glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, gl1_overbrightbits->value);
						}

						R_DrawGLPolyChain(drawsurf->polys,
								(drawsurf->light_s - drawsurf->dlight_s) * (1.0 / (float)gl_state.block_width),
								(drawsurf->light_t - drawsurf->dlight_t) * (1.0 / (float)gl_state.block_height));
					}
				}

				newdrawsurf = drawsurf;

				/* clear the block */
				LM_InitBlock();

				/* try uploading the block now */
				if (!LM_AllocBlock(smax, tmax, &surf->dlight_s, &surf->dlight_t))
				{
					ri.Sys_Error(ERR_FATAL,
							"Consecutive calls to LM_AllocBlock(%d,%d) failed (dynamic)\n",
							smax, tmax);
				}

				base = gl_lms.lightmap_buffer[0];
				base += (surf->dlight_t * gl_state.block_width +
						surf->dlight_s) * LIGHTMAP_BYTES;

				R_BuildLightMap(surf, base, gl_state.block_width * LIGHTMAP_BYTES);
			}
		}

		/* draw remainder of dynamic lightmaps that haven't been uploaded yet */
		if (newdrawsurf)
		{
			LM_UploadBlock(true);
		}

		for (surf = newdrawsurf; surf != 0; surf = surf->lightmapchain)
		{
			if (surf->polys)
			{
				// Apply overbright bits to the remainder lightmaps
				if (gl1_overbrightbits->value)
				{
					R_TexEnv(GL_COMBINE);
					glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, gl1_overbrightbits->value);
				}

				R_DrawGLPolyChain(surf->polys,
						(surf->light_s - surf->dlight_s) * (1.0 / (float)gl_state.block_width),
						(surf->light_t - surf->dlight_t) * (1.0 / (float)gl_state.block_height));
			}
		}
	}

	/* restore state */
	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glDepthMask(GL_TRUE);
}

static void
R_RenderBrushPoly(entity_t *currententity, msurface_t *fa)
{
	int maps;
	qboolean is_dynamic = false;

	c_brush_polys++;

	if (fa->flags & SURF_DRAWTURB)
	{
		/* This is a hack ontop of a hack. Warping surfaces like those generated
		   by R_EmitWaterPolys() don't have a lightmap. Original Quake II therefore
		   negated the global intensity on those surfaces, because otherwise they
		   would show up much too bright. When we implemented overbright bits this
		   hack modified the global GL state in an incompatible way. So implement
		   a new hack, based on overbright bits... Depending on the value set to
		   gl1_overbrightbits the result is different:

		    0: Old behaviour.
		    1: No overbright bits on the global scene but correct lighting on
		       warping surfaces.
		    2: Overbright bits on the global scene but not on warping surfaces.
		        They oversaturate otherwise. */
		if (gl1_overbrightbits->value)
		{
			R_TexEnv(GL_COMBINE);
			glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, 1);
		}
		else
		{
			R_TexEnv(GL_MODULATE);
			glColor4f(gl_state.inverse_intensity, gl_state.inverse_intensity,
					  gl_state.inverse_intensity, 1.0f);
		}

		R_EmitWaterPolys(fa);
		R_TexEnv(GL_REPLACE);

		return;
	}

	R_TexEnv(GL_REPLACE);

	if (fa->texinfo->flags & SURF_FLOWING)
	{
		R_DrawGLFlowingPoly(fa);
	}
	else
	{
		R_DrawGLPoly(fa->polys);
	}

	if (gl_config.multitexture)
	{
		return;	// lighting already done at this point for mtex
	}

	/* check for lightmap modification */
	for (maps = 0; maps < MAXLIGHTMAPS && fa->styles[maps] != 255; maps++)
	{
		if (r_newrefdef.lightstyles[fa->styles[maps]].white !=
			fa->cached_light[maps])
		{
			goto dynamic;
		}
	}

	/* dynamic this frame or dynamic previously */
	if (fa->dlightframe == r_framecount)
	{
	dynamic:

		if (gl1_dynamic->value)
		{
			if (!(fa->texinfo->flags &
				  (SURF_SKY | SURF_TRANS33 |
				   SURF_TRANS66 | SURF_WARP)))
			{
				is_dynamic = true;
			}
		}
	}

	if (is_dynamic)
	{
		if (maps < MAXLIGHTMAPS &&
			((fa->styles[maps] >= 32) ||
			 (fa->styles[maps] == 0)) &&
			  (fa->dlightframe != r_framecount))
		{
			unsigned temp[34 * 34];
			int smax, tmax;

			smax = (fa->extents[0] >> 4) + 1;
			tmax = (fa->extents[1] >> 4) + 1;

			R_BuildLightMap(fa, (void *)temp, smax * 4);
			R_SetCacheState(fa);

			R_Bind(gl_state.lightmap_textures + fa->lightmaptexturenum);

			glTexSubImage2D(GL_TEXTURE_2D, 0, fa->light_s, fa->light_t,
					smax, tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, temp);

			fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
			gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
		}
		else
		{
			fa->lightmapchain = gl_lms.lightmap_surfaces[0];
			gl_lms.lightmap_surfaces[0] = fa;
		}
	}
	else
	{
		fa->lightmapchain = gl_lms.lightmap_surfaces[fa->lightmaptexturenum];
		gl_lms.lightmap_surfaces[fa->lightmaptexturenum] = fa;
	}
}

/*
 * Draw water surfaces and windows.
 * The BSP tree is waled front to back, so unwinding the chain
 * of alpha_surfaces will draw back to front, giving proper ordering.
 */
void
R_DrawAlphaSurfaces(void)
{
	msurface_t *s;
	float intens;

	/* go back to the world matrix */
	glLoadMatrixf(r_world_matrix);

	glEnable(GL_BLEND);
	R_TexEnv(GL_MODULATE);

	/* the textures are prescaled up for a better
	   lighting range, so scale it back down */
	intens = gl_state.inverse_intensity;

	for (s = r_alpha_surfaces; s; s = s->texturechain)
	{
		R_Bind(s->texinfo->image->texnum);
		c_brush_polys++;

		if (s->texinfo->flags & SURF_TRANS33)
		{
			glColor4f(intens, intens, intens, 0.33);
		}
		else if (s->texinfo->flags & SURF_TRANS66)
		{
			glColor4f(intens, intens, intens, 0.66);
		}
		else
		{
			glColor4f(intens, intens, intens, 1);
		}

		if (s->flags & SURF_DRAWTURB)
		{
			R_EmitWaterPolys(s);
		}
		else if (s->texinfo->flags & SURF_FLOWING)
		{
			R_DrawGLFlowingPoly(s);
		}
		else
		{
			R_DrawGLPoly(s->polys);
		}
	}

	R_TexEnv(GL_REPLACE);
	glColor4f(1, 1, 1, 1);
	glDisable(GL_BLEND);

	r_alpha_surfaces = NULL;
}

static qboolean
R_HasDynamicLights(msurface_t *surf, int *mapNum)
{
	int map;
	qboolean is_dynamic = false;

	if ( r_fullbright->value || !gl1_dynamic->value ||
		(surf->texinfo->flags & (SURF_SKY | SURF_TRANS33 | SURF_TRANS66 | SURF_WARP)) )
	{
		return false;
	}

	// Any dynamic lights on this surface?
	for (map = 0; map < MAXLIGHTMAPS && surf->styles[map] != 255; map++)
	{
		if (r_newrefdef.lightstyles[surf->styles[map]].white != surf->cached_light[map])
		{
			is_dynamic = true;
			break;
		}
	}

	// No matter if it is this frame or was in the previous one: has dynamic lights
	if ( !is_dynamic && (surf->dlightframe == r_framecount || surf->dirty_lightmap) )
	{
		is_dynamic = true;
	}

	if (mapNum)
	{
		*mapNum = map;
	}
	return is_dynamic;
}

static void
R_UpdateSurfCache(msurface_t *surf, int map)
{
	if ( ((surf->styles[map] >= 32) || (surf->styles[map] == 0))
		&& (surf->dlightframe != r_framecount) )
	{
		R_SetCacheState(surf);
	}
	surf->dirty_lightmap = (surf->dlightframe == r_framecount);
}

static void
R_RenderLightmappedPoly(entity_t *currententity, msurface_t *surf)
{
	int i;
	int nv = surf->polys->numverts;
	float scroll;
	float *v;

	R_MBind(GL_TEXTURE1, gl_state.lightmap_textures + surf->lightmaptexturenum);

	// Apply overbrightbits to TMU 1 (lightmap)
	if (gl1_overbrightbits->value)
	{
		R_TexEnv(GL_COMBINE);
		glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, gl1_overbrightbits->value);
	}

	c_brush_polys++;
	v = surf->polys->verts[0];

	if (surf->texinfo->flags & SURF_FLOWING)
	{
		scroll = -64 * ((r_newrefdef.time / 40.0) - (int) (r_newrefdef.time / 40.0));

		if (scroll == 0.0)
		{
			scroll = -64.0;
		}

		YQ2_VLA(GLfloat, tex, 4 * nv);
		unsigned int index_tex = 0;

		for (i = 0; i < nv; i++, v += VERTEXSIZE)
		{
			tex[index_tex++] = v[3] + scroll;
			tex[index_tex++] = v[4];
			tex[index_tex++] = v[5];
			tex[index_tex++] = v[6];
		}
		v = surf->polys->verts[0];

		// Polygon
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(GLfloat), v);

		// Texture
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		qglClientActiveTexture(GL_TEXTURE0);
		glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(GLfloat), tex);

		// Lightmap
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		qglClientActiveTexture(GL_TEXTURE1);
		glTexCoordPointer(2, GL_FLOAT, 4 * sizeof(GLfloat), tex + 2);

		// Draw the thing
		glDrawArrays(GL_TRIANGLE_FAN, 0, nv);

		YQ2_VLAFREE(tex);
	}
	else
	{
		// Polygon
		glEnableClientState(GL_VERTEX_ARRAY);
		glVertexPointer(3, GL_FLOAT, VERTEXSIZE * sizeof(GLfloat), v);

		// Texture
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		qglClientActiveTexture(GL_TEXTURE0);
		glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(GLfloat), v + 3);

		// Lightmap
		glEnableClientState(GL_TEXTURE_COORD_ARRAY);
		qglClientActiveTexture(GL_TEXTURE1);
		glTexCoordPointer(2, GL_FLOAT, VERTEXSIZE * sizeof(GLfloat), v + 5);

		// Draw it
		glDrawArrays(GL_TRIANGLE_FAN, 0, nv);
	}

	glDisableClientState(GL_VERTEX_ARRAY);
	glDisableClientState(GL_TEXTURE_COORD_ARRAY);
}

static void
R_UploadDynamicLights(msurface_t *surf)
{
	int map, smax, tmax;

	if ( !gl_config.multitexture || !R_HasDynamicLights(surf, &map) )
	{
		return;
	}
	YQ2_VLA(byte, temp, gl_state.block_width * gl_state.block_height);

	smax = (surf->extents[0] >> 4) + 1;
	tmax = (surf->extents[1] >> 4) + 1;
	R_BuildLightMap(surf, (void *) temp, smax * LIGHTMAP_BYTES);
	R_UpdateSurfCache(surf, map);

	R_Bind(gl_state.lightmap_textures + surf->lightmaptexturenum);
	glTexSubImage2D(GL_TEXTURE_2D, 0, surf->light_s, surf->light_t, smax,
					tmax, GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, temp);
	YQ2_VLAFREE(temp);
}

/* Upload dynamic lights to each lightmap texture (multitexture path only) */
static void
R_RegenAllLightmaps()
{
	int i, map, smax, tmax, top, bottom, left, right, bt, bb, bl, br;
	qboolean affected_lightmap, pixelstore_set = false;
	msurface_t *surf;
	byte *base;

	if ( !gl_config.multitexture )
	{
		return;
	}

	for (i = 1; i < gl_state.max_lightmaps; i++)
	{
		if (!gl_lms.lightmap_surfaces[i] || !gl_lms.lightmap_buffer[i])
		{
			continue;
		}

		affected_lightmap = false;
		bt = gl_state.block_height;
		bl = gl_state.block_width;
		bb = br = 0;

		for (surf = gl_lms.lightmap_surfaces[i];
			 surf != 0;
			 surf = surf->lightmapchain)
		{
			if ( !R_HasDynamicLights(surf, &map) )
			{
				continue;
			}

			affected_lightmap = true;
			smax = (surf->extents[0] >> 4) + 1;
			tmax = (surf->extents[1] >> 4) + 1;

			left = surf->light_s;
			right = surf->light_s + smax;
			top = surf->light_t;
			bottom = surf->light_t + tmax;

			base = gl_lms.lightmap_buffer[i];
			base += (top * gl_state.block_width + left) * LIGHTMAP_BYTES;

			R_BuildLightMap(surf, base, gl_state.block_width * LIGHTMAP_BYTES);
			R_UpdateSurfCache(surf, map);

			if (left < bl)
			{
				bl = left;
			}
			if (right > br)
			{
				br = right;
			}
			if (top < bt)
			{
				bt = top;
			}
			if (bottom > bb)
			{
				bb = bottom;
			}
		}

		if (!affected_lightmap)
		{
			continue;
		}

		if (!pixelstore_set)
		{
			glPixelStorei(GL_UNPACK_ROW_LENGTH, gl_state.block_width);
			pixelstore_set = true;
		}

		base = gl_lms.lightmap_buffer[i];
		base += (bt * gl_state.block_width + bl) * LIGHTMAP_BYTES;

		// upload changes
		R_Bind(gl_state.lightmap_textures + i);
		glTexSubImage2D(GL_TEXTURE_2D, 0, bl, bt, br - bl, bb - bt,
						GL_LIGHTMAP_FORMAT, GL_UNSIGNED_BYTE, base);
	}

	if (pixelstore_set)
	{
		glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
	}
}

static void
R_DrawTextureChains(entity_t *currententity)
{
	int i;
	msurface_t *s;
	image_t *image;

	c_visible_textures = 0;

	if (!gl_config.multitexture)	// classic path
	{
		for (i = 0, image = gltextures; i < numgltextures; i++, image++)
		{
			if (!image->registration_sequence)
			{
				continue;
			}

			s = image->texturechain;

			if (!s)
			{
				continue;
			}

			c_visible_textures++;

			for ( ; s; s = s->texturechain)
			{
				R_Bind(image->texnum);  // may reset because of dynamic lighting in R_RenderBrushPoly
				R_RenderBrushPoly(currententity, s);
			}

			image->texturechain = NULL;
		}
	}
	else	// multitexture
	{
		R_EnableMultitexture(true);

		for (i = 0, image = gltextures; i < numgltextures; i++, image++)
		{
			if (!image->registration_sequence || !image->texturechain)
			{
				continue;
			}

			R_MBind(GL_TEXTURE0, image->texnum);	// setting it only once
			c_visible_textures++;

			for (s = image->texturechain; s; s = s->texturechain)
			{
				if (!(s->flags & SURF_DRAWTURB))
				{
					R_RenderLightmappedPoly(currententity, s);
				}
			}
		}

		R_EnableMultitexture(false);

		for (i = 0, image = gltextures; i < numgltextures; i++, image++)
		{
			if (!image->registration_sequence || !image->texturechain)
			{
				continue;
			}

			for (s = image->texturechain; s; s = s->texturechain)
			{
				if (s->flags & SURF_DRAWTURB)
				{
					R_Bind(image->texnum);
					R_RenderBrushPoly(currententity, s);
				}
			}

			image->texturechain = NULL;
		}
	}
}

static void
R_DrawInlineBModel(entity_t *currententity, const model_t *currentmodel)
{
	int i, k;
	cplane_t *pplane;
	float dot;
	msurface_t *psurf;
	dlight_t *lt;
	image_t *image;

	/* calculate dynamic lighting for bmodel */
	if (!gl1_flashblend->value)
	{
		lt = r_newrefdef.dlights;

		for (k = 0; k < r_newrefdef.num_dlights; k++, lt++)
		{
			R_MarkLights(lt, 1 << k,
				currentmodel->nodes + currentmodel->firstnode,
				r_dlightframecount, R_MarkSurfaceLights);
		}
	}

	psurf = &currentmodel->surfaces[currentmodel->firstmodelsurface];

	if (currententity->flags & RF_TRANSLUCENT)
	{
		glEnable(GL_BLEND);
		glColor4f(1, 1, 1, 0.25);
		R_TexEnv(GL_MODULATE);
	}

	/* draw texture */
	for (i = 0; i < currentmodel->nummodelsurfaces; i++, psurf++)
	{
		/* find which side of the node we are on */
		pplane = psurf->plane;

		dot = DotProduct(modelorg, pplane->normal) - pplane->dist;

		/* draw the polygon */
		if (((psurf->flags & SURF_PLANEBACK) && (dot < -BACKFACE_EPSILON)) ||
			(!(psurf->flags & SURF_PLANEBACK) && (dot > BACKFACE_EPSILON)))
		{
			if (psurf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
			{
				/* add to the translucent chain */
				psurf->texturechain = r_alpha_surfaces;
				r_alpha_surfaces = psurf;
			}
			else
			{
				image = R_TextureAnimation(currententity, psurf->texinfo);

				if (gl_config.multitexture && !(psurf->flags & SURF_DRAWTURB))
				{
					R_UploadDynamicLights(psurf);
					R_EnableMultitexture(true);
					R_MBind(GL_TEXTURE0, image->texnum);
					R_RenderLightmappedPoly(currententity, psurf);
				}
				else
				{
					R_EnableMultitexture(false);
					R_Bind(image->texnum);
					R_RenderBrushPoly(currententity, psurf);
				}
			}
		}
	}

	if (!(currententity->flags & RF_TRANSLUCENT))
	{
		R_BlendLightmaps(currentmodel);
	}
	else
	{
		glDisable(GL_BLEND);
		glColor4f(1, 1, 1, 1);
		R_TexEnv(GL_REPLACE);
	}
}

void
R_DrawBrushModel(entity_t *currententity, const model_t *currentmodel)
{
	vec3_t mins, maxs;
	int i;
	qboolean rotated;

	if (currentmodel->nummodelsurfaces == 0)
	{
		return;
	}

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	if (currententity->angles[0] || currententity->angles[1] || currententity->angles[2])
	{
		rotated = true;

		for (i = 0; i < 3; i++)
		{
			mins[i] = currententity->origin[i] - currentmodel->radius;
			maxs[i] = currententity->origin[i] + currentmodel->radius;
		}
	}
	else
	{
		rotated = false;
		VectorAdd(currententity->origin, currentmodel->mins, mins);
		VectorAdd(currententity->origin, currentmodel->maxs, maxs);
	}

	if (r_cull->value && R_CullBox(mins, maxs, frustum))
	{
		return;
	}

	if (gl_zfix->value)
	{
		glEnable(GL_POLYGON_OFFSET_FILL);
	}

	glColor4f(1, 1, 1, 1);
	memset(gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	VectorSubtract(r_newrefdef.vieworg, currententity->origin, modelorg);

	if (rotated)
	{
		vec3_t temp;
		vec3_t forward, right, up;

		VectorCopy(modelorg, temp);
		AngleVectors(currententity->angles, forward, right, up);
		modelorg[0] = DotProduct(temp, forward);
		modelorg[1] = -DotProduct(temp, right);
		modelorg[2] = DotProduct(temp, up);
	}

	glPushMatrix();
	currententity->angles[0] = -currententity->angles[0];
	currententity->angles[2] = -currententity->angles[2];
	R_RotateForEntity(currententity);
	currententity->angles[0] = -currententity->angles[0];
	currententity->angles[2] = -currententity->angles[2];

	if (gl_lightmap->value)
	{
		R_TexEnv(GL_REPLACE);
	}
	else
	{
		R_TexEnv(GL_MODULATE);
	}

	R_DrawInlineBModel(currententity, currentmodel);

	glPopMatrix();

	if (gl_zfix->value)
	{
		glDisable(GL_POLYGON_OFFSET_FILL);
	}
}

static void
R_RecursiveWorldNode(entity_t *currententity, mnode_t *node)
{
	int c, side, sidebit;
	cplane_t *plane;
	msurface_t *surf, **mark;
	mleaf_t *pleaf;
	float dot;
	image_t *image;

	if (node->contents == CONTENTS_SOLID)
	{
		return; /* solid */
	}

	if (node->visframe != r_visframecount)
	{
		return;
	}

	if (r_cull->value && R_CullBox(node->minmaxs, node->minmaxs + 3, frustum))
	{
		return;
	}

	/* if a leaf node, draw stuff */
	if (node->contents != CONTENTS_NODE)
	{
		pleaf = (mleaf_t *)node;

		/* check for door connected areas */
		if (!R_AreaVisible(r_newrefdef.areabits, pleaf))
			return;	// not visible

		mark = pleaf->firstmarksurface;
		c = pleaf->nummarksurfaces;

		if (c)
		{
			do
			{
				(*mark)->visframe = r_framecount;
				mark++;
			}
			while (--c);
		}

		return;
	}

	/* node is just a decision point, so go down the apropriate
	   sides find which side of the node we are on */
	plane = node->plane;

	switch (plane->type)
	{
		case PLANE_X:
			dot = modelorg[0] - plane->dist;
			break;
		case PLANE_Y:
			dot = modelorg[1] - plane->dist;
			break;
		case PLANE_Z:
			dot = modelorg[2] - plane->dist;
			break;
		default:
			dot = DotProduct(modelorg, plane->normal) - plane->dist;
			break;
	}

	if (dot >= 0)
	{
		side = 0;
		sidebit = 0;
	}
	else
	{
		side = 1;
		sidebit = SURF_PLANEBACK;
	}

	/* recurse down the children, front side first */
	R_RecursiveWorldNode(currententity, node->children[side]);

	/* draw stuff */
	for (c = node->numsurfaces,
		 surf = r_worldmodel->surfaces + node->firstsurface;
		 c; c--, surf++)
	{
		if (surf->visframe != r_framecount)
		{
			continue;
		}

		if ((surf->flags & SURF_PLANEBACK) != sidebit)
		{
			continue; /* wrong side */
		}

		if (surf->texinfo->flags & SURF_SKY)
		{
			/* just adds to visible sky bounds */
			R_AddSkySurface(surf);
		}
		else if (surf->texinfo->flags & (SURF_TRANS33 | SURF_TRANS66))
		{
			/* add to the translucent chain */
			surf->texturechain = r_alpha_surfaces;
			r_alpha_surfaces = surf;
			r_alpha_surfaces->texinfo->image = R_TextureAnimation(currententity, surf->texinfo);
		}
		else
		{
			/* the polygon is visible, so add it to the texture sorted chain */
			image = R_TextureAnimation(currententity, surf->texinfo);
			surf->texturechain = image->texturechain;
			image->texturechain = surf;

			if (gl_config.multitexture && !(surf->texinfo->flags & SURF_WARP))	// needed for R_RegenAllLightmaps()
			{
				surf->lightmapchain = gl_lms.lightmap_surfaces[surf->lightmaptexturenum];
				gl_lms.lightmap_surfaces[surf->lightmaptexturenum] = surf;
			}
		}
	}

	/* recurse down the back side */
	R_RecursiveWorldNode(currententity, node->children[!side]);
}

void
R_DrawWorld(void)
{
	entity_t ent;

	if (!r_drawworld->value)
	{
		return;
	}

	if (r_newrefdef.rdflags & RDF_NOWORLDMODEL)
	{
		return;
	}

	VectorCopy(r_newrefdef.vieworg, modelorg);

	/* auto cycle the world frame for texture animation */
	memset(&ent, 0, sizeof(ent));
	ent.frame = (int)(r_newrefdef.time * 2);

	gl_state.currenttextures[0] = gl_state.currenttextures[1] = -1;

	glColor4f(1, 1, 1, 1);
	memset(gl_lms.lightmap_surfaces, 0, sizeof(gl_lms.lightmap_surfaces));

	R_ClearSkyBox();
	R_RecursiveWorldNode(&ent, r_worldmodel->nodes);
	R_RegenAllLightmaps();
	R_DrawTextureChains(&ent);
	R_BlendLightmaps(r_worldmodel);
	R_DrawSkyBox();
	R_DrawTriangleOutlines();
}

/*
 * Mark the leaves and nodes that are
 * in the PVS for the current cluster
 */
void
R_MarkLeaves(void)
{
	const byte *vis;
	YQ2_ALIGNAS_TYPE(int) byte fatvis[MAX_MAP_LEAFS / 8];
	mnode_t *node;
	int i, c;
	mleaf_t *leaf;
	int cluster;

	if ((r_oldviewcluster == r_viewcluster) &&
		(r_oldviewcluster2 == r_viewcluster2) &&
		!r_novis->value &&
		(r_viewcluster != -1))
	{
		return;
	}

	/* development aid to let you run around
	   and see exactly where the pvs ends */
	if (r_lockpvs->value)
	{
		return;
	}

	r_visframecount++;
	r_oldviewcluster = r_viewcluster;
	r_oldviewcluster2 = r_viewcluster2;

	if (r_novis->value || (r_viewcluster == -1) || !r_worldmodel->vis)
	{
		/* mark everything */
		for (i = 0; i < r_worldmodel->numleafs; i++)
		{
			r_worldmodel->leafs[i].visframe = r_visframecount;
		}

		for (i = 0; i < r_worldmodel->numnodes; i++)
		{
			r_worldmodel->nodes[i].visframe = r_visframecount;
		}

		return;
	}

	vis = Mod_ClusterPVS(r_viewcluster, r_worldmodel);

	/* may have to combine two clusters because of solid water boundaries */
	if (r_viewcluster2 != r_viewcluster)
	{
		memcpy(fatvis, vis, (r_worldmodel->numleafs + 7) / 8);
		vis = Mod_ClusterPVS(r_viewcluster2, r_worldmodel);
		c = (r_worldmodel->numleafs + 31) / 32;

		for (i = 0; i < c; i++)
		{
			((int *)fatvis)[i] |= ((int *)vis)[i];
		}

		vis = fatvis;
	}

	for (i = 0, leaf = r_worldmodel->leafs;
		 i < r_worldmodel->numleafs;
		 i++, leaf++)
	{
		cluster = leaf->cluster;

		if (cluster == -1)
		{
			continue;
		}

		if (vis[cluster >> 3] & (1 << (cluster & 7)))
		{
			node = (mnode_t *)leaf;

			do
			{
				if (node->visframe == r_visframecount)
				{
					break;
				}

				node->visframe = r_visframecount;
				node = node->parent;
			}
			while (node);
		}
	}
}
