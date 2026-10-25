/**************************************************************************/
/*  material_instance.cpp                                                 */
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

#include "material_instance.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/object/property_info.h"
#include "servers/rendering/rendering_server.h"

static constexpr int MAX_PARENT_DEPTH = 64;

void MaterialInstance::set_parent(const Ref<Material> &p_parent) {
	if (p_parent == parent) {
		return;
	}

	if (p_parent.is_valid()) {
		ERR_FAIL_COND_MSG(!Object::cast_to<ShaderMaterial>(p_parent.ptr()) && !Object::cast_to<MaterialInstance>(p_parent.ptr()), "A MaterialInstance parent must be a ShaderMaterial or another MaterialInstance.");

		Ref<MaterialInstance> ancestor = p_parent;
		for (int depth = 0; ancestor.is_valid(); depth++) {
			ERR_FAIL_COND_MSG(ancestor == this, "Can't use this MaterialInstance (or one of its children) as its own parent.");
			ERR_FAIL_COND_MSG(depth > MAX_PARENT_DEPTH, "MaterialInstance parent chain is too deep.");
			ancestor = ancestor->get_parent();
		}
	}

	if (parent.is_valid()) {
		parent->disconnect_changed(callable_mp(this, &MaterialInstance::_parent_changed));
	}
	parent = p_parent;
	if (parent.is_valid()) {
		parent->connect_changed(callable_mp(this, &MaterialInstance::_parent_changed));
	}

	_update_connections();
	_refresh();
}

Ref<Material> MaterialInstance::get_parent() const {
	return parent;
}

Ref<ShaderMaterial> MaterialInstance::get_master_material() const {
	Ref<Material> current = parent;
	for (int depth = 0; current.is_valid() && depth <= MAX_PARENT_DEPTH; depth++) {
		Ref<ShaderMaterial> shader_material = current;
		if (shader_material.is_valid()) {
			return shader_material;
		}
		Ref<MaterialInstance> instance = current;
		if (instance.is_null()) {
			break;
		}
		current = instance->get_parent();
	}
	return Ref<ShaderMaterial>();
}

Ref<Shader> MaterialInstance::get_shader() const {
	Ref<ShaderMaterial> master = get_master_material();
	return master.is_valid() ? master->get_shader() : Ref<Shader>();
}

void MaterialInstance::_update_connections() {
	// Instances parented to another instance get shader changes through the parent's `changed` signal.
	Ref<Shader> shader;
	Ref<ShaderMaterial> shader_material = parent;
	if (shader_material.is_valid()) {
		shader = shader_material->get_shader();
	}

	if (shader == connected_shader) {
		return;
	}
	if (connected_shader.is_valid()) {
		connected_shader->disconnect_changed(callable_mp(this, &MaterialInstance::_shader_changed));
	}
	connected_shader = shader;
	if (connected_shader.is_valid()) {
		connected_shader->connect_changed(callable_mp(this, &MaterialInstance::_shader_changed));
	}
}

void MaterialInstance::_parent_changed() {
	_update_connections();
	_refresh();
}

void MaterialInstance::_shader_changed() {
	_refresh();
}

void MaterialInstance::_refresh() {
	if (_get_material().is_valid()) {
		_update_variant();
		_apply_all_parameters();
	}
	notify_property_list_changed();
	emit_changed();
}

///////////////////////////////////
// Static switches.

void MaterialInstance::_collect_static_switches(Dictionary &r_switches) const {
	Ref<MaterialInstance> parent_instance = parent;
	if (parent_instance.is_valid()) {
		parent_instance->_collect_static_switches(r_switches);
	}
	for (const KeyValue<StringName, bool> &E : static_switch_overrides) {
		r_switches[E.key] = E.value;
	}
}

void MaterialInstance::set_static_switch(const StringName &p_name, bool p_enabled) {
	const bool *current = static_switch_overrides.getptr(p_name);
	if (current && *current == p_enabled) {
		return;
	}
	static_switch_overrides[p_name] = p_enabled;
	_refresh();
}

bool MaterialInstance::get_static_switch(const StringName &p_name) const {
	const bool *value = static_switch_overrides.getptr(p_name);
	return value ? *value : get_inherited_static_switch(p_name);
}

void MaterialInstance::clear_static_switch(const StringName &p_name) {
	if (static_switch_overrides.erase(p_name)) {
		_refresh();
	}
}

bool MaterialInstance::is_static_switch_overridden(const StringName &p_name) const {
	return static_switch_overrides.has(p_name);
}

bool MaterialInstance::get_inherited_static_switch(const StringName &p_name) const {
	Ref<MaterialInstance> parent_instance = parent;
	if (parent_instance.is_valid()) {
		return parent_instance->get_static_switch(p_name);
	}
	Ref<Shader> shader = get_shader();
	return shader.is_valid() && shader->get_static_switch_default(p_name);
}

Dictionary MaterialInstance::get_static_switches() const {
	Dictionary ret;
	Ref<Shader> shader = get_shader();
	if (shader.is_valid()) {
		for (const StringName &E : shader->get_static_switch_list()) {
			ret[E] = get_static_switch(E);
		}
	}
	return ret;
}

void MaterialInstance::_update_variant() const {
	RID material_rid = _get_material();
	Ref<Shader> shader = get_shader();

	RID new_rid;
	String new_key;
	if (shader.is_valid()) {
		Dictionary switches;
		_collect_static_switches(switches);
		new_rid = shader->acquire_static_switch_variant(switches, new_key);
	}

	// Acquire before releasing, so a variant still in use is not recompiled.
	_release_variant();
	variant_shader = shader;
	variant_key = new_key;

	if (new_rid != variant_rid) {
		variant_rid = new_rid;
		RS::get_singleton()->material_set_shader(material_rid, variant_rid);
	}
}

void MaterialInstance::_release_variant() const {
	if (variant_shader.is_valid()) {
		variant_shader->release_static_switch_variant(variant_key);
	}
	variant_shader.unref();
	variant_key = String();
}

///////////////////////////////////
// Shader parameters.

void MaterialInstance::set_shader_parameter(const StringName &p_param, const Variant &p_value) {
	if (p_value.get_type() == Variant::NIL) {
		clear_shader_parameter(p_param);
		return;
	}

	const bool was_overridden = parameter_overrides.has(p_param);
	parameter_overrides[p_param] = p_value;
	_apply_parameter(p_param);

	if (!was_overridden) {
		notify_property_list_changed();
	}
	emit_changed();
}

Variant MaterialInstance::get_shader_parameter(const StringName &p_param) const {
	const Variant *value = parameter_overrides.getptr(p_param);
	return value ? *value : get_inherited_shader_parameter(p_param);
}

void MaterialInstance::clear_shader_parameter(const StringName &p_param) {
	if (!parameter_overrides.erase(p_param)) {
		return;
	}
	_apply_parameter(p_param);
	notify_property_list_changed();
	emit_changed();
}

bool MaterialInstance::is_shader_parameter_overridden(const StringName &p_param) const {
	return parameter_overrides.has(p_param);
}

Variant MaterialInstance::get_inherited_shader_parameter(const StringName &p_param) const {
	Ref<MaterialInstance> parent_instance = parent;
	if (parent_instance.is_valid()) {
		return parent_instance->get_shader_parameter(p_param);
	}
	Ref<ShaderMaterial> shader_material = parent;
	if (shader_material.is_valid()) {
		Variant value = shader_material->get_shader_parameter(p_param);
		if (value.get_type() != Variant::NIL) {
			return value;
		}
	}
	return _get_parameter_default(p_param);
}

Variant MaterialInstance::_get_parameter_default(const StringName &p_param) const {
	RID shader_rid = variant_rid;
	if (shader_rid.is_null()) {
		Ref<Shader> shader = get_shader();
		if (shader.is_null()) {
			return Variant();
		}
		shader_rid = shader->get_rid();
	}
	return RS::get_singleton()->shader_get_parameter_default(shader_rid, p_param);
}

void MaterialInstance::_apply_parameter(const StringName &p_param) const {
	RID material_rid = _get_material();
	if (material_rid.is_null()) {
		return;
	}

	Variant value = get_shader_parameter(p_param);
	if (value.get_type() == Variant::OBJECT) {
		RID texture_rid = value;
		RS::get_singleton()->material_set_param(material_rid, p_param, texture_rid.is_valid() ? Variant(texture_rid) : Variant());
	} else {
		RS::get_singleton()->material_set_param(material_rid, p_param, value);
	}
}

void MaterialInstance::_apply_all_parameters() const {
	List<PropertyInfo> uniforms;
	_get_variant_uniform_list(&uniforms);
	for (const PropertyInfo &E : uniforms) {
		if (!(E.usage & (PROPERTY_USAGE_GROUP | PROPERTY_USAGE_SUBGROUP))) {
			_apply_parameter(E.name);
		}
	}
}

void MaterialInstance::_get_variant_uniform_list(List<PropertyInfo> *r_list) const {
	Ref<Shader> shader = get_shader();
	if (shader.is_null()) {
		return;
	}

	// Uniforms stripped out by static switches are not part of the variant, so they are hidden like in Unreal.
	RID shader_rid = variant_rid.is_valid() ? variant_rid : shader->get_rid();
	List<PropertyInfo> uniforms;
	RS::get_singleton()->get_shader_parameter_list(shader_rid, &uniforms);

	List<StringName> default_textures;
	shader->get_default_texture_parameter_list(&default_textures);

	for (PropertyInfo &pi : uniforms) {
		if (default_textures.find(pi.name)) {
			continue;
		}
		if (pi.type == Variant::RID) {
			pi.type = Variant::OBJECT;
		}
		r_list->push_back(pi);
	}
}

Variant MaterialInstance::get_inherited_property_value(const StringName &p_property) const {
	const String name = p_property;
	if (name.begins_with(PARAMETER_PREFIX)) {
		return get_inherited_shader_parameter(name.trim_prefix(PARAMETER_PREFIX));
	}
	if (name.begins_with(STATIC_SWITCH_PREFIX)) {
		return get_inherited_static_switch(name.trim_prefix(STATIC_SWITCH_PREFIX));
	}
	return Variant();
}

bool MaterialInstance::is_property_overridden(const StringName &p_property) const {
	const String name = p_property;
	if (name.begins_with(PARAMETER_PREFIX)) {
		return is_shader_parameter_overridden(name.trim_prefix(PARAMETER_PREFIX));
	}
	if (name.begins_with(STATIC_SWITCH_PREFIX)) {
		return is_static_switch_overridden(name.trim_prefix(STATIC_SWITCH_PREFIX));
	}
	return true;
}

///////////////////////////////////
// Properties.

bool MaterialInstance::_set(const StringName &p_name, const Variant &p_value) {
	const String name = p_name;
	if (name.begins_with(PARAMETER_PREFIX)) {
		set_shader_parameter(name.trim_prefix(PARAMETER_PREFIX), p_value);
		return true;
	}
	if (name.begins_with(STATIC_SWITCH_PREFIX)) {
		const StringName switch_name = name.trim_prefix(STATIC_SWITCH_PREFIX);
		if (p_value.get_type() == Variant::NIL) {
			clear_static_switch(switch_name);
		} else {
			set_static_switch(switch_name, p_value);
		}
		return true;
	}
	return false;
}

bool MaterialInstance::_get(const StringName &p_name, Variant &r_ret) const {
	const String name = p_name;
	if (name.begins_with(PARAMETER_PREFIX)) {
		r_ret = get_shader_parameter(name.trim_prefix(PARAMETER_PREFIX));
		return true;
	}
	if (name.begins_with(STATIC_SWITCH_PREFIX)) {
		r_ret = get_static_switch(name.trim_prefix(STATIC_SWITCH_PREFIX));
		return true;
	}
	return false;
}

void MaterialInstance::_get_property_list(List<PropertyInfo> *p_list) const {
	// Every inherited value is shown with a checkbox; only checked (overridden) values are saved.
	HashSet<StringName> listed_switches;
	Ref<Shader> shader = get_shader();
	if (shader.is_valid()) {
		const Vector<StringName> switches = shader->get_static_switch_list();
		if (!switches.is_empty()) {
			p_list->push_back(PropertyInfo(Variant::NIL, "Static Switches", PROPERTY_HINT_NONE, STATIC_SWITCH_PREFIX, PROPERTY_USAGE_GROUP));
		}
		for (const StringName &E : switches) {
			uint32_t usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_CHECKABLE;
			if (static_switch_overrides.has(E)) {
				usage |= PROPERTY_USAGE_CHECKED | PROPERTY_USAGE_STORAGE;
			}
			p_list->push_back(PropertyInfo(Variant::BOOL, String(STATIC_SWITCH_PREFIX) + String(E), PROPERTY_HINT_NONE, "", usage));
			listed_switches.insert(E);
		}
	}

	HashSet<StringName> listed_parameters;
	List<PropertyInfo> uniforms;
	_get_variant_uniform_list(&uniforms);
	bool has_group = false;
	for (const PropertyInfo &pi : uniforms) {
		if (pi.usage == PROPERTY_USAGE_SUBGROUP) {
			continue;
		}
		if (pi.usage == PROPERTY_USAGE_GROUP) {
			p_list->push_back(PropertyInfo(Variant::NIL, pi.name.is_empty() ? String("Shader Parameters") : pi.name.capitalize(), PROPERTY_HINT_NONE, PARAMETER_PREFIX, PROPERTY_USAGE_GROUP));
			has_group = true;
			continue;
		}
		if (!has_group) {
			p_list->push_back(PropertyInfo(Variant::NIL, "Shader Parameters", PROPERTY_HINT_NONE, PARAMETER_PREFIX, PROPERTY_USAGE_GROUP));
			has_group = true;
		}

		PropertyInfo info = pi;
		info.name = PARAMETER_PREFIX + info.name;
		info.usage = PROPERTY_USAGE_EDITOR | PROPERTY_USAGE_CHECKABLE;
		if (parameter_overrides.has(pi.name)) {
			info.usage |= PROPERTY_USAGE_CHECKED | PROPERTY_USAGE_STORAGE;
		}
		p_list->push_back(info);
		listed_parameters.insert(pi.name);
	}

	// Keep overrides the current parent doesn't expose (missing parent, switched-off uniforms), so they aren't lost on save.
	for (const KeyValue<StringName, bool> &E : static_switch_overrides) {
		if (!listed_switches.has(E.key)) {
			p_list->push_back(PropertyInfo(Variant::BOOL, String(STATIC_SWITCH_PREFIX) + String(E.key), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		}
	}
	for (const KeyValue<StringName, Variant> &E : parameter_overrides) {
		if (!listed_parameters.has(E.key)) {
			p_list->push_back(PropertyInfo(E.value.get_type(), String(PARAMETER_PREFIX) + String(E.key), PROPERTY_HINT_NONE, "", PROPERTY_USAGE_STORAGE));
		}
	}
}

///////////////////////////////////
// Rendering.

void MaterialInstance::_check_material_rid() const {
	MutexLock lock(material_rid_mutex);
	if (_get_material().is_valid()) {
		return;
	}

	RID next_pass_rid;
	if (get_next_pass().is_valid()) {
		next_pass_rid = get_next_pass()->get_rid();
	}
	_set_material(RS::get_singleton()->material_create_from_shader(next_pass_rid, get_render_priority(), RID()));
	_update_variant();
	_apply_all_parameters();
}

RID MaterialInstance::get_rid() const {
	_check_material_rid();
	return Material::get_rid();
}

RID MaterialInstance::get_shader_rid() const {
	_check_material_rid();
	return variant_rid;
}

Shader::Mode MaterialInstance::get_shader_mode() const {
	Ref<Shader> shader = get_shader();
	return shader.is_valid() ? shader->get_mode() : Shader::MODE_SPATIAL;
}

bool MaterialInstance::_can_do_next_pass() const {
	Ref<Shader> shader = get_shader();
	return shader.is_valid() && shader->get_mode() == Shader::MODE_SPATIAL;
}

bool MaterialInstance::_can_use_render_priority() const {
	return _can_do_next_pass();
}

void MaterialInstance::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_parent", "parent"), &MaterialInstance::set_parent);
	ClassDB::bind_method(D_METHOD("get_parent"), &MaterialInstance::get_parent);
	ClassDB::bind_method(D_METHOD("get_master_material"), &MaterialInstance::get_master_material);
	ClassDB::bind_method(D_METHOD("get_shader"), &MaterialInstance::get_shader);
	ClassDB::bind_method(D_METHOD("get_shader_rid"), &MaterialInstance::get_shader_rid);

	ClassDB::bind_method(D_METHOD("set_shader_parameter", "param", "value"), &MaterialInstance::set_shader_parameter);
	ClassDB::bind_method(D_METHOD("get_shader_parameter", "param"), &MaterialInstance::get_shader_parameter);
	ClassDB::bind_method(D_METHOD("clear_shader_parameter", "param"), &MaterialInstance::clear_shader_parameter);
	ClassDB::bind_method(D_METHOD("is_shader_parameter_overridden", "param"), &MaterialInstance::is_shader_parameter_overridden);
	ClassDB::bind_method(D_METHOD("get_inherited_shader_parameter", "param"), &MaterialInstance::get_inherited_shader_parameter);

	ClassDB::bind_method(D_METHOD("set_static_switch", "name", "enabled"), &MaterialInstance::set_static_switch);
	ClassDB::bind_method(D_METHOD("get_static_switch", "name"), &MaterialInstance::get_static_switch);
	ClassDB::bind_method(D_METHOD("clear_static_switch", "name"), &MaterialInstance::clear_static_switch);
	ClassDB::bind_method(D_METHOD("is_static_switch_overridden", "name"), &MaterialInstance::is_static_switch_overridden);
	ClassDB::bind_method(D_METHOD("get_inherited_static_switch", "name"), &MaterialInstance::get_inherited_static_switch);
	ClassDB::bind_method(D_METHOD("get_static_switches"), &MaterialInstance::get_static_switches);

	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "parent", PROPERTY_HINT_RESOURCE_TYPE, "ShaderMaterial,MaterialInstance"), "set_parent", "get_parent");
}

MaterialInstance::MaterialInstance() {
	// Material RID will be empty until it is required.
}

MaterialInstance::~MaterialInstance() {
	// Free the material before its shader variant, so the variant isn't freed while still in use.
	RID material_rid = _get_material();
	if (material_rid.is_valid()) {
		RS::get_singleton()->free_rid(material_rid);
		_set_material(RID());
	}
	_release_variant();
}
