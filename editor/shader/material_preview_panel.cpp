/**************************************************************************/
/*  material_preview_panel.cpp                                            */
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

#include "material_preview_panel.h"

#include "core/object/callable_mp.h"
#include "editor/docks/inspector_dock.h"
#include "editor/inspector/editor_inspector.h"
#include "editor/scene/material_editor_plugin.h"
#include "editor/script/script_editor_base.h"
#include "editor/script/script_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/check_box.h"
#include "scene/gui/label.h"
#include "scene/gui/separator.h"
#include "scene/resources/environment.h"
#include "scene/resources/sky.h"
#include "scene/scene_string_names.h"

static constexpr double POLL_INTERVAL = 0.25;

Ref<Material> MaterialPreviewPanel::_find_inspected_material() const {
	EditorInspector *inspector = InspectorDock::get_inspector_singleton();
	if (!inspector) {
		return Ref<Material>();
	}

	Ref<ShaderMaterial> shader_material = Object::cast_to<ShaderMaterial>(inspector->get_edited_object());
	if (shader_material.is_valid() && shader_material->get_shader() == shader) {
		return shader_material;
	}
	Ref<MaterialInstance> instance = Object::cast_to<MaterialInstance>(inspector->get_edited_object());
	if (instance.is_valid() && instance->get_shader() == shader) {
		return instance;
	}
	return Ref<Material>();
}

void MaterialPreviewPanel::_poll() {
	Ref<Shader> current_shader;
	if (ScriptEditorBase *editor = container->get_current_editor()) {
		current_shader = editor->get_edited_resource();
	}

	if (current_shader != shader) {
		shader = current_shader;
		scratch_master->set_shader(shader);
	}

	Ref<Material> material;
	if (shader.is_valid()) {
		material = _find_inspected_material();
		if (material.is_null()) {
			material = scratch_instance;
		}
	}

	// The shader type can change while editing, so the preview mode has to be refreshed too.
	const Shader::Mode mode = shader.is_valid() ? shader->get_mode() : Shader::MODE_MAX;
	if (material != previewed_material || mode != previewed_mode) {
		previewed_material = material;
		previewed_mode = mode;

		if (material.is_valid()) {
			preview->show();
			preview->edit(material, environment);
		} else {
			preview->hide();
		}
	}

	// Rebuilt every poll: switching shaders keeps the same scratch material, and files can be renamed.
	String source_text;
	if (shader.is_null()) {
		source_text = TTR("Open a shader to preview it.");
	} else if (material == scratch_instance) {
		source_text = vformat(TTR("Previewing \"%s\" with default parameters."), shader->get_path().get_file());
	} else {
		source_text = vformat(TTR("Previewing \"%s\" from the inspector."), material->get_path().is_empty() ? material->get_class() : material->get_path().get_file());
	}
	if (source_label->get_text() != source_text) {
		source_label->set_text(source_text);
	}

	_update_switches();
}

void MaterialPreviewPanel::_update_switches() {
	Vector<StringName> switches;
	if (shader.is_valid() && previewed_material == scratch_instance) {
		switches = shader->get_static_switch_list();
	}
	if (switches == listed_switches) {
		return;
	}
	listed_switches = switches;

	for (int i = switches_box->get_child_count() - 1; i >= 0; i--) {
		memdelete(switches_box->get_child(i));
	}
	switches_label->set_visible(!switches.is_empty());

	for (const StringName &E : switches) {
		CheckBox *check = memnew(CheckBox);
		check->set_text(E);
		check->set_pressed_no_signal(scratch_instance->get_static_switch(E));
		check->set_tooltip_text(TTR("Preview this static switch combination. Variants are compiled on demand."));
		check->connect(SceneStringName(toggled), callable_mp(this, &MaterialPreviewPanel::_switch_toggled).bind(E));
		switches_box->add_child(check);
	}
}

void MaterialPreviewPanel::_switch_toggled(bool p_enabled, const StringName &p_name) {
	scratch_instance->set_static_switch(p_name, p_enabled);
}

void MaterialPreviewPanel::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_PROCESS: {
			if (!is_visible_in_tree()) {
				return;
			}
			poll_time -= get_process_delta_time();
			if (poll_time <= 0.0) {
				poll_time = POLL_INTERVAL;
				_poll();
			}
		} break;
	}
}

MaterialPreviewPanel::MaterialPreviewPanel(DocumentEditorContainer *p_container) {
	container = p_container;
	set_custom_minimum_size(Size2(260 * EDSCALE, 0));

	environment.instantiate();
	Ref<Sky> sky;
	sky.instantiate();
	environment->set_sky(sky);
	environment->set_background(Environment::BG_COLOR);
	environment->set_ambient_source(Environment::AMBIENT_SOURCE_SKY);
	environment->set_reflection_source(Environment::REFLECTION_SOURCE_SKY);

	scratch_master.instantiate();
	scratch_instance.instantiate();
	scratch_instance->set_parent(scratch_master);

	Label *title = memnew(Label(TTR("Material Preview")));
	title->set_theme_type_variation("HeaderSmall");
	add_child(title);

	source_label = memnew(Label);
	source_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	add_child(source_label);

	body = memnew(VBoxContainer);
	body->set_v_size_flags(SIZE_EXPAND_FILL);
	add_child(body);

	preview = memnew(MaterialEditor);
	preview->set_v_size_flags(SIZE_EXPAND_FILL);
	preview->set_custom_minimum_size(Size2(0, 240 * EDSCALE));
	body->add_child(preview);

	switches_label = memnew(Label(TTR("Static Switches")));
	switches_label->set_theme_type_variation("HeaderSmall");
	switches_label->hide();
	body->add_child(switches_label);

	switches_box = memnew(VBoxContainer);
	body->add_child(switches_box);

	set_process(true);
}
