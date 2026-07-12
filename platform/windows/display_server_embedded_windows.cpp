/**************************************************************************/
/*  display_server_embedded_windows.cpp                                   */
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

#include "display_server_embedded_windows.h"

#include "rendering_native_surface_windows.h"

#include "core/config/project_settings.h"
#include "core/input/input.h"
#include "core/os/main_loop.h"
#include "core/os/os.h"

#if defined(RD_ENABLED)
#include "servers/rendering/renderer_rd/renderer_compositor_rd.h"
#include "servers/rendering/rendering_device.h"

#if defined(VULKAN_ENABLED)
#include "rendering_context_driver_vulkan_windows.h"
#endif
#endif

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

Ref<RenderingNativeSurface> DisplayServerEmbedded::native_surface;

DisplayServerEmbedded *DisplayServerEmbedded::get_singleton() {
	return (DisplayServerEmbedded *)DisplayServer::get_singleton();
}

void DisplayServerEmbedded::set_native_surface(Ref<RenderingNativeSurface> p_native_surface) {
	native_surface = p_native_surface;
}

void DisplayServerEmbedded::_bind_methods() {
	ClassDB::bind_static_method("DisplayServerEmbedded", D_METHOD("set_native_surface", "native_surface"), &DisplayServerEmbedded::set_native_surface);
	ClassDB::bind_static_method("DisplayServerEmbedded", D_METHOD("get_singleton"), &DisplayServerEmbedded::get_singleton);
	ClassDB::bind_method(D_METHOD("resize_window", "size", "id"), &DisplayServerEmbedded::resize_window);
	ClassDB::bind_method(D_METHOD("set_content_scale", "content_scale"), &DisplayServerEmbedded::set_content_scale);
	ClassDB::bind_method(D_METHOD("touch_press", "idx", "x", "y", "pressed", "double_click", "window"), &DisplayServerEmbedded::touch_press);
	ClassDB::bind_method(D_METHOD("touch_drag", "idx", "prev_x", "prev_y", "x", "y", "pressure", "tilt", "window"), &DisplayServerEmbedded::touch_drag);
	ClassDB::bind_method(D_METHOD("touches_canceled", "idx", "window"), &DisplayServerEmbedded::touches_canceled);
	ClassDB::bind_method(D_METHOD("key", "key", "char", "unshifted", "physical", "modifiers", "pressed", "window"), &DisplayServerEmbedded::key, DEFVAL(MAIN_WINDOW_ID));
}

DisplayServerEmbedded::DisplayServerEmbedded(const String &p_rendering_driver, WindowMode p_mode, DisplayServer::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, Context p_context, Error &r_error) {
	r_error = OK; // default to OK

	Input::get_singleton()->set_event_dispatch_function(_dispatch_input_events);

	rendering_driver = p_rendering_driver;

	Ref<RenderingNativeSurfaceWindows> win_surface = native_surface;
	ERR_FAIL_COND_MSG(win_surface.is_null(), "Embedded rendering on Windows requires a RenderingNativeSurfaceWindows set via DisplayServerEmbedded::set_native_surface before start.");

	HWND hwnd = (HWND)(uintptr_t)win_surface->get_window_handle();
	HINSTANCE hinstance = (HINSTANCE)(uintptr_t)win_surface->get_instance_handle();
	if (hinstance == nullptr) {
		hinstance = GetModuleHandle(nullptr);
	}
	ERR_FAIL_COND_MSG(hwnd == nullptr || !IsWindow(hwnd), "The window handle passed to RenderingNativeSurfaceWindows is not a valid HWND.");

#if defined(RD_ENABLED)
#if defined(VULKAN_ENABLED)
	if (rendering_driver == "vulkan") {
		rendering_context = memnew(RenderingContextDriverVulkanWindows);
	}
#endif

	if (rendering_context) {
		if (rendering_context->initialize() != OK) {
			memdelete(rendering_context);
			rendering_context = nullptr;
			r_error = ERR_CANT_CREATE;
			ERR_FAIL_MSG("Could not initialize " + rendering_driver);
		}

		union {
#ifdef VULKAN_ENABLED
			RenderingContextDriverVulkanWindows::WindowPlatformData vulkan;
#endif
		} wpd;
#ifdef VULKAN_ENABLED
		if (rendering_driver == "vulkan") {
			wpd.vulkan.window = hwnd;
			wpd.vulkan.instance = hinstance;
		}
#endif
		Error err = rendering_context->window_create(window_id_counter, &wpd);
		if (err != OK) {
			memdelete(rendering_context);
			rendering_context = nullptr;
			r_error = ERR_CANT_CREATE;
			ERR_FAIL_MSG(vformat("Can't create a %s context", rendering_driver));
		}

		// The rendering context size is always in pixels.
		rendering_context->window_set_size(window_id_counter, p_resolution.width, p_resolution.height);
		rendering_context->window_set_vsync_mode(window_id_counter, p_vsync_mode);

		rendering_device = memnew(RenderingDevice);
		rendering_device->initialize(rendering_context, MAIN_WINDOW_ID);
		rendering_device->screen_create(MAIN_WINDOW_ID);

		RendererCompositorRD::make_current();
	} else {
		r_error = ERR_CANT_CREATE;
		ERR_FAIL_MSG(vformat("Unsupported rendering driver for embedded display server: %s", rendering_driver));
	}
#else
	r_error = ERR_CANT_CREATE;
	ERR_FAIL_MSG("Embedded display server on Windows requires RenderingDevice support (Vulkan).");
#endif

	UINT dpi = 96;
	{
		HMODULE user32 = GetModuleHandleW(L"user32.dll");
		if (user32) {
			typedef UINT(WINAPI * GetDpiForWindowPtr)(HWND);
			GetDpiForWindowPtr get_dpi_for_window = (GetDpiForWindowPtr)(void *)GetProcAddress(user32, "GetDpiForWindow");
			if (get_dpi_for_window) {
				dpi = get_dpi_for_window(hwnd);
			}
		}
	}
	screen_scale = (float)dpi / 96.0f;

	transparent = ((p_flags & WINDOW_FLAG_TRANSPARENT_BIT) == WINDOW_FLAG_TRANSPARENT_BIT);
}

DisplayServerEmbedded::~DisplayServerEmbedded() {
#if defined(RD_ENABLED)
	if (rendering_device) {
		memdelete(rendering_device);
		rendering_device = nullptr;
	}

	if (rendering_context) {
		memdelete(rendering_context);
		rendering_context = nullptr;
	}
#endif
}

DisplayServer *DisplayServerEmbedded::create_func(const String &p_rendering_driver, WindowMode p_mode, DisplayServer::VSyncMode p_vsync_mode, uint32_t p_flags, const Vector2i *p_position, const Vector2i &p_resolution, int p_screen, Context p_context, int64_t /* p_parent_window */, Error &r_error) {
	return memnew(DisplayServerEmbedded(p_rendering_driver, p_mode, p_vsync_mode, p_flags, p_position, p_resolution, p_screen, p_context, r_error));
}

Vector<String> DisplayServerEmbedded::get_rendering_drivers_func() {
	Vector<String> drivers;

#if defined(VULKAN_ENABLED)
	drivers.push_back("vulkan");
#endif

	return drivers;
}

void DisplayServerEmbedded::register_embedded_driver() {
	register_create_function("embedded", create_func, get_rendering_drivers_func);
}

void DisplayServerEmbedded::beep() const {
	MessageBeep(MB_OK);
}

// MARK: - Mouse

void DisplayServerEmbedded::mouse_set_mode(MouseMode p_mode) {
	mouse_mode = p_mode;
}

DisplayServerEmbedded::MouseMode DisplayServerEmbedded::mouse_get_mode() const {
	return mouse_mode;
}

void DisplayServerEmbedded::warp_mouse(const Point2i &p_position) {
	_THREAD_SAFE_METHOD_
	Input::get_singleton()->set_mouse_position(p_position);
}

Point2i DisplayServerEmbedded::mouse_get_position() const {
	_THREAD_SAFE_METHOD_
	return Input::get_singleton()->get_mouse_position();
}

BitField<MouseButtonMask> DisplayServerEmbedded::mouse_get_button_state() const {
	return Input::get_singleton()->get_mouse_button_mask();
}

// MARK: Events

void DisplayServerEmbedded::window_set_rect_changed_callback(const Callable &p_callable, WindowID p_window) {
	window_resize_callbacks[p_window] = p_callable;
}

void DisplayServerEmbedded::window_set_window_event_callback(const Callable &p_callable, WindowID p_window) {
	window_event_callbacks[p_window] = p_callable;
}
void DisplayServerEmbedded::window_set_input_event_callback(const Callable &p_callable, WindowID p_window) {
	input_event_callbacks[p_window] = p_callable;
}

void DisplayServerEmbedded::window_set_input_text_callback(const Callable &p_callable, WindowID p_window) {
	input_text_callbacks[p_window] = p_callable;
}

void DisplayServerEmbedded::window_set_drop_files_callback(const Callable &p_callable, WindowID p_window) {
	// Not supported
}

void DisplayServerEmbedded::process_events() {
	Input *input = Input::get_singleton();
	input->flush_buffered_events();
}

void DisplayServerEmbedded::_dispatch_input_events(const Ref<InputEvent> &p_event) {
	Ref<InputEventFromWindow> event_from_window = p_event;
	WindowID window_id = INVALID_WINDOW_ID;
	if (event_from_window.is_valid()) {
		window_id = event_from_window->get_window_id();
	}
	DisplayServerEmbedded *ds = (DisplayServerEmbedded *)DisplayServer::get_singleton();
	ds->send_input_event(p_event, window_id);
}

void DisplayServerEmbedded::send_input_event(const Ref<InputEvent> &p_event, WindowID p_id) const {
	if (p_id != INVALID_WINDOW_ID) {
		const Callable *cb = input_event_callbacks.getptr(p_id);
		if (cb) {
			_window_callback(*cb, p_event);
		}
	} else {
		for (const KeyValue<WindowID, Callable> &E : input_event_callbacks) {
			_window_callback(E.value, p_event);
		}
	}
}

void DisplayServerEmbedded::send_input_text(const String &p_text, WindowID p_id) const {
	const Callable *cb = input_text_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, p_text);
	}
}

void DisplayServerEmbedded::send_window_event(DisplayServer::WindowEvent p_event, WindowID p_id) const {
	const Callable *cb = window_event_callbacks.getptr(p_id);
	if (cb) {
		_window_callback(*cb, int(p_event));
	}
}

void DisplayServerEmbedded::_window_callback(const Callable &p_callable, const Variant &p_arg) const {
	if (p_callable.is_valid()) {
		p_callable.call(p_arg);
	}
}

void DisplayServerEmbedded::perform_event(const Ref<InputEvent> &p_event) {
	Input::get_singleton()->parse_input_event(p_event);
}

void DisplayServerEmbedded::resize_window(Size2i p_size, WindowID p_id) {
	Size2i scaled_size = Size2i(int32_t(p_size.x * content_scale), int32_t(p_size.y * content_scale));
	_window_set_size(scaled_size, p_id);
}

void DisplayServerEmbedded::set_content_scale(float p_content_scale) {
	if (Math::is_equal_approx(content_scale, p_content_scale)) {
		return;
	}
	content_scale = p_content_scale;
}

void DisplayServerEmbedded::touch_press(int p_idx, int p_x, int p_y, bool p_pressed, bool p_double_click, DisplayServer::WindowID p_window) {
	Ref<InputEventScreenTouch> ev;
	ev.instantiate();
	ev->set_window_id(p_window);
	ev->set_index(p_idx);
	ev->set_pressed(p_pressed);
	ev->set_position(Vector2(p_x, p_y));
	ev->set_double_tap(p_double_click);
	perform_event(ev);
}

void DisplayServerEmbedded::touch_drag(int p_idx, int p_prev_x, int p_prev_y, int p_x, int p_y, float p_pressure, Vector2 p_tilt, DisplayServer::WindowID p_window) {
	Ref<InputEventScreenDrag> ev;
	ev.instantiate();
	ev->set_window_id(p_window);
	ev->set_index(p_idx);
	ev->set_pressure(p_pressure);
	ev->set_tilt(p_tilt);
	ev->set_position(Vector2(p_x, p_y));
	ev->set_relative(Vector2(p_x - p_prev_x, p_y - p_prev_y));
	ev->set_relative_screen_position(ev->get_relative());
	perform_event(ev);
}

void DisplayServerEmbedded::touches_canceled(int p_idx, DisplayServer::WindowID p_window) {
	touch_press(p_idx, -1, -1, false, false, p_window);
}

void DisplayServerEmbedded::key(Key p_key, char32_t p_char, Key p_unshifted, Key p_physical, BitField<KeyModifierMask> p_modifiers, bool p_pressed, DisplayServer::WindowID p_window) {
	Ref<InputEventKey> ev;
	ev.instantiate();
	ev->set_window_id(p_window);
	ev->set_echo(false);
	ev->set_pressed(p_pressed);
	ev->set_keycode(fix_keycode(p_char, p_key));
	if (p_key != Key::SHIFT) {
		ev->set_shift_pressed(p_modifiers.has_flag(KeyModifierMask::SHIFT));
	}
	if (p_key != Key::CTRL) {
		ev->set_ctrl_pressed(p_modifiers.has_flag(KeyModifierMask::CTRL));
	}
	if (p_key != Key::ALT) {
		ev->set_alt_pressed(p_modifiers.has_flag(KeyModifierMask::ALT));
	}
	if (p_key != Key::META) {
		ev->set_meta_pressed(p_modifiers.has_flag(KeyModifierMask::META));
	}
	ev->set_key_label(p_unshifted);
	ev->set_physical_keycode(p_physical);
	ev->set_unicode(fix_unicode(p_char));
	perform_event(ev);
}

// MARK: -

bool DisplayServerEmbedded::has_feature(Feature p_feature) const {
	switch (p_feature) {
		case FEATURE_TOUCHSCREEN:
			return true;
		default:
			return false;
	}
}

String DisplayServerEmbedded::get_name() const {
	return "embedded";
}

int DisplayServerEmbedded::get_screen_count() const {
	return 1;
}

int DisplayServerEmbedded::get_primary_screen() const {
	return 0;
}

Point2i DisplayServerEmbedded::screen_get_position(int p_screen) const {
	_THREAD_SAFE_METHOD_

	p_screen = _get_screen_index(p_screen);
	int screen_count = get_screen_count();
	ERR_FAIL_INDEX_V(p_screen, screen_count, Point2i());

	return Point2i(0, 0);
}

Size2i DisplayServerEmbedded::screen_get_size(int p_screen) const {
	_THREAD_SAFE_METHOD_

	p_screen = _get_screen_index(p_screen);
	int screen_count = get_screen_count();
	ERR_FAIL_INDEX_V(p_screen, screen_count, Size2i());

	return window_get_size(MAIN_WINDOW_ID);
}

Rect2i DisplayServerEmbedded::screen_get_usable_rect(int p_screen) const {
	_THREAD_SAFE_METHOD_

	p_screen = _get_screen_index(p_screen);
	int screen_count = get_screen_count();
	ERR_FAIL_INDEX_V(p_screen, screen_count, Rect2i());

	return Rect2i(screen_get_position(p_screen), screen_get_size(p_screen));
}

int DisplayServerEmbedded::screen_get_dpi(int p_screen) const {
	_THREAD_SAFE_METHOD_

	p_screen = _get_screen_index(p_screen);
	int screen_count = get_screen_count();
	ERR_FAIL_INDEX_V(p_screen, screen_count, 72);

	return (int)(96.0f * screen_scale);
}

float DisplayServerEmbedded::screen_get_scale(int p_screen) const {
	_THREAD_SAFE_METHOD_

	switch (p_screen) {
		case SCREEN_WITH_MOUSE_FOCUS:
		case SCREEN_WITH_KEYBOARD_FOCUS:
		case SCREEN_PRIMARY:
		case SCREEN_OF_MAIN_WINDOW:
		case 0:
			return screen_scale;
		default:
			return 1.0;
	}
}

float DisplayServerEmbedded::screen_get_refresh_rate(int p_screen) const {
	_THREAD_SAFE_METHOD_

	p_screen = _get_screen_index(p_screen);
	int screen_count = get_screen_count();
	ERR_FAIL_INDEX_V(p_screen, screen_count, SCREEN_REFRESH_RATE_FALLBACK);

	DEVMODEW dm;
	memset(&dm, 0, sizeof(dm));
	dm.dmSize = sizeof(dm);
	if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1) {
		return (float)dm.dmDisplayFrequency;
	}
	return SCREEN_REFRESH_RATE_FALLBACK;
}

Vector<DisplayServer::WindowID> DisplayServerEmbedded::get_window_list() const {
	Vector<DisplayServer::WindowID> list;
	list.push_back(MAIN_WINDOW_ID);
	return list;
}

DisplayServer::WindowID DisplayServerEmbedded::get_window_at_screen_position(const Point2i &p_position) const {
	return MAIN_WINDOW_ID;
}

void DisplayServerEmbedded::window_attach_instance_id(ObjectID p_instance, WindowID p_window) {
	window_attached_instance_id[p_window] = p_instance;
}

ObjectID DisplayServerEmbedded::window_get_attached_instance_id(WindowID p_window) const {
	return window_attached_instance_id[p_window];
}

void DisplayServerEmbedded::window_set_title(const String &p_title, WindowID p_window) {
	// Not supported
}

int DisplayServerEmbedded::window_get_current_screen(WindowID p_window) const {
	_THREAD_SAFE_METHOD_
	ERR_FAIL_COND_V(p_window != MAIN_WINDOW_ID, INVALID_SCREEN);

	return 0;
}

void DisplayServerEmbedded::window_set_current_screen(int p_screen, WindowID p_window) {
	// Not supported
}

Point2i DisplayServerEmbedded::window_get_position(WindowID p_window) const {
	return Point2i();
}

Point2i DisplayServerEmbedded::window_get_position_with_decorations(WindowID p_window) const {
	return Point2i();
}

void DisplayServerEmbedded::window_set_position(const Point2i &p_position, WindowID p_window) {
	// Not supported — the host window is owned by the embedding application.
}

void DisplayServerEmbedded::window_set_transient(WindowID p_window, WindowID p_parent) {
	// Not supported
}

void DisplayServerEmbedded::window_set_max_size(const Size2i p_size, WindowID p_window) {
	// Not supported
}

Size2i DisplayServerEmbedded::window_get_max_size(WindowID p_window) const {
	return Size2i();
}

void DisplayServerEmbedded::window_set_min_size(const Size2i p_size, WindowID p_window) {
	// Not supported
}

Size2i DisplayServerEmbedded::window_get_min_size(WindowID p_window) const {
	return Size2i();
}

void DisplayServerEmbedded::window_set_size(const Size2i p_size, WindowID p_window) {
	_window_set_size(p_size, p_window);
}

void DisplayServerEmbedded::_window_set_size(const Size2i p_size, WindowID p_window) {
#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_size(p_window, p_size.width, p_size.height);
	}
#endif

	Callable *cb = window_resize_callbacks.getptr(p_window);
	if (cb) {
		Variant resize_rect = Rect2i(Point2i(), p_size);
		_window_callback(window_resize_callbacks[p_window], resize_rect);
	}
}

Size2i DisplayServerEmbedded::window_get_size(WindowID p_window) const {
#if defined(RD_ENABLED)
	if (rendering_context) {
		RenderingContextDriver::SurfaceID surface = rendering_context->surface_get_from_window(p_window);
		ERR_FAIL_COND_V_MSG(surface == 0, Size2i(), "Invalid window ID");
		uint32_t width = rendering_context->surface_get_width(surface);
		uint32_t height = rendering_context->surface_get_height(surface);
		return Size2i(width, height);
	}
#endif
	return Size2i();
}

Size2i DisplayServerEmbedded::window_get_size_with_decorations(WindowID p_window) const {
	return window_get_size(p_window);
}

void DisplayServerEmbedded::window_set_mode(WindowMode p_mode, WindowID p_window) {
	// Not supported
}

DisplayServer::WindowMode DisplayServerEmbedded::window_get_mode(WindowID p_window) const {
	return WindowMode::WINDOW_MODE_WINDOWED;
}

bool DisplayServerEmbedded::window_is_maximize_allowed(WindowID p_window) const {
	return false;
}

void DisplayServerEmbedded::window_set_flag(WindowFlags p_flag, bool p_enabled, WindowID p_window) {
	if (p_flag == WINDOW_FLAG_TRANSPARENT && p_window == MAIN_WINDOW_ID) {
		transparent = p_enabled;
	}
}

bool DisplayServerEmbedded::window_get_flag(WindowFlags p_flag, WindowID p_window) const {
	if (p_flag == WINDOW_FLAG_TRANSPARENT && p_window == MAIN_WINDOW_ID) {
		return transparent;
	}
	return false;
}

void DisplayServerEmbedded::window_request_attention(WindowID p_window) {
	// Not supported
}

void DisplayServerEmbedded::window_move_to_foreground(WindowID p_window) {
	// Not supported
}

bool DisplayServerEmbedded::window_is_focused(WindowID p_window) const {
	return true;
}

float DisplayServerEmbedded::screen_get_max_scale() const {
	return screen_scale;
}

bool DisplayServerEmbedded::window_can_draw(WindowID p_window) const {
	return true;
}

bool DisplayServerEmbedded::can_any_window_draw() const {
	return true;
}

void DisplayServerEmbedded::window_set_ime_active(const bool p_active, WindowID p_window) {
	// Not supported
}

void DisplayServerEmbedded::window_set_ime_position(const Point2i &p_pos, WindowID p_window) {
	// Not supported
}

void DisplayServerEmbedded::window_set_vsync_mode(DisplayServer::VSyncMode p_vsync_mode, WindowID p_window) {
#if defined(RD_ENABLED)
	if (rendering_context) {
		rendering_context->window_set_vsync_mode(p_window, p_vsync_mode);
	}
#endif
}

DisplayServer::VSyncMode DisplayServerEmbedded::window_get_vsync_mode(WindowID p_window) const {
	_THREAD_SAFE_METHOD_
#if defined(RD_ENABLED)
	if (rendering_context) {
		return rendering_context->window_get_vsync_mode(p_window);
	}
#endif
	return DisplayServer::VSYNC_ENABLED;
}

void DisplayServerEmbedded::update_im_text(const Point2i &p_selection, const String &p_text) {
	im_selection = p_selection;
	im_text = p_text;

	OS::get_singleton()->get_main_loop()->notification(MainLoop::NOTIFICATION_OS_IME_UPDATE);
}

Point2i DisplayServerEmbedded::ime_get_selection() const {
	return im_selection;
}

String DisplayServerEmbedded::ime_get_text() const {
	return im_text;
}

void DisplayServerEmbedded::cursor_set_shape(CursorShape p_shape) {
	cursor_shape = p_shape;
}

DisplayServer::CursorShape DisplayServerEmbedded::cursor_get_shape() const {
	return cursor_shape;
}

void DisplayServerEmbedded::cursor_set_custom_image(const Ref<Resource> &p_cursor, CursorShape p_shape, const Vector2 &p_hotspot) {
	// Not supported — the cursor belongs to the embedding application.
}

void DisplayServerEmbedded::swap_buffers() {
	// RenderingDevice presents on its own; nothing to do.
}
