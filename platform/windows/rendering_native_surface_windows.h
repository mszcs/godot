/**************************************************************************/
/*  rendering_native_surface_windows.h                                    */
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

#include "servers/rendering/rendering_native_surface.h"

// Wraps a Win32 window handle (HWND) provided by a host application that
// embeds libgodot. The Windows counterpart of RenderingNativeSurfaceApple:
// the host creates a child window, wraps it in this surface and hands it to
// DisplayServerEmbedded::set_native_surface() before GodotInstance::start().
class RenderingNativeSurfaceWindows : public RenderingNativeSurface {
	GDCLASS(RenderingNativeSurfaceWindows, RenderingNativeSurface);

	static void _bind_methods();

	uint64_t window_handle = 0; // HWND
	uint64_t instance_handle = 0; // HINSTANCE; 0 = GetModuleHandle(nullptr)

public:
	static Ref<RenderingNativeSurfaceWindows> create_api(uint64_t p_window_handle, uint64_t p_instance_handle);

	void set_window_handle(uint64_t p_window_handle) {
		window_handle = p_window_handle;
	}

	uint64_t get_window_handle() const {
		return window_handle;
	}

	void set_instance_handle(uint64_t p_instance_handle) {
		instance_handle = p_instance_handle;
	}

	uint64_t get_instance_handle() const {
		return instance_handle;
	}

	RenderingNativeSurfaceWindows();
	~RenderingNativeSurfaceWindows();
};
