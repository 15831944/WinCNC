#include "CNC.h"
#include "geometry.h"
#include "gcode.h"
#include "socket.h"
#include "motor.h"

#include <wchar.h>
#include <mmsystem.h>

DWORD pathSteps;
DWORD maxPathSteps;
t3DPoint *path;

extern HWND hMainWindow;

POINT O = { 50, 500 };

t2DPoint vuX = { 100, 0 };
t2DPoint vuY = { 60, -40 };
t2DPoint vuZ = { 0, -100 };

extern t3DPoint g_displayPos;
extern int g_debug[4];

void MoveTo3D(HDC hdc, t3DPoint p)
{
	int x,y;
	x = O.x + (LONG)(p.x * vuX.x + p.y * vuY.x + p.z * vuZ.x);
	y = O.y + (LONG)(p.x * vuX.y + p.y * vuY.y + p.z * vuZ.y);
	MoveToEx(hdc, x, y, NULL);
}

void LineTo3D(HDC hdc, t3DPoint p)
{
	int x, y;
	x = O.x + (LONG)(p.x * vuX.x + p.y * vuY.x + p.z * vuZ.x);
	y = O.y + (LONG)(p.x * vuX.y + p.y * vuY.y + p.z * vuZ.y);
	LineTo(hdc, x, y);
}

const t3DPoint   O3D = { 0, 0, 0 };
const t3DPoint vu3DX = { 1, 0, 0 };
const t3DPoint vu3DY = { 0, 1, 0 };
const t3DPoint vu3DZ = { 0, 0, 1 };

#define VIEW_MARGIN			10
#define VIEW_STATUS_FONT	L"Arial"
#define VIEW_POSITION_FONT	L"Courier New"

void OnPaint(HWND hWnd)
{
	RECT view;
	RECT rect;
	HFONT font;
	CHAR str[100];
	HDC hdcMem;
	HBITMAP hbmMem;
	HANDLE hOld;
	PAINTSTRUCT ps;
	HDC hdc;
	int height,width;

	hdc = BeginPaint(hWnd, &ps);

	// Get the size of our client view for later
	GetClientRect(hWnd, &view);
	width = view.right - view.left;
	height = view.bottom - view.top;

	// Create an off-screen DC for double-buffering
	hdcMem = CreateCompatibleDC(hdc);
	hbmMem = CreateCompatibleBitmap(hdc, width, height);
	hOld = SelectObject(hdcMem, hbmMem);
	
	// This comes black by default. Clear the memory DC with a white brush.
	HBRUSH hBrush = CreateSolidBrush(GetBkColor(hdcMem));
	FillRect(hdcMem, &view, hBrush);

	// TOP banner : Show status of the CNC communication pipe
	int statusHeight = (height - (VIEW_MARGIN * 2)) / 16;
	rect.left = view.left + VIEW_MARGIN;
	rect.right = rect.left + width - VIEW_MARGIN * 2;
	rect.top = view.top + VIEW_MARGIN;
	rect.bottom = rect.top + statusHeight;
	font = CreateFont(
		statusHeight, 0, 0, 0, 
		FW_REGULAR, false, false, false, 
		DEFAULT_CHARSET, 
		OUT_DEFAULT_PRECIS, 
		CLIP_DEFAULT_PRECIS, 
		DEFAULT_QUALITY, 
		DEFAULT_PITCH, 
		VIEW_STATUS_FONT);
	SelectObject(hdcMem, font);
	getSocketStatusString(str, sizeof(str));
	DrawTextA(hdcMem, str, -1, &rect, 0);
	DeleteObject(font);

	// Upper Right corner : Real position of CNC 
	int positionHeight = (height - (VIEW_MARGIN * 3) - statusHeight ) / 2;
	rect.left = view.left + VIEW_MARGIN;
	rect.right = rect.left + ( width - ( VIEW_MARGIN * 3 )) / 2 ;
	rect.top = view.top + VIEW_MARGIN * 2 + statusHeight;
	rect.bottom = rect.top + positionHeight;
	font = CreateFont(
		positionHeight/3, 0, 0, 0, 
		FW_REGULAR, false, false, false, 
		DEFAULT_CHARSET, 
		OUT_DEFAULT_PRECIS, 
		CLIP_DEFAULT_PRECIS, 
		DEFAULT_QUALITY, 
		DEFAULT_PITCH,
		VIEW_POSITION_FONT );
	SelectObject(hdcMem, font);
	//swprintf(str, 100, L"X:%.4f\r\nY:%.4f\r\nZ:%.4f",
	sprintf_s(str, sizeof(str), "X:%.3f\r\nY:%.3f\r\nZ:%.3f",
		g_displayPos.x,
		g_displayPos.y,
		g_displayPos.z);
	DrawTextA(hdcMem, str, -1, &rect, 0);
	DeleteObject(font);

	// Lower right corner : Debug information
	int debugHeight = (height - (VIEW_MARGIN * 3) - statusHeight) / 2;
	rect.left = view.left + VIEW_MARGIN;
	rect.right = rect.left + (width - (VIEW_MARGIN * 3)) / 2;
	rect.top = view.top + VIEW_MARGIN * 2 + statusHeight + positionHeight;
	rect.bottom = rect.top + debugHeight;
	font = CreateFont(
		positionHeight / 10, 0, 0, 0,
		FW_REGULAR, false, false, false,
		DEFAULT_CHARSET,
		OUT_DEFAULT_PRECIS,
		CLIP_DEFAULT_PRECIS,
		DEFAULT_QUALITY,
		DEFAULT_PITCH,
		VIEW_POSITION_FONT);
	SelectObject(hdcMem, font);

	long xInPipe, yInPipe, zInPipe;
	t3DPoint current, inPipe;
	getCurPos(&current);
	getDistanceInPipe(&xInPipe, &yInPipe, &zInPipe);
	stepToPos(xInPipe, yInPipe, zInPipe, &inPipe);
	current.x -= inPipe.x;
	current.y -= inPipe.y;
	current.z -= inPipe.z;

	sprintf_s(str, sizeof(str), "X:%.3f Y:%.3f Z:%.3f\r\nA:%d\r\nB:%d\r\nC:%d\r\nD:%d",
		current.x,
		current.y,
		current.z,
		g_debug[0],
		g_debug[1],
		g_debug[2],
		g_debug[3]);
	DrawTextA(hdcMem, str, -1, &rect, 0);
	DeleteObject(font);

	// Transfer the off-screen DC to the screen
	BitBlt(hdc, 0, 0, width, height, hdcMem, 0, 0, SRCCOPY);

	// Free-up the off-screen DC
	SelectObject(hdcMem, hOld);
	DeleteObject(hbmMem);
	DeleteObject(hBrush);
	DeleteDC(hdcMem);
	EndPaint(hWnd, &ps);
}

typedef struct
{
	DWORD id;
	DWORD dx;
	DWORD dy;
	float res;
	DWORD cbAlt;
} header_t;

float g_res;
float *g_alt = NULL;
header_t g_header;
DWORD g_cbAlt = 0;
DWORD countX,countY;
WCHAR g_szAltFileName[MAX_PATH];
HANDLE g_hFileChangeEvent = INVALID_HANDLE_VALUE;

#define MAX_TOOL_SHAPE	1000

typedef struct {
	int dx;
	int dy;
} g_toolShape_t;

g_toolShape_t* g_toolShape = NULL;

DWORD iToolPoints = 0;

//
// TODO : Find the path dynamically instead of hardcoded.
//
void startViewer(const WCHAR* szFilePath)
{
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	
	memset(&pi, 0x00, sizeof(pi));
	memset(&si, 0x00, sizeof(si));
	si.cb = sizeof(STARTUPINFO);
	GetStartupInfo(&si);

	g_hFileChangeEvent = CreateEvent(NULL, FALSE, FALSE, L"Local\\AltFileChangeEvent");

	// This to make sure Viewer.exe finds the file Viewer.fx
	SetCurrentDirectory(L"C:\\Users\\Eric\\Documents\\GitHub\\WinCnc\\Viewer");

	WCHAR cmdLine[MAX_PATH];
	wcscpy_s(cmdLine, MAX_PATH, L"Viewer.exe ");
	wcscat_s(cmdLine, MAX_PATH, szFilePath);

	if (!CreateProcess(L"C:\\Users\\Eric\\Documents\\GitHub\\WinCnc\\Debug\\Viewer.exe", cmdLine, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi))
	{
		WCHAR msg[MAX_PATH + 80];
		wsprintf(msg, L"Unable to open viewer. Reason %d.\r\n", GetLastError());
		MessageBox(NULL, msg, L"CNC", MB_OK);
	}
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
}

void saveAltitude(WCHAR* szFilePath)
{
	HANDLE hFile;
	hFile = CreateFile(szFilePath, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
	if (hFile != INVALID_HANDLE_VALUE)
	{
		DWORD dwWritten;
		WriteFile(hFile, &g_header, sizeof(header_t), &dwWritten, NULL);
		WriteFile(hFile, g_alt, g_cbAlt, &dwWritten, NULL);
		CloseHandle(hFile);
		SetEvent(g_hFileChangeEvent);
	}
}

extern tMetaData g_MetaData;

void resetBlockSurface()
{
	if (g_alt)
	{
		// Set the height of the block for all the vertexes on the surface
		for (DWORD x = 0; x < countX; x++) for (DWORD y = 0; y < countY; y++)
		{
			g_alt[x*countY + y] = (float)(g_MetaData.blockZ - g_MetaData.offsetZ);
		}

		saveAltitude(g_szAltFileName);
	}
}

void init3DView( float res )
{
	g_res = res;

	countX = 1 + (DWORD)(g_MetaData.blockX / res);
	countY = 1 + (DWORD)(g_MetaData.blockY / res);

	g_cbAlt = sizeof(float) * countX * countY;

	g_header.id = 0x00010002;
	g_header.dx = countX;
	g_header.dy = countY;
	g_header.res = g_res;
	g_header.cbAlt = g_cbAlt;

	if (g_alt) free(g_alt);
	g_alt = (float*)malloc(g_cbAlt);

	// Set the height of the block for all the vertexes on the surface
	for (DWORD x = 0; x < countX; x++) for (DWORD y = 0; y < countY; y++)
	{
		g_alt[x*countY + y] = (float)(g_MetaData.blockZ - g_MetaData.offsetZ);
	}

	// Set the 4 edges of the array to the zero offset so that the edges of
	// the block will be rendered. There is a small vertical line artifact 
	// when the tool crosses the edge of the block but it's acceptable.
	for (DWORD x = 0; x < countX; x++)
	{
		g_alt[x*countY + countY - 1] = (float)-g_MetaData.offsetZ;
		g_alt[x*countY] = (float)-g_MetaData.offsetZ;
	}
	for (DWORD y = 0; y < countY; y++)
	{
		g_alt[y] = (float)-g_MetaData.offsetZ;
		g_alt[(countX - 1)*countY + y] = (float)-g_MetaData.offsetZ;
	}

	// Fill a matrix that approximates the points the tool will remove over
	// the surface. This is an optimization to avoid calculating all the points
	// contained within the tool for every position of the tool motion.
	//
	int iRtool = (int)(g_MetaData.toolRadius / res) + 1;

	iToolPoints = 0;
	if (g_toolShape) free(g_toolShape);
	g_toolShape = (g_toolShape_t*)malloc(iRtool*iRtool*sizeof(g_toolShape_t));

	for (int i = 0; i < iRtool; i++) for (int j = 0; j < iRtool; j++)
	{
		float x = i*res;
		float y = j*res;
		if (sqrt(x*x + y*y) <= g_MetaData.toolRadius)
		{
			g_toolShape[iToolPoints].dx = i;
			g_toolShape[iToolPoints].dy = j;
			iToolPoints++;
		}
	}

	wcscpy_s(g_szAltFileName, MAX_PATH, L"C:\\windows\\temp\\simAltitude.dat");

	saveAltitude(g_szAltFileName);

	// This launches the 3D viewer window
	startViewer(g_szAltFileName);
}

void update3DView()
{
	if (g_alt)
	{
		saveAltitude(g_szAltFileName);
	}
	// This launches the 3D viewer window
	// startViewer(g_szAltFileName);
}

inline void toolAt(long x, long y, float z)
{
	if (x >= 0 && y >= 0 && x<(long)countX && y<(long)countY)
	{
		float* point = &g_alt[x * countY + y];
		if (*point > z)
		{
			*point = z;
		}
	}
}

tStatus buildPath(t3DPoint P, long x, long y, long z, long d, long s)
{
	static DWORD prevTime = 0;
	static t3DPoint p = { 0.0, 0.0, 0.0 };
	t3DPoint v = { P.x - p.x, P.y - p.y, P.z - p.z };

	double l = vector3DLength(v);
	unsigned long step = (unsigned long)(l / g_res);
	if (step == 0)
	{
		step = 1;
	}
	else
	{
		v.x = v.x / step;
		v.y = v.y / step;
		v.z = v.z / step;
	}

	for (unsigned long n = 0; n < step; n++)
	{
		p.x += v.x;
		p.y += v.y;
		p.z += v.z;

		DWORD iX = (DWORD)((p.x - g_MetaData.offsetX) / g_res);
		DWORD iY = (DWORD)((p.y - g_MetaData.offsetY) / g_res);

		for (DWORD i = 0; i < iToolPoints; i++)
		{
			toolAt(iX + g_toolShape[i].dx, iY + g_toolShape[i].dy, (float)p.z);
			if (i != 0)
			{
				toolAt(iX - g_toolShape[i].dx, iY + g_toolShape[i].dy, (float)p.z);
				toolAt(iX - g_toolShape[i].dx, iY - g_toolShape[i].dy, (float)p.z);
				toolAt(iX + g_toolShape[i].dx, iY - g_toolShape[i].dy, (float)p.z);
			}
		}
		DWORD current = timeGetTime();
		if (current > prevTime + 50 )
		{
			PostMessage(hMainWindow, WM_UPDATE_POSITION, 0, 0);
			prevTime = current;
		}

		Sleep(1);
	}
	p = P;

	/*
	if (pathSteps >= maxPathSteps)
	{
	// Increase the size of our buffer
	maxPathSteps = (maxPathSteps + 2) * 2;
	t3DPoint* newPath = (t3DPoint*)malloc(sizeof(t3DPoint)*maxPathSteps);
	if (path && pathSteps )
	{
	memcpy(newPath, path, pathSteps * sizeof(t3DPoint));
	free(path);
	}
	path = newPath;
	}

	path[pathSteps++] = P;
	*/

	// Normal speed
	//Sleep( d / 1000 );
	
	// 3x faster
	//Sleep(d / 3000);

	return retSuccess;
}
