/**************************************************************************/
/*  material_instance.h                                                   */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

#include "scene/resources/material.h"

// Unreal-style material instance: inherits everything from a parent (a master ShaderMaterial or
// another MaterialInstance) and only stores the shader parameters and static switches it overrides.
// Static switches select a precompiled variant of the master shader instead of branching at runtime.
class MaterialInstance : public Material {
	GDCLASS(MaterialInstance, Material);

	Ref<Material> parent;
	Ref<Shader> connected_shader;

	HashMap<StringName, Variant> parameter_overrides;
	HashMap<StringName, bool> static_switch_overrides;

	// The shader variant this instance renders with. Only acquired once the material RID exists.
	mutable Ref<Shader> variant_shader;
	mutable String variant_key;
	mutable RID variant_rid;
	mutable Mutex material_rid_mutex;

	void _parent_changed();
	void _shader_changed();
	void _update_connections();

	void _collect_static_switches(Dictionary &r_switches) const;
	void _update_variant() const;
	void _release_variant() const;
	void _check_material_rid() const;
	void _apply_parameter(const StringName &p_param) const;
	void _apply_all_parameters() const;
	void _refresh();

	Variant _get_parameter_default(const StringName &p_param) const;
	void _get_variant_uniform_list(List<PropertyInfo> *r_list) const;

protected:
	bool _set(const StringName &p_name, const Variant &p_value);
	bool _get(const StringName &p_name, Variant &r_ret) const;
	void _get_property_list(List<PropertyInfo> *p_list) const;

	static void _bind_methods();

	virtual bool _can_do_next_pass() const override;
	virtual bool _can_use_render_priority() const override;

public:
	static constexpr const char *PARAMETER_PREFIX = "shader_parameter/";
	static constexpr const char *STATIC_SWITCH_PREFIX = "static_switch/";

	void set_parent(const Ref<Material> &p_parent);
	Ref<Material> get_parent() const;

	Ref<ShaderMaterial> get_master_material() const;
	Ref<Shader> get_shader() const;

	void set_shader_parameter(const StringName &p_param, const Variant &p_value);
	Variant get_shader_parameter(const StringName &p_param) const;
	void clear_shader_parameter(const StringName &p_param);
	bool is_shader_parameter_overridden(const StringName &p_param) const;
	Variant get_inherited_shader_parameter(const StringName &p_param) const;

	void set_static_switch(const StringName &p_name, bool p_enabled);
	bool get_static_switch(const StringName &p_name) const;
	void clear_static_switch(const StringName &p_name);
	bool is_static_switch_overridden(const StringName &p_name) const;
	bool get_inherited_static_switch(const StringName &p_name) const;
	Dictionary get_static_switches() const;

	// Value the property would have without this instance's override (used by the inspector's override checkboxes).
	Variant get_inherited_property_value(const StringName &p_property) const;

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_rid() const override;
	virtual RID get_shader_rid() const override;

	MaterialInstance();
	~MaterialInstance();
};
