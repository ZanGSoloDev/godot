/**************************************************************************/
/*  material_preview_panel.h                                              */
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

#include "scene/gui/box_container.h"
#include "scene/resources/material_instance.h"

class Button;
class DocumentEditorContainer;
class Environment;
class Label;
class MaterialEditor;

// Unreal-style material editor viewport shown next to the shader graph / code.
// Previews the shader being edited, using the inspected ShaderMaterial or MaterialInstance
// built from it when there is one, and lets static switch combinations be tried out.
class MaterialPreviewPanel : public VBoxContainer {
	GDCLASS(MaterialPreviewPanel, VBoxContainer);

	DocumentEditorContainer *container = nullptr;

	Label *source_label = nullptr;
	Button *collapse_button = nullptr;
	VBoxContainer *body = nullptr;
	MaterialEditor *preview = nullptr;
	VBoxContainer *switches_box = nullptr;
	Label *switches_label = nullptr;

	Ref<Environment> environment;

	Ref<Shader> shader;
	// Used when no material made from the shader is being inspected; its parent holds the shader defaults.
	Ref<ShaderMaterial> scratch_master;
	Ref<MaterialInstance> scratch_instance;

	Ref<Material> previewed_material;
	Shader::Mode previewed_mode = Shader::MODE_MAX;
	Vector<StringName> listed_switches;

	double poll_time = 0.0;

	Ref<Material> _find_inspected_material() const;
	void _poll();
	void _update_switches();
	void _switch_toggled(bool p_enabled, const StringName &p_name);
	void _collapse_toggled(bool p_collapsed);

protected:
	void _notification(int p_what);

public:
	MaterialPreviewPanel(DocumentEditorContainer *p_container);
};
