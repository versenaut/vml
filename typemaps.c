/*
 * Utility routines for mapping strings containing Verse type enums
 * to the corresponding binary values. The contents of this file
 * could to a large extent probably be pre-generated from the verse.h
 * header file. Something for a rainy day, I guess.
 *
 * Written by Emil Brink, Copyright (c) PDC, KTH. This code is licensed
 * under the GPL license, see the COPYING.loader file for details.
*/

#include <stdio.h>
#include <string.h>

#include "verse.h"

typedef struct
{
	const char	*name;
	int		value;
} Entry;

static int lookup(const Entry *map, size_t elems, const char *string)
{
	int	i;

	if(string == NULL)
		return -1;

	for(i = 0; i < elems; i++)
	{
		if(strcmp(map[i].name, string) == 0)
			return map[i].value;
	}
	fprintf(stderr, "** Couldn't map symbol '%s' to a value\n", string);
	return -1;
}

VNodeType node_type_from_string(const char *str)
{
	const Entry map[] = { { "object", V_NT_OBJECT }, { "geometry", V_NT_GEOMETRY }, { "material", V_NT_MATERIAL },
		{ "bitmap", V_NT_BITMAP }, { "text", V_NT_TEXT }, { "curve", V_NT_CURVE }, { "audio", V_NT_AUDIO } 
	};
	return lookup(map, sizeof map / sizeof *map, str);
}

VNOParamType o_method_param_type_from_string(const char *str)
{
#define	PT(n)	{ "VN_O_METHOD_PTYPE_" #n, VN_O_METHOD_PTYPE_##n }
	const Entry map[] = {
		PT(INT8), PT(INT16), PT(INT32), PT(UINT8), PT(UINT16), PT(UINT32),
		PT(REAL32), PT(REAL64), PT(REAL32_VEC2), PT(REAL32_VEC3), PT(REAL32_VEC4),
		   PT(REAL64_VEC2), PT(REAL64_VEC3), PT(REAL64_VEC4),
		   PT(REAL32_MAT4), PT(REAL32_MAT9), PT(REAL32_MAT16),
		   PT(REAL64_MAT4), PT(REAL64_MAT9), PT(REAL64_MAT16),
		   PT(STRING), PT(NODE), PT(LAYER)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef	PT
}

VNGLayerType g_layer_type_from_string(const char *str)
{
#define LT(n,t)	{ n, VN_G_LAYER_##t }
	const Entry map[] = {
		LT("vertex-xyz", VERTEX_XYZ), LT("vertex-uint32", VERTEX_UINT32), LT("vertex-real", VERTEX_REAL),
		LT("polygon-corner-uint32", POLYGON_CORNER_UINT32), LT("polygon-corner-real", POLYGON_CORNER_REAL),
		LT("polygon-face-uint8", POLYGON_FACE_UINT8), LT("polygon-face-uint32", POLYGON_FACE_UINT32), LT("polygon-face-real", POLYGON_FACE_REAL)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef	LT
}

VNMFragmentType m_fragment_type_from_string(const char *str)
{
#define	FT(n, t)	{ n, VN_M_FT_##t }
	const Entry map[] = {
		FT("color", COLOR), FT("light", LIGHT), FT("reflection", REFLECTION),
		FT("transparency", TRANSPARENCY), FT("volume", VOLUME), FT("view", VIEW), FT("geometry", GEOMETRY), FT("texture", TEXTURE), FT("noise", NOISE),
		FT("blender", BLENDER), FT("clamp", CLAMP), FT("matrix", MATRIX), FT("ramp", RAMP), FT("animation", ANIMATION),
		FT("alternative", ALTERNATIVE), FT("output", OUTPUT)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef FT
}

VNMLightType m_light_type_from_string(const char *str)
{
#define	LT(n)	{ "VN_M_LIGHT_" #n, VN_M_LIGHT_##n }
	const Entry map[] = {
		LT(DIRECT), LT(AMBIENT), LT(DIRECT_AND_AMBIENT),
		LT(BACK_DIRECT), LT(BACK_AMBIENT), LT(BACK_DIRECT_AND_AMBIENT)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef LT
}

VNMNoiseType m_noise_type_from_string(const char *str)
{
#define	NT(n)	{ "VN_M_NOISE_" #n, VN_M_NOISE_##n }
	const Entry	map[] = { NT(PERLIN_ZERO_TO_ONE), NT(PERLIN_MINUS_ONE_TO_ONE), NT(POINT_ZERO_TO_ONE), NT(POINT_MINUS_ONE_TO_ONE)};
	return lookup(map, sizeof map / sizeof *map, str);
#undef NT
}

VNMBlendType m_blend_type_from_string(const char *str)
{
#define	BT(n)	{ "VN_M_BLEND_" #n, VN_M_BLEND_##n }
	const Entry	map[] = {
		BT(FADE), BT(ADD), BT(SUBTRACT), BT(MULTIPLY), BT(DIVIDE),
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef BT
}

VNMRampType m_ramp_type_from_string(const char *str)
{
#define	RT(n)	{ "VN_M_RAMP_" #n, VN_M_RAMP_##n }
	const Entry	map[] = {
		RT(SQUARE), RT(LINEAR), RT(SMOOTH)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef	RT
}

VNMRampChannel m_ramp_channel_from_string(const char *str)
{
#define	RC(n)	{ "VN_M_RAMP_" #n, VN_M_RAMP_##n }
	const Entry	map[] = {
		RC(RED), RC(GREEN), RC(BLUE)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef	RC
}

VNBLayerType b_layer_type_from_string(const char *str)
{
#define	LT(n,t)	{ n, VN_B_LAYER_##t }
	const Entry map[] = {
		LT("uint1", UINT1), LT("uint8", UINT8), LT("uint16", UINT16), LT("real32", REAL32), LT("real64", REAL64)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef LT
}

VNABlockType a_block_type_from_string(const char *str)
{
#define	BT(n,t)	{ n, VN_A_BLOCK_##t }
	const Entry map[] = {
		BT("int8", INT8), BT("int16", INT16), BT("int24", INT24), BT("real32", REAL32), BT("real64", REAL64)
	};
	return lookup(map, sizeof map / sizeof *map, str);
#undef BT
}
