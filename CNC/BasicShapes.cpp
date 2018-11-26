#include "CNC.h"
#include "Resource.h"
#include "Windowsx.h"
#include "math.h"

#include "gcode.h"
#include "fileParser.h"
#include "Shapes.h"
#include "3Dview.h"

typedef struct
{
	tGeneralToolInfo tool;

	// Circle
	float circleRadiusX;
	float circleRadiusY;
	int circleOval;
	float circleDepth;
	int circleExternal;
	int circleFill;

	// Rectangle
	float rectDepth;
	float rectX;
	float rectY;
	int rectFill;
	int rectExternal;
	int rectRounded;
	float rectRadius;

} tSimpleShapeParam;

tSimpleShapeParam g_Params;

void BasicShapeInit(HWND hWnd)
{
	int i;
	HWND hItem;

	hItem = GetDlgItem(hWnd, IDC_TOOL_SIZE);
	for (i = 0; i < ITEM_CNT(TOOL_SIZES);i++) ComboBox_AddString(hItem, TOOL_SIZES[i].str);

	hItem = GetDlgItem(hWnd, IDC_CUT_SPEED);
	for (i = 0; i < ITEM_CNT(CUT_SPEED); i++) ComboBox_AddString(hItem, CUT_SPEED[i].str);
	
	HKEY hKey;
	if (RegOpenKey(HKEY_CURRENT_USER, L"SOFTWARE", &hKey) == ERROR_SUCCESS)
	{
		DWORD cbData = sizeof(g_Params);
		if (RegGetValue(hKey, L"WinCNC", L"BasicShape", RRF_RT_REG_BINARY, NULL, &g_Params, &cbData) == ERROR_SUCCESS)
		{
			return;
		}
	}
	ShapeInitToolInfo(&g_Params.tool);
}

void BasicShapeSave()
{
	HKEY hKey;
	if (RegCreateKey(HKEY_CURRENT_USER, L"SOFTWARE\\WinCNC", &hKey) == ERROR_SUCCESS)
	{
		RegSetValueEx(hKey, L"BasicShape", 0, REG_BINARY, (BYTE*)&g_Params, sizeof(g_Params));
	}
}

UINT BasicShapeGetSet(BOOL get, HWND hWnd )
{
	ShapeGetSetTool(hWnd, get, &g_Params.tool);

	ShapeGetSetFloat(hWnd, IDC_CIRCLE_RADIUS_X, get, &g_Params.circleRadiusX);
	ShapeGetSetFloat(hWnd, IDC_CIRCLE_RADIUS_Y, get, &g_Params.circleRadiusY);
	ShapeGetSetFloat(hWnd, IDC_CIRCLE_DEPTH, get, &g_Params.circleDepth);
	ShapeGetSetBool(hWnd, IDC_CIRCLE_OVAL, get, &g_Params.circleOval);
	ShapeGetSetBool(hWnd, IDC_CIRCLE_FILL, get, &g_Params.circleFill);
	ShapeGetSetBool(hWnd, IDC_CIRCLE_EXTERNAL_RADIUS, get, &g_Params.circleExternal);
	
	ShapeGetSetFloat(hWnd, IDC_RECT_DEPTH, get, &g_Params.rectDepth);
	ShapeGetSetFloat(hWnd, IDC_RECT_X, get, &g_Params.rectX);
	ShapeGetSetFloat(hWnd, IDC_RECT_Y, get, &g_Params.rectY);
	ShapeGetSetFloat(hWnd, IDC_RECT_RADIUS, get, &g_Params.rectRadius);
	ShapeGetSetBool(hWnd, IDC_RECT_FILL, get, &g_Params.rectFill);
	ShapeGetSetBool(hWnd, IDC_RECT_EXTERNAL, get, &g_Params.rectExternal);
	ShapeGetSetBool(hWnd, IDC_RECT_ROUNDED, get, &g_Params.rectRounded);
	
	return 0;
}

#define MAX_BUF 100000

/*
G2 X2.3125 Y2.3125 I2.3125 Z - 0.3
G2 X2.3125 Y - 2.3125 J - 2.3125 P2
G0 Z0.3
G0 X - 4.625
*/

#define NEAR_ZERO			0.00001f
#define SMALL_OVELAP		(1/64.0f)
#define SMALLEST_RADIUS		( g_Params.tool.radius + SMALL_OVELAP )
#define SAFE_TRAVEL_HEIGHT	0.25f

BOOL CarveCircle(HWND hWnd)
{
	char str[MAX_STR];
	char* cmd;

	// Can't use a tool so small
	if (g_Params.tool.radius < SMALL_OVELAP * 2)
	{
		MessageBoxA(hWnd, "Tool is too small.", "Circle", MB_ICONERROR);
		return FALSE;
	}

	// Can't carve a hole smaller than the smallest radius we can 
	// circle down (aribitrary to limit cutting down without advancing.
	if (g_Params.circleRadiusX < SMALLEST_RADIUS || ( g_Params.circleOval && g_Params.circleRadiusY < SMALLEST_RADIUS ))
	{
		MessageBoxA(hWnd, "Radius is too small.", "Circle", MB_ICONERROR);
		return FALSE;
	}

	// Can't carve deeper than the tool max depth
	if (g_Params.circleDepth > g_Params.tool.maxDepth)
	{
		MessageBoxA(hWnd, "Carving too deep.", "Circle", MB_ICONERROR);
		return FALSE;
	}

	// Can't carve 
	if (g_Params.circleDepth < NEAR_ZERO)
	{
		MessageBoxA(hWnd, "Carving is too shallow.", "Circle", MB_ICONERROR);
		return FALSE;
	}

	cmd = (char*)malloc(MAX_BUF);
	if (cmd == NULL)
	{
		MessageBoxA(hWnd, "Failled to allocate buffer.", "Circle", MB_ICONERROR);
		return FALSE;
	}
	memset(cmd, 0x00, MAX_BUF);
	
	// G91: Relative Motion
	// Fxx : Carve speed
	// M3 : Motor ON
	// G4 P1 : Wait 1sec. for tool spindle to start
	sprintf_s(str, MAX_STR, "G91 F%d %s\r\n", g_Params.tool.cutSpeed, g_Params.tool.motorControl ? "M3 G4 P1" : "");
	strcat_s(cmd, MAX_BUF, str);

	// Calculate the distance between concentric circles needed to fill
	// the circle with a small overlap between each of them.
	float R = g_Params.circleRadiusX + SMALL_OVELAP;
	int t =  1 + (int)(R / (( g_Params.tool.radius * 2 ) - SMALL_OVELAP ));
	float d = R / t;
	float rx,ry;

	bool bDone = false;
	rx = g_Params.circleRadiusX - g_Params.tool.radius;
	ry = g_Params.circleRadiusY - g_Params.tool.radius;

	do
	{
		// Move from the center to the radius of the circle (9 o'clock)
		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", SAFE_TRAVEL_HEIGHT );
		strcat_s(cmd, MAX_BUF, str);
		sprintf_s(str, MAX_STR, "G0 X%f\r\n", -rx );
		strcat_s(cmd, MAX_BUF, str);
		sprintf_s(str, MAX_STR, "G1 Z%f\r\n", -SAFE_TRAVEL_HEIGHT);
		strcat_s(cmd, MAX_BUF, str);

		// Spiral down, making as many turns as needed to cut less than 'tool.cutDepth' 
		// for each pass. When P is two G2 makes a 450 degree turn. Starts at 9, end
		// at 12 o'clock.
		int P = 2 + (int)(g_Params.circleDepth / g_Params.tool.cutDepth);
		sprintf_s(str, MAX_STR, "G2 X%f Y%f I%f Z%f P%d\r\n", rx, ry, rx, -g_Params.circleDepth, P);
		strcat_s(cmd, MAX_BUF, str);
		
		// Make another full circle to finish the bottom flat. To avoid making
		// an additional 1/2 turn using the P parameter, do it in 4 G2 commands.
		// First quarter (12 to 3)
		sprintf_s(str, MAX_STR, "G2 X%f Y%f J%f\r\n", rx, -ry, -ry);
		strcat_s(cmd, MAX_BUF, str);
		// Second quarter (3 to 6)
		sprintf_s(str, MAX_STR, "G2 X%f Y%f I%f\r\n", -rx, -ry, -rx);
		strcat_s(cmd, MAX_BUF, str);
		// Third quarter (6 to 9)
		sprintf_s(str, MAX_STR, "G2 X%f Y%f J%f\r\n", -rx, ry, ry);
		strcat_s(cmd, MAX_BUF, str);
		// Fourth quarter (9 to 12)
		sprintf_s(str, MAX_STR, "G2 X%f Y%f I%f\r\n", rx, ry, rx);
		strcat_s(cmd, MAX_BUF, str);

		// Come back to starting height
		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", g_Params.circleDepth + SAFE_TRAVEL_HEIGHT);
		strcat_s(cmd, MAX_BUF, str);
		// Move back to center of circle.
		sprintf_s(str, MAX_STR, "G0 Y%f\r\n", -ry);
		strcat_s(cmd, MAX_BUF, str);
		sprintf_s(str, MAX_STR, "G1 Z%f", -SAFE_TRAVEL_HEIGHT);
		strcat_s(cmd, MAX_BUF, str);


		if( g_Params.circleFill &&
		  ( rx > (g_Params.tool.radius - SMALL_OVELAP)) && 
		  (rx > SMALLEST_RADIUS ))
		{
			// Move to next concentric circle
			// Overlap each concentic circle to ensure smooth bottom
			//r = r - ((g_Params.tool.radius * 2) - SMALL_OVELAP);
			rx = rx - d;
			if (rx < SMALLEST_RADIUS)
			{
				rx = SMALLEST_RADIUS;
			}
		}
		else
		{
			bDone = true;
		}
		// End the line and stop the motor if needed
		strcat_s(cmd, MAX_BUF, bDone && g_Params.tool.motorControl ? " M0\r\n" : "\r\n");
	}
	while (!bDone);

	HWND hItem = GetDlgItem(hWnd, IDC_GCODE);
	SetWindowTextA(hItem, cmd);
	free(cmd);

	return TRUE;
}

void MakeRectMove(bool bXisLong, char* cmd, float L, float S, float z)
{
	char str[MAX_STR];
	float x, y;
	if (bXisLong)
	{
		x = L;
		y = S;
	}
	else
	{
		x = S;
		y = L;
	}
	strcat_s(cmd, MAX_BUF, "G1");
	if (x != 0.0f)
	{
		sprintf_s(str, MAX_STR, " X%f", x);
		strcat_s(cmd, MAX_BUF, str);
	}
	if (y != 0.0f)
	{
		sprintf_s(str, MAX_STR, " Y%f", y);
		strcat_s(cmd, MAX_BUF, str);
	}
	if (z != 0.0f)
	{
		sprintf_s(str, MAX_STR, " Z%f", z);
		strcat_s(cmd, MAX_BUF, str);
	}
	strcat_s(cmd, MAX_BUF, "\r\n");
}

BOOL CarveRect(HWND hWnd)
{
	char str[MAX_STR];
	char* cmd;

	// Can't use a tool so small
	if (g_Params.tool.radius < SMALL_OVELAP * 2) return FALSE;

	// Can't carve a rectangle so small the tool has no space to dive
	if (g_Params.rectX < SMALLEST_RADIUS * 2) return FALSE;
	if (g_Params.rectY < SMALLEST_RADIUS * 2) return FALSE;

	// Can't carve deeper than the tool max depth
	if (g_Params.rectDepth > g_Params.tool.maxDepth) return FALSE;

	// Nothing to carve if there is no depth
	if (g_Params.rectDepth < NEAR_ZERO) return FALSE;

	cmd = (char*)malloc(MAX_BUF);
	if (cmd == NULL) return FALSE;
	memset(cmd, 0x00, MAX_BUF);

	// G91: Relative Motion
	// Fxx : Carve speed
	// M3 : Motor ON
	// G4 P1 : Wait 1sec for tool spindle to start
	sprintf_s(str, MAX_STR, "G91 F%d %s\r\n", g_Params.tool.cutSpeed, g_Params.tool.motorControl ? "M3 G4 P1" : "");
	strcat_s(cmd, MAX_BUF, str);

	// True if X is longest side of the rect
	bool bXisLong = (g_Params.rectX > g_Params.rectY);

	// Current long and short side of the rect remaining to carve
	float L = bXisLong ? g_Params.rectX : g_Params.rectY;
	float S = bXisLong ? g_Params.rectY : g_Params.rectX;
	float r = g_Params.rectRadius;
	
	// If the X and Y are external dimensions, remove tool radius
	// for each corner (twice)
	if (g_Params.rectExternal)
	{
		L = L - (g_Params.tool.radius * 2);
		S = S - (g_Params.tool.radius * 2);
		r = r - g_Params.tool.radius;
	}
	else
	{
		// Dimensions are the internal ones. Then add the tool
		// radius
		L = L + (g_Params.tool.radius * 2);
		S = S + (g_Params.tool.radius * 2);
		r = r + g_Params.tool.radius;
	}

	// If the corners are rounded, remove the radius for
	// each side (twice) on each length of the sides.
	if (g_Params.rectRounded)
	{
		L = L - r * 2;
		S = S - r * 2;
		// Also check the radius is't zero either
		if (r <= NEAR_ZERO) return FALSE;
	}
	else
	{
		// Clear this to avoid using when rounded corners are
		// not enabled and filled (empty) is enabled
		r = 0.0f;
	}

	// final check that dimensions make sense (no zero or negative)
	if ((L <= NEAR_ZERO) || (S <= NEAR_ZERO)) return FALSE;

	float OffsetL = 0.0f;
	float OffsetS = 0.0f;
	bool bDone = false;
	bool bClipCornerTriangle = false;
	bool bRounded = g_Params.rectRounded;

	// Move to the start of the first corner. Do it above in case
	// the tool is alread flush
	if (bRounded)
	{
		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", g_Params.tool.radius );
		strcat_s(cmd, MAX_BUF, str);
		if (bXisLong)
		{
			sprintf_s(str, MAX_STR, "G0 X%f\r\n", r);
			strcat_s(cmd, MAX_BUF, str);
		}
		else
		{
			sprintf_s(str, MAX_STR, "G0 Y%f\r\n", r);
			strcat_s(cmd, MAX_BUF, str);
		}
		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", -g_Params.tool.radius );
		strcat_s(cmd, MAX_BUF, str);
	}

	do
	{
		// Start at zero depth.
		float Z = 0.0f;
		float w = g_Params.tool.radius / 2;
		float d = (g_Params.tool.radius * 2) - SMALL_OVELAP;

		// Dive while depth has not reached target rect depth
		while (Z < g_Params.rectDepth - NEAR_ZERO)
		{
			float D;
			// If what remains to be carve is larger than the max cut depth 
			if (g_Params.rectDepth - Z > g_Params.tool.cutDepth)
				// Dive by the max cut depth
				D = g_Params.tool.cutDepth;
			else
				// Dive by what's left
				D = g_Params.rectDepth - Z;

			// update the current depth
			Z += D;

			// Dive by 'D' on the long side of the rect
			MakeRectMove(bXisLong, cmd, L, 0.0f, -D);
			if (bClipCornerTriangle)
			{
				MakeRectMove(bXisLong, cmd, w, -w, 0.0f);
				MakeRectMove(bXisLong, cmd, -w, w, 0.0f);
			}

			// First corner
			if (bRounded && r > NEAR_ZERO)
			{
				if (bXisLong)
				{
					sprintf_s(str, MAX_STR, "G3 X%f Y%f J%f\r\n", r, r, r);
					strcat_s(cmd, MAX_BUF, str);
				}
				else
				{
					sprintf_s(str, MAX_STR, "G2 X%f Y%f I%f\r\n", r, r, r);
					strcat_s(cmd, MAX_BUF, str);
				}
			}

			// Go along the short side
			MakeRectMove(bXisLong, cmd, 0.0f, S, 0.0f);
			if (bClipCornerTriangle)
			{
				MakeRectMove(bXisLong, cmd, w, w, 0.0f );
				MakeRectMove(bXisLong, cmd, -w, -w, 0.0f );
			}

			// Second corner
			if (bRounded && r > NEAR_ZERO)
			{
				if (bXisLong)
				{
					sprintf_s(str, MAX_STR, "G3 X%f Y%f I%f\r\n", -r, r, -r);
					strcat_s(cmd, MAX_BUF, str);
				}
				else
				{
					sprintf_s(str, MAX_STR, "G2 X%f Y%f J%f\r\n", r, -r, -r);
					strcat_s(cmd, MAX_BUF, str);
				}
			}

			// Come back the long side
			MakeRectMove(bXisLong, cmd, -L, 0.0f, 0.0f);
			if (bClipCornerTriangle)
			{
				MakeRectMove(bXisLong, cmd, -w, w, 0.0f);
				MakeRectMove(bXisLong, cmd, w, -w, 0.0f);
			}

			// Third corner
			if (bRounded && r > NEAR_ZERO)
			{
				if (bXisLong)
				{
					sprintf_s(str, MAX_STR, "G3 X%f Y%f J%f\r\n", -r, -r, -r);
					strcat_s(cmd, MAX_BUF, str);
				}
				else
				{
					sprintf_s(str, MAX_STR, "G2 X%f Y%f I%f\r\n", -r, -r, -r);
					strcat_s(cmd, MAX_BUF, str);
				}
			}

			// Come back the short side
			MakeRectMove(bXisLong, cmd, 0.0f, -S, 0.0f);
			if (bClipCornerTriangle)
			{
				MakeRectMove(bXisLong, cmd, -w, -w, 0.0f);
				MakeRectMove(bXisLong, cmd, w, w, 0.0f);
			}

			// Fourth corner
			if (bRounded && r > NEAR_ZERO )
			{
				if (bXisLong)
				{
					sprintf_s(str, MAX_STR, "G3 X%f Y%f I%f\r\n", r, -r, r);
					strcat_s(cmd, MAX_BUF, str);
				}
				else
				{
					sprintf_s(str, MAX_STR, "G2 X%f Y%f J%f\r\n", -r, r, r);
					strcat_s(cmd, MAX_BUF, str);
				}
			}
		}

		// Flatten the bottom along the long side
		MakeRectMove(bXisLong, cmd, L, 0.0f, 0.0f);

		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", Z);
		strcat_s(cmd, MAX_BUF, str);

		// Come back to orgin on long axis and altitude
		if (bXisLong)
			sprintf_s(str, MAX_STR, "G0 X%f\r\n", -L );
		else
			sprintf_s(str, MAX_STR, "G0 Y%f\r\n", -L );
		strcat_s(cmd, MAX_BUF, str);

		// If we need to carve the inside and if the current rectangle still
		// has material in its center.
		if (g_Params.rectFill && S > NEAR_ZERO )
		{
			if (r > d)
			{
				r = r - d;
				MakeRectMove(bXisLong, cmd, 0.0f, d, 0.0f);
				OffsetS += d;
			}
			else
			{
				if (r > NEAR_ZERO)
				{
					if (( S + r ) > d)
					{
						MakeRectMove(bXisLong, cmd, (d - r) / 2, d, 0.0f);
						OffsetL += ((d - r) / 2);
						OffsetS += d;
						L = L - d + r;
						S = S - d + r;
					}
					else
					{
						MakeRectMove(bXisLong, cmd, (d - r) / 2, S / 2, 0.0f);
						OffsetL += ((d - r) / 2);
						OffsetS += (S / 2);
						L = L - d + r;
						S = 0.0f;
					}
					r = 0.0;
					bClipCornerTriangle = true;
				}
				else
				{
					if (S > d * 2)
					{
						MakeRectMove(bXisLong, cmd, d, d, 0.0f);
						OffsetL += d;
						OffsetS += d;
						L = L - d * 2;
						S = S - d * 2;
					}
					else
					{
						MakeRectMove(bXisLong, cmd, d, S / 2, 0.0f);
						OffsetL += d;
						OffsetS += (S / 2);
						L = L - d * 2;
						S = 0.0f;
					}

					bClipCornerTriangle = true;
				}				
			}			
		}
		else
		{
			if (OffsetL != 0.0f || OffsetS != 0.0f)
			{
				// Come back up to origin
				MakeRectMove(bXisLong, cmd, -OffsetL, -OffsetS, 0.0f);
			}
			bDone = true;
		}
	} while (!bDone);


	if (bRounded)
	{
		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", g_Params.tool.radius);
		strcat_s(cmd, MAX_BUF, str);
		if (bXisLong)
		{
			sprintf_s(str, MAX_STR, "G0 X%f\r\n", -r);
			strcat_s(cmd, MAX_BUF, str);
		}
		else
		{
			sprintf_s(str, MAX_STR, "G0 Y%f\r\n", -r);
			strcat_s(cmd, MAX_BUF, str);
		}
		sprintf_s(str, MAX_STR, "G0 Z%f\r\n", -g_Params.tool.radius);
		strcat_s(cmd, MAX_BUF, str);
	}

	// End the line and stop the motor if needed
	if (g_Params.tool.motorControl)
	{
		strcat_s(cmd, MAX_BUF, "M0\r\n");
	}

	HWND hItem = GetDlgItem(hWnd, IDC_GCODE);
	SetWindowTextA(hItem, cmd);
	free(cmd);

	return TRUE;
}

void BasicShapeOneCommand(HWND hWnd)
{
	char* cmd;
	HWND hItem;
	hItem = GetDlgItem(hWnd, IDC_GCODE );

	int l = GetWindowTextLength(hItem) + 1;
	cmd = (char*)malloc(l);
	if (cmd)
	{
		GetWindowTextA(hItem, cmd, l);
		ParseBuffer(hWnd, cmd, l, doGcode);
		free(cmd);
	}
}

WNDPROC g_oldBasicDlgdProc = NULL;
BOOL CALLBACK BasicInterceptWndProc(HWND hWnd,
	UINT message,
	WPARAM wParam,
	LPARAM lParam)
{
	if (message == WM_KEYDOWN )
	{
		if (wParam == 'a' || wParam == 'A' && GetKeyState(VK_CONTROL))
		{
			PostMessage(hWnd, EM_SETSEL, 0, -1);
			return MNC_CLOSE << 16;
		}	
	}
	return g_oldBasicDlgdProc(hWnd, message, wParam, lParam);
}

BOOL CALLBACK BasicShapesProc(HWND hWnd,
	UINT message,
	WPARAM wParam,
	LPARAM lParam)
{
	HWND hDlg;

	switch (message)
	{
	case WM_INITDIALOG:
		BasicShapeInit(hWnd);
		BasicShapeGetSet(FALSE, hWnd);

		// This is to capture the CTRL+A on the GCode edit box
		hDlg = GetDlgItem(hWnd, IDC_GCODE);
		g_oldBasicDlgdProc = (WNDPROC)GetWindowLong(hDlg, GWL_WNDPROC);
		SetWindowLong(hDlg, GWL_WNDPROC, (LONG)BasicInterceptWndProc);
		return TRUE;
		break;

	case WM_CLOSE:
		BasicShapeGetSet(TRUE, hWnd);
		BasicShapeSave();
		break;

	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_EXECUTE :
			BasicShapeOneCommand(hWnd);
			break;
		case IDC_RESET_SIM :
			resetBlockSurface();
			break;
		case IDC_CARVE_CIRCLE:
			BasicShapeGetSet(TRUE , hWnd );
			CarveCircle(hWnd);
			break;
		case IDC_CARVE_RECT:
			BasicShapeGetSet(TRUE, hWnd);
			CarveRect(hWnd);
			break;
		case IDOK:
			/*
			if (!GetDlgItemText(hwndDlg, ID_ITEMNAME, szItemName, 80))
			*szItemName = 0;

			// Fall through.
			*/
		case IDCANCEL:
			EndDialog(hWnd, wParam);
			return TRUE;
		}
	}
	return FALSE;
}

void BasicShapes(HWND hWnd)
{
	DialogBox(NULL,
		MAKEINTRESOURCE(IDD_BASIC_SHAPES),
		hWnd,
		(DLGPROC)BasicShapesProc);
}