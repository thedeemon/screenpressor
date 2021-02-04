#include <windows.h>
#include <vfw.h>
#include "screencap.h"
#pragma hdrstop

static const DWORD FOURCC_SCPR = mmioFOURCC('S','C','P','R');   // our compressed format

struct CodecInst {
	static std::vector<CodecInst*> instances;
	static LRESULT Open(ICOPEN* icinfo); //returns inst_id - index in instances
	static void Close(DWORD inst_id);
	static CodecInst* GetInstance(DWORD inst_id);

	bool decompressing;
	ScreenCodec sc;

	DWORD rmask, gmask, bmask;
	int npframes, kf_interval, conf_loss;
	BOOL force_interval, force_loss;
	int size_image; //stride * height, used for decompressing

	// methods
	BOOL QueryAbout();
	DWORD About(HWND hwnd);

	BOOL QueryConfigure();
	DWORD Configure(HWND hwnd);

	DWORD GetState(LPVOID pv, DWORD dwSize);
	DWORD SetState(LPVOID pv, DWORD dwSize);

	DWORD GetInfo(ICINFO* icinfo, DWORD dwSize);

	DWORD CompressQuery(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD CompressGetFormat(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD CompressBegin(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD CompressGetSize(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD Compress(ICCOMPRESS* icinfo, DWORD dwSize);
	DWORD CompressEnd();


	DWORD DecompressQuery(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD DecompressGetFormat(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD DecompressBegin(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD Decompress(ICDECOMPRESS* icinfo, DWORD dwSize);
	DWORD DecompressGetPalette(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);
	DWORD DecompressEnd();

	bool CanCompress(LPBITMAPINFOHEADER lpbiIn);
	bool CanDecompress(LPBITMAPINFOHEADER lpbiIn, LPBITMAPINFOHEADER lpbiOut);

	int InferFrameType(BYTE first_byte, DWORD data_size); //0=I, 1=P, -1=error
};

