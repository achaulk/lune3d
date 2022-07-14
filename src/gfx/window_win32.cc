#include "window.h"

#include "logging.h"
#include "event.h"
#include "sys/thread.h"

#include <Windows.h>
#undef CreateWindow

LUNE_MODULE()

namespace lune {
namespace gfx {
namespace {

struct WindowCreateInfo
{
	DWORD style = WS_OVERLAPPEDWINDOW;
	DWORD ex_style = WS_EX_OVERLAPPEDWINDOW;
	std::wstring wnd_name;
	int w = CW_USEDEFAULT, h = CW_USEDEFAULT, x = CW_USEDEFAULT, y = CW_USEDEFAULT;
	void *arg;
	int dpi = 96;

	void AdjustSize()
	{
		if(w == CW_USEDEFAULT || h == CW_USEDEFAULT)
			return;
		RECT r = {0, 0, w, h};
		if(AdjustWindowRectExForDpi(&r, style, FALSE, ex_style, dpi)) {
			w = r.right - r.left;
			h = r.bottom - r.top;
		}
	}

	void RegClass(WNDPROC wndproc)
	{
		static bool registered = false;
		if(registered)
			return;
		registered = true;
		DWORD cs_style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
		WNDCLASSEXW wc = {sizeof(WNDCLASSEXW), cs_style, wndproc, 0, 0, NULL, NULL, LoadCursor(NULL, IDC_ARROW),
		    (HBRUSH)GetStockObject(BLACK_BRUSH), NULL, L"LuneWindow", NULL};
		if(!RegisterClassExW(&wc))
			abort();
	}

	HWND Create(WNDPROC wndproc)
	{
		RegClass(wndproc);
		return CreateWindowExW(ex_style, L"LuneWindow", wnd_name.c_str(), style, x, y, w, h, NULL, NULL, NULL, arg);
	}
};


class Win32Window : public Window
{
public:
	~Win32Window() override
	{
		DestroyWindow(wnd_);
		WindowMessageLoop m;
		m.RunUntilIdle();
		SetWindowLongPtr(wnd_, GWLP_USERDATA, 0);
	}

	void *GetHandle() override
	{
		return wnd_;
	}

	Size GetSize() override
	{
		RECT r;
		GetClientRect(wnd_, &r);
		return Size{(uint32_t)(r.right - r.left), (uint32_t)(r.bottom - r.top)};
	}

	static LRESULT CALLBACK WndProc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
	{
		Win32Window *self = (Win32Window *)GetWindowLongPtr(wnd, GWLP_USERDATA);
		switch(msg) {
		case WM_CREATE:
			SetWindowLongPtr(wnd, GWLP_USERDATA, (LONG_PTR)((LPCREATESTRUCT)lp)->lpCreateParams);
			break;
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
			printf("DOWN\n");
			break;
		case WM_LBUTTONUP:
		case WM_RBUTTONUP:
		case WM_MBUTTONUP:
			break;
		case WM_MOUSEMOVE:
			break;
		case WM_MOUSEACTIVATE:
			return (LOWORD(lp) == HTCLIENT) ? MA_ACTIVATEANDEAT : MA_ACTIVATE;
		case WM_ACTIVATE:
			self->active_ = LOWORD(wp) != WA_INACTIVE;
			break;
		case WM_UNICHAR:
			if(wp == UNICODE_NOCHAR)
				return TRUE;
			[[fallthrough]];
		case WM_KEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP:
		case WM_SYSKEYDOWN:
		case WM_CHAR:
			switch(msg) {
			case WM_KEYDOWN:
			case WM_SYSKEYDOWN:
				break;
			case WM_KEYUP:
			case WM_SYSKEYUP:
				break;
			case WM_CHAR:
			case WM_UNICHAR:
				break;
			}
			return 0;
		case WM_NCMOUSEMOVE:
		case WM_SETCURSOR:
		case WM_NCHITTEST:
			return DefWindowProc(wnd, msg, wp, lp);
		case WM_CLOSE:
			PostQuitMessage(0);
			break;
		case WM_SIZE:
			break;
		default:
			LOGW("Unhandled message: %d %04X / %p / %p", msg, msg, wp, lp);
		}
		return DefWindowProc(wnd, msg, wp, lp);
	}

	HWND wnd_;
	bool active_ = true;
};

} // namespace

std::unique_ptr<Window> CreateWindow(const WindowOptions &opts)
{
	WindowCreateInfo wci;

	auto w = new Win32Window();
	wci.arg = w;
	w->wnd_ = wci.Create(&Win32Window::WndProc);
	ShowWindow(w->wnd_, SW_SHOWNORMAL);

	return std::unique_ptr<Window>(w);
}

} // namespace gfx
} // namespace lune
