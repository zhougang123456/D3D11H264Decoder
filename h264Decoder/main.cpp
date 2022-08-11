#include <stdio.h>
#include "d3d11_video_decoder.hpp"

HWND g_hWnd(NULL);

LRESULT CALLBACK WinProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg)
	{
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}

BOOL InitWin32(HINSTANCE hInstance)
{
	WNDCLASS wndcls;
	wndcls.cbClsExtra = 0;
	wndcls.cbWndExtra = 0;
	wndcls.hbrBackground = (HBRUSH)GetStockObject(NULL_BRUSH);
	wndcls.hCursor = LoadCursor(NULL, IDC_ARROW);
	wndcls.hIcon = LoadIcon(NULL, IDI_APPLICATION);
	wndcls.hInstance = hInstance;
	wndcls.lpfnWndProc = WinProc;
	wndcls.lpszClassName = L"D3D11Decoder";
	wndcls.lpszMenuName = NULL;
	wndcls.style = CS_HREDRAW | CS_VREDRAW;

	if (!RegisterClass(&wndcls))
	{
		MessageBox(NULL, L"Register window failed!", L"error", MB_OK);
		return FALSE;
	}

	g_hWnd = CreateWindow(L"D3D11Decoder",
		L"D3D11Decoder",
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		960, 400,
		NULL,
		NULL,
		hInstance,
		NULL);
	if (!g_hWnd)
	{
		MessageBox(NULL, L"Create window failed!", L"error", MB_OK);
		return FALSE;
	}

	ShowWindow(g_hWnd, SW_SHOW);
	UpdateWindow(g_hWnd);

	return TRUE;
}


int main(HINSTANCE hInstance) {
    std::unique_ptr<VideoDecoder> video_decoder = D3D11VideoDecoder::Create();
    VideoColorSpace color_space = VideoColorSpace::REC709();
    Size code_size(600, 480);
	InitWin32(hInstance);
    video_decoder->Initialize(color_space, code_size, g_hWnd);
    const char* filename = "oceans.h264";
    FILE* fd = fopen(filename, "r");
    fseek(fd, 0, SEEK_END);
    int size = ftell(fd);
    printf("stream size is %d\n", size);
    uint8_t* stream = (uint8_t*)malloc(sizeof(uint8_t) * size);
    fseek(fd, 0, SEEK_SET);
    fread(stream, 1, size, fd);
    video_decoder->Decode(stream, size);
    return 0;
}
