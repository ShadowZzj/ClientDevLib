
#include "Window.h"
#include <sstream>
#include <General/util/Exception/Exception.h>
using namespace zzj;

// Window Class Stuff
Window::WindowClass Window::WindowClass::wndClass;

Window::WindowClass::WindowClass() noexcept
	:
	hInst( GetModuleHandle( nullptr ) )
{
	WNDCLASSEXA wc = { 0 };
	wc.cbSize = sizeof( wc );
	wc.style = CS_OWNDC;
	wc.lpfnWndProc = HandleMsgSetup;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = GetInstance();
	wc.hIcon = nullptr;
	wc.hCursor = nullptr;
	wc.hbrBackground = nullptr;
	wc.lpszMenuName = nullptr;
	wc.lpszClassName = GetName();
	wc.hIconSm = nullptr;
	RegisterClassExA( &wc );
}

Window::WindowClass::~WindowClass()
{
	UnregisterClassA( wndClassName,GetInstance() );
}

const char* Window::WindowClass::GetName() noexcept
{
	return wndClassName;
}

HINSTANCE Window::WindowClass::GetInstance() noexcept
{
	return wndClass.hInst;
}

HWND zzj::Window::GetHWND() const noexcept
{
	return hWnd;
}

// Window Stuff
Window::Window( int width,int height,const std::string& name )
	:
	width( width ),
	height( height )
{
	// calculate window size based on desired client region size
	RECT wr;
	wr.left = 100;
	wr.right = width + wr.left;
	wr.top = 100;
	wr.bottom = height + wr.top;
	if( AdjustWindowRect( &wr,WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU,FALSE ) == 0 )
	{
		throw ZZJ_LAST_WIN32_EXCEPTION();
	}
	// create window & get hWnd
	hWnd = CreateWindowExA(0,
		WindowClass::GetName(),name.c_str(),
		WS_CAPTION | WS_MINIMIZEBOX | WS_SYSMENU,
		CW_USEDEFAULT,CW_USEDEFAULT,wr.right - wr.left,wr.bottom - wr.top,
		nullptr,nullptr,WindowClass::GetInstance(),this
	);
	SetWindowTextA( hWnd,name.c_str() );

	// check for error
	if( hWnd == nullptr )
	{
		throw ZZJ_LAST_WIN32_EXCEPTION();
	}
	// show window
	ShowWindow( hWnd,SW_SHOWDEFAULT );
}

Window::~Window()
{
	DestroyWindow( hWnd );
}

void Window::SetTitle( const std::string& title )
{
	if( SetWindowTextA( hWnd,title.c_str() ) == 0 )
	{
		throw ZZJ_LAST_WIN32_EXCEPTION();
	}
}

std::optional<int> Window::ProcessMessages()
{
	MSG msg;
	// while queue has messages, remove and dispatch them (but do not block on empty queue)
	while( PeekMessage( &msg,nullptr,0,0,PM_REMOVE ) )
	{
		// check for quit because peekmessage does not signal this via return val
		if( msg.message == WM_QUIT )
		{
			// return optional wrapping int (arg to PostQuitMessage is in wparam) signals quit
			return (int)msg.wParam;
		}

		// TranslateMessage will post auxilliary WM_CHAR messages from key msgs
		TranslateMessage( &msg );
		DispatchMessage( &msg );
	}

	// return empty optional when not quitting app
	return {};
}


LRESULT CALLBACK Window::HandleMsgSetup( HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam ) noexcept
{
	// use create parameter passed in from CreateWindow() to store window class pointer at WinAPI side
	if( msg == WM_NCCREATE )
	{
		// extract ptr to window class from creation data
		const CREATESTRUCTW* const pCreate = reinterpret_cast<CREATESTRUCTW*>(lParam);
		Window* const pWnd = static_cast<Window*>(pCreate->lpCreateParams);
		// set WinAPI-managed user data to store ptr to window instance
		SetWindowLongPtr( hWnd,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(pWnd) );
		// set message proc to normal (non-setup) handler now that setup is finished
		SetWindowLongPtr( hWnd,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&Window::HandleMsgThunk) );
		// forward message to window instance handler
		return pWnd->HandleMsg( hWnd,msg,wParam,lParam );
	}
	// if we get a message before the WM_NCCREATE message, handle with default handler
	return DefWindowProc( hWnd,msg,wParam,lParam );
}

LRESULT CALLBACK Window::HandleMsgThunk( HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam ) noexcept
{
	// retrieve ptr to window instance
	Window* const pWnd = reinterpret_cast<Window*>(GetWindowLongPtr( hWnd,GWLP_USERDATA ));
	// forward message to window instance handler
	return pWnd->HandleMsg( hWnd,msg,wParam,lParam );
}

LRESULT Window::HandleMsg( HWND hWnd,UINT msg,WPARAM wParam,LPARAM lParam ) noexcept
{
	switch( msg )
	{
	// we don't want the DefProc to handle this message because
	// we want our destructor to destroy the window, so return 0 instead of break
	case WM_CLOSE:
		PostQuitMessage( 0 );
		return 0;
	// clear keystate when window loses focus to prevent input getting "stuck"
	case WM_KILLFOCUS:
		kbd.ClearState();
		break;

	/*********** KEYBOARD MESSAGES ***********/
	case WM_KEYDOWN:
	// syskey commands need to be handled to track ALT key (VK_MENU) and F10
	case WM_SYSKEYDOWN:
		if( !(lParam & 0x40000000) || kbd.AutorepeatIsEnabled() ) // filter autorepeat
		{
			kbd.OnKeyPressed( static_cast<unsigned char>(wParam) );
		}
		break;
	case WM_KEYUP:
	case WM_SYSKEYUP:
		kbd.OnKeyReleased( static_cast<unsigned char>(wParam) );
		break;
	case WM_CHAR:
		kbd.OnChar( static_cast<unsigned char>(wParam) );
		break;
	/*********** END KEYBOARD MESSAGES ***********/

	/************* MOUSE MESSAGES ****************/
	case WM_MOUSEMOVE:
	{
		const POINTS pt = MAKEPOINTS( lParam );
		// in client region -> log move, and log enter + capture mouse (if not previously in window)
		if( pt.x >= 0 && pt.x < width && pt.y >= 0 && pt.y < height )
		{
			mouse.OnMouseMove( pt.x,pt.y );
			if( !mouse.IsInWindow() )
			{
				SetCapture( hWnd );
				mouse.OnMouseEnter();
			}
		}
		// not in client -> log move / maintain capture if button down
		else
		{
			if( wParam & (MK_LBUTTON | MK_RBUTTON) )
			{
				mouse.OnMouseMove( pt.x,pt.y );
			}
			// button up -> release capture / log event for leaving
			else
			{
				ReleaseCapture();
				mouse.OnMouseLeave();
			}
		}
		break;
	}
	case WM_LBUTTONDOWN:
	{
		const POINTS pt = MAKEPOINTS( lParam );
		mouse.OnLeftPressed( pt.x,pt.y );
		break;
	}
	case WM_RBUTTONDOWN:
	{
		const POINTS pt = MAKEPOINTS( lParam );
		mouse.OnRightPressed( pt.x,pt.y );
		break;
	}
	case WM_LBUTTONUP:
	{
		const POINTS pt = MAKEPOINTS( lParam );
		mouse.OnLeftReleased( pt.x,pt.y );
		// release mouse if outside of window
		if( pt.x < 0 || pt.x >= width || pt.y < 0 || pt.y >= height )
		{
			ReleaseCapture();
			mouse.OnMouseLeave();
		}
		break;
	}
	case WM_RBUTTONUP:
	{
		const POINTS pt = MAKEPOINTS( lParam );
		mouse.OnRightReleased( pt.x,pt.y );
		// release mouse if outside of window
		if( pt.x < 0 || pt.x >= width || pt.y < 0 || pt.y >= height )
		{
			ReleaseCapture();
			mouse.OnMouseLeave();
		}
		break;
	}
	case WM_MOUSEWHEEL:
	{
		const POINTS pt = MAKEPOINTS( lParam );
		const int delta = GET_WHEEL_DELTA_WPARAM( wParam );
		mouse.OnWheelDelta( pt.x,pt.y,delta );
		break;
	}
	/************** END MOUSE MESSAGES **************/
	}

	return DefWindowProc( hWnd,msg,wParam,lParam );
}

