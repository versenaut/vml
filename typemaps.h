/*
 * Header file for the type-mapping module used by the loader.
 *
 * Written by Emil Brink, Copyright (c) PDC, KTH. This code is licensed under
 * the GPL license, see the COPYING.loader file for details.
*/

extern VNodeType	node_type_from_string(const char *str);
extern VNOParamType	o_method_param_type_from_string(const char *str);
extern VNGLayerType	g_layer_type_from_string(const char *str);
extern VNMFragmentType	m_fragment_type_from_string(const char *str);
extern VNMLightType	m_light_type_from_string(const char *str);
extern VNMNoiseType	m_noise_type_from_string(const char *str);
extern VNMBlendType	m_blend_type_from_string(const char *str);
extern VNMRampType	m_ramp_type_from_string(const char *str);
extern VNMRampChannel	m_ramp_channel_from_string(const char *str);
extern VNBLayerType	b_layer_type_from_string(const char *str);
extern VNABlockType	a_block_type_from_string(const char *str);
