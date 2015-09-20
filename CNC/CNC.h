#pragma once

#include "targetver.h"

#define WIN32_LEAN_AND_MEAN             // Exclude rarely-used stuff from Windows headers
// Windows Header Files:
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <tchar.h>
#include <stdio.h>	// for printf

#include "resource.h"
#include "status.h"


typedef struct
{
	double blockX;       // Dimension of the block being machined
	double blockY;
	double blockZ;       // Z position has no offset 
	double offsetX;      // Position block relative to initial tool position
	double offsetY;
	double offsetZ;
	double toolRadius;
	double toolHeight;   // Max cutting height of the tool
} tMetaData;
