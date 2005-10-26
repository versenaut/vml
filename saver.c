/*
 * This the Verse VML saver. It will download the contents of Verse server, and
 * then save it out in the XML-based VML format. It can optionally run periodically,
 * saving out a new "snapshot" every N seconds.
 * 
 * Written by Eskil Steenberg. Copyright (c) 2005 PDC, KTH. This code is licensed
 * under the BSD license, see the COPYING.saver file for details.
*/

#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "verse.h"
#include "enough.h"

static void save_object(FILE *f, ENode *o_node)
{
	static const char *method_types[] = {
		"VN_O_METHOD_PTYPE_INT8", "VN_O_METHOD_PTYPE_INT16", "VN_O_METHOD_PTYPE_INT32",
		"VN_O_METHOD_PTYPE_UINT8", "VN_O_METHOD_PTYPE_UINT16", "VN_O_METHOD_PTYPE_UINT32",
		"VN_O_METHOD_PTYPE_REAL32", "VN_O_METHOD_PTYPE_REAL64",
		"VN_O_METHOD_PTYPE_REAL32_VEC2", "VN_O_METHOD_PTYPE_REAL32_VEC3", "VN_O_METHOD_PTYPE_REAL32_VEC4",
		"VN_O_METHOD_PTYPE_REAL64_VEC2", "VN_O_METHOD_PTYPE_REAL64_VEC3", "VN_O_METHOD_PTYPE_REAL64_VEC4",
		"VN_O_METHOD_PTYPE_REAL32_MAT4", "VN_O_METHOD_PTYPE_REAL32_MAT9", "VN_O_METHOD_PTYPE_REAL32_MAT16",
		"VN_O_METHOD_PTYPE_REAL64_MAT4", "VN_O_METHOD_PTYPE_REAL64_MAT9", "VN_O_METHOD_PTYPE_REAL64_MAT16",
		"VN_O_METHOD_PTYPE_STRING",
		"VN_O_METHOD_PTYPE_NODE", "VN_O_METHOD_PTYPE_LAYER"};
	double tmp[4];
	VNQuat64	rot;
	uint32 time[2], i;
	EObjLink *link;
	uint16 group_id, method_id;

	fprintf(f, "\t<transform>\n");
	fprintf(f, "\t\t<position>");
	e_nso_get_pos(o_node, tmp, NULL, NULL, NULL, NULL, time);
	fprintf(f, "%f %f %f", tmp[0], tmp[1], tmp[2]);
	fprintf(f, "</position>\n");
	fprintf(f, "\t\t<rotation>");
	e_nso_get_rot(o_node, &rot, NULL, NULL, NULL, NULL, time);
	fprintf(f, "%f %f %f %f", rot.x, rot.y, rot.z, rot.w);
	fprintf(f, "</rotation>\n");
	fprintf(f, "\t\t<scale>");
	e_nso_get_scale(o_node, tmp);
	fprintf(f, "%f %f %f", tmp[0], tmp[1], tmp[2]);
	fprintf(f, "</scale>\n");
	fprintf(f, "\t</transform>\n");
	e_nso_get_light(o_node, tmp);
	if(tmp[0] != 0 || tmp[1] != 0 || tmp[2] != 0)
	{
		fprintf(f, "\t<light>\n");
		fprintf(f, "\t\t%f %f %f\n", tmp[0], tmp[1], tmp[2]);
		fprintf(f, "\t</light>\n");
	}
	if(e_nso_get_next_link(o_node, 0) != NULL)
	{
		fprintf(f, "\t<links>\n");
		for(link = e_nso_get_next_link(o_node, 0); link != NULL; link = e_nso_get_next_link(o_node, 1 + e_nso_get_link_id(link)))
			fprintf(f, "\t\t<link node=\"n%u\" label=\"%s\" target=\"%u\"/>\n", e_nso_get_link_node(link), e_nso_get_link_name(link), e_nso_get_link_target_id(link));
		fprintf(f, "\t</links>\n");
	}

	if(e_nso_get_next_method_group(o_node, 0) != (uint16)-1)
	{
		fprintf(f, "\t<methodgroups>\n");
		for(group_id = e_nso_get_next_method_group(o_node, 0); group_id != (uint16)-1 ; group_id = e_nso_get_next_method_group(o_node, group_id + 1))
		{
			fprintf(f, "\t\t<methodgroup name=\"%s\">\n", e_nso_get_method_group(o_node, group_id));
			for(method_id = e_nso_get_next_method(o_node, group_id, 0); method_id != (uint16)-1 ; method_id = e_nso_get_next_method(o_node, group_id, method_id + 1))
			{
				fprintf(f, "\t\t\t<method name=\"%s\">\n", e_nso_get_method(o_node, group_id, method_id));
				for(i = 0; i < e_nso_get_method_param_count(o_node, group_id, method_id); i++)
				{
					fprintf(f, "\t\t\t\t<param name=\"%s\" type=\"%s\"/>\n", e_nso_get_method_param_names(o_node, group_id, method_id)[i], method_types[e_nso_get_method_param_types(o_node, group_id, method_id)[i]]);
				}
				fprintf(f, "\t\t\t</method>\n");
			}
			fprintf(f, "\t\t</methodgroup>\n");
		}
		fprintf(f, "\t</methodgroups>\n");
	}
}

static void save_geometry(FILE *f, ENode *g_node)
{
	static const char *layer_el[] = { "vertex-xyz", "vertex-uint32", "vertex-real",
		"polygon-corner-uint32", "polygon-corner-real", "polygon_face_uint8",
		"polygon-face-uint32", "polygon-face-real" };
	const char	*lt;
	EGeoLayer *layer;
	VNGLayerType	type;
	uint i, vertex_count = 0, poly_count = 0;
	uint *ref;
	egreal *vertex;
	void *data;
	uint16 bone_id;

	fprintf(f, "\t<layers>\n");
	vertex_count = e_nsg_get_vertex_length(g_node);
	poly_count = e_nsg_get_polygon_length(g_node);
	vertex = e_nsg_get_layer_data(g_node, e_nsg_get_layer_by_id(g_node,  0));
	ref = e_nsg_get_layer_data(g_node, e_nsg_get_layer_by_id(g_node,  1));

	for(layer = e_nsg_get_layer_next(g_node, 0); layer != NULL; layer = e_nsg_get_layer_next(g_node, e_nsg_get_layer_id(layer) + 1))
	{
		data = e_nsg_get_layer_data(g_node, layer);
		type = e_nsg_get_layer_type(layer);
		if(type < VN_G_LAYER_POLYGON_CORNER_UINT32)
			lt = layer_el[type];
		else
			lt = layer_el[3 + type - VN_G_LAYER_POLYGON_CORNER_UINT32];	/* Hack, hack. */
		fprintf(f, "\t\t<layer-%s name=\"%s\">\n", lt, e_nsg_get_layer_name(layer));
		switch(e_nsg_get_layer_type(layer))
		{
			case VN_G_LAYER_VERTEX_XYZ :
				for(i = 0; i < vertex_count; i++)
					if(vertex[i * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<v>%u %f %f %f</v>\n", i, ((egreal *)data)[i * 3], ((egreal *)data)[i * 3 + 1], ((egreal *)data)[i * 3 + 2]);
			break;
			case VN_G_LAYER_VERTEX_UINT32 :
				for(i = 0; i < vertex_count; i++)
					if(vertex[i * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<v>%u %u</v>\n", i, ((uint32 *)data)[i]);
			break;
			case VN_G_LAYER_VERTEX_REAL :
				for(i = 0; i < vertex_count; i++)
					if(vertex[i * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<v>%u %f</v>\n", i, ((egreal *)data)[i]);
			break;
			case VN_G_LAYER_POLYGON_CORNER_UINT32 :
				for(i = 0; i < poly_count; i++)
					if(ref[i * 4] < vertex_count && vertex[ref[i * 4] * 3] != E_REAL_MAX &&
						ref[i * 4 + 1] < vertex_count && vertex[ref[i * 4 + 1] * 3] != E_REAL_MAX &&
						ref[i * 4 + 2] < vertex_count && vertex[ref[i * 4 + 2] * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<p>%u %u %u %u</p>\n", ((uint32 *)data)[i * 4], ((uint32 *)data)[i * 4 + 1], ((uint32 *)data)[i * 4 + 2], ((uint32 *)data)[i * 4 + 3]);
			break;
			case VN_G_LAYER_POLYGON_CORNER_REAL :
				for(i = 0; i < poly_count; i++)
					if(ref[i * 4] < vertex_count && vertex[ref[i * 4] * 3] != E_REAL_MAX &&
						ref[i * 4 + 1] < vertex_count && vertex[ref[i * 4 + 1] * 3] != E_REAL_MAX &&
						ref[i * 4 + 2] < vertex_count && vertex[ref[i * 4 + 2] * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<p>%f %f %f %f</p>\n", ((egreal *)data)[i * 4], ((egreal *)data)[i * 4 + 1], ((egreal *)data)[i * 4 + 2], ((egreal *)data)[i * 4 + 3]);
			break;
			case VN_G_LAYER_POLYGON_FACE_UINT8 :
				for(i = 0; i < poly_count; i++)
					if(ref[i * 4] < vertex_count && vertex[ref[i * 4] * 3] != E_REAL_MAX &&
						ref[i * 4 + 1] < vertex_count && vertex[ref[i * 4 + 1] * 3] != E_REAL_MAX &&
						ref[i * 4 + 2] < vertex_count && vertex[ref[i * 4 + 2] * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<p>%u</p>\n", ((uint8 *)data)[i]);
			break;
			case VN_G_LAYER_POLYGON_FACE_UINT32 :
				for(i = 0; i < poly_count; i++)
					if(ref[i * 4] < vertex_count && vertex[ref[i * 4] * 3] != E_REAL_MAX &&
						ref[i * 4 + 1] < vertex_count && vertex[ref[i * 4 + 1] * 3] != E_REAL_MAX &&
						ref[i * 4 + 2] < vertex_count && vertex[ref[i * 4 + 2] * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<p>%u</p>\n", ((uint32 *)data)[i]);
			break;
			case VN_G_LAYER_POLYGON_FACE_REAL :
				for(i = 0; i < poly_count; i++)
					if(ref[i * 4] < vertex_count && vertex[ref[i * 4] * 3] != E_REAL_MAX &&
						ref[i * 4 + 1] < vertex_count && vertex[ref[i * 4 + 1] * 3] != E_REAL_MAX &&
						ref[i * 4 + 2] < vertex_count && vertex[ref[i * 4 + 2] * 3] != E_REAL_MAX)
						fprintf(f, "\t\t\t<p>%f</p>\n", ((egreal *)data)[i]);
			break;
		}
		fprintf(f, "\t\t</layer-%s>\n", lt);
	}
	fprintf(f, "\t</layers>\n");

	fprintf(f, "\t<vertexcrease layer=\"%s\" default=\"%u\"/>\n", e_nsg_get_layer_crease_vertex_name(g_node), e_nsg_get_layer_crease_vertex_value(g_node));
	fprintf(f, "\t<edgecrease layer=\"%s\" default=\"%u\"/>\n", e_nsg_get_layer_crease_edge_name(g_node), e_nsg_get_layer_crease_edge_value(g_node));

	if((bone_id = bone_id = e_nsg_get_bone_next(g_node, 0)) != (uint16) -1)
	{
		fprintf(f, "\t<bones>\n");
		for(; bone_id != (uint16)-1 ; bone_id = e_nsg_get_bone_next(g_node, bone_id + 1))
		{
			real64	 tmp[4];
			VNQuat64 rot;
			fprintf(f, "\t\t<bone id=\"b%u\">\n", bone_id);
			fprintf(f, "\t\t\t<weight>%s</weight>\n", e_nsg_get_bone_weight(g_node, bone_id));
			fprintf(f, "\t\t\t<reference>%s</reference>\n", e_nsg_get_bone_reference(g_node, bone_id));
			if(e_nsg_get_bone_parent(g_node, bone_id) != (uint16) ~0u)
				fprintf(f, "\t\t\t<parent>b%u</parent>\n", e_nsg_get_bone_parent(g_node, bone_id));
			else
				fprintf(f, "\t\t\t<parent/>\n");
			fprintf(f, "\t\t\t<pos>");
			e_nsg_get_bone_pos64(g_node, bone_id, tmp);
			fprintf(f, "%f %f %f", tmp[0], tmp[1], tmp[2]);
			fprintf(f, "</pos>\n");
			fprintf(f, "\t\t\t<pos-label>%s</pos-label>\n", e_nsg_get_bone_pos_label(g_node, bone_id));
			fprintf(f, "\t\t\t<rot>");
			e_nsg_get_bone_rot64(g_node, bone_id, &rot);
			fprintf(f, "%f %f %f %f", rot.x, rot.y, rot.z, rot.w);
			fprintf(f, "</rot>\n");
			fprintf(f, "\t\t\t<rot-label>%s</rot-label>\n", e_nsg_get_bone_rot_label(g_node, bone_id));
			fprintf(f, "\t\t</bone>\n");
		}
		fprintf(f, "\t</bones>\n");
	}
}

static const char * m_link_to_element(const char *indent, const char *name, VNMFragmentID id)
{
	static char	buf[8][32];
	static int	next = 0;

	next = (next + 1) % (sizeof buf / sizeof *buf);
	if(id != (VNMFragmentID) ~0u)
		sprintf(buf[next], "%s<%s>f%u</%s>\n", indent, name, id, name);
	else
		return "";
	return buf[next];
}

static void save_material(FILE *f, ENode *m_node)
{
	static const char *light_type[] = {"VN_M_LIGHT_DIRECT", "VN_M_LIGHT_AMBIENT", "VN_M_LIGHT_DIRECT_AND_AMBIENT", "VN_M_LIGHT_BACK_DIRECT", "VN_M_LIGHT_BACK_AMBIENT", "VN_M_LIGHT_BACK_DIRECT_AND_AMBIENT"};
	static const char *noise_type[] = {"VN_M_NOISE_PERLIN_ZERO_TO_ONE", "VN_M_NOISE_PERLIN_MINUS_ONE_TO_ONE"};
	static const char *ramp_type[] = {"VN_M_RAMP_SQUARE", "VN_M_RAMP_LINEAR", "VN_M_RAMP_SMOOTH"};
	static const char *ramp_channel[] = {"VN_M_RAMP_RED", "VN_M_RAMP_GREEN", "VN_M_RAMP_BLUE"};
	static const char *blend_type[] = {"VN_M_BLEND_FADE", "VN_M_BLEND_ADD", "VN_M_BLEND_SUBTRACT", "VN_M_BLEND_MULTIPLY", "VN_M_BLEND_DIVIDE", "VN_M_BLEND_DOT"};
/*	static const char *frag_names[] = {"VN_M_FT_COLOR", "VN_M_FT_LIGHT", "VN_M_FT_REFLECTION", "VN_M_FT_TRANSPARENCY", "VN_M_FT_VOLUME", "VN_M_FT_GEOMETRY", "VN_M_FT_TEXTURE", "VN_M_FT_NOISE", "VN_M_FT_BLENDER", "VN_M_FT_MATRIX", "VN_M_FT_RAMP", "VN_M_FT_ANIMATION", "VN_M_FT_ALTERNATIVE", "VN_M_FT_OUTPUT"};*/
	static const char *frag_el[] = { "color", "light", "reflection", "transparency", "volume", "geometry", "texture", "noise", "blender", "matrix", "ramp", "animation", "alternative", "output" };
	VMatFrag *frag;
	VNMFragmentID id;
	uint i;

	fprintf(f, "\t<fragments>\n");
	for(id = e_nsm_get_fragment_next(m_node, 0); id != (uint16)-1 ; id = e_nsm_get_fragment_next(m_node, id + 1))
	{
		frag = e_nsm_get_fragment(m_node, id);
		fprintf(f, "\t\t<fragment-%s id=\"f%u\">\n", frag_el[e_nsm_get_fragment_type(m_node, id)], id);
		switch(e_nsm_get_fragment_type(m_node, id))
		{
			case VN_M_FT_COLOR :
				fprintf(f, "\t\t\t<color>%f %f %f</color>\n", 
					frag->color.red, 
					frag->color.green, 
					frag->color.blue);
			break;
			case VN_M_FT_LIGHT :
				fprintf(f,
					"\t\t\t<type>%s</type>\n"
					"\t\t\t<normal_falloff>%f</normal_falloff>\n", 
					light_type[frag->light.type],
					frag->light.normal_falloff);
				if(frag->light.brdf != ~0u)
				{
					fprintf(f,
						"\t\t\t<brdf>n%u</brdf>\n"
						"\t\t\t<brdf_r>%s</brdf_r>\n"
						"\t\t\t<brdf_g>%s</brdf_g>\n"
						"\t\t\t<brdf_b>%s</brdf_b>\n",
						frag->light.brdf,
						frag->light.brdf_r,
						frag->light.brdf_g,
						frag->light.brdf_b);
				}
			break;
			case VN_M_FT_REFLECTION :
				fprintf(f, "\t\t\t<normal_falloff>%f</normal_falloff>\n",
					frag->reflection.normal_falloff);
			break;
			case VN_M_FT_TRANSPARENCY :
				fprintf(f,
					"\t\t\t<normal_falloff>%f</normal_falloff>\n"
					"\t\t\t<refraction_index>%f</refraction_index>\n",
					frag->transparency.normal_falloff, frag->transparency.refraction_index);
			break;
			case VN_M_FT_VOLUME :
				fprintf(f,
					"\t\t\t<diffusion>%f</diffusion>\n"
					"\t\t\t<col>%f %f %f</col>\n"
					"%s",
					frag->volume.diffusion,
					frag->volume.col_r, frag->volume.col_g, frag->volume.col_b,
					m_link_to_element("\t\t\t", "color", frag->volume.color));
			break;
			case VN_M_FT_GEOMETRY :
				fprintf(f,
					"\t\t\t<layer_r>%s</layer_r>\n"
					"\t\t\t<layer_g>%s</layer_g>\n"
					"\t\t\t<layer_b>%s</layer_b>\n",
					frag->geometry.layer_r,
					frag->geometry.layer_g,
					frag->geometry.layer_b);
			break;
			case VN_M_FT_TEXTURE :
				if(frag->texture.bitmap != ~0u)
					fprintf(f, "\t\t\t<bitmap>n%u</bitmap>\n", frag->texture.bitmap);
				fprintf(f,
					"\t\t\t<layer_r>%s</layer_r>\n"
					"\t\t\t<layer_g>%s</layer_g>\n"
					"\t\t\t<layer_b>%s</layer_b>\n"
					"%s",
					frag->texture.layer_r,
					frag->texture.layer_g,
					frag->texture.layer_b,
					m_link_to_element("\t\t\t", "mapping", frag->texture.mapping));
			break;
			case VN_M_FT_NOISE :
				fprintf(f,
					"\t\t\t<type>%s</type>\n"
					"%s",
					noise_type[frag->noise.type],
					m_link_to_element("\t\t\t", "mapping", frag->noise.mapping));
			break;
			case VN_M_FT_BLENDER :
				fprintf(f,
					"\t\t\t<type>%s</type>\n"
					"%s"
					"%s"
					"%s",
					blend_type[frag->blender.type],
					m_link_to_element("\t\t\t", "data_a", frag->blender.data_a),
					m_link_to_element("\t\t\t", "data_b", frag->blender.data_b),
					m_link_to_element("\t\t\t", "control", frag->blender.control));
			break;
			case VN_M_FT_MATRIX :
					fprintf(f, "%s", m_link_to_element("\t\t\t", "data", frag->matrix.data));
				fprintf(f, "\t\t\t<matrix>\n");
				fprintf(f, "\t\t\t\t%f %f %f %f\n", frag->matrix.matrix[0], frag->matrix.matrix[1], frag->matrix.matrix[2], frag->matrix.matrix[3]);
				fprintf(f, "\t\t\t\t%f %f %f %f\n", frag->matrix.matrix[4], frag->matrix.matrix[5], frag->matrix.matrix[6], frag->matrix.matrix[7]);
				fprintf(f, "\t\t\t\t%f %f %f %f\n", frag->matrix.matrix[8], frag->matrix.matrix[9], frag->matrix.matrix[10], frag->matrix.matrix[11]);
				fprintf(f, "\t\t\t\t%f %f %f %f\n", frag->matrix.matrix[12], frag->matrix.matrix[13], frag->matrix.matrix[14], frag->matrix.matrix[15]);
				fprintf(f, "\t\t\t</matrix>\n");
			break;
			case VN_M_FT_RAMP :
				fprintf(f,
					"\t\t\t<type>%s</type>\n"
					"\t\t\t<channel>%s</channel>\n"
					"%s",
					ramp_type[frag->ramp.type],
					ramp_channel[frag->ramp.channel],
					m_link_to_element("\t\t\t", "mapping", frag->ramp.mapping));
				fprintf(f, "\t\t\t<ramp>\n");
				for(i = 0; i < frag->ramp.point_count; i++)
					fprintf(f, "\t\t\t\t<ramppoint pos=\"%g\">%f %f %f</ramppoint>\n",
						frag->ramp.ramp[i].pos,
						frag->ramp.ramp[i].red,
						frag->ramp.ramp[i].green,
						frag->ramp.ramp[i].blue);
				fprintf(f, "\t\t\t</ramp>\n");
			break;
			case VN_M_FT_ANIMATION :
				fprintf(f, "\t\t\t<label>%s</label>\n", frag->animation.label);
			break;
			case VN_M_FT_ALTERNATIVE :
				fprintf(f,
					"%s"
					"%s",
					m_link_to_element("\t\t\t", "alt_a", frag->alternative.alt_a),
					m_link_to_element("\t\t\t", "alt_b", frag->alternative.alt_b));
			break;
			case VN_M_FT_OUTPUT :
				fprintf(f,
					"\t\t\t<label>%s</label>\n"
					"%s"
					"%s",
					frag->output.label,
					m_link_to_element("\t\t\t", "front", frag->output.front),
					m_link_to_element("\t\t\t", "back", frag->output.back));
			break;
		}
		fprintf(f, "\t\t</fragment-%s>\n", frag_el[e_nsm_get_fragment_type(m_node, id)]);
	}
	fprintf(f, "\t</fragments>\n");
}

static void tile_get(VNBTile *tile, int x, int y, int z, const void *data, VNBLayerType type, const uint *size)
{
	uint32	wt = (size[0] + VN_B_TILE_SIZE - 1) / VN_B_TILE_SIZE, ht = (size[1] + VN_B_TILE_SIZE - 1) / VN_B_TILE_SIZE,
		tw = x == (wt - 1) && (size[0] % VN_B_TILE_SIZE) != 0 ? size[0] % VN_B_TILE_SIZE : VN_B_TILE_SIZE,
		th = y == (ht - 1) && (size[1] % VN_B_TILE_SIZE) != 0 ? size[1] % VN_B_TILE_SIZE : VN_B_TILE_SIZE,
		zoff, tx, ty, put, get;

	zoff = z * size[0] * size[1];	/* Z offset, in pixels. */

	memset(tile, 0, sizeof *tile);
	for(ty = 0; ty < th; ty++)
	{
		get = zoff + (y * VN_B_TILE_SIZE + ty) * size[0] + x * VN_B_TILE_SIZE;
		put = ty * VN_B_TILE_SIZE;	/* Reset for each row, as they might be short. */
		for(tx = 0; tx < tw; tx++, put++, get++)
		{
			if(type == VN_B_LAYER_UINT1)
			{
				size_t	rw = (size[0] + 7) / 8;

				zoff = size[1] * rw * z;
				put = ty;
				tile->vuint1[put] = ((uint8 *) data)[zoff + (y * VN_B_TILE_SIZE + ty) * rw + x];
				break;	/* Don't need to loop in X dimension, above copies all pixels. */
			}
			else if(type == VN_B_LAYER_UINT8)
				tile->vuint8[put] = ((uint8 *) data)[get];
			else if(type == VN_B_LAYER_UINT16)
				tile->vuint16[put] = ((uint16 *) data)[get];
			else if(type == VN_B_LAYER_REAL32)
				tile->vreal32[put] = ((real32 *) data)[get];
			else if(type == VN_B_LAYER_REAL64)
				tile->vreal64[put] = ((real64 *) data)[get];
		}
	}
}

static void save_bitmap(FILE *f, ENode *b_node)
{
/*	static const char *layer_type[] = {"VN_B_LAYER_UINT1", "VN_B_LAYER_UINT8", "VN_B_LAYER_UINT16", "VN_B_LAYER_REAL32", "VN_B_LAYER_REAL64"};*/
	const char *layer_el[] = { "uint1", "uint8", "uint16", "real32", "real64" };
	EBitLayer *layer;
	uint size[3], i, j, k, tiles[2], tx, ty;
	void *data;
	VNBTile	tile;
	VNBLayerType	type;

	e_nsb_get_size(b_node, &size[0], &size[1], &size[2]);
	fprintf(f, "\t<dimensions>%u %u %u</dimensions>\n", size[0], size[1], size[2]);
	fprintf(f, "\t<layers>\n");
	
	tiles[0] = (size[0] + VN_B_TILE_SIZE - 1) / VN_B_TILE_SIZE;
	tiles[1] = (size[1] + VN_B_TILE_SIZE - 1) / VN_B_TILE_SIZE;
	
	for(layer = e_nsb_get_layer_next(b_node, 0); layer != NULL; layer = e_nsb_get_layer_next(b_node, e_nsb_get_layer_id(layer) + 1))
	{
		fprintf(f, "\t\t<layer-%s name=\"%s\">\n", layer_el[e_nsb_get_layer_type(layer)], e_nsb_get_layer_name(layer));
		fprintf(f, "\t\t<tiles>\n");
		data = e_nsb_get_layer_data(b_node, layer);
		type = e_nsb_get_layer_type(layer);

		for(i = 0; i < size[2]; i++)
		{
			for(j = 0; j < tiles[1]; j++)
			{
				for(k = 0; k < tiles[0]; k++)
				{
					fprintf(f, "\t\t<tile tile_x=\"%u\" tile_y=\"%u\" tile_z=\"%u\">\n", k, j, i);
					tile_get(&tile, k, j, i, data, e_nsb_get_layer_type(layer), size);
					for(ty = 0; ty < VN_B_TILE_SIZE; ty++)
					{
						fprintf(f, "\t\t");
						for(tx = 0; tx < VN_B_TILE_SIZE; tx++)
						{
							if(type == VN_B_LAYER_UINT1)
								fprintf(f, " %c", tile.vuint1[ty * VN_B_TILE_SIZE / CHAR_BIT] & (1 << (CHAR_BIT - tx - 1)) ? '1' : '0');
							else if(type == VN_B_LAYER_UINT8)
								fprintf(f, " %u", tile.vuint8[ty * VN_B_TILE_SIZE + tx]);
							else if(type == VN_B_LAYER_UINT16)
								fprintf(f, " %u", tile.vuint16[ty * VN_B_TILE_SIZE + tx]);
							else if(type == VN_B_LAYER_REAL32)
								fprintf(f, " %g", tile.vreal32[ty * VN_B_TILE_SIZE + tx]);
							else if(type == VN_B_LAYER_REAL64)
								fprintf(f, " %g", tile.vreal64[ty * VN_B_TILE_SIZE + tx]);
						}
						fprintf(f, "\n");
					}
					fprintf(f, "\t\t</tile>\n");
				}
			}
		}
		fprintf(f, "\t\t</tiles>\n");
		fprintf(f, "\t\t</layer-%s>\n", layer_el[e_nsb_get_layer_type(layer)]);
	}
	fprintf(f, "\t</layers>\n");
}

static void save_text(FILE *f, ENode *t_node)
{
	ETextBuffer *buffer;

	fprintf(f, "\t<language>%s</language>\n", e_nst_get_language(t_node));
	fprintf(f, "\t<buffers>\n");
	for(buffer = e_nst_get_buffer_next(t_node, 0); buffer != NULL; buffer = e_nst_get_buffer_next(t_node, e_nst_get_buffer_id(buffer) + 1))
	{
		const char	*text = e_nst_get_buffer_data(t_node, buffer), *eptr, *p;
		size_t		len = e_nst_get_buffer_data_length(t_node, buffer);

		fprintf(f, "\t<buffer name=\"%s\">\n<![CDATA[", e_nst_get_buffer_name(buffer));
		/* Go through the text and escape any occurance of "]]>" into "]]&gt;". Leave the rest as-is, in CDATA cozyness. */
		for(eptr = text + len; text < eptr;)
		{
			if((p = strstr(text, "]]>")) != NULL)
			{
				fwrite(text, p - text, 1, f);
				fprintf(f, "]]&gt;");
				text = p + 3;
			}
			else
			{
				fwrite(text, len, 1, f);
				text += len;
			}
		}
		fprintf(f, "]]>\t</buffer>\n");
	}
	fprintf(f, "\t</buffers>\n");
}

static void save_curve(FILE *f, ENode *c_node)
{
	ECurve *curve;
	real64 pre_value[4];
	uint32 pre_pos[4];
	real64 value[4];
	real64 pos;
	real64 post_value[4];
	uint32 post_pos[4], dim;
	uint i, j;

	curve = e_nsc_get_curve_next(c_node, 0);
	if(curve == NULL)
		return;
	fprintf(f, "\t<curves>\n");
	for(; curve != NULL; curve = e_nsc_get_curve_next(c_node, e_nsc_get_curve_id(curve) + 1))
	{
		fprintf(f, "\t\t<curve-%ud name=\"%s\">\n", e_nsc_get_curve_dimensions(curve), e_nsc_get_curve_name(curve));
		dim = e_nsc_get_curve_dimensions(curve);
		for(i = e_nsc_get_point_next(curve, 0); i != -1; i = e_nsc_get_point_next(curve, i + 1))
		{
			e_nsc_get_point(curve, i, pre_value, pre_pos, value, &pos, post_value, post_pos);
			fprintf(f, "\t\t\t<key pos=\"%g\">\n", pos);
			fprintf(f, "\t\t\t\t<pre-value>");
			for(j = 0; j < dim; j++)
				fprintf(f, "%f%s", pre_value[j], (j == dim - 1) ? "" : " ");
			fprintf(f, "</pre-value>\n");
			fprintf(f, "\t\t\t\t<pre-pos>");
			for(j = 0; j < dim; j++)
				fprintf(f, "%u%s", pre_pos[j], (j == dim - 1) ? "" : " ");
			fprintf(f, "</pre-pos>\n");
			fprintf(f, "\t\t\t\t<value>");
			for(j = 0; j < dim; j++)
				fprintf(f, "%f%s", value[j], (j == dim - 1) ? "" : " ");
			fprintf(f, "</value>\n");
			fprintf(f, "\t\t\t\t<post-value>");
			for(j = 0; j < dim; j++)
				fprintf(f, "%f%s", post_value[j], (j == dim - 1) ? "" : " ");
			fprintf(f, "</post-value>\n");
			fprintf(f, "\t\t\t\t<post-pos>");
			for(j = 0; j < dim; j++)
				fprintf(f, "%u%s", post_pos[j], (j == dim - 1) ? "" : " ");
			fprintf(f, "</post-pos>\n");
			fprintf(f, "\t\t\t</key>\n");
		}
		fprintf(f, "\t\t</curve-%ud>\n", e_nsc_get_curve_dimensions(curve));
	}
	fprintf(f, "\t</curves>\n");
}

static void save_node(FILE *f, ENode *node)
{
	static const char *node_el[] = { "node-object", "node-geometry", "node-material", "node-bitmap", "node-text", "node-curve", "node-audio" };
/*	static const char *node_type[] = {"V_NT_OBJECT", "V_NT_GEOMETRY", "V_NT_MATERIAL", "V_NT_BITMAP", "V_NT_TEXT", "V_NT_CURVE", "V_NT_AUDIO"};*/
/*	static const char *tag_type[] = {"VN_TAG_BOOLEAN", "VN_TAG_UINT32", "VN_TAG_REAL64", "VN_TAG_STRING", "VN_TAG_REAL64_VEC3", "VN_TAG_LINK", "VN_TAG_ANIMATION", "VN_TAG_BLOB", "VN_TAG_TYPE_COUNT"};*/
	static const char *tag_el[] = { "boolean", "uint32", "real64", "string", "real64-vec3", "link", "animation", "blob" };
	uint16 group_id, tag_id;
	uint i;
	VNTag *tag;

	fprintf(f, "<%s id=\"n%u\" name=\"%s\">\n", node_el[e_ns_get_node_type(node)], e_ns_get_node_id(node), e_ns_get_node_name(node));
	if(e_ns_get_next_tag_group(node, 0) != (uint16)-1)
	{
		fprintf(f, "\t<tags>\n");

		for(group_id = e_ns_get_next_tag_group(node, 0); group_id != (uint16)-1 ; group_id = e_ns_get_next_tag_group(node, group_id + 1))
		{
			fprintf(f, "\t\t<taggroup name=\"%s\">\n", e_ns_get_tag_group(node, group_id));
			for(tag_id = e_ns_get_next_tag(node, group_id, 0); tag_id != (uint16)-1 ; tag_id = e_ns_get_next_tag(node, group_id, tag_id + 1))
			{
				fprintf(f, "\t\t\t<tag-%s name=\"%s\">", tag_el[e_ns_get_tag_type(node, group_id, tag_id)], e_ns_get_tag_name(node, group_id, tag_id));
				tag = e_ns_get_tag(node, group_id, tag_id);
				switch(e_ns_get_tag_type(node, group_id, tag_id))
				{
					case VN_TAG_BOOLEAN :
						fprintf(f, "%s", tag->vboolean ? "true" : "false");
					break;
					case VN_TAG_UINT32 :
						fprintf(f, "%u", tag->vuint32);
					break;
					case VN_TAG_REAL64 :
						fprintf(f, "%f", tag->vreal64);
					break;
					case VN_TAG_STRING :
						fprintf(f, "%s", tag->vstring);
					break;
					case VN_TAG_REAL64_VEC3 :
						fprintf(f, "%f %f %f", tag->vreal64_vec3[0], tag->vreal64_vec3[1], tag->vreal64_vec3[2]);
					break;
					case VN_TAG_LINK :
						fprintf(f, "n%u", tag->vlink);
					break;
					case VN_TAG_ANIMATION:
						fprintf(f, "<curve>n%u</curve><start>%u</start><end>%u</end>", tag->vanimation.curve, tag->vanimation.start, tag->vanimation.end);
					break;
					case VN_TAG_BLOB :
						for(i = 0; i < tag->vblob.size; i++)
						{
							fprintf(f, "%u ", ((uint8 *)tag->vblob.blob)[i]);
							if(((i + 1) % 32) == 0)
								fprintf(f, "\n\t\t\t\t");
						}
						fprintf(f, "\n\t\t\t");
					break;
				default:
					;
				}
				fprintf(f, "</tag-%s>\n", tag_el[e_ns_get_tag_type(node, group_id, tag_id)]);
			}
			fprintf(f, "\t\t</taggroup>\n");
		}
		fprintf(f, "\t</tags>\n");
	}
	switch(e_ns_get_node_type(node))
	{
		case V_NT_OBJECT :
			save_object(f, node);
		break;
		case V_NT_GEOMETRY :
			save_geometry(f, node);
		break;
		case V_NT_MATERIAL :
			save_material(f, node);
		break;
		case V_NT_BITMAP :
			save_bitmap(f, node);
		break;
		case V_NT_TEXT :
			save_text(f, node);
		break;
		case V_NT_CURVE :
			save_curve(f, node);
		break;
		case V_NT_AUDIO :
		break;
		default:
			;
	}
	fprintf(f, "</%s>\n\n", node_el[e_ns_get_node_type(node)]);
}

static void save_data(FILE *f)
{
	ENode *node;
	uint i;

	fprintf(f, "<?xml version=\"1.0\" encoding=\"latin1\"?>\n\n");
	fprintf(f, "<vml version=\"1.0\">\n\n");
	for(i = 0; i < V_NT_NUM_TYPES; i++)
		for(node = e_ns_get_node_next(0, 0, i); node != NULL; node = e_ns_get_node_next(e_ns_get_node_id(node) + 1, 0, i))
			save_node(f, node);
	fprintf(f, "</vml>\n\n");
	fclose(f);
}

static void download_data(void)
{
	void *layer, *buffer;
	ENode *node;

	for(node = e_ns_get_node_next(0, 0, V_NT_GEOMETRY); node != NULL; node = e_ns_get_node_next(e_ns_get_node_id(node) + 1, 0, V_NT_GEOMETRY))
		for(layer = e_nsg_get_layer_next(node, 0); layer != NULL; layer = e_nsg_get_layer_next(node, e_nsg_get_layer_id(layer) + 1))
			e_nsg_get_layer_data(node, layer);
	for(node = e_ns_get_node_next(0, 0, V_NT_BITMAP); node != NULL; node = e_ns_get_node_next(e_ns_get_node_id(node) + 1, 0, V_NT_BITMAP))
	{
		for(layer = e_nsb_get_layer_next(node, 0); layer != NULL; layer = e_nsb_get_layer_next(node, e_nsb_get_layer_id(layer) + 1))
			e_nsb_get_layer_data(node, layer);
	}
	for(node = e_ns_get_node_next(0, 0, V_NT_TEXT); node != NULL; node = e_ns_get_node_next(e_ns_get_node_id(node) + 1, 0, V_NT_TEXT))
		for(buffer = e_nst_get_buffer_next(node, 0); buffer != NULL; buffer = e_nst_get_buffer_next(node, e_nst_get_buffer_id(buffer) + 1))
			e_nst_get_buffer_data(node, buffer);
}

static const char * find_param(int argc, char **argv, const char *option, const char *default_text)
{
	int i;

	for(i = 1; i < argc - 1; i++)
		if(strcmp(argv[i], option) == 0)
			return argv[i + 1];
	return default_text;
}

int main(int argc, char **argv)
{
	uint32 i, seconds, s, interval;
	const char *name, *pass, *address, *file, *tmp;
	FILE *f;
	int	repeat = 0;

	enough_init();
	name = find_param(argc, argv, "-n", "saver");
	pass = find_param(argc, argv, "-p", "pass");
	address = find_param(argc, argv, "-a", "localhost");
	file = find_param(argc, argv, "-f", "dump.vml");
	repeat = find_param(argc, argv, "-r", NULL) != NULL;
	tmp = find_param(argc, argv, "-i", "10");
	if(tmp != NULL)
	{
		interval = strtoul(tmp, NULL, 10);
		printf("Waiting %u seconds until save\n", interval);
	}

	for(i = 1; i < argc; i++)
	{
		if(strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "help") == 0)
		{
			printf("Usage: verse_saver <pamams>\n");
			printf("Options:\n");
			printf("-h This text.\n");
			printf("-n <name>\n");
			printf("-p <pass>\n");
			printf("-a <address>\n");
			printf("-f <filename>\n");
			printf("-i <save interval in seconds>\n");
			printf("-1 Save only once, then exit.\n");
			return FALSE;
		}
	}
	if(e_vc_connect(address, name, pass, NULL) == -1)
	{
		printf("Error: Invalid server address '%s'\n", address);
		return TRUE;
	}
	printf("Connecting to %s\n", address);
	while(interval != 0)
	{
		verse_session_get_time(&seconds, NULL);
		s = seconds;
		while(seconds < s + interval/* && verse_session_get_size() == 0*/)
		{
			verse_callback_update(1000);
			download_data();
			verse_session_get_time(&seconds, NULL);
		}
		printf("Done waiting, beginning save\n");
		if((f = fopen(file, "w")) != NULL)
			save_data(f);
		printf("Save complete\n");
		if(!repeat)
			break;
	}
	return TRUE;
}
