/*
 * Load Verse node data in the VML (XML-based) format, and issue the required
 * commands to create that data on a Verse host.
 * 
 * Written by Emil Brink, Copyright (c) PDC, KTH. This code is licensed
 * under the GPL license, see the COPYING.loader file for details.
*/

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynstr.h"
#include "list.h"
#include "mem.h"
#include "xmlnode.h"

#include "verse.h"

#include "typemaps.h"

typedef enum
{
	PEND_NONE = 0, PEND_CONNECT, PEND_NODE_CREATE, PEND_TAGGROUP_CREATE,
	PEND_METHODGROUP_CREATE,
	PEND_LAYER_CREATE, PEND_FRAGMENT_CREATE, PEND_CURVE_CREATE, PEND_BUFFER_CREATE
} Pending;

typedef struct {
	char	name[32];
	uint32	id;		/* Less casting, this way. */
} Entry;

typedef struct {
	List	*entries;		/* List of Entry. */
} Dict;

typedef struct
{
	char		name[32];
	VLayerID	id;	/* ~0 until callback. */
} LayerID;

typedef struct
{
	List		*files;		/* Files as loaded, top-level XmlNode from each. */
	List		*file_iter;	/* Iterator over files. */
	List		*file_nodes;	/* Children of <vml> element, re-sorted for better uploading. */
	const List	*iter;		/* Iterator over file_nodes. */
	int		skip_objects;	/* How do we react to object nodes in step()? Skip, or process. */
	VNodeType	type;
	const XmlNode	*node;
	VNodeID		node_id;
	Pending		pending;
	int		pend_count;
	Dict		tag_groups;
	Dict		layer_ids;	/* Temporary structure for tracking layers. */

	VNMFragmentID	*fragment_map;
	size_t		fragment_map_size;

	VNodeID		*node_map;
	size_t		node_map_size;

	VNodeID		avatar;

	int		log_level;
} MainInfo;

/* ----------------------------------------------------------------------------------------- */

static void message(const MainInfo *min, int level, const char *fmt, ...)
{
	va_list	arg;

	if(min->log_level < level)
		return;
	va_start(arg, fmt);
	vprintf(fmt, arg);
	va_end(arg);
}

/* ----------------------------------------------------------------------------------------- */


static void dict_ctor(Dict *dict)
{
	dict->entries = NULL;
}

static void dict_clear(Dict *dict)
{
	List	*iter;

	for(iter = dict->entries; iter != NULL; iter = list_next(iter))
		mem_free(list_data(iter));
	list_destroy(dict->entries);
	dict->entries = NULL;
}

static Entry * dict_find(const Dict *dict, const char *name)
{
	List	*iter;

	for(iter = dict->entries; iter != NULL; iter = list_next(iter))
	{
		if(strcmp(name, ((Entry *) list_data(iter))->name) == 0)
			return list_data(iter);
	}
	return NULL;
}

static void dict_set(Dict *dict, const char *name, uint32 id)
{
	Entry	*e;

	if((e = dict_find(dict, name)) == NULL)
	{
		e = mem_alloc(sizeof *e);
		strcpy(e->name, name);
		dict->entries = list_prepend(dict->entries, e);
	}
	e->id = id;
}

static uint32 dict_get(const Dict *dict, const char *name)
{
	Entry	*e;

	if((e = dict_find(dict, name)) != NULL)
		return e->id;
	return ~0;
}

/* ----------------------------------------------------------------------------------------- */

static void layer_id_clear(MainInfo *min)
{
	dict_clear(&min->layer_ids);
}

static void layer_id_set(MainInfo *min, const char *name, VLayerID id)
{
	dict_set(&min->layer_ids, name, id);
	message(min, 3, "layer id for '%s' set to %u\n", name, id);
}

static VLayerID layer_id_get(const MainInfo *min, const char *name)
{
	return dict_get(&min->layer_ids, name);
}

/* ----------------------------------------------------------------------------------------- */

static void fragment_map_clear(MainInfo *min)
{
	mem_free(min->fragment_map);
	min->fragment_map = NULL;
	min->fragment_map_size = 0u;
}

static void fragment_map_store(MainInfo *min, VNMFragmentID local, VNMFragmentID remote)
{
	if(local >= min->fragment_map_size)
	{
		min->fragment_map = mem_realloc(min->fragment_map, (1 + local) * sizeof *min->fragment_map);
		min->fragment_map_size = local + 1;
	}
	min->fragment_map[local] = remote;
}

static VNMFragmentID fragment_map_get(const MainInfo *min, VNMFragmentID local)
{
	if(local > min->fragment_map_size)
		return (VNMFragmentID) ~0u;
	return min->fragment_map[local];
}

/* ----------------------------------------------------------------------------------------- */

static void pend_add(MainInfo *min, Pending what, int counting)
{
	if(what != min->pending)
		min->pend_count = 0;
	min->pending = what;
	if(counting)
		min->pend_count++;
	message(min, 4, "pend_add(): count=%u what=%d\n", min->pend_count, what);
}

static void pend_sub(MainInfo *min)
{
	message(min, 4, "in pend_sub(): count=%u\n", min->pend_count);
	if(min->pend_count > 0)
	{
		if(--min->pend_count > 0)
			return;
	}
	min->pending = PEND_NONE;
}

static void node_map_set(MainInfo *min, VNodeID local, VNodeID remote)
{
	if(local >= min->node_map_size)
	{
		min->node_map = mem_realloc(min->node_map, (local + 1) * sizeof *min->node_map);
		min->node_map_size = local + 1;
	}
	min->node_map[local] = remote;	
}

static real64 child_get_uint32(const XmlNode *node, const char *name, uint32 def)
{
	const char	*v;

	if((v = xmlnode_eval_single(node, name)) != NULL)
	{
		uint32	x;

		if(sscanf(v, "%u", &x) == 1)
			return x;
	}
	return def;
}

static real64 child_get_real64(const XmlNode *node, const char *name, real64 def)
{
	const char	*v;

	if((v = xmlnode_eval_single(node, name)) != NULL)
	{
		real64	x;

		if(sscanf(v, "%lg", &x) == 1)
			return x;
	}
	return def;
}

static void child_get_string(char *string, size_t max, const XmlNode *node, const char *child)
{
	const char	*get = xmlnode_eval_single(node, child);

	if(max == 0)
		return;
	max--;
	if(get != NULL)
	{
		for(; *get != '\0' && max > 0; max--)
			*string++ = *get++;
	}
	*string = '\0';
	return;
}

static uint32 attrib_get_uint32(const XmlNode *node, const char *name, uint32 def)
{
	const char	*v;

	if((v = xmlnode_attrib_get_value(node, name)) != NULL)
	{
		char	*eptr;
		uint32	val;

		val = strtoul(v, &eptr, 10);
		if(eptr > v)
			return val;
	}
	return def;
}

static uint32 attrib_get_ref(const XmlNode *node, const char *name, char prefix, uint32 def)
{
	const char	*v;

	if((v = xmlnode_attrib_get_value(node, name)) != NULL)
	{
		if(*v == prefix)
		{
			char	*eptr;
			uint32	val;

			val = strtoul(v + 1, &eptr, 10);
			if(eptr > v + 1)
				return val;
		}
	}
	return def;
}

static real64 attrib_get_real64(const XmlNode *node, const char *name, real64 def)
{
	const char	*v;

	if((v = xmlnode_attrib_get_value(node, name)) != NULL)
	{
		char	*eptr;
		real64	val;

		val = strtod(v, &eptr);
		if(eptr > v)
			return val;
	}
	return def;
}

static uint32 child_get_ref(const XmlNode *node, const char *name, char prefix, uint32 def)
{
	const char	*v;

	if((v = xmlnode_eval_single(node, name)) != NULL)
	{
		if(*v == prefix)
		{
			char	*eptr;
			uint32	val;

			val = strtoul(v + 1, &eptr, 10);
			if(eptr > v + 1)
				return val;
		}
	}
	return def;
}

static int process_common(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here);

	if(strcmp(el, "tags") == 0)
	{
		List	*groups, *iter;

		groups = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("taggroup"), XMLNODE_DONE);
		for(iter = groups; iter != NULL; iter = list_next(iter))
		{
			verse_send_tag_group_create(min->node_id, (uint16) ~0u, xmlnode_attrib_get_value(list_data(iter), "name"));
			pend_add(min, PEND_TAGGROUP_CREATE, 1);
		}
		list_destroy(groups);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strcmp(el, "taggroup") == 0)
	{
		const char	*name = xmlnode_attrib_get_value(here, "name");
		uint32		id;

		id = dict_get(&min->tag_groups, name);
		if(id != (uint32) ~0u)
		{
			List	*tags, *iter;

			tags = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("tag-"), XMLNODE_DONE);
			for(iter = tags; iter != NULL; iter = list_next(iter))
			{
				const char	*name = xmlnode_attrib_get_value(list_data(iter), "name"),
						*type = xmlnode_get_name(list_data(iter)) + 4,
						*value = xmlnode_eval_single(list_data(iter), "");
				VNTag		tag;

				if(strcmp(type, "boolean") == 0)
				{
					tag.vboolean = (strcmp(value, "true") == 0) || (strcmp(value, "1") == 0);
					verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_BOOLEAN, &tag);
				}
				else if(strcmp(type, "uint32") == 0)
				{
					tag.vuint32 = child_get_uint32(list_data(iter), "", 0);
					verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_UINT32, &tag);
				}
				else if(strcmp(type, "string") == 0)
				{
					tag.vstring = (char *) value;	/* Drop the const. */
					verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_STRING, &tag);
				}
				else if(strcmp(type, "real64") == 0)
				{
					if(sscanf(value, "%lg", &tag.vreal64) == 1)
						verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_REAL64, &tag);
					else
						fprintf(stderr, "loader: Parse error on real64 tag \"%s\" value\n", name);
				}
				else if(strcmp(type, "real64-vec3") == 0)
				{
					if(sscanf(value, "%lg %lg %lg", &tag.vreal64_vec3[0], &tag.vreal64_vec3[1], &tag.vreal64_vec3[2]) == 3)
						verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_REAL64_VEC3, &tag);
					else
						fprintf(stderr, "loader: Parse error on real64_vec3 tag \"%s\" value\n", name);
				}
				else if(strcmp(type, "link") == 0)
				{
					tag.vlink = child_get_ref(list_data(iter), "", 'n', ~0u);
					verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_LINK, &tag);
				}
				else if(strcmp(type, "animation") == 0)
				{
					tag.vanimation.curve = child_get_ref(list_data(iter), "curve", 'n', ~0u);
					tag.vanimation.start = child_get_uint32(list_data(iter), "start", 0u);
					tag.vanimation.end   = child_get_uint32(list_data(iter), "end", 0u);
					verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_ANIMATION, &tag);
				}
				else if(strcmp(type, "blob") == 0)
				{
					char		*eptr;
					unsigned char	data[65536];
					size_t		size;
					unsigned long	x;

					for(; size < sizeof data && (x = strtoul(value, &eptr, 10), eptr > value); size++)
					{
						data[size] = x;
						value = eptr;
					}
					tag.vblob.size = size;
					tag.vblob.blob = data;
					verse_send_tag_create(min->node_id, id, ~0, name, VN_TAG_BLOB, &tag);
				}
				else
					fprintf(stderr, "loader: Ignoring tag of type \"%s\" -- not implemented\n", type);
			}
			list_destroy(tags);
			min->iter = xmlnode_iter_next(min->iter, here);	/* Skip entire group. */
		}
		else
			fprintf(stderr, "loader: Unknown tag group %u.\"%s\" -- still waiting for server response?\n", min->node_id, name);
	}
	else if(strcmp(el, "tag") == 0)
	{
		min->iter = xmlnode_iter_next(min->iter, NULL);
		exit(0);
	}
	else
		return 0;
	return 1;
}

static int process_object(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here);
	const char	*txt;

	if(strcmp(el, "transform") == 0)
	{
		if((txt = xmlnode_eval_single(here, "position")) != NULL)
		{
			real64	pos[3];

			if(sscanf(txt, "%lg %lg %lg", pos, pos + 1, pos + 2) == 3)
			{	
				verse_send_o_transform_pos_real64(min->node_id, 0u, 0u, pos, NULL, NULL, NULL, 0.0);
				min->iter = xmlnode_iter_next(min->iter, NULL);
			}
		}
		if((txt = xmlnode_eval_single(here, "rotation")) != NULL)
		{
			VNQuat64	rot;

			if(sscanf(txt, "%lg %lg %lg %lg", &rot.x, &rot.y, &rot.z, &rot.w) == 4)
			{
				verse_send_o_transform_rot_real64(min->node_id, 0u, 0u, &rot, NULL, NULL, NULL, 0.0);
				min->iter = xmlnode_iter_next(min->iter, NULL);
			}
		}
		if((txt = xmlnode_eval_single(here, "scale")) != NULL)
		{
			real64	scale[3];

			if(sscanf(txt, "%lg %lg %lg", scale, scale + 1, scale + 2) == 3)
			{
				verse_send_o_transform_scale_real64(min->node_id, scale[0], scale[1], scale[2]);
				min->iter = xmlnode_iter_next(min->iter, NULL);
			}
		}
		min->iter = xmlnode_iter_next(min->iter, here);	/* Skip all of transform. */
	}
	else if(strcmp(el, "light") == 0)
	{
		if((txt = xmlnode_eval_single(here, "")) != NULL)
		{
			real64	r, g, b;

			if(sscanf(txt, "%lg %lg %lg", &r, &g, &b) == 3)
				verse_send_o_light_set(min->node_id, r, g, b);
		}
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else if(strcmp(el, "links") == 0)
	{
		List	*link, *li;
		uint16	id = 0;

		link = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("link"), XMLNODE_DONE);
		for(li = link; li != NULL; li = list_next(li))
		{
			const XmlNode	*l = list_data(li);
			uint32		ln = attrib_get_ref(l, "node", 'n', ~0u), target = attrib_get_uint32(l, "target", 0u);

			if(ln != ~0u)
			{
				const char	*label = xmlnode_attrib_get_value(l, "label");

				verse_send_o_link_set(min->node_id, id, min->node_map[ln], label, target);
				id++;
			}
		}
		list_destroy(link);
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else if(strcmp(el, "methodgroups") == 0)
	{
		List	*groups, *iter;

		groups = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("methodgroup"), XMLNODE_DONE);
		for(iter = groups; iter != NULL; iter = list_next(iter))
		{
			const char	*mn = xmlnode_attrib_get_value(list_data(iter), "name");

			message(min, 2, "Creating method group \"%s\" in object %u\n", mn, min->node_id);
			verse_send_o_method_group_create(min->node_id, ~0, mn);
			pend_add(min, PEND_METHODGROUP_CREATE, 1);
		}
		list_destroy(groups);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strcmp(el, "methodgroup") == 0)
	{
		const char	*gn = xmlnode_attrib_get_value(here, "name");
		uint16		gid;
		List		*methods, *iter, *params, *piter;

		gid = layer_id_get(min, gn);
		if(gid == (uint16) ~0)
		{
			fprintf(stderr, "loader: Couldn't look up ID for method group \"%s\" -- creation failed?\n", gn);
			min->iter = xmlnode_iter_next(min->iter, here);
			return 0;
		}
		methods = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("method"), XMLNODE_DONE);
		for(iter = methods; iter != NULL; iter = list_next(iter))
		{
			const char	*mn = xmlnode_attrib_get_value(list_data(iter), "name");
			uint8		pn, err;
			const char	*pname[64];
			VNOParamType	ptype[64];

			params = xmlnode_nodeset_get(list_data(iter), XMLNODE_AXIS_CHILD, XMLNODE_NAME("param"), XMLNODE_DONE);
			for(piter = params, pn = err = 0; piter != NULL; piter = list_next(piter))
			{
				pname[pn] = xmlnode_attrib_get_value(list_data(piter), "name");
				ptype[pn] = o_method_param_type_from_string(xmlnode_attrib_get_value(list_data(piter), "type"));
				if(pname[pn] != NULL && ptype[pn] != -1)
					pn++;
				else
					err++;
			}
			list_destroy(params);
			if(err == 0)
			{
				message(min, 3, "Creating method %u.%u, \"%s\" with %u params\n", min->node_id, gid, mn, pn);
				verse_send_o_method_create(min->node_id, gid, ~0, mn, pn, ptype, pname);
			}
		}
		list_destroy(methods);
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else
		return 0;
	return 1;
}

static int g_scan_send_vertex_xyz(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	real64	*xyz = tmp;

	if(sscanf(element, "%u %lg %lg %lg", &index, xyz, xyz + 1, xyz + 2) == 4)
	{
		verse_send_g_vertex_set_xyz_real64(node_id, layer_id, index, xyz[0], xyz[1], xyz[2]);
		return 1;
	}
	else
		fprintf(stderr, "loader: Couldn't parse vertex from '%s'\n", element);
	return 0;
}

static int g_scan_send_vertex_uint32(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	uint32	*v = tmp;

	if(sscanf(element, "%u %u", &index, v) == 2)
	{
		verse_send_g_vertex_set_uint32(node_id, layer_id, index, *v);
		return 1;
	}
	return 0;
}

static int g_scan_send_vertex_real(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	real64	*v = tmp;

	if(sscanf(element, "%u %lg", &index, v) == 2)
	{
		verse_send_g_vertex_set_real64(node_id, layer_id, index, *v);
		return 1;
	}
	return 0;
}

static int g_scan_send_polygon_corner_uint32(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	uint32	*v = tmp;

	v[3] = ~0u;
	if(sscanf(element, "%u %u %u %u", v, v + 1, v + 2, v + 3) >= 3)
	{
		verse_send_g_polygon_set_corner_uint32(node_id, layer_id, index, v[0], v[1], v[2], v[3]);
		return 1;
	}
	return 0;
}

static int g_scan_send_polygon_corner_real(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	real64	*v = tmp;

	if(sscanf(element, "%lg %lg %lg %lg", v, v + 1, v + 2, v + 3) == 4)
	{
		verse_send_g_polygon_set_corner_real64(node_id, layer_id, index, v[0], v[1], v[2], v[3]);
		return 1;
	}
	return 0;
}

static int g_scan_send_polygon_face_uint8(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	uint32	*v = tmp;

	if(sscanf(element, "%u", v) == 1)
	{
		verse_send_g_polygon_set_face_uint8(node_id, layer_id, index, v[0]);
		return 1;
	}
	return 0;
}

static int g_scan_send_polygon_face_uint32(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	uint32	*v = tmp;

	if(sscanf(element, "%u", v) == 1)
	{
		verse_send_g_polygon_set_face_uint32(node_id, layer_id, index, v[0]);
		return 1;
	}
	return 0;
}

static int g_scan_send_polygon_face_real(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index)
{
	real64	*v = tmp;

	if(sscanf(element, "%lg", v) == 1)
	{
		verse_send_g_polygon_set_face_real64(node_id, layer_id, index, v[0]);
		return 1;
	}
	return 0;
}

/* Helper function for setting a geometry layer. Extracts children ("elements", i.e. vertices or polygons)
 * named <elname> from <layer>, then calls the scan_send() function on each, providing it with some tempo-
 * rary storage and an index number. The storage should be enough for the largest type (4 * real64).
*/
static void g_set_layer(VNodeID node_id, VLayerID layer_id, const XmlNode *layer, const char *elname,
			int (*scan_send)(VNodeID node_id, VLayerID layer_id, const char *element, void *tmp, uint32 index))
{
	List	*points, *iter;
	real64	tmp[4];
	uint32	index = 0;

	for(iter = points = xmlnode_nodeset_get(layer, XMLNODE_AXIS_CHILD, XMLNODE_NAME(elname), XMLNODE_DONE);
	    iter != NULL; iter = list_next(iter))
	{
		const char	*txt = xmlnode_eval_single(list_data(iter), "");

		if(txt != NULL && scan_send(node_id, layer_id, txt, tmp, index))
			index++;
	}
	list_destroy(points);
}

static int process_geometry(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here);

	if(strcmp(el, "layers") == 0)
	{
		List	*layers, *iter;

		layers = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("layer-"), XMLNODE_DONE);
		for(iter = layers; iter != NULL; iter = list_next(iter))
		{
			const char	*ln = xmlnode_attrib_get_value(list_data(iter), "name");
			VNGLayerType	type = g_layer_type_from_string(xmlnode_get_name(list_data(iter)) + 6);

			if(layer_id_get(min, ln) == (VLayerID) ~0u)
			{
				verse_send_g_layer_create(min->node_id, ~0, ln, type, 0, 0);
				layer_id_set(min, ln, ~0);
				pend_add(min, PEND_LAYER_CREATE, 1);
			}
		}
		list_destroy(layers);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strncmp(el, "layer-", 6) == 0)
	{
		const char	*ln;
		VLayerID	id;
		VNGLayerType	lt;

		ln = xmlnode_attrib_get_value(here, "name");
		id = layer_id_get(min, ln);
		if(id == (VLayerID) ~0)
		{
			fprintf(stderr, "loader: Unknown geometry layer \"%s\"\n", ln);
			return 1;
		}
		message(min, 3, "  ID of geometry layer %s is %u\n", ln, layer_id_get(min, ln));
		lt = g_layer_type_from_string(xmlnode_get_name(here) + 6);
		if(lt == (VNGLayerType) ~0)
		{
			fprintf(stderr, "loader: Unknown layer type \"%s\"\n", xmlnode_get_name(here) + 6);
			return 1;
		}
		switch(lt)
		{
		case VN_G_LAYER_VERTEX_XYZ:
			g_set_layer(min->node_id, id, here, "v", g_scan_send_vertex_xyz);
			break;
		case VN_G_LAYER_VERTEX_UINT32:
			g_set_layer(min->node_id, id, here, "v", g_scan_send_vertex_uint32);
			break;
		case VN_G_LAYER_VERTEX_REAL:
			g_set_layer(min->node_id, id, here, "v", g_scan_send_vertex_real);
			break;
		case VN_G_LAYER_POLYGON_CORNER_UINT32:
			g_set_layer(min->node_id, id, here, "p", g_scan_send_polygon_corner_uint32);
			break;
		case VN_G_LAYER_POLYGON_CORNER_REAL:
			g_set_layer(min->node_id, id, here, "p", g_scan_send_polygon_corner_real);
			break;
		case VN_G_LAYER_POLYGON_FACE_UINT8:
			g_set_layer(min->node_id, id, here, "p", g_scan_send_polygon_face_uint8);
			break;
		case VN_G_LAYER_POLYGON_FACE_UINT32:
			g_set_layer(min->node_id, id, here, "p", g_scan_send_polygon_face_uint32);
			break;
		case VN_G_LAYER_POLYGON_FACE_REAL:
			g_set_layer(min->node_id, id, here, "p", g_scan_send_polygon_face_real);
			break;
		}
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else if(strcmp(el, "vertexcrease") == 0)
	{
		const char	*lname = NULL;
		uint32		def;

		lname = xmlnode_attrib_get_value(here, "layer");
		def   = attrib_get_uint32(here, "default", ~0u);
		verse_send_g_crease_set_vertex(min->node_id, lname, def);
		message(min, 4, "vertex crease set to '%s' def=%u\n", lname, def);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strcmp(el, "edgecrease") == 0)
	{
		const char	*lname = NULL;
		uint32		def;

		lname = xmlnode_attrib_get_value(here, "layer");
		def   = attrib_get_uint32(here, "default", ~0u);
		verse_send_g_crease_set_edge(min->node_id, lname, def);
		message(min, 4, "edge crease set to '%s'\n", lname);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strcmp(el, "bones") == 0)
	{
		List	*bones, *iter;
		const VNQuat64	norot = { 0 };
		int	i = 0;

		/* This uses the (new) support for user-assigned ID:s, and just uploads the bones straight
		 * away, without doing a round-trip with a create-and-pend approach like the rest of this
		 * program does. If this works, it's something to consider for other items (layers, etc) too.
		*/
		bones = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("bone"), XMLNODE_DONE);
		for(iter = bones; iter != NULL; iter = list_next(iter), i++)
		{
			const XmlNode	*b = list_data(iter);
			const char	*wght = xmlnode_eval_single(b, "weight"),
					*ref = xmlnode_eval_single(b, "reference"),
					*pr = xmlnode_eval_single(b, "pos-label"),
					*rr = xmlnode_eval_single(b, "rot-label");
			uint32		par = child_get_ref(b, "parent", 'b', ~0u);

			verse_send_g_bone_create(min->node_id, (uint16) i, wght, ref, par, i, 0.0, 0.0, pr, &norot, rr);
		}
		list_destroy(bones);
		min->iter = xmlnode_iter_next(min->iter, here);	/* That's it, skip it now. */
	}
	else
		return 0;
	return 1;
}

static void fragment_clear(VNMFragmentType type, VMatFrag *f)
{
	switch(type)
	{
	case VN_M_FT_COLOR:
		f->color.red = f->color.green = f->color.blue = 0.0;
		break;
	case VN_M_FT_LIGHT:
		f->light.type = 0;
		f->light.brdf = ~0u;
		f->light.brdf_r[0] = f->light.brdf_g[0] = f->light.brdf_b[0] = '\0';
		break;
	case VN_M_FT_REFLECTION:
		f->reflection.normal_falloff = 0.0;
		break;
	case VN_M_FT_TRANSPARENCY:
		f->transparency.normal_falloff = f->transparency.refraction_index = 0.0;
		break;
	case VN_M_FT_GEOMETRY:
	 	f->geometry.layer_r[0] = f->geometry.layer_g[0] = f->geometry.layer_b[0] = '\0';
		break;
	case VN_M_FT_TEXTURE:
		f->texture.bitmap = ~0u;
		f->texture.layer_r[0] = f->texture.layer_g[0] = f->texture.layer_b[0] = '\0';
		f->texture.mapping = ~0;
		break;
	case VN_M_FT_BLENDER:
		f->blender.type = 0;
		f->blender.data_a = f->blender.data_b = f->blender.control = (VNMFragmentID) ~0u;
		break;
	case VN_M_FT_MATRIX:
		f->matrix.data = ~0;
		break;
	case VN_M_FT_RAMP:
		f->ramp.mapping = ~0;
		f->ramp.point_count = 0;
		break;
	case VN_M_FT_OUTPUT:
		f->output.label[0] = '\0';
		f->output.front = f->output.back = (VNMFragmentID) ~0u;
		break;
	default:
		;
	}
}

/* Set fragment from XML. */
static int fragment_set(const MainInfo *min, VNMFragmentType type, VMatFrag *f, const XmlNode *frag)
{
	const char	*v;

	switch(type)
	{
	case VN_M_FT_COLOR:
		v = xmlnode_eval_single(frag, "color");
		return sscanf(v, "%lg %lg %lg", &f->color.red, &f->color.green, &f->color.blue) == 3;
	case VN_M_FT_LIGHT:
		v = xmlnode_eval_single(frag, "type");
		f->light.type = m_light_type_from_string(v);
		f->light.normal_falloff = child_get_real64(frag, "normal_falloff", 0.0);
		f->light.brdf = child_get_ref(frag, "brdf", 'n', ~0u);
		child_get_string(f->light.brdf_r, sizeof f->light.brdf_r, frag, "brdf_r");
		child_get_string(f->light.brdf_g, sizeof f->light.brdf_g, frag, "brdf_g");
		child_get_string(f->light.brdf_b, sizeof f->light.brdf_b, frag, "brdf_b");
		break;
	case VN_M_FT_REFLECTION:
		f->reflection.normal_falloff = child_get_real64(frag, "normal_falloff", 0.0);
		break;
	case VN_M_FT_TRANSPARENCY:
		f->transparency.normal_falloff   = child_get_real64(frag, "normal_falloff", 0.0);
		f->transparency.refraction_index = child_get_real64(frag, "refraction_index", 0.0);
		break;
	case VN_M_FT_GEOMETRY:
		child_get_string(f->geometry.layer_r, sizeof f->geometry.layer_r, frag, "layer_r");
		child_get_string(f->geometry.layer_g, sizeof f->geometry.layer_g, frag, "layer_g");
		child_get_string(f->geometry.layer_b, sizeof f->geometry.layer_b, frag, "layer_b");
		break;
	case VN_M_FT_TEXTURE:
		f->texture.bitmap = min->node_map[child_get_ref(frag, "bitmap", 'n', ~0u)];
		child_get_string(f->texture.layer_r, sizeof f->texture.layer_r, frag, "layer_r");
		child_get_string(f->texture.layer_g, sizeof f->texture.layer_g, frag, "layer_g");
		child_get_string(f->texture.layer_b, sizeof f->texture.layer_b, frag, "layer_b");
		f->texture.mapping = fragment_map_get(min, child_get_ref(frag, "mapping", 'f', ~0u));
		break;
	case VN_M_FT_NOISE:
		f->noise.type = m_noise_type_from_string(xmlnode_eval_single(frag, "type"));
		f->noise.mapping = fragment_map_get(min, child_get_ref(frag, "mapping", 'f', ~0u));
		break;
	case VN_M_FT_BLENDER:
		f->blender.type = m_blend_type_from_string(xmlnode_eval_single(frag, "type"));
		f->blender.data_a  = fragment_map_get(min, child_get_ref(frag, "data_a", 'f', ~0u));
		f->blender.data_b  = fragment_map_get(min, child_get_ref(frag, "data_b", 'f', ~0u));
		f->blender.control = fragment_map_get(min, child_get_ref(frag, "control", 'f', ~0u));
		break;
	case VN_M_FT_MATRIX:
		f->matrix.data = fragment_map_get(min, child_get_ref(frag, "data", 'f', ~0u));
		return sscanf(xmlnode_eval_single(frag, "matrix"),
		       "%lg %lg %lg %lg"
		       "%lg %lg %lg %lg"
		       "%lg %lg %lg %lg"
		       "%lg %lg %lg %lg",
		       &f->matrix.matrix[ 0], &f->matrix.matrix[ 1], &f->matrix.matrix[ 2], &f->matrix.matrix[ 3],
		       &f->matrix.matrix[ 4], &f->matrix.matrix[ 5], &f->matrix.matrix[ 6], &f->matrix.matrix[ 7],
		       &f->matrix.matrix[ 8], &f->matrix.matrix[ 9], &f->matrix.matrix[10], &f->matrix.matrix[11],
		       &f->matrix.matrix[12], &f->matrix.matrix[13], &f->matrix.matrix[14], &f->matrix.matrix[15]) == 16;
	case VN_M_FT_RAMP:
		f->ramp.type    = m_ramp_type_from_string(xmlnode_eval_single(frag, "type"));
		f->ramp.channel = m_ramp_channel_from_string(xmlnode_eval_single(frag, "channel"));
		f->ramp.mapping = fragment_map_get(min, child_get_ref(frag, "mapping", 'f', ~0u));
		f->ramp.point_count = 0;
		{
			List		*iter;
			VNMRampPoint	*p;
			const char	*txt;

			for(iter = xmlnode_nodeset_get(frag, XMLNODE_AXIS_CHILD, XMLNODE_NAME("ramp"), XMLNODE_AXIS_CHILD, XMLNODE_NAME("ramppoint"), XMLNODE_DONE);
			    iter != NULL;
			    iter = list_next(iter))
			{
				p = &f->ramp.ramp[f->ramp.point_count];
				if((txt = xmlnode_eval_single(list_data(iter), "")) != NULL)
				{
					p->pos = attrib_get_real64(list_data(iter), "pos", 0.0);
					if(sscanf(txt, "%lg %lg %lg", &p->red, &p->green, &p->blue) == 3)
						f->ramp.point_count++;
					else
						fprintf(stderr, "loader: Parse error in ramp element, expected three doubles\n");
				}
			}
		}
		return f->ramp.point_count > 0;
	case VN_M_FT_OUTPUT:
		child_get_string(f->output.label, sizeof f->output.label, frag, "label");
		f->output.front = fragment_map_get(min, child_get_ref(frag, "front", 'f', ~0u));
		f->output.back  = fragment_map_get(min, child_get_ref(frag, "back", 'f', ~0u));
		break;
	default:
		fprintf(stderr, "loader: Can't set type-%d material fragment from XML\n", type);
		return 0;
	}
	return 1;
}

static int m_create_fragment(MainInfo *min, VNMFragmentID id, const XmlNode *frag)
{
	VNMFragmentType	type;
	uint32		lid, ok = 1;
	VMatFrag	f;

	if((type = m_fragment_type_from_string(xmlnode_get_name(frag) + 9)) == ~0u)
		return 0;
	lid = attrib_get_ref(frag, "id", 'f', ~0u);
	if(lid == ~0u)
		return 0;
	fragment_map_store(min, lid, id);
	if(id == (VNMFragmentID) ~0u)
		fragment_clear(type, &f);
	else
		ok = fragment_set(min, type, &f, frag);
	if(ok)
	{
		message(min, 3, "creating material fragment, node %u, type %d\n", min->node_id, type);
		verse_send_m_fragment_create(min->node_id, id, type, &f);
	}
	return 1;
}

static int process_material(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here);

	if(strcmp(el, "fragments") == 0)
	{
		List	*frags, *iter;

		frags = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("fragment-"), XMLNODE_DONE);
		fragment_map_clear(min);
		for(iter = frags; iter != NULL; iter = list_next(iter))
		{
			m_create_fragment(min, (VNMFragmentID) ~0u, list_data(iter));
			pend_add(min, PEND_FRAGMENT_CREATE, 1);
		}
		list_destroy(frags);
		min->iter = xmlnode_iter_next(min->iter, NULL);	/* Enter for fragment list. */
	}
	else if(strncmp(el, "fragment-", 9) == 0)
	{
		uint32	id;

		id = attrib_get_ref(here, "id", 'f', ~0u);
		if(id < min->fragment_map_size)
			m_create_fragment(min, min->fragment_map[id], here);
		min->iter = xmlnode_iter_next(min->iter, here);	/* Skip children. */
	}
	else
		return 0;
	return 1;
}

static int process_bitmap(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here), *txt;

	txt = xmlnode_eval_single(here, "");

	if(strcmp(el, "dimensions") == 0)
	{
		uint16	w, h, d;

		if(sscanf(txt, "%hu %hu %hu", &w, &h, &d) == 3)
		{
			message(min, 2, " got dimensions: %ux%ux%u\n", w, h, d);
			verse_send_b_dimensions_set(min->node_id, w, h, d);
			min->iter = xmlnode_iter_next(min->iter, NULL);
		}
	}
	else if(strcmp(el, "layers") == 0)
	{
		List	*layers, *iter;

		layers = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("layer-"), XMLNODE_DONE);
		for(iter = layers; iter != NULL; iter = list_next(iter))
		{
			const char	*ln = xmlnode_attrib_get_value(list_data(iter), "name");
			VNBLayerType	lt = b_layer_type_from_string(xmlnode_get_name(list_data(iter)) + 6);

			verse_send_b_layer_create(min->node_id, (VLayerID) ~0u, ln, lt);
			layer_id_set(min, ln, ~0);
			pend_add(min, PEND_LAYER_CREATE, 1);
		}
		list_destroy(layers);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strncmp(el, "layer-", 6) == 0)
	{
		const char	*ln = xmlnode_attrib_get_value(here, "name");
		VLayerID	lid = layer_id_get(min, ln);
		List		*tiles, *iter;
		VNBLayerType	lt = b_layer_type_from_string(el + 6);

		tiles = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("tiles"), XMLNODE_AXIS_CHILD, XMLNODE_NAME("tile"), XMLNODE_DONE);
		for(iter = tiles; iter != NULL; iter = list_next(iter))
		{
			const char	*xs = xmlnode_attrib_get_value(list_data(iter), "tile_x"),
					*ys = xmlnode_attrib_get_value(list_data(iter), "tile_y"),
					*zs = xmlnode_attrib_get_value(list_data(iter), "tile_z"),
					*ts = xmlnode_eval_single(list_data(iter), "");
			char		*eptr;
			VNBTile		tile;
			uint16		x, y, z, i;

			if(xs == NULL || ys == NULL || zs == NULL || ts == NULL)
				continue;
			x = strtoul(xs, NULL, 10);
			y = strtoul(ys, NULL, 10);
			z = strtoul(zs, NULL, 10);
			i = 0;
			for(i = 0; i < sizeof tile.vuint8 / sizeof *tile.vuint8; i++)
			{
				/* Inspect the type for each pixel. We can afford the overhead, and it saves code. */
				if(lt == VN_B_LAYER_UINT1)
				{
					fprintf(stderr, "loader: Can't parse 1-bpp pixel data\n");
					break;
				}
				else if(lt == VN_B_LAYER_UINT8)
					tile.vuint8[i] = strtoul(ts, &eptr, 10);
				else if(lt == VN_B_LAYER_UINT16)
					tile.vuint16[i] = strtoul(ts, &eptr, 10);
				else if(lt == VN_B_LAYER_REAL32)
					tile.vreal32[i] = strtod(ts, &eptr);
				else if(lt == VN_B_LAYER_REAL64)
					tile.vreal64[i] = strtod(ts, &eptr);
				
				if(eptr > ts)
					ts = eptr;
				else
				{
					fprintf(stderr, "loader: Parse error in tile (%u,%u,%u), pixel %u\n", x, y, z, i);
					break;
				}
			}
			if(i == sizeof tile.vuint8 / sizeof *tile.vuint8)
				verse_send_b_tile_set(min->node_id, lid, x, y, z, lt, &tile);
		}
		list_destroy(tiles);
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else
		return 0;
	return 1;
}

static int process_curve(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here), *txt;

	if(strcmp(el, "curves") == 0)
	{
		List	*curves, *iter;

		curves = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("curve-"), XMLNODE_DONE);
		for(iter = curves; iter != NULL; iter = list_next(iter))
		{
			const char	*cn = xmlnode_attrib_get_value(list_data(iter), "name");
			unsigned int	cd;

			cd = strtoul(xmlnode_get_name(list_data(iter)) + 6, NULL, 10);
			verse_send_c_curve_create(min->node_id, (VLayerID) ~0u, cn, cd);
			layer_id_set(min, cn, ~0);
			pend_add(min, PEND_CURVE_CREATE, 1);
		}
		list_destroy(curves);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strncmp(el, "curve-", 6) == 0)
	{
		const char	*cn = xmlnode_attrib_get_value(here, "name");
		VLayerID	cid = layer_id_get(min, cn);
		unsigned int	dim;
		List		*keys, *iter;
		uint16		key_id = 0;

		dim = strtoul(el + 6, NULL, 10);
		keys = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("key"), XMLNODE_DONE);
		for(iter = keys; iter != NULL; iter = list_next(iter))
		{
			real64	pos, value[4], pre_val[4], post_val[4];
			uint32	pre_pos[4], post_pos[4];
			XmlNode	*key = list_data(iter);
			char	*eptr;
			int	i, got = 0;

			pos = attrib_get_real64(key, "pos", 0.0);
			if((txt = xmlnode_eval_single(key, "value")) != NULL)
			{
				for(i = 0; i < dim; i++)
				{
					value[i] = strtod(txt, &eptr);
					got += eptr > txt;
					txt = eptr;
				}
			}
			if((txt = xmlnode_eval_single(key, "pre-value")) != NULL)
			{
				for(i = 0; i < dim; i++)
				{
					pre_val[i] = strtod(txt, &eptr);
					got += eptr > txt;
					txt = eptr;
				}
			}
			if((txt = xmlnode_eval_single(key, "pre-pos")) != NULL)
			{
				for(i = 0; i < dim; i++)
				{
					pre_pos[i] = strtoul(txt, &eptr, 10);
					got += eptr > txt;
					txt = eptr;
				}
			}
			if((txt = xmlnode_eval_single(key, "post-value")) != NULL)
			{
				for(i = 0; i < dim; i++)
				{
					post_val[i] = strtod(txt, &eptr);
					got += eptr > txt;
					txt = eptr;
				}
			}
			if((txt = xmlnode_eval_single(key, "post-pos")) != NULL)
			{
				for(i = 0; i < dim; i++)
				{
					post_pos[i] = strtoul(txt, &eptr, 10);
					got += eptr > txt;
					txt = eptr;
				}
			}
			if(got == 5 * dim)
				verse_send_c_key_set(min->node_id, cid, key_id++, dim, pre_val, pre_pos, value, pos, post_val, post_pos);
			else
				fprintf(stderr, "loader: Parse error in curve key, got %d values (expected %d)\n", got, 5 * dim);
		}
		list_destroy(keys);
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else
		return 0;
	return 1;
}

static int a_parse_int8(VNABlock *block, const char *data)
{
	char	*eptr;
	size_t	i;

	for(i = 0; i < VN_A_BLOCK_SIZE_INT8; i++)
	{
		block->vint8[i] = strtol(data, &eptr, 10);
		if(eptr == data)
			return 0;
	}
	return 1;
}

static int a_parse_int16(VNABlock *block, const char *data)
{
	char	*eptr;
	size_t	i;

	for(i = 0; i < VN_A_BLOCK_SIZE_INT8; i++)
	{
		block->vint16[i] = strtol(data, &eptr, 10);
		if(eptr == data)
			return 0;
	}
	return 1;
}

static int a_parse_int24(VNABlock *block, const char *data)
{
	char	*eptr;
	size_t	i;

	for(i = 0; i < VN_A_BLOCK_SIZE_INT8; i++)
	{
		block->vint24[i] = strtol(data, &eptr, 10);
		if(eptr == data)
			return 0;
	}
	return 1;
}

static int a_parse_int32(VNABlock *block, const char *data)
{
	char	*eptr;
	size_t	i;

	for(i = 0; i < VN_A_BLOCK_SIZE_INT8; i++)
	{
		block->vint32[i] = strtol(data, &eptr, 10);
		if(eptr == data)
			return 0;
	}
	return 1;
}

static int a_parse_real32(VNABlock *block, const char *data)
{
	char	*eptr;
	size_t	i;

	for(i = 0; i < VN_A_BLOCK_SIZE_INT8; i++)
	{
		block->vreal32[i] = strtod(data, &eptr);
		if(eptr == data)
			return 0;
	}
	return 1;
}

static int a_parse_real64(VNABlock *block, const char *data)
{
	char	*eptr;
	size_t	i;

	for(i = 0; i < VN_A_BLOCK_SIZE_INT8; i++)
	{
		block->vreal64[i] = strtod(data, &eptr);
		if(eptr == data)
			return 0;
	}
	return 1;
}

static int process_audio(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here);

	if(strcmp(el, "buffers") == 0)
	{
		List	*buffers, *iter;

		buffers = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("buffer-"), XMLNODE_DONE);
		for(iter = buffers; iter != NULL; iter = list_next(iter))
		{
			const char	*bn = xmlnode_attrib_get_value(list_data(iter), "name");
			VNABlockType	bt = a_block_type_from_string(xmlnode_get_name(list_data(iter)) + 7);
			real64		freq = strtod(xmlnode_attrib_get_value(list_data(iter), "frequency"), NULL);

			message(min, 2, "there's an audio buffer called \"%s\", type %d, freq %g Hz\n", bn, bt, freq);
			verse_send_a_buffer_create(min->node_id, (VLayerID) ~0u, bn, bt, freq);
			layer_id_set(min, bn, ~0);
			pend_add(min, PEND_BUFFER_CREATE, 1);
		}
		list_destroy(buffers);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strncmp(el, "buffer-", 7) == 0)
	{
		const char	*bn;
		VNABlockType	bt;
		VLayerID	id;
		List		*blocks, *iter;
		uint32		index;
		VNABlock	block;
		int		(*parser[])(VNABlock *block, const char *data) = { a_parse_int8, a_parse_int16, a_parse_int24, a_parse_int32, a_parse_real32, a_parse_real64 };

		bn = xmlnode_attrib_get_value(here, "name");
		bt = a_block_type_from_string(el + 7);
		if(bn == NULL || bt < 0)
		{
			fprintf(stderr, "loader: Error in audio buffer block parsing\n");
			return 0;
		}
		id = layer_id_get(min, bn);
		blocks = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("blocks"), XMLNODE_AXIS_CHILD, XMLNODE_NAME("block"), XMLNODE_DONE);
		for(iter = blocks; iter != NULL; iter = list_next(iter))
		{
			index = attrib_get_uint32(list_data(iter), "index", ~0);
			if(index == ~0)
				continue;
			if(parser[bt](&block, xmlnode_eval_single(list_data(iter), "")))
			{
				message(min, 3, " sending audio block %u.%u.%u\n", min->node_id, id, index);
				verse_send_a_block_set(min->node_id, id, index, bt, &block);
			}
			else
				break;
		}
		list_destroy(blocks);
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else
		return 0;
	return 1;
}

static int process_text(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);
	const char	*el = xmlnode_get_name(here), *txt;

	if(strcmp(el, "language") == 0)
	{
		const char	*lang = xmlnode_eval_single(here, "");

		message(min, 2, "text node langauge: '%s'\n", lang);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strcmp(el, "buffers") == 0)
	{
		List	*buffers, *iter;

		buffers = xmlnode_nodeset_get(here, XMLNODE_AXIS_CHILD, XMLNODE_NAME("buffer"), XMLNODE_DONE);
		for(iter = buffers; iter != NULL; iter = list_next(iter))
		{
			const char	*bn = xmlnode_attrib_get_value(list_data(iter), "name");

			verse_send_t_buffer_create(min->node_id, (VLayerID) ~0u, bn);
			layer_id_set(min, bn, ~0);
			pend_add(min, PEND_BUFFER_CREATE, 1);
		}
		list_destroy(buffers);
		min->iter = xmlnode_iter_next(min->iter, NULL);
	}
	else if(strcmp(el, "buffer") == 0)
	{
		const char	*bn = xmlnode_attrib_get_value(here, "name");
		VLayerID	bid = layer_id_get(min, bn);

		if((txt = xmlnode_eval_single(here, "")) != NULL)
		{
			char	buf[1280];
			size_t	len, chunk, pos;

			/* Split text, which might be too long for a single command, into chunks. */
			for(pos = 0, len = strlen(txt); len != 0; len -= chunk, pos += chunk)
			{
				chunk = (len > sizeof buf - 1) ? sizeof buf - 1 : len;
				memcpy(buf, txt + pos, chunk);	/* We're not in a hurry, here. */
				buf[chunk] = '\0';
				verse_send_t_text_set(min->node_id, bid, pos, chunk, buf);
			}
		}
		min->iter = xmlnode_iter_next(min->iter, here);
	}
	else
		return 0;
	return 1;
}

static void step(MainInfo *min)
{
	const XmlNode	*here = list_data(min->iter);

	/* New node reached? */
	if(strncmp(xmlnode_get_name(here), "node-", 5) == 0)
	{
		min->node = here;
		min->type = node_type_from_string(xmlnode_get_name(here) + 5);
		message(min, 3, "In step(), found node type %d at %p\n", min->type, min->node);
		if(min->type == V_NT_OBJECT && min->skip_objects)
		{
			min->iter = xmlnode_iter_next(min->iter, here);
			message(min, 3, " skipping object\n");
			return;
		}
		else if(min->type != V_NT_OBJECT && !min->skip_objects)
		{
			message(min, 3, " ignoring non-object node in non-skip mode\n");
			min->iter = xmlnode_iter_next(min->iter, here);
			return;
		}
		message(min, 3, " sending create, local ID is %s\n", xmlnode_attrib_get_value(min->node, "id"));
		min->iter = xmlnode_iter_next(min->iter, NULL);
		verse_send_node_create(~0, min->type, 0);
		pend_add(min, PEND_NODE_CREATE, 0);
		dict_clear(&min->tag_groups);
		layer_id_clear(min);
	}
	else	/* Nope, so keep processing the previous one. */
	{
		int	ok;

		if(process_common(min))
			return;
		switch(min->type)
		{
		case V_NT_OBJECT:
			ok = process_object(min);
			break;
		case V_NT_GEOMETRY:
			ok = process_geometry(min);
			break;
		case V_NT_MATERIAL:
			ok = process_material(min);
			break;
		case V_NT_BITMAP:
			ok = process_bitmap(min);
			break;
		case V_NT_CURVE:
			ok = process_curve(min);
			break;
		case V_NT_TEXT:
			ok = process_text(min);
			break;
		case V_NT_AUDIO:
			ok = process_audio(min);
			break;
		default:
			fprintf(stderr, "loader: Not processing node of type %d, code missing\n", min->type);
			ok = 0;
		}
		if(ok == 0)
		{
			fprintf(stderr, "loader: Unknown element \"%s\" found, skipping forward\n", xmlnode_get_name(list_data(min->iter)));
			min->iter = xmlnode_iter_next(min->iter, min->node);
		}
	}
}

static void cb_a_buffer_create(void *user, VNodeID node_id, VLayerID buffer_id, const char *name)
{
	MainInfo	*min = user;

	message(min, 4, "audio buffer %u.%u (%s) created\n", node_id, buffer_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		if(min->pending == PEND_BUFFER_CREATE)
		{
			message(min, 5, "  and we're buffer-create blocked, how interesting\n");
			layer_id_set(min, name, buffer_id);
			pend_sub(min);
		}
	}
}

static void cb_t_buffer_create(void *user, VNodeID node_id, VLayerID buffer_id, const char *name)
{
	MainInfo	*min = user;

	message(min, 4, "text buffer %u.%u (%s) created\n", node_id, buffer_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		if(min->pending == PEND_BUFFER_CREATE)
		{
			message(min, 5, "  and we're buffer-create blocked, how interesting\n");
			layer_id_set(min, name, buffer_id);
			pend_sub(min);
		}
	}
}

static void cb_c_curve_create(void *user, VNodeID node_id, VLayerID curve_id, const char *name, uint8 dimensions)
{
	MainInfo	*min = user;

	message(min, 4, "curve curve %u.%u (%s) created\n", node_id, curve_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		if(min->pending == PEND_CURVE_CREATE)
		{
			message(min, 5, "  and we're curve-create blocked, how interesting\n");
			layer_id_set(min, name, curve_id);
			pend_sub(min);
		}
	}
}

static void cb_b_layer_create(void *user, VNodeID node_id, VLayerID layer_id, const char *name, VNBLayerType type)
{
	MainInfo	*min = user;

	message(min, 4, "bitmap layer %u.%u (%s) created\n", node_id, layer_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		if(min->pending == PEND_LAYER_CREATE)
		{
			message(min, 5, "  and we're layer-create blocked, how interesting\n");
			layer_id_set(min, name, layer_id);
			pend_sub(min);
		}
	}
}

static void cb_m_fragment_create(void *user, VNodeID node_id, VNMFragmentID fragment_id,
				 VNMFragmentType type, const VMatFrag *fragment)
{
	MainInfo	*min = user;

	message(min, 4, "fragment %u created in node %u, type %d\n", fragment_id, node_id, type);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in the current node\n");
		if(min->pending == PEND_FRAGMENT_CREATE)
		{
			List	*frags, *iter;

			message(min, 5, "  and we're fragment-create blocked, how interesting\n");
			frags = xmlnode_nodeset_get(min->node, XMLNODE_AXIS_CHILD, XMLNODE_NAME("fragments"),
						     XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("fragment-"), XMLNODE_DONE);
			for(iter = frags; iter != NULL; iter = list_next(iter))
			{
				const XmlNode	*here = list_data(iter);
				VNMFragmentType	ftype = m_fragment_type_from_string(xmlnode_get_name(here) + 9);

				if(ftype == type)
				{
					uint32	id = attrib_get_ref(here, "id", 'f', ~0u);
					if(id != ~0u)
					{
						if(id < min->fragment_map_size && min->fragment_map[id] == (VNMFragmentID) ~0u)
						{
							fragment_map_store(min, id, fragment_id);
							pend_sub(min);
							break;
						}
					}
				}
			}
			list_destroy(frags);
		}
	}
}

static void cb_g_layer_create(void *user, VNodeID node_id, VLayerID layer_id, const char *name,
			      VNGLayerType type, uint32 def_uint, real64 def_real)
{
	MainInfo	*min = user;

	message(min, 4, "geometry layer %u.%u (%s) created\n", node_id, layer_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		layer_id_set(min, name, layer_id);	/* Always store, even if not expected (server sends vertex/poly spontaneously). */
		if(min->pending == PEND_LAYER_CREATE)
		{
			message(min, 5, "  and we're layer-create blocked, how interesting\n");
			pend_sub(min);
		}
		else
			message(min, 5, "unexpected, so no de-pend, but still stored\n");
	}
}

static void cb_o_method_group_create(void *user, VNodeID node_id, uint16 group_id, const char *name)
{
	MainInfo	*min = user;

	message(min, 4, "method group %u.%u (%s) created\n", node_id, group_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		layer_id_set(min, name, group_id);
		if(min->pending == PEND_METHODGROUP_CREATE)
		{
			message(min, 5, "  and we're group-create blocked, how interesting\n");
			pend_sub(min);
		}
		else
			message(min, 5, "unexpected, so no de-pend, but still stored\n");
	}
}

static void cb_tag_group_create(void *user, VNodeID node_id, uint16 group_id, const char *name)
{
	MainInfo	*min = user;

	message(min, 4, "there's a tag group %u.%u called \"%s\"\n", node_id, group_id, name);
	if(node_id == min->node_id)
	{
		message(min, 5, " that's in our current node\n");
		if(min->pending == PEND_TAGGROUP_CREATE)
		{
			message(min, 5, "  and we're tag group-create blocked, how interesting\n");
			dict_set(&min->tag_groups, name, group_id);
			pend_sub(min);
		}
		else
			message(min, 5, "Unexpected tag group creation received (%u.%u \"%s\")\n", node_id, group_id, name);
	}
}

static void cb_node_create(void *user, VNodeID node_id, VNodeType type, VNodeOwner owner)
{
	MainInfo	*min = user;

	if(type == V_NT_OBJECT && node_id == min->avatar)
		return;
	if(owner != VN_OWNER_MINE)
		return;
	message(min, 2, "There's a node called %u of type %d\n", node_id, type);
	if(min->pending == PEND_NODE_CREATE && type == min->type)
	{
		VNodeID	local;
		const char	*name;

		local = attrib_get_ref(min->node, "id", 'n', ~0);
		if(local == ~0)
		{
			fprintf(stderr, "loader: Failed to read ID attrib\n");
			return;
		}
		message(min, 2, "that means node '%s' got created\n", xmlnode_attrib_get_value(min->node, "id"));
		node_map_set(min, local, node_id);
		min->node_id = node_id;
		min->pending = PEND_NONE;
		message(min, 3, "sending node_subscribe, node %u\n", node_id);
		verse_send_node_subscribe(node_id);
		if((name = xmlnode_attrib_get_value(min->node, "name")) != NULL)
		{
			verse_send_node_name_set(node_id, name);
			message(min, 2, "Name set, node %u is \"%s\"\n", node_id, name);
		}
	}
}

static int type_to_position(const char *type)
{
	const char	*order[] = { "audio", "bitmap", "curve", "geometry",
				     "text", "material", "object" };
	size_t	i;

	for(i = 0; i < sizeof order / sizeof *order; i++)
	{
		if(strcmp(order[i], type) == 0)
			return i;
	}
	return 0;
}

static int cmp_node(const void *a, const void *b)
{
	const XmlNode	*na = a, *nb = b;
	const char	*ta = xmlnode_get_name(na) + 4 + 1,
			*tb = xmlnode_get_name(nb) + 4 + 1;
	int		pa = type_to_position(ta), pb = type_to_position(tb);

	return pa < pb ? -1 : pa > pb;
}

static List * file_begin(MainInfo *min)
{
	List	*list, *iter, *sorted = NULL;

	list = xmlnode_nodeset_get(list_data(min->files), XMLNODE_AXIS_CHILD, XMLNODE_NAME_PREFIX("node"), XMLNODE_DONE);
	for(iter = list; iter != NULL; iter = list_next(iter))
		sorted = list_insert_sorted(sorted, list_data(iter), cmp_node);
	return sorted;
}

static void cb_connect_accept(void *user, VNodeID avatar, const char *address, const uint8 *host_id)
{
	MainInfo	*min = user;
	List		*sorted, *iter, *last = NULL;

	min->avatar = avatar;
	message(min, 1, "Connected as %u to %s\n", avatar, address);
	verse_send_node_index_subscribe(~0);

	/* Sort the nodes in a good access-order, then flatten them and concatenate the resulting element-lists. */
	min->file_nodes = NULL;
	/* This loop avoids doing concat() from the head of the list, instead maintaining a last-pointer
	 * and concatenating only from there. This (roughly) doubles the speed on a big VML file.
	*/
	for(iter = sorted = file_begin(min); iter != NULL; iter = list_next(iter))
	{
		List *elems = xmlnode_iter_begin(list_data(iter));
		if(last == NULL)
			last = min->file_nodes = list_concat(min->file_nodes, elems);
		else
			list_concat(last, elems);
		last = list_last(elems);
	}

	list_destroy(sorted);
	min->iter = min->file_nodes;/*xmlnode_iter_next(min->list, NULL);*/	/* Skip the toplevel "vml" node. */

	min->pending = PEND_NONE;
}

/* ----------------------------------------------------------------------------------------- */

XmlNode * load(const char *filename)
{
	FILE	*in;

	if((in = fopen(filename, "r")) != NULL)
	{
		char	buf[1024];
		DynStr	*text = dynstr_new_sized(2048);
		size_t	got;
		XmlNode	*n;

		while((got = fread(buf, 1, sizeof buf - 1, in)) > 0)
		{
			buf[got] = '\0';
			dynstr_append(text, buf);
		}
		fclose(in);
		n = xmlnode_new(dynstr_string(text));
		dynstr_destroy(text, 1);
		return n;
	}
	return NULL;
}

int main(int argc, char *argv[])
{
	int		i, j, last_pend, last_count;
	XmlNode		*n;
	MainInfo	min;
	const char	*server = "localhost";

	list_init();

	min.files = NULL;
	min.log_level = 0;

	for(i = 1; argv[i] != NULL; i++)
	{
		if(strncmp(argv[i], "-ip=", 4) == 0)
			server = argv[i] + 4;
		else if(strncmp(argv[i], "-v", 2) == 0)
			for(j = 1; argv[i][j] == 'v'; j++, min.log_level++);
		else if(strncmp(argv[i], "-q", 2) == 0)
			for(j = 1; argv[i][j] == 'q'; j++, min.log_level--);
		else if(argv[i][0] != '-')
		{
			n = load(argv[i]);
			if(n != NULL)
			{
				min.files = list_append(min.files, n);
			}
			else
				fprintf(stderr, "loader: Couldn't load VML from \"%s\"\n", argv[i]);
		}
	}
	message(&min, 0, "Loaded %u VML files, about to connect\n", list_length(min.files));

	if(min.files == NULL)
	{
		fprintf(stderr, "loader: No VML files loaded, aborting\n");
		return EXIT_FAILURE;
	}

	verse_callback_set(verse_send_connect_accept,		cb_connect_accept,		&min);
	verse_callback_set(verse_send_node_create,		cb_node_create,			&min);
	verse_callback_set(verse_send_tag_group_create,		cb_tag_group_create,		&min);
	verse_callback_set(verse_send_o_method_group_create,	cb_o_method_group_create,	&min);
	verse_callback_set(verse_send_g_layer_create,		cb_g_layer_create,		&min);
	verse_callback_set(verse_send_m_fragment_create,	cb_m_fragment_create,		&min);
	verse_callback_set(verse_send_b_layer_create,		cb_b_layer_create,		&min);
	verse_callback_set(verse_send_c_curve_create,		cb_c_curve_create,		&min);
	verse_callback_set(verse_send_t_buffer_create,		cb_t_buffer_create,		&min);
	verse_callback_set(verse_send_a_buffer_create,		cb_a_buffer_create,		&min);

	verse_send_connect("loader", "<secret>", server, NULL);

	min.avatar = ~0u;
	min.pending = PEND_CONNECT;
	min.pend_count = 0;
	min.node = NULL;
	min.node_id = ~0u;
	min.node_map = NULL;
	min.node_map_size = 0u;
	min.fragment_map = NULL;
	min.fragment_map_size = 0u;
	dict_ctor(&min.tag_groups);
	dict_ctor(&min.layer_ids);

	/* Wait for connect to happen, otherwise min.iter is NULL and below loop exits too soon. */
	while(min.avatar == ~0u)
		verse_callback_update(10000);

	/* Upload everything but objects. */
	min.skip_objects = 1;
	last_pend = -1;
	last_count = -1;
	message(&min, 1, "About to upload non-object nodes\n2");
	while(min.iter != NULL)
	{
		verse_callback_update(10000);
		if(min.pending != last_pend || min.pend_count != last_count)
		{
			last_pend = min.pending;
			last_count = min.pend_count;
		}
		if(min.pending == PEND_NONE && min.iter != NULL)
			step(&min);
	}

	/* Then iterate again, now over objects only. */
	message(&min, 1, "Done, this would be a good time to upload the object nodes\n");
	min.skip_objects = 0;
	min.iter = min.file_nodes;
	while(min.iter != NULL)
	{
		verse_callback_update(10000);
		if(min.pending == PEND_NONE && min.iter != NULL)
			step(&min);
	}
	do
	{
		verse_callback_update(10000);
	} while(verse_session_get_size() >= 10);

	verse_send_connect_terminate("localhost", "All done, exiting");
	message(&min, 2, "All done, exiting\n");

	return EXIT_SUCCESS;
}
