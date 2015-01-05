/*
 * Copyright (c) 2014, NVIDIA CORPORATION.  All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
/*
 * Function naming determines intended use:
 *
 *     <x>_r(void) : Returns the offset for register <x>.
 *
 *     <x>_o(void) : Returns the offset for element <x>.
 *
 *     <x>_w(void) : Returns the word offset for word (4 byte) element <x>.
 *
 *     <x>_<y>_s(void) : Returns size of field <y> of register <x> in bits.
 *
 *     <x>_<y>_f(u32 v) : Returns a value based on 'v' which has been shifted
 *         and masked to place it at field <y> of register <x>.  This value
 *         can be |'d with others to produce a full register value for
 *         register <x>.
 *
 *     <x>_<y>_m(void) : Returns a mask for field <y> of register <x>.  This
 *         value can be ~'d and then &'d to clear the value of field <y> for
 *         register <x>.
 *
 *     <x>_<y>_<z>_f(void) : Returns the constant value <z> after being shifted
 *         to place it at field <y> of register <x>.  This value can be |'d
 *         with others to produce a full register value for <x>.
 *
 *     <x>_<y>_v(u32 r) : Returns the value of field <y> from a full register
 *         <x> value 'r' after being shifted to place its LSB at bit 0.
 *         This value is suitable for direct comparison with other unshifted
 *         values appropriate for use in field <y> of register <x>.
 *
 *     <x>_<y>_<z>_v(void) : Returns the constant value for <z> defined for
 *         field <y> of register <x>.  This value is suitable for direct
 *         comparison with unshifted values appropriate for use in field <y>
 *         of register <x>.
 */
#ifndef _hw_win_nvdisp_h_
#define _hw_win_nvdisp_h_

static inline u32 win_base_addr_dc_winc_r(void)
{
	return 0x00000500;
}
static inline u32 win_base_addr_dc_win_r(void)
{
	return 0x00000700;
}
static inline u32 win_base_addr_dc_winbuf_r(void)
{
	return 0x00000800;
}
static inline u32 win_base_addr_dc_a_winc_r(void)
{
	return 0x00000a00;
}
static inline u32 win_base_addr_dc_a_win_r(void)
{
	return 0x00000b80;
}
static inline u32 win_base_addr_dc_a_winbuf_r(void)
{
	return 0x00000bc0;
}
static inline u32 win_base_addr_dc_b_winc_r(void)
{
	return 0x00000d00;
}
static inline u32 win_base_addr_dc_b_win_r(void)
{
	return 0x00000e80;
}
static inline u32 win_base_addr_dc_b_winbuf_r(void)
{
	return 0x00000ec0;
}
static inline u32 win_base_addr_dc_c_winc_r(void)
{
	return 0x00001000;
}
static inline u32 win_base_addr_dc_c_win_r(void)
{
	return 0x00001180;
}
static inline u32 win_base_addr_dc_c_winbuf_r(void)
{
	return 0x000011c0;
}
static inline u32 win_base_addr_dc_d_winc_r(void)
{
	return 0x00001300;
}
static inline u32 win_base_addr_dc_d_win_r(void)
{
	return 0x00001480;
}
static inline u32 win_base_addr_dc_d_winbuf_r(void)
{
	return 0x000014c0;
}
static inline u32 win_base_addr_dc_e_winc_r(void)
{
	return 0x00001600;
}
static inline u32 win_base_addr_dc_e_win_r(void)
{
	return 0x00001780;
}
static inline u32 win_base_addr_dc_e_winbuf_r(void)
{
	return 0x000017c0;
}
static inline u32 win_base_addr_dc_f_winc_r(void)
{
	return 0x00001900;
}
static inline u32 win_base_addr_dc_f_win_r(void)
{
	return 0x00001a80;
}
static inline u32 win_base_addr_dc_f_winbuf_r(void)
{
	return 0x00001ac0;
}
static inline u32 win_act_control_r(void)
{
	return 0x0000050e;
}
static inline u32 win_act_control_ctrl_sel_vcounter_f(void)
{
	return 0x0;
}
static inline u32 win_act_control_ctrl_sel_hcounter_f(void)
{
	return 0x1;
}
static inline u32 win_options_r(void)
{
	return 0x00000700;
}
static inline u32 win_options_v_direction_increment_f(void)
{
	return 0x0;
}
static inline u32 win_options_v_direction_decrement_f(void)
{
	return 0x4;
}
static inline u32 win_options_h_direction_f(u32 v)
{
	return (v & 0x1) << 0;
}
static inline u32 win_options_h_direction_increment_f(void)
{
	return 0x0;
}
static inline u32 win_options_h_direction_decrement_f(void)
{
	return 0x1;
}
static inline u32 win_options_scan_column_f(u32 v)
{
	return (v & 0x1) << 4;
}
static inline u32 win_options_scan_column_disable_f(void)
{
	return 0x0;
}
static inline u32 win_options_scan_column_enable_f(void)
{
	return 0x10;
}
static inline u32 win_options_win_enable_enable_f(void)
{
	return 0x40000000;
}
static inline u32 win_options_win_enable_disable_f(void)
{
	return 0x0;
}
static inline u32 win_options_color_expand_enable_f(void)
{
	return 0x40;
}
static inline u32 win_options_cp_enable_enable_f(void)
{
	return 0x10000;
}
static inline u32 win_set_control_r(void)
{
	return 0x00000702;
}
static inline u32 win_set_control_owner_f(u32 v)
{
	return (v & 0xf) << 0;
}
static inline u32 win_set_control_owner_none_f(void)
{
	return 0xf;
}
static inline u32 win_color_depth_r(void)
{
	return 0x00000703;
}
static inline u32 win_position_r(void)
{
	return 0x00000704;
}
static inline u32 win_position_h_position_f(u32 v)
{
	return (v & 0x7fff) << 0;
}
static inline u32 win_position_v_position_f(u32 v)
{
	return (v & 0x7fff) << 16;
}
static inline u32 win_size_r(void)
{
	return 0x00000705;
}
static inline u32 win_size_h_size_f(u32 v)
{
	return (v & 0x7fff) << 0;
}
static inline u32 win_size_v_size_f(u32 v)
{
	return (v & 0x7fff) << 16;
}
static inline u32 win_set_cropped_size_in_r(void)
{
	return 0x00000706;
}
static inline u32 win_set_cropped_size_in_height_f(u32 v)
{
	return (v & 0x7fff) << 16;
}
static inline u32 win_set_cropped_size_in_width_f(u32 v)
{
	return (v & 0x7fff) << 0;
}
static inline u32 win_set_planar_storage_r(void)
{
	return 0x00000709;
}
static inline u32 win_set_planar_storage_uv_r(void)
{
	return 0x0000070a;
}
static inline u32 win_set_planar_storage_uv_uv0_f(u32 v)
{
	return (v & 0x1fff) << 0;
}
static inline u32 win_set_planar_storage_uv_uv1_f(u32 v)
{
	return (v & 0x1fff) << 16;
}
static inline u32 win_win_set_params_r(void)
{
	return 0x0000070d;
}
static inline u32 win_win_set_params_cs_range_yuv_709_f(void)
{
	return 0x200;
}
static inline u32 win_win_set_params_cs_range_rgb_f(void)
{
	return 0x0;
}
static inline u32 win_win_set_params_in_range_bypass_f(void)
{
	return 0x0;
}
static inline u32 win_win_set_params_degamma_range_none_f(void)
{
	return 0x0;
}
static inline u32 win_blend_layer_control_r(void)
{
	return 0x00000716;
}
static inline u32 win_blend_layer_control_color_key_src_f(void)
{
	return 0x2000000;
}
static inline u32 win_blend_layer_control_color_key_dest_f(void)
{
	return 0x4000000;
}
static inline u32 win_blend_layer_control_color_key_none_f(void)
{
	return 0x0;
}
static inline u32 win_blend_layer_control_blend_enable_bypass_f(void)
{
	return 0x1000000;
}
static inline u32 win_blend_layer_control_blend_enable_enable_f(void)
{
	return 0x0;
}
static inline u32 win_blend_layer_control_blend_enable_f(u32 v)
{
	return (v & 0x1) << 24;
}
static inline u32 win_blend_layer_control_k1_f(u32 v)
{
	return (v & 0xff) << 8;
}
static inline u32 win_blend_layer_control_k2_f(u32 v)
{
	return (v & 0xff) << 16;
}
static inline u32 win_blend_layer_control_depth_f(u32 v)
{
	return (v & 0xff) << 0;
}
static inline u32 win_blend_match_select_r(void)
{
	return 0x00000717;
}
static inline u32 win_blend_nomatch_select_r(void)
{
	return 0x00000718;
}
static inline u32 win_input_lut_base_r(void)
{
	return 0x00000720;
}
static inline u32 win_input_lut_base_hi_r(void)
{
	return 0x00000721;
}
static inline u32 win_input_lut_ctl_r(void)
{
	return 0x00000723;
}
static inline u32 win_window_set_control_r(void)
{
	return 0x00000730;
}
static inline u32 win_window_set_control_csc_enable_f(void)
{
	return 0x20;
}
static inline u32 win_start_addr_r(void)
{
	return 0x00000800;
}
static inline u32 win_start_addr_u_r(void)
{
	return 0x00000802;
}
static inline u32 win_start_addr_v_r(void)
{
	return 0x00000804;
}
static inline u32 win_cropped_point_r(void)
{
	return 0x00000806;
}
static inline u32 win_cropped_point_v_offset_f(u32 v)
{
	return (v & 0xffff) << 16;
}
static inline u32 win_cropped_point_h_offset_f(u32 v)
{
	return (v & 0xffff) << 0;
}
static inline u32 win_surface_kind_r(void)
{
	return 0x0000080b;
}
static inline u32 win_surface_kind_kind_pitch_f(void)
{
	return 0x0;
}
static inline u32 win_surface_kind_kind_tiled_f(void)
{
	return 0x1;
}
static inline u32 win_surface_kind_kind_bl_f(void)
{
	return 0x2;
}
static inline u32 win_surface_kind_block_height_f(u32 v)
{
	return (v & 0x7) << 4;
}
static inline u32 win_start_addr_hi_r(void)
{
	return 0x0000080d;
}
static inline u32 win_start_addr_hi_u_r(void)
{
	return 0x0000080f;
}
static inline u32 win_start_addr_hi_v_r(void)
{
	return 0x00000811;
}
static inline u32 win_start_addr_fld2_r(void)
{
	return 0x00000813;
}
static inline u32 win_start_addr_fld2_u_r(void)
{
	return 0x00000815;
}
static inline u32 win_start_addr_fld2_v_r(void)
{
	return 0x00000817;
}
static inline u32 win_start_addr_fld2_hi_r(void)
{
	return 0x00000819;
}
static inline u32 win_start_addr_fld2_hi_u_r(void)
{
	return 0x0000081b;
}
static inline u32 win_start_addr_fld2_hi_v_r(void)
{
	return 0x0000081d;
}
static inline u32 win_cropped_point_fld2_r(void)
{
	return 0x0000081f;
}
static inline u32 win_cropped_point_fld2_v_f(u32 v)
{
	return (v & 0xffff) << 16;
}
static inline u32 win_cropped_point_fld2_h_f(u32 v)
{
	return (v & 0xffff) << 0;
}
#endif
