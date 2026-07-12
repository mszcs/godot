/**************************************************************************/
/*  libgodot_windows.cpp                                                  */
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

#include "core/extension/libgodot.h"

#include "core/extension/godot_instance.h"
#include "core/object/class_db.h"
#include "main/main.h"

#include "display_server_embedded_windows.h"
#include "os_windows.h"
#include "rendering_native_surface_windows.h"

static OS_Windows *os = nullptr;

static GodotInstance *instance = nullptr;
static bool embedded_driver_registered = false;
static bool embedded_class_registered = false;

static bool _wants_embedded_driver(int p_argc, char *p_argv[]) {
	for (int i = 1; i < p_argc; i++) {
		if (strcmp("--embedded", p_argv[i]) == 0) {
			return true;
		}
		if (i < p_argc - 1 && strcmp("--display-driver", p_argv[i]) == 0 && strcmp("embedded", p_argv[i + 1]) == 0) {
			return true;
		}
	}
	return false;
}

GDExtensionObjectPtr libgodot_create_godot_instance(int p_argc, char *p_argv[], GDExtensionInitializationFunction p_init_func) {
	ERR_FAIL_COND_V_MSG(instance != nullptr, nullptr, "Only one Godot Instance may be created at a time.");

	os = new OS_Windows(GetModuleHandle(nullptr));

	if (!embedded_driver_registered && _wants_embedded_driver(p_argc, p_argv)) {
		DisplayServerEmbedded::register_embedded_driver();
		embedded_driver_registered = true;
	}

	Error err = Main::setup(p_argv[0], p_argc - 1, &p_argv[1], false);
	if (err != OK) {
		return nullptr;
	}

	if (!embedded_class_registered) {
		ClassDB::register_abstract_class<DisplayServerEmbedded>();
		ClassDB::register_abstract_class<RenderingNativeSurfaceWindows>();
		embedded_class_registered = true;
	}

	instance = memnew(GodotInstance);
	if (!instance->initialize(p_init_func)) {
		memdelete(instance);
		instance = nullptr;
		return nullptr;
	}

	return (GDExtensionObjectPtr)instance;
}

void libgodot_destroy_godot_instance(GDExtensionObjectPtr p_godot_instance) {
	GodotInstance *godot_instance = (GodotInstance *)p_godot_instance;
	if (instance == godot_instance) {
		godot_instance->stop();
		memdelete(godot_instance);
		// Note: When Godot Engine supports reinitialization, clear the instance pointer here.
		//instance = nullptr;
		Main::cleanup();
	}
}

// Embedded-mode helper for foreign-language hosts (C#/P.Invoke and similar).
//
// RenderingNativeSurfaceWindows::create_api() returns Ref<> by value, which
// cannot be called across a plain C FFI boundary (the x64 MSVC ABI returns
// non-trivially-copyable types through a hidden pointer parameter). This
// extern "C" wrapper is compiled with the proper C++ ABI inside the DLL,
// mirroring the Apple shim (libgodot_shim_setup_apple_surface).
//
// Must be called AFTER libgodot_create_godot_instance and BEFORE
// GodotInstance::start() — DisplayServerEmbedded reads the static surface
// in its constructor during Main::setup2.
extern "C" LIBGODOT_API int libgodot_shim_setup_windows_surface(uint64_t p_hwnd, uint64_t p_hinstance) {
	Ref<RenderingNativeSurfaceWindows> surface = RenderingNativeSurfaceWindows::create_api(p_hwnd, p_hinstance);
	if (surface.is_null()) {
		return 0;
	}
	DisplayServerEmbedded::set_native_surface(surface);
	return 1;
}
