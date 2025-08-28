/*
 * \brief  Virtualbox framebuffer implementation for Genode
 * \author Alexander Boettcher
 * \author Christian Helmuth
 * \date   2013-10-16
 */

/*
 * Copyright (C) 2013-2025 Genode Labs GmbH
 *
 * This file is distributed under the terms of the GNU General Public License
 * version 2.
 */

/* Genode includes */
#define Framebuffer Fb_Genode
#include <framebuffer_session/connection.h>
#include <gui_session/connection.h>
#undef Framebuffer

#include <os/texture_rgb888.h>
#include <nitpicker_gfx/texture_painter.h>

/* VirtualBox includes */
#include <Global.h>
#include <VirtualBoxBase.h>
#include <DisplayWrap.h>

/* Genode port specific includes */
#include <attempt.h>

class Genodefb :
	VBOX_SCRIPTABLE_IMPL(IFramebuffer)
{
	private:

		Genode::Env         &_env;
		Gui::Connection     &_gui;
		Gui::Top_level_view &_view;
		Gui::Rect            _gui_win { { }, { 1024, 768 } };

		/*
		 * The mode currently used by the VM. Can be smaller than the
		 * framebuffer mode.
		 */
		Gui::Area _virtual_fb_area;

		void *_attach()
		{
			return _env.rm().attach(_gui.framebuffer.dataspace(), {
				.size = { },  .offset     = { },  .use_at     = { },
				.at   = { },  .executable = { },  .writeable  = true
			}).convert<void *>(
				[&] (Genode::Env::Local_rm::Attachment &a) {
					a.deallocate = false; return a.ptr; },
				[&] (Genode::Env::Local_rm::Error) { return nullptr; }
			);
		}

		void      *_fb_base = _attach();
		RTCRITSECT _fb_lock;

		ComPtr<IDisplay>             _display;
		ComPtr<IDisplaySourceBitmap> _display_bitmap;

		unsigned const _id;

		void _clear_screen()
		{
			if (!_fb_base) return;

			size_t const max_h = Genode::min(_gui_win.area.h, _virtual_fb_area.h);
			size_t const num_pixels = _gui_win.area.w * max_h;
			memset(_fb_base, 0, num_pixels * sizeof(Genode::Pixel_rgb888));
			_gui.framebuffer.refresh({ _gui_win.at, _virtual_fb_area });
		}

		void _adjust_buffer()
		{
			_gui.buffer({ .area = _gui_win.area, .alpha = false });
			_view.area(_gui_win.area);
		}

		Gui::Area _initial_setup()
		{
			_adjust_buffer();
			_view.front();
			return _gui_win.area;
		}

	public:

		NS_DECL_ISUPPORTS

		Genodefb(Genode::Env &env, Gui::Connection &gui,
		         ComPtr<IDisplay> const &display, unsigned id,
		         Gui::Top_level_view &view)
		:
			_env(env), _gui(gui), _view(view),
			_virtual_fb_area(_initial_setup()),
			_display(display), _id(id)
		{
			attempt([&] () { return RTCritSectInit(&_fb_lock); },
			        "unable to initialize critsect");
		}

		virtual ~Genodefb() { }

		int w() const { return _gui_win.area.w; }
		int h() const { return _gui_win.area.h; }

		void update_mode(Gui::Rect gui_win)
		{
			Lock();

			_gui_win = gui_win;

			if (_fb_base)
				_env.rm().detach(Genode::addr_t(_fb_base));

			_adjust_buffer();

			_fb_base = _attach();

			Unlock();
		}

		STDMETHODIMP Lock()
		{
			return Global::vboxStatusCodeToCOM(RTCritSectEnter(&_fb_lock));
		}
	
		STDMETHODIMP Unlock()
		{
			return Global::vboxStatusCodeToCOM(RTCritSectLeave(&_fb_lock));
		}

		STDMETHODIMP NotifyChange(PRUint32 screen, PRUint32 ox, PRUint32 oy,
		                          PRUint32 w, PRUint32 h) override
		{
			HRESULT result = E_FAIL;


			/* save the new bitmap reference */
			_display->QuerySourceBitmap(screen, _display_bitmap.asOutParam());

			Lock();

			bool const ok = (w <= (ULONG)_gui_win.area.w) &&
			                (h <= (ULONG)_gui_win.area.h);

			bool const changed = (w != (ULONG)_virtual_fb_area.w) ||
			                     (h != (ULONG)_virtual_fb_area.h);

			if (ok && changed) {
				Genode::log("fb resize : [", screen, "] ",
				            _virtual_fb_area, " -> ",
				            w, "x", h,
				            " (host: ", _gui_win.area, ") origin: ", ox, ",", oy);

				if ((w < (ULONG)_gui_win.area.w) ||
				    (h < (ULONG)_gui_win.area.h)) {
					/* clear the old content around the new, smaller area. */
					_clear_screen();
				}

				_virtual_fb_area = { w, h };

				result = S_OK;
			} else if (changed) {
				Genode::log("fb resize : [", screen, "] ",
				            _virtual_fb_area, " -> ",
				            w, "x", h, " ignored"
				            " (host: ", _gui_win.area, ") origin: ", ox, ",", oy);
			}

			Unlock();

			/* request appropriate NotifyUpdate() */
			_display->InvalidateAndUpdateScreen(screen);

			return result;
		}

		STDMETHODIMP COMGETTER(Capabilities)(ComSafeArrayOut(FramebufferCapabilities_T, enmCapabilities)) override
		{
			if (ComSafeArrayOutIsNull(enmCapabilities))
				return E_POINTER;

			return S_OK;
		}

		STDMETHODIMP COMGETTER(HeightReduction) (ULONG *reduce) override
		{
			if (!reduce)
				return E_POINTER;

			*reduce = 0;
			return S_OK;
		}

		HRESULT NotifyUpdate(ULONG o_x, ULONG o_y, ULONG width, ULONG height) override
		{
			if (!_fb_base) return S_OK;

			Lock();

			if (_display_bitmap.isNull()) {
				_clear_screen();
				Unlock();
				return S_OK;
			}

			BYTE *pAddress = NULL;
			ULONG ulWidth = 0;
			ULONG ulHeight = 0;
			ULONG ulBitsPerPixel = 0;
			ULONG ulBytesPerLine = 0;
			BitmapFormat_T bitmapFormat = BitmapFormat_Opaque;
			_display_bitmap->QueryBitmapInfo(&pAddress,
			                                 &ulWidth,
			                                 &ulHeight,
			                                 &ulBitsPerPixel,
			                                 &ulBytesPerLine,
			                                 &bitmapFormat);

			Gui::Area const area_fb = Gui::Area(_gui_win.area.w,
			                                    _gui_win.area.h);
			Gui::Area const area_vm = Gui::Area(ulBytesPerLine/(ulBitsPerPixel/8), ulHeight);

			using namespace Genode;

			using Pixel_src = Pixel_rgb888;
			using Pixel_dst = Pixel_rgb888;

			Texture<Pixel_src> texture((Pixel_src *)pAddress, nullptr, area_vm);
			Surface<Pixel_dst> surface((Pixel_dst *)_fb_base, area_fb);

			surface.clip(Gui::Rect(Gui::Point(o_x, o_y), Gui::Area(width, height)));

			Texture_painter::paint(surface,
			                       texture,
			                       Genode::Color(0, 0, 0),
			                       Gui::Point(0, 0),
			                       Texture_painter::SOLID,
			                       false);

			_gui.framebuffer.refresh(o_x, o_y, width, height);

			Unlock();

			return S_OK;
		}

		STDMETHODIMP NotifyUpdateImage(PRUint32 o_x, PRUint32 o_y,
		                               PRUint32 width, PRUint32 height,
		                               PRUint32 imageSize,
		                               PRUint8 *image) override
		{
			if (!_fb_base) return S_OK;

			Lock();

			Gui::Area const area_fb = _gui_win.area;
			Gui::Area const area_vm = Gui::Area(width, height);

			using namespace Genode;

			using Pixel_src = Pixel_rgb888;
			using Pixel_dst = Pixel_rgb888;

			Texture<Pixel_src> texture((Pixel_src *)image, nullptr, area_vm);
			Surface<Pixel_dst> surface((Pixel_dst *)_fb_base, area_fb);

			Texture_painter::paint(surface,
			                       texture,
			                       Genode::Color(0, 0, 0),
			                       Gui::Point(o_x, o_y),
			                       Texture_painter::SOLID,
			                       false);

			_gui.framebuffer.refresh(o_x, o_y, area_vm.w, area_vm.h);

			Unlock();

			return S_OK;
		}

		STDMETHODIMP COMGETTER(Overlay) (IFramebufferOverlay **) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(WinId) (PRInt64 *winId) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP VideoModeSupported(ULONG width, ULONG height,
		                                ULONG bpp, BOOL *supported) override
		{
			if (!supported)
				return E_POINTER;

			*supported = ((width <= (ULONG)_gui_win.area.w) &&
			              (height <= (ULONG)_gui_win.area.h));

			return S_OK;
		}

		STDMETHODIMP Notify3DEvent(PRUint32, PRUint32, PRUint8 *) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP ProcessVHWACommand(BYTE *, LONG, BOOL) override {
			Assert(!"FixMe");
		    return E_NOTIMPL; }

		STDMETHODIMP GetVisibleRegion(BYTE *, ULONG, ULONG *) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }
		
		STDMETHODIMP SetVisibleRegion(BYTE *, ULONG) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(PixelFormat) (BitmapFormat_T *format) {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(BitsPerPixel)(ULONG *bits) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(BytesPerLine)(ULONG *line) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(Width)(ULONG *width) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }

		STDMETHODIMP COMGETTER(Height)(ULONG *height) override {
			Assert(!"FixMe");
			return E_NOTIMPL; }
};
