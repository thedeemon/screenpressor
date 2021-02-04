#include "screenpressor.h"


/***************************************************************************
 * DriverProc  -  The entry point for an installable driver.
 *
 * PARAMETERS
 * dwDriverId:  For most messages, <dwDriverId> is the DWORD
 *     value that the driver returns in response to a <DRV_OPEN> message.
 *     Each time that the driver is opened, through the <DrvOpen> API,
 *     the driver receives a <DRV_OPEN> message and can return an
 *     arbitrary, non-zero value. The installable driver interface
 *     saves this value and returns a unique driver handle to the
 *     application. Whenever the application sends a message to the
 *     driver using the driver handle, the interface routes the message
 *     to this entry point and passes the corresponding <dwDriverId>.
 *     This mechanism allows the driver to use the same or different
 *     identifiers for multiple opens but ensures that driver handles
 *     are unique at the application interface layer.
 *
 *     The following messages are not related to a particular open
 *     instance of the driver. For these messages, the dwDriverId
 *     will always be zero.
 *
 *         DRV_LOAD, DRV_FREE, DRV_ENABLE, DRV_DISABLE, DRV_OPEN
 *
 * hDriver: This is the handle returned to the application by the
 *    driver interface.
 *
 * uiMessage: The requested action to be performed. Message
 *     values below <DRV_RESERVED> are used for globally defined messages.
 *     Message values from <DRV_RESERVED> to <DRV_USER> are used for
 *     defined driver protocols. Messages above <DRV_USER> are used
 *     for driver specific messages.
 *
 * lParam1: Data for this message.  Defined separately for
 *     each message
 *
 * lParam2: Data for this message.  Defined separately for
 *     each message
 *
 * RETURNS
 *   Defined separately for each message.
 *
 ***************************************************************************/
LRESULT PASCAL DriverProc(DWORD_PTR dwDriverID, HDRVR hDriver, UINT uiMessage, LPARAM lParam1, LPARAM lParam2) {
	//flogn("DriverProc uiMessage=", uiMessage);
	CodecInst* pi = CodecInst::GetInstance(dwDriverID);
	LRESULT r = 0;
  switch (uiMessage) {
    case DRV_LOAD:
      return (LRESULT)1L;

    case DRV_FREE:
      return (LRESULT)1L;

    case DRV_OPEN:
		r = CodecInst::Open((ICOPEN*) lParam2);
		//flogn("Open=", r);
		//if (lParam2 != 0)
		//	flogn("icopen.err=", ((ICOPEN*) lParam2)->dwError);
		//else flog("lParam2=0!");
		return r;

    case DRV_CLOSE:
		CodecInst::Close(dwDriverID);
      return (LRESULT)1L;

    /*********************************************************************

      state messages

    *********************************************************************/

    // cwk
    case DRV_QUERYCONFIGURE:    // configuration from drivers applet
      return (LRESULT)1L;

    case DRV_CONFIGURE:
      pi->Configure((HWND)lParam1);
      return DRV_OK;

    case ICM_CONFIGURE:
      //
      //  return ICERR_OK if you will do a configure box, error otherwise
      //
      if (lParam1 == -1)
        return pi->QueryConfigure() ? ICERR_OK : ICERR_UNSUPPORTED;
      else
        return pi->Configure((HWND)lParam1);

    case ICM_ABOUT:
      //
      //  return ICERR_OK if you will do a about box, error otherwise
      //
      if (lParam1 == -1)
        return pi->QueryAbout() ? ICERR_OK : ICERR_UNSUPPORTED;
      else
        return pi->About((HWND)lParam1);

    case ICM_GETSTATE:
      return pi->GetState((LPVOID)lParam1, (DWORD)lParam2);

    case ICM_SETSTATE:
      return pi->SetState((LPVOID)lParam1, (DWORD)lParam2);

    case ICM_GETINFO:
      return pi->GetInfo((ICINFO*)lParam1, (DWORD)lParam2);

    case ICM_GETDEFAULTQUALITY:
      if (lParam1) {
        *((LPDWORD)lParam1) = 10000;
        return ICERR_OK;
      }
      break;

    /*********************************************************************

      compression messages

    *********************************************************************/

    case ICM_COMPRESS_QUERY:
      return pi->CompressQuery((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS_BEGIN:
      return pi->CompressBegin((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS_GET_FORMAT:
      return pi->CompressGetFormat((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS_GET_SIZE:
      return pi->CompressGetSize((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_COMPRESS:
      return pi->Compress((ICCOMPRESS*)lParam1, (DWORD)lParam2);

    case ICM_COMPRESS_END:
      return pi->CompressEnd();

    /*********************************************************************

      decompress messages

    *********************************************************************/

    case ICM_DECOMPRESS_QUERY:
      return pi->DecompressQuery((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_DECOMPRESS_BEGIN:
      return pi->DecompressBegin((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_DECOMPRESS_GET_FORMAT:
      return pi->DecompressGetFormat((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_DECOMPRESS_GET_PALETTE:
      return pi->DecompressGetPalette((LPBITMAPINFOHEADER)lParam1, (LPBITMAPINFOHEADER)lParam2);

    case ICM_DECOMPRESS:
      return pi->Decompress((ICDECOMPRESS*)lParam1, (DWORD)lParam2);

    case ICM_DECOMPRESS_END:
      return pi->DecompressEnd();

    /*********************************************************************

      standard driver messages

    *********************************************************************/

    case DRV_DISABLE:
    case DRV_ENABLE:
      return (LRESULT)1L;

    case DRV_INSTALL:
    case DRV_REMOVE:
      return (LRESULT)DRV_OK;
  }

  if (uiMessage < DRV_USER)
    return DefDriverProc(dwDriverID, hDriver, uiMessage, lParam1, lParam2);
  else
    return ICERR_UNSUPPORTED;
}


HMODULE hmoduleSCPR = NULL;

DWORD dwTlsIndex;

void SetThreadLocalInt(int v) {
	TlsSetValue(dwTlsIndex, (LPVOID) v);
}

int GetThreadLocalInt() {
	return (int)TlsGetValue(dwTlsIndex);
}

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID) {
  hmoduleSCPR = (HMODULE) hinstDLL;

  switch (fdwReason) 
    { 
        // The DLL is loading due to process 
        // initialization or a call to LoadLibrary. 
        case DLL_PROCESS_ATTACH: 
			dwTlsIndex = TlsAlloc();
			break;

		// DLL unload due to process termination or FreeLibrary. 
        case DLL_PROCESS_DETACH: 
			TlsFree(dwTlsIndex); 
  }
  return TRUE;
}
