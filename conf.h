#pragma once

#include "defines.h"
#include <windows.h>
#include <aclapi.h>

#define default_interval 500

//Storing and reading settings in registry
class Configuration
{
public:
	DWORD KeyFrameInterval;
	char email[256];
	char regcode[256];
	BOOL ForceInterval;
	DWORD loss; //in bits
	BOOL ForceLoss;

	Configuration() : KeyFrameInterval(default_interval), hkSub(0), hkSoft(0), 
		ForceInterval(TRUE), loss(0), ForceLoss(TRUE)
	{
		memset(email, 0, sizeof(email));
		memset(regcode, 0, sizeof(regcode));
	};

	void GetCurConfig();
	void SetCurConfig();

protected:
	HKEY hkSub, hkSoft;

	BOOL OpenKeyRead(HKEY branch);
	BOOL OpenKeyWrite(HKEY branch);
	void CloseKey();
	void ShowError(long lRes, const char *capt, const char *text);
	BOOL WriteTo(HKEY branch);
	void WriteValues();
};