#include "screenpressor.h"
#include "resource.h"

#include <crtdbg.h>
#include <stdio.h>
#include "conf.h"
#include <commctrl.h>

TCHAR szDescription[] = TEXT("Infognition ScreenPressor");
TCHAR szName[]        = TEXT("SCPR");

#define VERSION         0x00000004      

//#define LOGGIN

#ifdef LOGGIN

void flog(const char *s)
{
	FILE *f=fopen("c:\\temp\\scrprs.txt","at");
	if (f) {
		fprintf(f,"%s\n",s);
		fclose(f);
	}
}

void flogn(const char *s, int n)
{
	FILE *f=fopen("c:\\temp\\scrprs.txt","at");
	if (f) {
		fprintf(f,"%s %d\n",s,n);
		fclose(f);
	}
}

#define LOG flog
#define LOGN flogn

#else

#define LOGN
#define LOG
#define flog(s)
#define flogn(s,n)

#endif 

extern HMODULE hmoduleSCPR;

std::vector<CodecInst*> CodecInst::instances;

LRESULT CodecInst::Open(ICOPEN* icinfo) //returns inst_id - index in instances
{
	if (icinfo && icinfo->fccType != ICTYPE_VIDEO) return 0;

	//zero index is not a valid one
	if (instances.empty())
		instances.push_back(NULL);

	CodecInst* pinst = new CodecInst();
	instances.push_back(pinst);

	if (icinfo) icinfo->dwError = pinst ? ICERR_OK : ICERR_MEMORY;

	return instances.size()-1;
}

void CodecInst::Close(DWORD inst_id)
{
	if (inst_id > 0 && inst_id < instances.size()) {
		if (instances[inst_id] != NULL) {
			delete instances[inst_id];
			instances[inst_id] = NULL;
		}
	}
}

CodecInst* CodecInst::GetInstance(DWORD inst_id)
{
	if (inst_id > 0 && inst_id < instances.size())
		return instances[inst_id];
	return NULL;
}

BOOL CodecInst::QueryAbout() { return TRUE; }

static INT_PTR CALLBACK AboutDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) {
  if (uMsg == WM_COMMAND) {
    switch (LOWORD(wParam)) {
    case IDOK:
      EndDialog(hwndDlg, 0);
      break;
    case IDC_HOMEPAGE:
      ShellExecute(NULL, NULL, "http://www.infognition.com/", NULL, NULL, SW_SHOW);
      break;
    }
  }
  return FALSE;
}

DWORD CodecInst::About(HWND hwnd) {
  DialogBox(hmoduleSCPR, MAKEINTRESOURCE(IDD_ABOUT), hwnd, AboutDialogProc);
  return ICERR_OK;
}

extern BOOL CheckCode(const char *email, const char *code);

#ifndef NOPROTECT

static INT_PTR CALLBACK RegisterDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
	Configuration conf;
	if (uMsg == WM_INITDIALOG) {		
		conf.GetCurConfig();
		SetDlgItemText(hwndDlg, IDC_EMAIL, conf.email);
		SetDlgItemText(hwndDlg, IDC_REGCODE, conf.regcode);

	} else 
	if (uMsg == WM_COMMAND) {
		char email[256] = {0}, code[256] = {0}, url[2048] = {0};
		switch (LOWORD(wParam)) {
		case IDOK:
			GetDlgItemText(hwndDlg, IDC_EMAIL, email, 250);
			GetDlgItemText(hwndDlg, IDC_REGCODE, code, 250);
			if (CheckCode(email, code)) {
				MessageBox(hwndDlg, "Thanks!", "ScreenPressor", MB_OK);
				conf.GetCurConfig();
				strcpy(conf.email, email);
				strcpy(conf.regcode, code);
				conf.SetCurConfig();
				EndDialog(hwndDlg, 1);
			} else
				MessageBox(hwndDlg, "The code is invalid.", "ScreenPressor", MB_ICONWARNING);		
			break;
		case IDCANCEL:
			EndDialog(hwndDlg, 0);
			break;
		case IDC_GETCODE:
			//if (!GetBuyURL(url, 2048, "Dee Mon", "ScreenPressor", "0") &&
			//	!GetBuyURL(url, 2048, "Infognition Co. Ltd.", "ScreenPressor", "0")) 				
					strcpy(url, "http://www.infognition.com/ScreenPressor/register.html");			
			ShellExecute(NULL, NULL, url, NULL, NULL, SW_SHOW);
			break;
		}
	}
	return FALSE;
}
#endif

static void ShowRegisteredStatus(HWND hwndDlg, Configuration &conf)
{
#ifndef NOPROTECT
	SetDlgItemText(hwndDlg, IDC_REGISTER, "Registered");
	char str[1024] = {0};
	strcpy(str, "Licensed to: ");
	strcat(str, conf.email);
	SetDlgItemText(hwndDlg, IDC_REGDESCR, str);
#else
	SetDlgItemText(hwndDlg, IDC_REGDESCR, "Freeware version.");
#endif
}

void ShowLossString(HWND hwndDlg)
{
	int qual = SendMessage(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), TBM_GETPOS, 0, 0); 
	switch(qual) {
	case 0: SetDlgItemText(hwndDlg, IDC_LOSS_TEXT, "50% - low quality (4 bits)"); break;
	case 1: SetDlgItemText(hwndDlg, IDC_LOSS_TEXT, "62% - some loss (3 bits)"); break;
	case 2: SetDlgItemText(hwndDlg, IDC_LOSS_TEXT, "75% - some loss (2 bits)"); break;
	case 3: SetDlgItemText(hwndDlg, IDC_LOSS_TEXT, "87% - minor loss (1 bit)"); break;
	case 4: SetDlgItemText(hwndDlg, IDC_LOSS_TEXT, "100% - absolutely lossless"); break;
	}
}

static INT_PTR CALLBACK ConfigureDialogProc(HWND hwndDlg, UINT uMsg, WPARAM wParam, LPARAM lParam) 
{
	Configuration conf;

	if (uMsg == WM_INITDIALOG) {		
		conf.GetCurConfig();
		SetDlgItemInt(hwndDlg, IDC_INTERVAL, conf.KeyFrameInterval, FALSE);
		EnableWindow(GetDlgItem(hwndDlg, IDC_INTERVAL), conf.ForceInterval);
		CheckRadioButton(hwndDlg, IDC_KFBYHOST, IDC_KFHERE, conf.ForceInterval ? IDC_KFHERE : IDC_KFBYHOST);
		SendMessage(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), TBM_SETRANGE, 0, MAKELONG(0, 4)); 
		SendMessage(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), TBM_SETPOS, 1, 4 - conf.loss); 
		ShowLossString(hwndDlg);

		CheckDlgButton(hwndDlg, IDC_LOSSBYHOST, conf.ForceLoss ? BST_UNCHECKED : BST_CHECKED);		
		EnableWindow(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), conf.ForceLoss); 

		if (CheckCode(conf.email, conf.regcode))
			ShowRegisteredStatus(hwndDlg, conf);
#ifdef NOPROTECT
		ShowWindow(GetDlgItem(hwndDlg, IDC_REGISTER), SW_HIDE);
		ShowWindow(GetDlgItem(hwndDlg, IDC_REGHINT), SW_HIDE);
#endif
	} else 
	if (uMsg == WM_COMMAND) {
		BOOL parsed = FALSE;
		switch (LOWORD(wParam)) {
			case IDOK:
				conf.GetCurConfig();
				conf.ForceInterval = IsDlgButtonChecked(hwndDlg, IDC_KFHERE);
				conf.KeyFrameInterval = GetDlgItemInt(hwndDlg, IDC_INTERVAL, &parsed, FALSE);
				conf.loss = 4 - SendMessage(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), TBM_GETPOS, 0, 0);
				conf.ForceLoss = IsDlgButtonChecked(hwndDlg, IDC_LOSSBYHOST)==BST_UNCHECKED;
				if (parsed)
					conf.SetCurConfig();
			case IDCANCEL:
				EndDialog(hwndDlg, 0);
				break;
			case IDC_REGISTER:
#ifndef NOPROTECT
				if (1==DialogBox(hmoduleSCPR, MAKEINTRESOURCE(IDD_REGISTRATION), hwndDlg, RegisterDialogProc)) {
					conf.GetCurConfig();
					ShowRegisteredStatus(hwndDlg, conf);
				}
#endif
				break;
			case IDC_KFBYHOST:
				EnableWindow(GetDlgItem(hwndDlg, IDC_INTERVAL), FALSE);
				break;
			case IDC_KFHERE:
				EnableWindow(GetDlgItem(hwndDlg, IDC_INTERVAL), TRUE);
				break;
			case IDC_LOSSBYHOST:
				if (IsDlgButtonChecked(hwndDlg, IDC_LOSSBYHOST)==BST_CHECKED)
					EnableWindow(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), FALSE);
				else
					EnableWindow(GetDlgItem(hwndDlg, IDC_LOSS_SLIDER), TRUE);
				break;
			default:
				return AboutDialogProc(hwndDlg, uMsg, wParam, lParam);    // handle email and home-page buttons
		}
	}
	if (uMsg == WM_HSCROLL) {
		ShowLossString(hwndDlg);
	}
	return FALSE;
}

BOOL CodecInst::QueryConfigure() { return TRUE; }

DWORD CodecInst::Configure(HWND hwnd) {
  DialogBox(hmoduleSCPR, MAKEINTRESOURCE(IDD_CONFIGURE), hwnd, ConfigureDialogProc);
  return ICERR_OK;
}


DWORD CodecInst::GetState(LPVOID pv, DWORD dwSize) { return 0; }

DWORD CodecInst::SetState(LPVOID pv, DWORD dwSize) { return 0; }


DWORD CodecInst::GetInfo(ICINFO* icinfo, DWORD dwSize) {
  if (icinfo == NULL)
    return sizeof(ICINFO);

  if (dwSize < sizeof(ICINFO))
    return 0;

  icinfo->dwSize            = sizeof(ICINFO);
  icinfo->fccType           = ICTYPE_VIDEO;
  icinfo->fccHandler        = FOURCC_SCPR;
  icinfo->dwFlags           = VIDCF_TEMPORAL | VIDCF_FASTTEMPORALC | VIDCF_FASTTEMPORALD | VIDCF_QUALITY;

  icinfo->dwVersion         = VERSION;
  icinfo->dwVersionICM      = ICVERSION;
  MultiByteToWideChar(CP_ACP, 0, szDescription, -1, icinfo->szDescription, sizeof(icinfo->szDescription)/sizeof(WCHAR));
  MultiByteToWideChar(CP_ACP, 0, szName, -1, icinfo->szName, sizeof(icinfo->szName)/sizeof(WCHAR));

  return sizeof(ICINFO);
}


bool CodecInst::CanCompress(LPBITMAPINFOHEADER lpbiIn) {
	const DWORD fourcc = lpbiIn->biCompression;
	const int bitcount = lpbiIn->biBitCount;
	LOGN("CanCompress: in.BitCount=", bitcount);
	LOGN("fourcc =", fourcc);

	if (fourcc == 0 || fourcc == ' BID' || fourcc == BI_BITFIELDS) { //uncompressed
		if ((bitcount == 24) || (bitcount==32))
			return true;
		if (bitcount == 16) {
			if (fourcc == BI_RGB || fourcc == ' BID') {
				rmask = 0x7C00;
				gmask = 0x3E0;
				bmask = 0x1F;
			} else
			if (fourcc == BI_BITFIELDS) {
				BITMAPINFO *pBI = (BITMAPINFO *)lpbiIn;
				rmask = *((DWORD*)&pBI->bmiColors[0]);
				gmask = *((DWORD*)&pBI->bmiColors[1]);
				bmask = *((DWORD*)&pBI->bmiColors[2]);
			} else {
				LOG("compr not BI_RGB or BI_BITFIELDS");
				return false;
			}
			return true;
		} else
			LOG("bitcount not 16,24 or 32, false");
	}
	LOG("fourcc not 0 or ' BID', false");
	return false;
}


DWORD CodecInst::CompressQuery(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut)
{
	if (CanCompress(lpbiIn))
		return ICERR_OK;
  return ICERR_BADFORMAT;
}


DWORD CodecInst::CompressGetFormat(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) {
  if (!CanCompress(lpbiIn))
    return ICERR_BADFORMAT;

  int nextra = 0;
  if (lpbiIn->biBitCount==16)
	  nextra = 12;//3 DWORDs for masks

  if (!lpbiOut)
    return sizeof(BITMAPINFOHEADER) + nextra;

  *lpbiOut = *lpbiIn;
  lpbiOut->biSize = sizeof(BITMAPINFOHEADER) + nextra;;
  lpbiOut->biCompression = FOURCC_SCPR;
  if (nextra) {
	BYTE *extra = (BYTE*)lpbiOut + sizeof(BITMAPINFOHEADER);
	DWORD *pdw = (DWORD*)extra;
	*pdw++ = rmask;
	*pdw++ = gmask;
	*pdw++ = bmask;
  }
  return ICERR_OK;
}



DWORD CodecInst::CompressBegin(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) 
{
	Configuration conf;
	LOG("CompressBegin, calling CanCompress");
  
	if (!CanCompress(lpbiIn))
	    return ICERR_BADFORMAT;

	LOG("CompressBegin calling CompressEnd");
	CompressEnd();  // free resources if necessary

	LOG("CopressBegin: sc.Init:");
	LOGN("X =",lpbiIn->biWidth);
	LOGN("Y =",lpbiIn->biHeight);

	DWORD compr = lpbiIn->biCompression;
	LOGN("in.compr =", compr);
	LOGN("in.bitcount =", lpbiIn->biBitCount);
	LOGN("rmask =", rmask);
	LOGN("gmask =", gmask);
	LOGN("bmask =", bmask);

	conf.GetCurConfig();
	kf_interval = conf.KeyFrameInterval;
	force_interval = conf.ForceInterval;
	force_loss = conf.ForceLoss;
	conf_loss = conf.loss;
	npframes = 0;

	CheckCode(conf.email, conf.regcode);

	CodecParameters params;
	params.width = lpbiIn->biWidth; params.height = lpbiIn->biHeight; params.bits_per_pixel = lpbiIn->biBitCount;
	params.redmask = rmask; params.greenmask = gmask; params.bluemask = bmask;
	params.high_range_x = 256; params.high_range_y = 256;
	params.low_range_x = 8; params.low_range_y = 8;
	params.loss = conf.loss;
	
	sc.Init(&params);

	return ICERR_OK;
}

DWORD CodecInst::CompressGetSize(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) {
	return lpbiIn->biWidth * lpbiIn->biHeight * 6;
}



DWORD CodecInst::Compress(ICCOMPRESS* icinfo, DWORD dwSize) {

	if (icinfo->lpckid)
	    *icinfo->lpckid = FOURCC_SCPR;

	LOG("Compress");
	LOGN("quality =", icinfo->dwQuality);
    BYTE* const in = (BYTE*)icinfo->lpInput;
    BYTE* const out = (BYTE*)icinfo->lpOutput;

	int ftype = 1;
	bool forced_kf = force_interval && (npframes + 1 >= kf_interval);
	bool host_kf = !force_interval && (icinfo->dwFlags & ICCOMPRESS_KEYFRAME);
	if (host_kf || forced_kf)
		ftype = 0;
	LOGN("lpbiOutput->biSizeImage=", icinfo->lpbiOutput->biSizeImage);
	auto outBufSz = max(icinfo->lpbiOutput->biSizeImage, icinfo->lpbiInput->biSizeImage);


	/* quality - loss
	0-2000 - 4 
	2001-4000 - 3 
	4001-6000 - 2 
	6001-8000 - 1 
	8001-10000 - 0
	*/
	int loss = conf_loss;
	if (!force_loss) {
		DWORD quality = min(icinfo->dwQuality, 10000);
		loss = min( (10000 - quality)/2000, 4);
	}
	//int ScreenCodec::CompressFrame(BYTE *pSrc, BYTE *pDst, int dstLength, int &ftype) //frame type 0-I, 1-P
	//int sz = sc.CompressFrame(in, out, ftype, loss);
	int sz = sc.CompressFrame(in, out, outBufSz, ftype, loss);
	if (!ftype) {
		*icinfo->lpdwFlags = AVIIF_KEYFRAME; 
		npframes = 0;
	} else {
		*icinfo->lpdwFlags = 0; 
		npframes++;
	}
	LOGN("ftype =",ftype);
	LOGN("sz =",sz);
	LOGN("npframes =", npframes);

    icinfo->lpbiOutput->biSizeImage = sz;
    return ICERR_OK;
}


DWORD CodecInst::CompressEnd() {
	LOG("CompressEnd");
	sc.Deinit();
	return ICERR_OK;
}

bool CodecInst::CanDecompress(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) {
	LOG("CanDecompress");
	if (!lpbiOut) {
		LOG("lpbiOut is NULL");
	    return (lpbiIn->biCompression == FOURCC_SCPR);
	}

	// must be 1:1 (no stretching)
	if (lpbiOut && (lpbiOut->biWidth != lpbiIn->biWidth || lpbiOut->biHeight != lpbiIn->biHeight)) {
		LOG("different width or height. false");
	    return false;
	}

	if ((lpbiIn->biBitCount>16) &&  (lpbiOut->biBitCount != 24) && (lpbiOut->biBitCount != 32)) {
		LOG("in.bitcount > 16 and out.bitcount not 24 nor 32. false");
		return false;
	}

	if (lpbiIn->biBitCount != lpbiOut->biBitCount) {
		LOGN("false: different bitcounts: in.bc=", lpbiIn->biBitCount);
		LOGN("out.bc=", lpbiOut->biBitCount);
		return false;
	}
	if (lpbiIn->biBitCount==16) { 
		LOG("in.bitcount=16..");

		if (lpbiOut->biCompression != BI_BITFIELDS) {
			LOG("out.compr not BITFIELDS..");
			const BITMAPINFO *pBI = (BITMAPINFO *)lpbiIn;
			const DWORD redmask = *((DWORD*)&pBI->bmiColors[0]);
			const DWORD greenmask = *((DWORD*)&pBI->bmiColors[1]);
			const DWORD bluemask = *((DWORD*)&pBI->bmiColors[2]);
			if (redmask==0x7C00 && greenmask==0x3E0 && bluemask==0x1F) {
				LOG("decompressing 555, ok");
				return true;
			}
			LOG("we're not 555, BITFIELDS required. false");
			return false;
		}
		if (lpbiOut->biSize==sizeof(BITMAPINFOHEADER)) {
			LOG("out.biSize too small. false");
			return false;
		}
	}

	LOG("ok, calling CanCompress..");
	return CanCompress(lpbiOut);
}


DWORD CodecInst::DecompressQuery(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) 
{
	return CanDecompress(lpbiIn, lpbiOut) ? ICERR_OK : ICERR_BADFORMAT;
}


DWORD CodecInst::DecompressGetFormat(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) 
{
	LOG("DecompressGetFormat");
	LOG("calling CanDecompress");
	if (!CanDecompress(lpbiIn, NULL))
	    return ICERR_BADFORMAT;
  // if lpbiOut == NULL, then return the size required to hold an output format struct
	int nextra = 0;
	if (lpbiIn->biBitCount==16)
		nextra = 12;

	if (lpbiOut == NULL) {
		LOG("lpbiOut is null");
		return sizeof(BITMAPINFOHEADER) + nextra;
	}

	memcpy(lpbiOut, lpbiIn, sizeof(BITMAPINFOHEADER) + nextra); //masks copied also
  
	lpbiOut->biSize = sizeof(BITMAPINFOHEADER) + nextra;
	lpbiOut->biPlanes = 1;

	//if (lpbiOut->biBitCount > 16)
	//  lpbiOut->biBitCount = 24;   // RGB
	lpbiOut->biCompression = nextra ? BI_BITFIELDS : 0;
	int bpp = lpbiOut->biBitCount/8;
	int stride = (lpbiIn->biWidth * bpp + 3) & (~3);
	size_image = lpbiOut->biSizeImage = stride * lpbiIn->biHeight;
	LOGN("DecompressGetFormat ok, out.biSize=",lpbiOut->biSize);
	return ICERR_OK;
}

DWORD CodecInst::DecompressBegin(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) 
{
	LOG("DecompressBegin");
	DecompressEnd();  // free resources if necessary

	if (!CanDecompress(lpbiIn, lpbiOut))
	    return ICERR_BADFORMAT;

	decompressing = true;
	LOG("sc.Init:");

	LOGN("X =",lpbiIn->biWidth);
	LOGN("Y =",lpbiIn->biHeight);

	if (lpbiOut->biBitCount == 16) {
		BITMAPINFO *pBI = (BITMAPINFO *)lpbiIn;
		rmask = *((DWORD*)&pBI->bmiColors[0]);
		gmask = *((DWORD*)&pBI->bmiColors[1]);
		bmask = *((DWORD*)&pBI->bmiColors[2]);
	}
	LOGN("in.bitcount =", lpbiIn->biBitCount);
	LOGN("out.bitcount =", lpbiOut->biBitCount);
	LOGN("out.biSize =", lpbiOut->biSize );

	LOGN("rmask =", rmask);
	LOGN("gmask =", gmask);
	LOGN("bmask =", bmask);

	int bits = lpbiIn->biBitCount;

	int stride = (lpbiIn->biWidth * bits / 8 + 3) & (~3);
	size_image = stride * lpbiIn->biHeight;

	CodecParameters params;
	params.width = lpbiIn->biWidth; params.height = lpbiIn->biHeight; params.bits_per_pixel = bits;
	params.redmask = rmask; params.greenmask = gmask; params.bluemask = bmask;
	params.high_range_x = 256; params.high_range_y = 256;
	params.low_range_x = 8; params.low_range_y = 8;
	params.loss = 0;
	
	sc.Init(&params);
	return ICERR_OK;
}

int CodecInst::InferFrameType(BYTE first_byte, DWORD data_size) //0=I, 1=P, -1=error
{
	switch(first_byte) {
		case 0: return 1;
		case 1: return data_size <= 4 ? 0 : 1; 
		case 0x02:
		case 0x11:
		case 0x12: return 0;
	}
	return -1;
}

DWORD CodecInst::Decompress(ICDECOMPRESS* icinfo, DWORD dwSize) {
	LOG("Decompress");
	try {
		if (!decompressing) {
		    DWORD retval = DecompressBegin(icinfo->lpbiInput, icinfo->lpbiOutput);
			if (retval != ICERR_OK)
			return retval;
		}
	
		LOGN("size:",icinfo->lpbiInput->biSizeImage);
		icinfo->lpbiOutput->biSizeImage = size_image;
	
	    BYTE* const in = (BYTE*)icinfo->lpInput;
	    BYTE* out = (BYTE*)icinfo->lpOutput;

		int ftype = 0;
		if (icinfo->dwFlags & ICDECOMPRESS_NOTKEYFRAME)
			ftype = 1;
		LOGN("ftype =",ftype);

		//determine frame type from the data 
		int inferred_ftype = InferFrameType(in[0], icinfo->lpbiInput->biSizeImage);
		if (inferred_ftype >= 0) ftype = inferred_ftype;
		LOGN("inferred ftype =",ftype);

		int bits = icinfo->lpbiInput->biBitCount;
		int stride = (icinfo->lpbiInput->biWidth * bits / 8 + 3) & (~3);
		LOGN("stride=",stride);
		//DecompressFrame(BYTE *pSrc, int srcLength, BYTE *pDst, int pitch, int ftype)
		sc.DecompressFrame(in, icinfo->lpbiInput->biSizeImage, out, stride, ftype);
	} catch(BadVersionException bve) {
		char buf[1024];
		switch(bve.version) {
		case 1:
			sprintf(buf, "ScreenPressor V4 cannot decode video created with version 1. Use version 2 to recode it, I can understand format of ver.2."); 
			break;
		case 48:
			sprintf(buf, "Unexpected video format: weird color depth."); 
			break;
		default:
			sprintf(buf, "Your ScreenPressor V4 is too old to decompress video created with version %d. Please install a newer version.", bve.version);
			break;
		}
		MessageBox(NULL, buf, "ScreenPressor 4", MB_ICONERROR);
		return ICERR_BADFORMAT;
	}
    return ICERR_OK;
}

DWORD CodecInst::DecompressGetPalette(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut) {
  return ICERR_BADFORMAT;
}

DWORD CodecInst::DecompressEnd() 
{
	LOG("DecompressEnd");
	sc.Deinit();
	decompressing = false;
	return ICERR_OK;
}
