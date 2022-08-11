#include "D3D11NV12Rendering.h"
#include "d3d11_video_decoder.hpp"

#define MAX_LOADSTRING 100

//
// Globals
//

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
HWND hWnd;										// The window handle created

// Forward declarations of functions included in this code module:
ATOM                MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    WndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);


int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
                     _In_opt_ HINSTANCE hPrevInstance,
                     _In_ LPWSTR    lpCmdLine,
                     _In_ int       nCmdShow)
{
    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

    // TODO: Place code here.

    // Initialize global strings
    LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
    LoadStringW(hInstance, IDC_D3D11NV12RENDERING, szWindowClass, MAX_LOADSTRING);
    MyRegisterClass(hInstance);

    // Perform application initialization:
    if (!InitInstance (hInstance, nCmdShow))
    {
        return FALSE;
    }

    HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_D3D11NV12RENDERING));

	RECT DeskBounds;
	bool FirstTime = true;
	bool Occluded = true;

	std::unique_ptr<VideoDecoder> video_decoder = D3D11VideoDecoder::Create();
	VideoColorSpace color_space = VideoColorSpace::REC709();
	Size code_size(960, 400);
	video_decoder->Initialize(color_space, code_size, hWnd);

	MSG msg = { 0 };

	while (WM_QUIT != msg.message)
	{
		DUPL_RETURN Ret = DUPL_RETURN_SUCCESS;
		if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
		{
			if (msg.message == OCCLUSION_STATUS_MSG)
			{
				// Present may not be occluded now so try again
				Occluded = false;
			}
			else
			{
				// Process window messages
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else if (FirstTime)
		{

			// First time through the loop so nothing to clean up
			FirstTime = false;
			
			const char* filename = "oceans.h264";
			FILE* fd = fopen(filename, "r");
			fseek(fd, 0, SEEK_END);
			int size = ftell(fd);
			printf("stream size is %d\n", size);
			uint8_t* stream = (uint8_t*)malloc(sizeof(uint8_t) * size);
			fseek(fd, 0, SEEK_SET);
			fread(stream, 1, size, fd);
			video_decoder->Decode(stream, size);
			// Re-initialize

			// We start off in occluded state and we should immediate get a occlusion status window message
			Occluded = true;
		}
		else
		{
			// Nothing else to do, so try to present to write out to window if not occluded
			if (!Occluded)
			{
				const char* filename = "oceans.h264";
				FILE* fd = fopen(filename, "r");
				fseek(fd, 0, SEEK_END);
				int size = ftell(fd);
				printf("stream size is %d\n", size);
				uint8_t* stream = (uint8_t*)malloc(sizeof(uint8_t) * size);
				fseek(fd, 0, SEEK_SET);
				fread(stream, 1, size, fd);
				video_decoder->Decode(stream, size);

			}
		}
	}

	if (msg.message == WM_QUIT)
	{
		// For a WM_QUIT message we should return the wParam value
		return static_cast<INT>(msg.wParam);
	}

    return (int) msg.wParam;
}



//
//  FUNCTION: MyRegisterClass()
//
//  PURPOSE: Registers the window class.
//
ATOM MyRegisterClass(HINSTANCE hInstance)
{
    WNDCLASSEXW wcex;

    wcex.cbSize = sizeof(WNDCLASSEX);

    wcex.style          = CS_HREDRAW | CS_VREDRAW;
    wcex.lpfnWndProc    = WndProc;
    wcex.cbClsExtra     = 0;
    wcex.cbWndExtra     = 0;
    wcex.hInstance      = hInstance;
    wcex.hIcon          = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_D3D11NV12RENDERING));
    wcex.hCursor        = LoadCursor(nullptr, IDC_ARROW);
    wcex.hbrBackground  = (HBRUSH)(COLOR_WINDOW+1);
	wcex.lpszMenuName = nullptr;// MAKEINTRESOURCEW(IDC_D3D11NV12RENDERING);
    wcex.lpszClassName  = szWindowClass;
    wcex.hIconSm        = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

    return RegisterClassExW(&wcex);
}

//
//   FUNCTION: InitInstance(HINSTANCE, int)
//
//   PURPOSE: Saves instance handle and creates main window
//
//   COMMENTS:
//
//        In this function, we save the instance handle in a global variable and
//        create and display the main program window.
//
BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
   hInst = hInstance; // Store instance handle in our global variable

   hWnd = CreateWindowW(szWindowClass, szTitle, WS_OVERLAPPEDWINDOW,
      CW_USEDEFAULT, 0, CW_USEDEFAULT, 0, nullptr, nullptr, hInstance, nullptr);

   if (!hWnd)
   {
      return FALSE;
   }

   ShowWindow(hWnd, nCmdShow);
   UpdateWindow(hWnd);

   return TRUE;
}

//
//  FUNCTION: WndProc(HWND, UINT, WPARAM, LPARAM)
//
//  PURPOSE:  Processes messages for the main window.
//
//  WM_COMMAND  - process the application menu
//  WM_PAINT    - Paint the main window
//  WM_DESTROY  - post a quit message and return
//
//
LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_DESTROY:
	{
		PostQuitMessage(0);
		break;
	}
	case WM_SIZE:
	{
		// Tell output manager that window size has changed
		break;
	}
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}

	return 0;
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
    UNREFERENCED_PARAMETER(lParam);
    switch (message)
    {
    case WM_INITDIALOG:
        return (INT_PTR)TRUE;

    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
        {
            EndDialog(hDlg, LOWORD(wParam));
            return (INT_PTR)TRUE;
        }
        break;
    }
    return (INT_PTR)FALSE;
}