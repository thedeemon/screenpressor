#include "conf.h"
#include <stdio.h>

void Configuration::ShowError(long lRes, const char *capt, const char *text)
{
	char buf[1024];
	if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM, NULL, lRes, 0, buf, 1000, 0))
		MessageBox(NULL, buf, capt, MB_ICONWARNING);
	else
		MessageBox(NULL, text, capt, MB_ICONWARNING);
}

void Configuration::SetCurConfig()
{
	BOOL cu = WriteTo(HKEY_CURRENT_USER);
	BOOL lm = WriteTo(HKEY_LOCAL_MACHINE);
	if (!cu && !lm) {
		MessageBox(NULL, "Can't save settings to registry.", "ScreenPressor", MB_ICONWARNING);
	}
}

BOOL Configuration::WriteTo(HKEY branch)
{
	if (OpenKeyWrite(branch)) {
		WriteValues();
		CloseKey();
		return TRUE;
	} else 		
		return FALSE;	
}

void Configuration::WriteValues()
{
	RegSetValueEx(hkSub, "KeyFrameInterval", 0, REG_DWORD, (BYTE*)&KeyFrameInterval, 4);
	RegSetValueEx(hkSub, "email", 0, REG_SZ, (BYTE*)email, strlen(email)+1);
	RegSetValueEx(hkSub, "regcode", 0, REG_SZ, (BYTE*)regcode, strlen(regcode)+1);
	RegSetValueEx(hkSub, "ForceInterval", 0, REG_DWORD, (BYTE*)&ForceInterval, 4);
	RegSetValueEx(hkSub, "ForceLoss", 0, REG_DWORD, (BYTE*)&ForceLoss, 4);
	RegSetValueEx(hkSub, "Loss", 0, REG_DWORD, (BYTE*)&loss, 4);
}

void Configuration::GetCurConfig()
{
	DWORD lRes, BufLen=sizeof(DWORD);

	if (!OpenKeyRead(HKEY_CURRENT_USER)) {
		if (!OpenKeyRead(HKEY_LOCAL_MACHINE)) {
			//SetCurConfig();
			return;
		}
	}

	BufLen=sizeof(DWORD);
	lRes = RegQueryValueEx(hkSub, "KeyFrameInterval", 0, 0, (BYTE*)&KeyFrameInterval, &BufLen);
	if (lRes != ERROR_SUCCESS)	{
		KeyFrameInterval = default_interval;
	}	

	BufLen=sizeof(DWORD);
	lRes = RegQueryValueEx(hkSub, "ForceInterval", 0, 0, (BYTE*)&ForceInterval, &BufLen);
	if (lRes != ERROR_SUCCESS)	{
		ForceInterval = TRUE;
	}

	BufLen=sizeof(DWORD);
	lRes = RegQueryValueEx(hkSub, "ForceLoss", 0, 0, (BYTE*)&ForceLoss, &BufLen);
	if (lRes != ERROR_SUCCESS)	{
		ForceLoss = TRUE;
	}


	BufLen=sizeof(DWORD);
	lRes = RegQueryValueEx(hkSub, "Loss", 0, 0, (BYTE*)&loss, &BufLen);
	if (lRes != ERROR_SUCCESS)	{
		loss = 0;	
	}

	BufLen = sizeof(email);
	lRes = RegQueryValueEx(hkSub, "email", 0, 0, (BYTE*)email, &BufLen);
	BufLen = sizeof(regcode);
	lRes = RegQueryValueEx(hkSub, "regcode", 0, 0, (BYTE*)regcode, &BufLen);	

	CloseKey();
}

BOOL Configuration::OpenKeyWrite(HKEY branch)
{
	long lOpenRes = RegCreateKeyEx(branch, "Software", 0, "", 0, KEY_WRITE, 0, &hkSoft, NULL);
	if (lOpenRes != ERROR_SUCCESS)	{
		//ShowError(lOpenRes, "ScreenPressor trying to open /Software", "Can't open registry branch /Software");
		return FALSE;
	}

	lOpenRes = RegCreateKeyEx(hkSoft, "ScreenPressor", 0, "", 0, KEY_WRITE, 0, &hkSub, NULL);
	if (lOpenRes != ERROR_SUCCESS)	{
		RegCloseKey(hkSoft);
		return FALSE;
	}
	return TRUE;
}

BOOL Configuration::OpenKeyRead(HKEY branch)
{
	long lOpenRes = RegOpenKeyEx(branch, "Software", 0, KEY_READ,  &hkSoft);
	if (lOpenRes != ERROR_SUCCESS)	{
		//ShowError(lOpenRes, "ScreenPressor trying to open /Software", "Can't open registry branch /Software.");
		return FALSE;
	}

	lOpenRes = RegOpenKeyEx(hkSoft, "ScreenPressor", 0,  KEY_READ,  &hkSub);
	if (lOpenRes != ERROR_SUCCESS)	{
		RegCloseKey(hkSoft);
		return FALSE;
	}
	return TRUE;
}


void Configuration::CloseKey()
{
	RegCloseKey(hkSub);
	RegCloseKey(hkSoft);
}