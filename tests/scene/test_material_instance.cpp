/**************************************************************************/
/*  test_material_instance.cpp                                            */
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

#include "tests/test_macros.h"

TEST_FORCE_LINK(test_material_instance)

#include "scene/resources/material_instance.h"
#include "servers/rendering/rendering_server.h"

#include "modules/modules_enabled.gen.h" // For visual_shader.

#ifdef MODULE_VISUAL_SHADER_ENABLED
#include "modules/visual_shader/visual_shader.h"
#include "modules/visual_shader/vs_nodes/visual_shader_nodes.h"
#endif

namespace TestMaterialInstance {

static const char *MASTER_CODE =
		"shader_type spatial;\n"
		"#pragma static_switch USE_DETAIL false\n"
		"#pragma static_switch USE_TINT true\n"
		"uniform float roughness_value = 0.5;\n"
		"#ifdef USE_DETAIL\n"
		"uniform float detail_strength = 0.25;\n"
		"#endif\n"
		"#ifdef USE_TINT\n"
		"uniform vec4 tint : source_color = vec4(1.0);\n"
		"#endif\n"
		"void fragment() {\n"
		"	ALBEDO = vec3(roughness_value);\n"
		"#ifdef USE_DETAIL\n"
		"	ALBEDO += detail_strength;\n"
		"#endif\n"
		"#ifdef USE_TINT\n"
		"	ALBEDO *= tint.rgb;\n"
		"#endif\n"
		"}\n";

static Ref<ShaderMaterial> make_master() {
	Ref<Shader> shader;
	shader.instantiate();
	shader->set_code(MASTER_CODE);
	Ref<ShaderMaterial> master;
	master.instantiate();
	master->set_shader(shader);
	return master;
}

static bool has_uniform(RID p_shader, const String &p_name) {
	List<PropertyInfo> uniforms;
	RS::get_singleton()->get_shader_parameter_list(p_shader, &uniforms);
	for (const PropertyInfo &E : uniforms) {
		if (E.name == p_name) {
			return true;
		}
	}
	return false;
}

TEST_CASE("[SceneTree][MaterialInstance] Shader declares static switches") {
	Ref<ShaderMaterial> master = make_master();
	Ref<Shader> shader = master->get_shader();

	CHECK(shader->get_static_switch_list().size() == 2);
	CHECK(shader->has_static_switch("USE_DETAIL"));
	CHECK_FALSE(shader->get_static_switch_default("USE_DETAIL"));
	CHECK(shader->get_static_switch_default("USE_TINT"));
	CHECK_FALSE(shader->has_static_switch("USE_NOTHING"));
}

TEST_CASE("[SceneTree][MaterialInstance] Parameter inheritance and overrides") {
	Ref<ShaderMaterial> master = make_master();
	master->set_shader_parameter("roughness_value", 0.8);

	Ref<MaterialInstance> instance;
	instance.instantiate();
	instance->set_parent(master);

	CHECK(instance->get_master_material() == master);
	CHECK(instance->get_shader() == master->get_shader());
	CHECK(double(instance->get_shader_parameter("roughness_value")) == doctest::Approx(0.8));

	SUBCASE("Master changes propagate") {
		master->set_shader_parameter("roughness_value", 0.3);
		CHECK(double(instance->get_shader_parameter("roughness_value")) == doctest::Approx(0.3));
	}

	SUBCASE("Overrides win and can be cleared") {
		instance->set_shader_parameter("roughness_value", 0.1);
		CHECK(instance->is_shader_parameter_overridden("roughness_value"));
		master->set_shader_parameter("roughness_value", 0.9);
		CHECK(double(instance->get_shader_parameter("roughness_value")) == doctest::Approx(0.1));
		CHECK(double(instance->get_inherited_shader_parameter("roughness_value")) == doctest::Approx(0.9));

		instance->clear_shader_parameter("roughness_value");
		CHECK_FALSE(instance->is_shader_parameter_overridden("roughness_value"));
		CHECK(double(instance->get_shader_parameter("roughness_value")) == doctest::Approx(0.9));
	}

	SUBCASE("Setting null clears the override") {
		instance->set("shader_parameter/roughness_value", 0.1);
		CHECK(instance->is_shader_parameter_overridden("roughness_value"));
		instance->set("shader_parameter/roughness_value", Variant());
		CHECK_FALSE(instance->is_shader_parameter_overridden("roughness_value"));
	}

	SUBCASE("Child instances inherit through the chain") {
		instance->set_shader_parameter("roughness_value", 0.2);
		Ref<MaterialInstance> child;
		child.instantiate();
		child->set_parent(instance);
		CHECK(child->get_master_material() == master);
		CHECK(double(child->get_shader_parameter("roughness_value")) == doctest::Approx(0.2));

		instance->set_shader_parameter("roughness_value", 0.4);
		CHECK(double(child->get_shader_parameter("roughness_value")) == doctest::Approx(0.4));
	}
}

TEST_CASE("[SceneTree][MaterialInstance] Invalid parents are rejected") {
	Ref<ShaderMaterial> master = make_master();
	Ref<MaterialInstance> instance;
	instance.instantiate();
	instance->set_parent(master);
	Ref<MaterialInstance> child;
	child.instantiate();
	child->set_parent(instance);

	ERR_PRINT_OFF;
	instance->set_parent(child);
	CHECK(instance->get_parent() == master);

	instance->set_parent(instance);
	CHECK(instance->get_parent() == master);

	Ref<StandardMaterial3D> standard;
	standard.instantiate();
	instance->set_parent(standard);
	CHECK(instance->get_parent() == master);
	ERR_PRINT_ON;
}

TEST_CASE("[SceneTree][MaterialInstance] Static switches select shader variants") {
	Ref<ShaderMaterial> master = make_master();
	Ref<Shader> shader = master->get_shader();

	Ref<MaterialInstance> instance;
	instance.instantiate();
	instance->set_parent(master);

	CHECK_FALSE(instance->get_static_switch("USE_DETAIL"));
	CHECK(instance->get_static_switch("USE_TINT"));

	CHECK(instance->get_rid().is_valid());
	// Default switches reuse the master shader.
	CHECK(instance->get_shader_rid() == shader->get_rid());
	CHECK_FALSE(has_uniform(instance->get_shader_rid(), "detail_strength"));

	instance->set_static_switch("USE_DETAIL", true);
	const RID detail_variant = instance->get_shader_rid();
	CHECK(detail_variant.is_valid());
	CHECK(detail_variant != shader->get_rid());
	CHECK(has_uniform(detail_variant, "detail_strength"));

	SUBCASE("Same combination shares the variant") {
		Ref<MaterialInstance> other;
		other.instantiate();
		other->set_parent(master);
		other->set_static_switch("USE_DETAIL", true);
		other->get_rid();
		CHECK(other->get_shader_rid() == detail_variant);
	}

	SUBCASE("Children inherit switches and can override them") {
		Ref<MaterialInstance> child;
		child.instantiate();
		child->set_parent(instance);
		CHECK(child->get_static_switch("USE_DETAIL"));
		child->get_rid();
		CHECK(child->get_shader_rid() == detail_variant);

		child->set_static_switch("USE_TINT", false);
		CHECK_FALSE(has_uniform(child->get_shader_rid(), "tint"));
		CHECK(has_uniform(instance->get_shader_rid(), "tint"));
	}

	SUBCASE("Clearing a switch returns to the inherited variant") {
		instance->clear_static_switch("USE_DETAIL");
		CHECK(instance->get_shader_rid() == shader->get_rid());
	}

	SUBCASE("Variants follow shader code changes") {
		shader->set_code(String(MASTER_CODE).replace("detail_strength", "detail_amount"));
		CHECK(has_uniform(instance->get_shader_rid(), "detail_amount"));
	}
}

TEST_CASE("[SceneTree][MaterialInstance] Only overrides are stored") {
	Ref<ShaderMaterial> master = make_master();
	Ref<MaterialInstance> instance;
	instance.instantiate();
	instance->set_parent(master);
	instance->set_shader_parameter("roughness_value", 0.42);
	instance->set_static_switch("USE_DETAIL", true);
	instance->set_shader_parameter("unknown_param", 1.0);

	List<PropertyInfo> properties;
	instance->get_property_list(&properties);

	bool roughness_stored = false;
	bool roughness_checkable = false;
	bool switch_stored = false;
	bool tint_switch_stored = false;
	bool unknown_stored = false;
	for (const PropertyInfo &E : properties) {
		if (E.name == "shader_parameter/roughness_value") {
			roughness_stored = E.usage & PROPERTY_USAGE_STORAGE;
			roughness_checkable = (E.usage & PROPERTY_USAGE_CHECKABLE) && (E.usage & PROPERTY_USAGE_CHECKED);
		} else if (E.name == "static_switch/USE_DETAIL") {
			switch_stored = E.usage & PROPERTY_USAGE_STORAGE;
		} else if (E.name == "static_switch/USE_TINT") {
			tint_switch_stored = E.usage & PROPERTY_USAGE_STORAGE;
		} else if (E.name == "shader_parameter/unknown_param") {
			unknown_stored = E.usage & PROPERTY_USAGE_STORAGE;
		}
	}
	CHECK(roughness_stored);
	CHECK(roughness_checkable);
	CHECK(switch_stored);
	CHECK_FALSE(tint_switch_stored);
	// Overrides the shader doesn't expose are kept, so they survive a missing or changed parent.
	CHECK(unknown_stored);

	// Used by the inspector so undoing an override removes it instead of pinning the inherited value.
	CHECK(instance->is_property_overridden("shader_parameter/roughness_value"));
	CHECK_FALSE(instance->is_property_overridden("static_switch/USE_TINT"));
	CHECK(instance->is_property_overridden("render_priority"));
}

#ifdef MODULE_VISUAL_SHADER_ENABLED
TEST_CASE("[SceneTree][MaterialInstance] Visual shader static switch node") {
	Ref<VisualShader> vs;
	vs.instantiate();

	Ref<VisualShaderNodeStaticSwitch> static_switch;
	static_switch.instantiate();
	static_switch->set_switch_name("USE_GLOW");
	static_switch->set_op_type(VisualShaderNodeStaticSwitch::OP_TYPE_VECTOR_3D);
	CHECK(static_switch->get_input_port_count() == 2);
	CHECK(static_switch->get_output_port_type(0) == VisualShaderNode::PORT_TYPE_VECTOR_3D);

	vs->add_node(VisualShader::TYPE_FRAGMENT, static_switch, Vector2(), 2);
	vs->connect_nodes(VisualShader::TYPE_FRAGMENT, 2, 0, 0, 0); // Albedo.

	const String code = vs->get_code();
	CHECK(code.contains("#pragma static_switch USE_GLOW false"));
	CHECK(code.contains("#ifdef USE_GLOW"));
	CHECK(vs->has_static_switch("USE_GLOW"));
	CHECK_FALSE(vs->get_static_switch_default("USE_GLOW"));

	SUBCASE("Default value is declared") {
		static_switch->set_default_value(true);
		CHECK(vs->get_code().contains("#pragma static_switch USE_GLOW true"));
		CHECK(vs->get_static_switch_default("USE_GLOW"));
	}

	SUBCASE("Invalid names are reported") {
		static_switch->set_switch_name("not valid");
		CHECK_FALSE(static_switch->get_warning(Shader::MODE_SPATIAL, VisualShader::TYPE_FRAGMENT).is_empty());
	}

	SUBCASE("Material instances compile a variant") {
		Ref<ShaderMaterial> master;
		master.instantiate();
		master->set_shader(vs);
		Ref<MaterialInstance> instance;
		instance.instantiate();
		instance->set_parent(master);
		instance->get_rid();
		const RID default_rid = instance->get_shader_rid();
		instance->set_static_switch("USE_GLOW", true);
		CHECK(instance->get_shader_rid().is_valid());
		CHECK(instance->get_shader_rid() != default_rid);
	}
}

TEST_CASE("[SceneTree][MaterialInstance] Visual shader code only used by a switch branch is stripped") {
	Ref<VisualShader> vs;
	vs.instantiate();
	const VisualShader::Type type = VisualShader::TYPE_FRAGMENT;

	// emission_color -> USE_EMISSION (true) -> Albedo.
	Ref<VisualShaderNodeColorParameter> emission_color;
	emission_color.instantiate();
	emission_color->set_parameter_name("emission_color");
	vs->add_node(type, emission_color, Vector2(), 2);

	// base_color feeds both branches, so it must stay unconditional.
	Ref<VisualShaderNodeColorParameter> base_color;
	base_color.instantiate();
	base_color->set_parameter_name("base_color");
	vs->add_node(type, base_color, Vector2(), 3);

	Ref<VisualShaderNodeVectorOp> add;
	add.instantiate();
	add->set_op_type(VisualShaderNodeVectorOp::OP_TYPE_VECTOR_3D);
	vs->add_node(type, add, Vector2(), 4);
	vs->connect_nodes(type, 2, 0, 4, 0);
	vs->connect_nodes(type, 3, 0, 4, 1);

	Ref<VisualShaderNodeStaticSwitch> static_switch;
	static_switch.instantiate();
	static_switch->set_switch_name("USE_EMISSION");
	static_switch->set_op_type(VisualShaderNodeStaticSwitch::OP_TYPE_VECTOR_3D);
	vs->add_node(type, static_switch, Vector2(), 5);
	vs->connect_nodes(type, 4, 0, 5, 0);
	vs->connect_nodes(type, 3, 0, 5, 1);
	vs->connect_nodes(type, 5, 0, 0, 0); // Albedo.

	const String code = vs->get_code();
	CHECK(code.contains("#ifdef USE_EMISSION\nuniform vec4 emission_color"));
	CHECK_FALSE(code.contains("#ifdef USE_EMISSION\nuniform vec4 base_color"));
	// The declaration of a nested switch is never wrapped.
	CHECK(code.find("#pragma static_switch USE_EMISSION") < code.find("#ifdef USE_EMISSION"));

	Ref<ShaderMaterial> master;
	master.instantiate();
	master->set_shader(vs);
	Ref<MaterialInstance> instance;
	instance.instantiate();
	instance->set_parent(master);
	instance->get_rid();
	CHECK_FALSE(has_uniform(instance->get_shader_rid(), "emission_color"));
	CHECK(has_uniform(instance->get_shader_rid(), "base_color"));

	instance->set_static_switch("USE_EMISSION", true);
	CHECK(has_uniform(instance->get_shader_rid(), "emission_color"));
	CHECK(has_uniform(instance->get_shader_rid(), "base_color"));

	SUBCASE("Parameters also used outside the branch stay unconditional") {
		Ref<VisualShaderNodeParameterRef> parameter_ref;
		parameter_ref.instantiate();
		parameter_ref->set_parameter_name("emission_color");
		parameter_ref->set("param_type", VisualShaderNodeParameterRef::PARAMETER_TYPE_COLOR); // Resolved by the editor otherwise.
		vs->add_node(type, parameter_ref, Vector2(), 6);
		vs->connect_nodes(type, 6, 0, 0, 5); // Emission.

		const String new_code = vs->get_code();
		CHECK_FALSE(new_code.contains("#ifdef USE_EMISSION\nuniform vec4 emission_color"));
		instance->clear_static_switch("USE_EMISSION");
		CHECK(has_uniform(instance->get_shader_rid(), "emission_color"));
	}
}
#endif // MODULE_VISUAL_SHADER_ENABLED

} // namespace TestMaterialInstance
