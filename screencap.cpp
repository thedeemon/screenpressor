//---------------------------------------------------------------------------
//  Part of ScreenPressor lossless video codec
//  (C) Infognition Co. Ltd.
//---------------------------------------------------------------------------
// Implementation of ScreenCodec, CScreenCapt and CScreenCapt16 classes.

// CScreenCapt performs compression and decompression in RGB24 format 
// using Range Coder or ANS Coder
// ScreenCodec calls one of these versions
// and performs RGB32 <-> RGB24 <-> RGB16 conversion when necessary.
#include "screencap.h"
#include <malloc.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
#include <algorithm> 
#ifndef NOPROTECT
#include "fib.h"
#include "vmdata.h"
#endif

//actions to perform in worker threads
#define CMD_BLOCKTYPE 1
#define CMD_CMPPREV 2
#define CMD_DOLOSS 3
#define CMD_CLASSIFYPIXELSI 4

template<class RC>
CScreenCapt<RC>::CScreenCapt(int ver) 
: init(false), loss_mask(0), msr_x(256), msr_y(256), msrlow_x(8), msrlow_y(8), pSquad(NULL), last_was_flat(false), myVersion(ver)
#ifndef NOPROTECT
  ,vm(102400,102400)
#endif
{
	memset(&last_flat_clr[0],0,4);
	InitializeCriticalSection(&rowsCritSec);

#ifdef DO_LOG
	char str[256];
	sprintf(str, "c:\\temp\\scpr%d.log", this);
	logF = fopen(str, "wt");
#endif
}

template<class RC>
CScreenCapt<RC>::~CScreenCapt<RC>() {
	DeleteCriticalSection(&rowsCritSec);
#ifdef DO_LOG
	if (logF) {
		fclose(logF);
		logF = NULL;
	}
#endif
}

#ifndef NOPROTECT
static BYTE fc_compress_begin[] = {
#if defined( _WIN64 )
#include "on_compress_begin.fc64"
#else
#include "on_compress_begin.fc32"
#endif
};

#endif

// allocate memory for stats tables 
template<class RC>
void CScreenCapt<RC>::Init(CodecParameters *pParams)
{
	if (init) Deinit();

	fn = 0;
	X = pParams->width; Y = pParams->height;
	stride = (X * pParams->bits_per_pixel/8 + 3) & (~3);
	if (myVersion < 3) {
		msr_x = pParams->high_range_x; msr_y = pParams->high_range_y;
	} else {
		msr_x = min(pParams->high_range_x, 256); msr_y = min(pParams->high_range_y, 256); // in v3 this is fixed for now
	}
	msrlow_x = pParams->low_range_x; msrlow_y = pParams->low_range_y;
	ec.setMotionRange(msr_x, msr_y);
	#ifdef TIMING
	QueryPerformanceFrequency(&perfreq);
	#endif

	nbx = (X+15)/16;
	nby = (Y+15)/16;
	prev = (BYTE*)calloc(Y,stride);
	bts = (BYTE*)calloc(nbx,nby);
	for(uint i=0;i<3;i++)
		for(int j=0;j<SC_CXMAX;j++) 
			cntab[i][j] = ec.createC();
	for(int i=0;i<4;i++)
		sxy[i] = (int*)calloc(nbx*nby,sizeof(int));
	for(int i=0;i<2;i++)
		mvs[i] = (int*)calloc(nbx*nby,sizeof(int));
	if (RC::CtxNalloc)
		for(int i=0;i<SC_NCXMAX;i++) 
			ntab[i] = ec.createN();

	if (RC::CtxMalloc) {
		mvtab[0] = ec.createMX();
		mvtab[1] = ec.createMY();
	}
#ifndef NOPROTECT
	on_begin_data data;
	data.X = X;
	data.Y = Y;
	data.bytespp = bytespp;
	data.tick0 = GetTickCount();
	data.prclow = (native_int)ec.lowPtr();
	data.vmAction = ec.vmAction();

	Fibonacci fib;
	std::vector<native_int> code = fib.Decompress(fc_compress_begin, sizeof(fc_compress_begin));
	vm.Run(&code[0], code.size(), &data, "vm.log");
#endif
	SetupLossMask(pParams->loss);
	rleData.resize(Y*X*5);
	
	last_was_flat = false;
	init = true;
}

template<class RC>
void CScreenCapt<RC>::SetupLossMask(int loss)
{
	int mask = 0;
	for(int i=0;i<loss;i++)
		mask = (mask<<1) | 1;
	mask = (mask << 8) + mask;
	mask = (mask << 16) + mask;
	loss_mask = ~mask;

	int cmask = (1 << loss) >> 1;
	cmask = (cmask << 8) + cmask;
	corr_mask = (cmask << 16) + cmask;
}

template<class RC>
void CScreenCapt<RC>::setCx6f0(int f0)
{
	ec.f0val = f0;
}

//free the tables
template<class RC>
void CScreenCapt<RC>::Deinit()
{
	if (!init) return;

	ec.stop();

	free(prev);
	free(bts);
	for(uint i=0;i<3;i++)
		for(int j=0;j<SC_CXMAX;j++) 
			ec.freeC(cntab[i][j]);
	for(int i=0;i<4;i++)
		free(sxy[i]);
	for(int i=0;i<2;i++)
		free(mvs[i]);
	for(int i=0;i<SC_NCXMAX;i++) 
		ec.freeN(ntab[i]);
	for(int i=0;i<2;i++)
		ec.freeM(mvtab[i]);

	if (pSquad) {
	    delete pSquad;
	    pSquad = NULL;
	}
	init = false;
}

//forget all previous data statistics and fill the tables with default values
template<class RC>
void CScreenCapt<RC>::RenewI()
{
	lprintf(logF, "RenewI()\n");
	for(int i=0;i<3;i++)
		for(int j=0;j<SC_CXMAX;j++) 
			ec.renewC(cntab[i][j]);		
	ec.renewBN(ntab2);
	ec.renewX(xxtab);
	for(int i=0;i<SC_NCXMAX;i++) 
		ec.renewN(ntab[i]);

	ec.renewBT(bttab);
	for(int i=0;i<4;i++) 
		ec.renewSXY(sxytab[i]);

	ec.renewM(mvtab[0], true);
	ec.renewM(mvtab[1], false);

	for(int n=0;n<6;n++) 
		ec.renewP(ptypetab[n]);
}

template<class RC>
void CScreenCapt<RC>::DoLoss(BYTE *pSrc, PrevCmpParams* pcparams) {
	if (loss_mask != -1)
		pSquad->RunParallel(CMD_DOLOSS, pcparams, this);

#ifndef NOPROTECT
	prepare_bc_compress();
	on_compress_data data;
	data.pSrc = pSrc;
	data.tick = GetTickCount();
	data._this = &data;
	vm.Run(&bc_compress[0], bc_compress.size(), &data, "vm.log");
#endif

	//fill the padding with 0. Do it after CMD_DOLOSS because loss adds corr_mask, making padding not 0
	if (X & 3) {		
		const int pad = stride - X * bytespp;
		for(int y=0; y<Y; y++)
			memset(&pSrc[y*stride+X*bytespp], 0, pad);
	}
}

extern HMODULE hmoduleSCPR;

#ifndef NOPROTECT

static BYTE fc_check[] = {
#if defined( _WIN64 )
#include "on_check.fc64"
#else
#include "on_check.fc32"
#endif
};

static BYTE fc_compress[] = {
#if defined( _WIN64 )
#include "on_compress.fc64"
#else
#include "on_compress.fc32"
#endif
}; 

std::vector<native_int> bc_compress;

void prepare_bc_compress()
{
	if (bc_compress.size() > 0) return;
	Fibonacci fib;
	bc_compress = fib.Decompress(fc_compress, sizeof(fc_compress));
}
#endif

BOOL CheckCode(const char *email, const char *code)
{
#ifndef NOPROTECT
/*	#r0 - on_compress.bc, should be at least 2000 bytes
	#r1 - email
	#r2 - code
	#r3 - [out] result (1 success, 0 fail)
	#r4 - matr0 (any 16 bytes)
	#r5 - exe
	#r6 - exelen
*/
	char fn[1024] = {0}, exename[1024] = {0};
	GetModuleFileName(hmoduleSCPR, fn, 1020);
	GetModuleFileName(NULL, exename, 1020);
	HANDLE f = CreateFile(fn, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0,0);
	if (f==INVALID_HANDLE_VALUE) {
		//MessageBox(NULL, "can't open file", "CheckCode",MB_OK);
		return FALSE;
	}
	int sz = GetFileSize(f, NULL);
	BYTE *exe = (BYTE*)calloc(sz,1);
	DWORD w;
	ReadFile(f, exe, sz, &w,0);
	CloseHandle(f);

	prepare_bc_compress();

	LVM2 vm(102400, 102400);
	on_check_data data;
	memset(&data, 0, sizeof(on_check_data));
	data.on_compress = &bc_compress[0];
	data.exe = exe;
	data.exelen = sz;
	data.email = email;
	data.code = code;
	data.exename = exename;

	Fibonacci fib;
	std::vector<native_int> bc_check = fib.Decompress(fc_check, sizeof(fc_check));
	vm.Run(&bc_check[0], bc_check.size(), &data, "vm.log");
	free(exe);
	return data.res;
#else
	return TRUE;
#endif
}

template<class RC>
void CScreenCapt<RC>::CheckDstLength(BYTE **ppDst, BYTE **ppDstStart)
{
	/*BYTE *pDst = *ppDst; //todo: fix to work both with RC and ANS
	BYTE *pDstStart = *ppDstStart;
	if (pDst >= pDstEnd) {
		const int sz = pDst - pDstStart;
		bool firstChange = saveBuffer.size()==0;
		saveBuffer.resize(sz * 2);
		if (firstChange)
			memcpy(&saveBuffer[0], pDstStart, sz);
		*ppDstStart = &saveBuffer[0];
		*ppDst = *ppDstStart + sz;
		pDstEnd = *ppDstStart + sz*2 - 32;
	}*/
}

//compress an RGB24 I-frame
//RLE + arithmetic coding of values using previous values as context
template<class RC>
int CScreenCapt<RC>::CompressI(BYTE *pSrc, BYTE *pDST)
{
	BYTE *pDst = pDST;	
	const int off = -stride-3;
	const int nThreads = pSquad->NumThreads();

#ifdef TIMING
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
#endif

	PrevCmpParams prevcmp(pSrc, nThreads);
	DoLoss(pSrc, &prevcmp); //do loss, if necessary
	cx = cx1 = 0;

	pSquad->RunParallel(CMD_CLASSIFYPIXELSI, pSrc, this); //fills tls[] and rleData[]
	#ifdef TIMING
	QueryPerformanceCounter(&t1);
	auto classifyTime = t1.QuadPart - t0.QuadPart;
	printf("classify {");
	for(int i=0;i<runCmdTimes.size();i++) printf("%lf ", runCmdTimes[i]);
	printf("} ");
	#endif
	ec.encodeBegin(pDst);
	RenewI(); //this can be done while waiting for CMD_CLASSIFYPIXELSI
	EncodeRGB(pSrc);

	int ptype = 0, lastptype = 0;
	int i, n = 1, lasti = 0;
	for(int k=1; k<X+1; k++) // first row and one pixel
	{
		i = (k / X)*stride + (k % X)*3;
		const int r=pSrc[i], g=pSrc[i+1], b=pSrc[i+2];
		if ((r==pSrc[lasti] && g==pSrc[lasti+1] && b==pSrc[lasti+2]) && n<255)
			n++;
		else {
			CheckDstLength(&ec.pDst, &pDST);
			ec.encodeN(n, ntab[ptype]);
			EncodeRGB(&pSrc[i]);
			n = 1;
		}
		lasti = i;
	}
	ec.encodeN(n, ntab[ptype]);
	int x = 0, y = 1; //lasti = y*stride + x*3

	for(int band=0; band < nThreads; band++) {
		const int jend = tls[band].rleStartPos + tls[band].rleSize;
		int j = tls[band].rleStartPos;
		while(j < jend) {
			ptype = rleData[j];

			cx1 = ((pSrc[lasti+1]>>SC_CXSHIFT)<<6)&0xFC0;
			cx = pSrc[lasti+2]>>SC_CXSHIFT;

			CheckDstLength(&ec.pDst, &pDST);
			WritePixel(ptype, lastptype, &rleData[j+1]);
			lastptype = ptype;
			if (!ptype) 
				j += 3;
			n = rleData[j+1];
			ec.encodeN(n, ntab[ptype]);
			j += 2;
			x += n;
			while(x >= X) {
				x -= X; y++;
			}
			lasti = y * stride + x*3;
		}
	}

	pDst = ec.encodeEnd();
	#ifdef TIMING
	QueryPerformanceCounter(&t0);
	auto encodeTime = t0.QuadPart - t1.QuadPart;
	#endif
	memcpy(prev, pSrc, Y*stride);
	if (saveBuffer.size() > 0)
		saveBuffer.resize(pDst - pDST);
	#ifdef TIMING
	printf(" CompressI: clsfy=%lf encode=%lf ", (double)classifyTime / perfreq.QuadPart, (double)encodeTime / perfreq.QuadPart);
	#endif

	return pDst - pDST;
}

#define GO_NEXT_PIXEL 	lasti = i; \
	x++; i += 3; \
	if (x>=X) { \
		x = 0; y++; \
		i = y*stride + x*3; \
	}

//decompress RGB24 I-frame
template<class RC>
int CScreenCapt<RC>::DecompressI(BYTE *pSrc, int srcLength, BYTE *pDst)
{
	int r,g,b;
	ec.decodeBegin(pSrc, srcLength);
	RenewI();
	cx = cx1 = 0;

	int ptype = 0, lastptype = 0;
	int i = 0, n = 1, k = 0, lasti=0;
	while(k<X+1) {
		DecodeRGB(r,g,b);
		lprintf(logF, "decodeN ptype=%d ", ptype);
		n = ec.decodeN(ntab[ptype]);
		lprintf(logF, "n=%d\n",n);
		for(int x=0;x<n;x++) {
			pDst[i] = r;
			pDst[i+1] = g;
			pDst[i+2] = b;
			k++;
			lasti = i;
			i+=3;
			if ((i % stride)>=X*3)
				i = (i / stride + 1) * stride;
		}		
	}

	const int off = -stride-3;

	int x = (i % stride)/3, y = i / stride;
	while(y<Y) {
		lastptype = ptype;
		ptype = ec.decodeP(ptypetab[lastptype]);
		lprintf(logF, "decodeP(%d) => %d\n", lastptype, ptype);
		if (!ptype) 
			DecodeRGB(r,g,b);	
		n = ec.decodeN(ntab[ptype]);
		lprintf(logF, "n=%d\n",n);
		i = y*stride + x*3;
		switch(ptype) {
		case 0:
			while(n-->0) {
				pDst[i] = r;
				pDst[i+1] = g;
				pDst[i+2] = b;
				GO_NEXT_PIXEL;
			}
			break;
		case 1:
			while(n-->0) {
				pDst[i] = pDst[lasti]; pDst[i+1] = pDst[lasti+1]; pDst[i+2] = pDst[lasti+2];
				GO_NEXT_PIXEL;
			}
			break;
		case 2:
			while(n-->0) {
				pDst[i] = pDst[i+off+3]; pDst[i+1] = pDst[i+off+4]; pDst[i+2] = pDst[i+off+5];
				GO_NEXT_PIXEL;
			}
			break;
		case 4:
			while(n-->0) {
				pDst[i] = (int)pDst[lasti] + (int)pDst[i+off+3] - (int)pDst[i+off];
				pDst[i+1] = (int)pDst[lasti+1] + (int)pDst[i+off+4] - (int)pDst[i+off+1];
				pDst[i+2] = (int)pDst[lasti+2] + (int)pDst[i+off+5] - (int)pDst[i+off+2];
				GO_NEXT_PIXEL;
			}
			break;
		case 5:
			while(n-->0) {
				pDst[i] = pDst[i+off]; pDst[i+1] = pDst[i+off+1]; pDst[i+2] = pDst[i+off+2];
				GO_NEXT_PIXEL;
		    }
			break;
		}
		g = pDst[lasti+1];
		b = pDst[lasti+2];

		cx = g>>SC_CXSHIFT;
		MAKECX1;
		cx = b>>SC_CXSHIFT;		
	}

	memcpy(prev, pDst, Y*stride);
	return 1;
}

//can pixel of I-frame be predicted by its neighbours?
template<class RC>
int CScreenCapt<RC>::GetPixelType(BYTE* pSrc, BYTE* pSrclast, const int off)
{
    const int r=pSrc[0], g=pSrc[1], b=pSrc[2];

	if (r==pSrclast[0] && g==pSrclast[1] && b==pSrclast[2]) //left, usually
		return 1;

	if (r==pSrc[off] && g==pSrc[off+1] && b==pSrc[off+2])
		return 5;
	
	if (r==pSrc[off+3] && g==pSrc[off+4] && b==pSrc[off+5])
		return 2;
	
	if ((r == (int)pSrclast[0] + (int)pSrc[off+3] - (int)pSrc[off]) &&
		(g == (int)pSrclast[1] + (int)pSrc[off+4] - (int)pSrc[off+1]) &&
		(b == (int)pSrclast[2] + (int)pSrc[off+5] - (int)pSrc[off+2]))
			return 4;

	return 0;
}

//can pixel of P-frame be predicted by its neighbours?
template<class RC>
int CScreenCapt<RC>::GetPixelTypeP(BYTE* pSrc, BYTE* pr, const int off)
{
    const int r=pSrc[0], g=pSrc[1], b=pSrc[2];

	if (r==pSrc[-3] && g==pSrc[-2] && b==pSrc[-1])
		return 1;

	if (r==pr[0] && g==pr[1] && b==pr[2])
		return 3;

	if (r==pSrc[off] && g==pSrc[off+1] && b==pSrc[off+2])
		return 5;
	
	if (r==pSrc[off+3] && g==pSrc[off+4] && b==pSrc[off+5])
		return 2;
	
	if ((r == (int)pSrc[-3] + (int)pSrc[off+3] - (int)pSrc[off]) &&
		(g == (int)pSrc[-2] + (int)pSrc[off+4] - (int)pSrc[off+1]) &&
		(b == (int)pSrc[-1] + (int)pSrc[off+5] - (int)pSrc[off+2]))
			return 4;

	return 0;
}

//pixel prediction for row 0
template<class RC>
int CScreenCapt<RC>::GetPixelTypeP0(BYTE* pSrc, BYTE* pr)
{
	if (pSrc[0]==pr[0] && pSrc[1]==pr[1] && pSrc[2]==pr[2])
		return 3;
	return 0;
}

//can we predict this pixel of I-frame using this prediction?
template<class RC>
bool CScreenCapt<RC>::PixelTypeFits(int ptype, BYTE *pSrc, BYTE* pSrclast, const int off)
{
    const int r=pSrc[0], g=pSrc[1], b=pSrc[2];
	switch(ptype) {
		case 0:	//return (r==pSrc[-3] && g==pSrc[-2] && b==pSrc[-1]);
		case 1: return (r==pSrclast[0] && g==pSrclast[1] && b==pSrclast[2]);
		case 2: return (r==pSrc[off+3] && g==pSrc[off+4] && b==pSrc[off+5]);
		case 4: return ((r == (int)pSrclast[0] + (int)pSrc[off+3] - (int)pSrc[off]) &&
						(g == (int)pSrclast[1] + (int)pSrc[off+4] - (int)pSrc[off+1]) &&
						(b == (int)pSrclast[2] + (int)pSrc[off+5] - (int)pSrc[off+2]));
		case 5:	return (r==pSrc[off] && g==pSrc[off+1] && b==pSrc[off+2]);

	}
	return false;
}

//can we predict this pixel of P-frame using this prediction?
template<class RC>
bool CScreenCapt<RC>::PixelTypeFitsP(int ptype, BYTE *pSrc, BYTE* pr, BYTE* pSrclast, const int off)
{
    const int r=pSrc[0], g=pSrc[1], b=pSrc[2];
	switch(ptype) {
		case 0:	return (r==pSrclast[0] && g==pSrclast[1] && b==pSrclast[2]);
		case 1: return (r==pSrc[-3] && g==pSrc[-2] && b==pSrc[-1]);
		case 2: return (r==pSrc[off+3] && g==pSrc[off+4] && b==pSrc[off+5]);
		case 3: return (r==pr[0] && g==pr[1] && b==pr[2]);
		case 4: return ((r == (int)pSrc[-3] + (int)pSrc[off+3] - (int)pSrc[off]) &&
						(g == (int)pSrc[-2] + (int)pSrc[off+4] - (int)pSrc[off+1]) &&
						(b == (int)pSrc[-1] + (int)pSrc[off+5] - (int)pSrc[off+2]));
		case 5:	return (r==pSrc[off] && g==pSrc[off+1] && b==pSrc[off+2]);

	}
	return false;
}

//test pixel prediction for row 0
template<class RC>
bool CScreenCapt<RC>::PixelTypeFitsP0(int ptype, BYTE *pSrc, BYTE* pr, BYTE* pSrclast)
{
	switch(ptype) {
		case 0: return (pSrc[0]==pSrclast[0] && pSrc[1]==pSrclast[1] && pSrc[2]==pSrclast[2]);
		case 3: return (pSrc[0]==pr[0] && pSrc[1]==pr[1] && pSrc[2]==pr[2]);
	}
	return false;
}

//int dbgflag = 0;

template<class RC>
void CScreenCapt<RC>::WritePixel(int ptype, int lastptype, BYTE* pSrc)
{
	const int r=pSrc[0], g=pSrc[1], b=pSrc[2];
	lprintf(logF, "encP (lastptype=%d) -> ptype=%d\n", lastptype, ptype);
	ec.encodeP(ptype, ptypetab[lastptype]);
	if (ptype) return;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	ec.encodeC(r, cntab[0][cx+cx1]);
	MAKECX1;
	cx = r>>SC_CXSHIFT;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	ec.encodeC(g, cntab[1][cx+cx1]);
	MAKECX1;
	cx = g>>SC_CXSHIFT;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	ec.encodeC(b, cntab[2][cx+cx1]);
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	lprintf(logF, "rgb=%d,%d,%d\n", r,g,b);
}

//write RGB values
template<class RC>
void CScreenCapt<RC>::EncodeRGB(BYTE *pSrc)
{
	const int r=pSrc[0], g=pSrc[1], b=pSrc[2];
	ec.encodeC(r, cntab[0][cx+cx1]);
	MAKECX1;
	cx = r>>SC_CXSHIFT;
	ec.encodeC(g, cntab[1][cx+cx1]);
	MAKECX1;
	cx = g>>SC_CXSHIFT;
	ec.encodeC(b, cntab[2][cx+cx1]);
	MAKECX1;
	cx = b>>SC_CXSHIFT;
}

/*template<class T> int GetSPVersion() { return 0; }

//template<> int GetSPVersion<RangeCoderC>() { return 1; }
template<> int GetSPVersion<RangeCoderSub>() { return 2; }
template<> int GetSPVersion<UseRC>() { return 2; }
template<> int GetSPVersion<UseANS>() { return 3; }
*/

template<class RC, class CC> void dbgShow(CC &ctx, RC &ec) {}

template<> void dbgShow(UseANS::CtxC &ctx, UseANS &ec) {
	lprintf(logF, "decodeC(3380): someFreq=%d\n", RansDecGet(&ec.ransDec, PROB_BITS));
	ctx.show();
}

//read RGB values
template<class RC>
void CScreenCapt<RC>::DecodeRGB(int &r, int &g, int &b)
{
	//const int cx0 = cx+cx1;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	r = ec.decodeC(cntab[0][cx+cx1]);
	MAKECX1;
	cx = r>>SC_CXSHIFT;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	g = ec.decodeC(cntab[1][cx+cx1]);
	MAKECX1;
	cx = g>>SC_CXSHIFT;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	b = ec.decodeC(cntab[2][cx+cx1]);
	MAKECX1;
	cx = b>>SC_CXSHIFT;
	lprintf(logF, "cx=%d cx1=%d\n",cx,cx1);
	lprintf(logF, "rgb=%d,%d,%d\n", r,g,b);
}

//find similar block in previous frame
//bi - block index in the table of blocks information
template<class RC>
bool CScreenCapt<RC>::FindMV(BYTE *pSrc, int bi, int &last_mvx, int &last_mvy, int upperBI)
{
	int x1 = sxy[0][bi];//bx*16;
	int y1 = sxy[1][bi];//by*16;
	int x2 = sxy[2][bi];//bx*16+16;
	int y2 = sxy[3][bi];//by*16+16;

	int rx1 = x1 - msrlow_x;
	int rx2 = x1 + msrlow_x;
	int ry1 = y1 - msrlow_y;
	int ry2 = y1 + msrlow_y;

	if (rx1<0) rx1 = 0;
	if (ry1<0) ry1 = 0;
	if ((rx2 + x2-x1) > X) rx2 = X - x2 + x1 +1;
	if ((ry2 + y2-y1) > Y) ry2 = Y - y2 + y1 +1;

	int fx1 = x1 - msr_x; // far search
	int fx2 = x1 + msr_x;
	int fy1 = y1 - msr_y;
	int fy2 = y1 + msr_y;

	if (fx1<0) fx1 = 0;
	if (fy1<0) fy1 = 0;
	if ((fx2 + x2-x1) > X) fx2 = X - x2 + x1 +1;
	if ((fy2 + y2-y1) > Y) fy2 = Y - y2 + y1 +1;

	const int is = y1*stride + x1*bytespp;
	const int width_bytes = (x2-x1)*bytespp;
	const int height = y2 - y1;

	const int sx = x1 + last_mvx;
	const int sy = y1 + last_mvy;

	if (sx>=fx1 && sx<fx2 && sy>=fy1 && sy<fy2)
	if (SameBlocks(pSrc, is, sy*stride + sx*bytespp, width_bytes, height))	{
		mvs[0][bi] = last_mvx;
		mvs[1][bi] = last_mvy;
		return true;
	}

	 //upperBI - index of a block above current, let's try its vector
	if (upperBI >= 0 && (mvs[0][upperBI] != last_mvx || mvs[1][upperBI] != last_mvy)) {
		const int x = x1 + mvs[0][upperBI];
		const int y = y1 + mvs[1][upperBI];
		if (x>=fx1 && x<fx2 && y>=fy1 && y<fy2)
		if (SameBlocks(pSrc, is, y*stride + x*bytespp, width_bytes, height))	{
			mvs[0][bi] = mvs[0][upperBI];
			mvs[1][bi] = mvs[1][upperBI];
			return true;
		}
	}

	const int commonYdist = min(y1 - fy1, fy2 - y1 - 1);
	//far search
	int yup = y1-1, ydown = y1+1;
	for(int k=0;k<commonYdist;k++,yup--,ydown++) {
		if (SameBlocks(pSrc, is, yup*stride + x1*bytespp, width_bytes, height))	{
			last_mvx = mvs[0][bi] = 0;
			last_mvy = mvs[1][bi] = yup - y1;
			return true;
		}
		if (SameBlocks(pSrc, is, ydown*stride + x1*bytespp, width_bytes, height))	{
			last_mvx = mvs[0][bi] = 0;
			last_mvy = mvs[1][bi] = ydown - y1;
			return true;
		}
	}

	for(;yup>=fy1; yup--)//up
		if (SameBlocks(pSrc, is, yup*stride + x1*bytespp, width_bytes, height))	{
			last_mvx = mvs[0][bi] = 0;
			last_mvy = mvs[1][bi] = yup - y1;
			return true;
		}
	for(; ydown<fy2; ydown++)//down
		if (SameBlocks(pSrc, is, ydown*stride + x1*bytespp, width_bytes, height))	{
			last_mvx = mvs[0][bi] = 0;
			last_mvy = mvs[1][bi] = ydown - y1;
			return true;
		}

	for(int x=x1; x>=fx1; x--) //far left
		if (SameBlocks(pSrc, is, y1*stride +x*bytespp, width_bytes, height))	{
			last_mvx = mvs[0][bi] = x - x1;
			last_mvy = mvs[1][bi] = 0;
			return true;
		}

	for(int x=x1; x<fx2; x++) //far right
		if (SameBlocks(pSrc, is, y1*stride + x*bytespp, width_bytes, height))	{
			last_mvx = mvs[0][bi] = x - x1;
			last_mvy = mvs[1][bi] = 0;
			return true;
		}

	//low range search
	for(int x=x1; x>=rx1; x--) {
		for(int y=y1; y>=ry1; y--)
			if (SameBlocks(pSrc, is, y*stride + x*bytespp, width_bytes, height))	{
				last_mvx = mvs[0][bi] = x - x1;
				last_mvy = mvs[1][bi] = y - y1;
				return true;
			}

		for(int y=y1+1; y<ry2; y++)
			if (SameBlocks(pSrc, is, y*stride + x*bytespp, width_bytes, height))	{
				last_mvx = mvs[0][bi] = x - x1;
				last_mvy = mvs[1][bi] = y - y1;
				return true;
			}
	}

	for(int x=x1+1; x<rx2; x++) {
		for(int y=y1; y>=ry1; y--)
			if (SameBlocks(pSrc, is, y*stride + x*bytespp, width_bytes, height))	{
				last_mvx = mvs[0][bi] = x - x1;
				last_mvy = mvs[1][bi] = y - y1;
				return true;
			}

		for(int y=y1+1; y<ry2; y++)
			if (SameBlocks(pSrc, is, y*stride + x*bytespp, width_bytes, height))	{
				last_mvx = mvs[0][bi] = x - x1;
				last_mvy = mvs[1][bi] = y - y1;
				return true;
			}
	}

	return false;
}

template<class RC>
bool CScreenCapt<RC>::SameBlocks(BYTE *pSrc, int is, int ip, int width_bytes, int height)
{
	for(int y=0; y<height; y++) {
		if (memcmp(&pSrc[is], &prev[ip], width_bytes)) //differ
			return false;
		is += stride; ip += stride;
	}
	return true;
}

//do some work in worker thread
template<class RC>
void CScreenCapt<RC>::RunCommand(int command, void *params, CSquadWorker *sqworker)
{
	const int myNum = sqworker->MyNum();

	#ifdef TIMING
	LARGE_INTEGER t0, t1;
	QueryPerformanceCounter(&t0);
	#endif
	switch(command) {
	case CMD_BLOCKTYPE: {
		int start=0, size=nby;
		sqworker->GetSegment(nby, start, size);
		DecideBlocksParams *blockparams = (DecideBlocksParams *)params;
		DecideBlockTypes(start, size, blockparams->pSrc, blockparams->regions[myNum], myNum);	
		break;
	} 
	case CMD_CMPPREV: {
		PrevCmpParams *prevcmp = (PrevCmpParams*) params;
		int y1=0, ys=Y;
		sqworker->GetSegment(Y, y1, ys);
		prevcmp->results[myNum] = memcmp(&prevcmp->pSrc[y1*stride], &prev[y1*stride], ys*stride);
		break;
	} 
	case CMD_DOLOSS: {
		PrevCmpParams *prevcmp = (PrevCmpParams*) params;
		int y1=0, ys=Y;
		sqworker->GetSegment(Y, y1, ys);
		int n = ys*stride / 4;
		int* pData = (int*)&prevcmp->pSrc[y1*stride];
		for(int i=0; i<n; i++)
			pData[i] = (pData[i] & loss_mask) | corr_mask;
		break;
	}
	case CMD_CLASSIFYPIXELSI: {
		int y0=0, ysize=1;
		sqworker->GetSegment(Y, y0, ysize);
		ClassifyPixelsI(myNum, y0, ysize, (BYTE*)params);
		break;
	}
	}//switch
	#ifdef TIMING
	QueryPerformanceCounter(&t1);
	runCmdTimes[myNum] = (double)(t1.QuadPart - t0.QuadPart) / perfreq.QuadPart;
	#endif
}

template<class RC>
void CScreenCapt<RC>::ClassifyPixelsI(int myNum, int y0, int ysize, BYTE *pSrc)
{
	int j = y0 * X * 5;
	tls[myNum].rleStartPos = j;

	int x = 0, y = y0, lasti = (y0-1) * stride + (X-1)*3;
	if (y0==0) {
		x = 1; y = 1; // very first row is different
		lasti = stride;
	}
	const int yend = y0 + ysize;
	const int off = -stride-3;
	const int i0 = y * stride + x*3;
	int ptype = GetPixelType(&pSrc[i0], &pSrc[lasti], off);	
	rleData[j++] = ptype;
	if (!ptype) {
		rleData[j++] = pSrc[i0]; rleData[j++] = pSrc[i0+1]; rleData[j++] = pSrc[i0+2];
	}
	int n = 1; 
	x++; lasti = i0;

	while(y < yend) {
		const int i = y * stride + x*3;
		if ((n<255) && PixelTypeFits(ptype, &pSrc[i], &pSrc[lasti], off)) 
			n++;		
		else {
			rleData[j++] = n;
			ptype = GetPixelType(&pSrc[i], &pSrc[lasti], off);
			rleData[j++] = ptype;
			if (!ptype) {
				rleData[j++] = pSrc[i]; rleData[j++] = pSrc[i+1]; rleData[j++] = pSrc[i+2];
			}
			n = 1;
		}	
		lasti = i;
		x++;
		if (x>=X) {
			x = 0; y++;
		}
	}
	rleData[j++] = n;

	tls[myNum].rleSize = j - tls[myNum].rleStartPos;
}

//Determine block types.
//Compare each 16x16 block with same block in previous frame.
//It's either complete copy or partial copy or completely different
//If some part is different, find minimal rectangular area where difference is
//and try to find similar block in previous frame.
//Remember block type, its bounds and motion vector (if found)
template<class RC>
void CScreenCapt<RC>::DecideBlockTypes(int by_start, int by_size, BYTE *pSrc, BlockRegion &rgn, int myNum)
{
	int bx1=nbx, bx2=-1, by1=nby, by2=-1;
	int last_mvx=0, last_mvy=0;
	const int off = -stride - bytespp;

	int phase = 1; //1: by_start .. by_start+by_size-1;  2: steal from others
	int by = by_start;
	const int by_end = by_start + by_size;

	int justFinished = -1; 
	while(true) {
		//decide on which row to work
		EnterCriticalSection(&rowsCritSec);

		if (justFinished >= 0) rowStates[justFinished] = RowState::Done;

		bool foundWork = false;
		if (phase==1) {
			while(by < by_end && rowStates[by] != RowState::Untouched)
				by++;
			if (by == by_end) { //no work in my own band
				phase = 2; //go stealing
			} else foundWork = true;
		}
		if (phase==2) {
			for(int i=1;i<nby;i++) { //find an unprocessed row
				int y = (by + nby - i) % nby;
				if (rowStates[y] == RowState::Untouched) {
					by = y; foundWork = true; break;
				}
			}
		}
		bool canUseUpper = false;
		if (foundWork) {// here rowStates[by] == Untouched
			rowStates[by] = RowState::Processing;
			if (by > 0 && rowStates[by-1]==RowState::Done)
				canUseUpper = true;
		}

		LeaveCriticalSection(&rowsCritSec);

		if (!foundWork) break; // no more work in whole frame!

		int j = by * 16 * X * 5;
		tls[by].rleStartPos = j;
		for(int bx=0;bx<nbx;bx++) {
			const int x1 = bx*16;
			const int x2 = min(bx*16+16, X);
			const int y1 = by*16;
			const int y2 = min(by*16+16, Y);
			int cp = 0;
			const int bi = by*nbx+bx;
			const int upperBI = canUseUpper ? bi - nbx : -1; //index of block above
			const int bwidth = (x2-x1)*bytespp;
			const int x1bytespp = x1*bytespp;
			bool thisBlockChanged = false;
			for(int y=y1;y<y2;y++) {
				int i = y*stride + x1bytespp;
				if (memcmp(&pSrc[i], &prev[i], bwidth)) {
					thisBlockChanged = true;
	
					//changed subblock
					int sx1=x2,sx2=x1,sy1=y,sy2=y;

					//vertically first changed line is at y, so sy1=y
					//what's the lowest changed line sy2?
					for(y=y2-1;y>sy1;y--) {
						const int si = y*stride + x1bytespp;
						if (memcmp(&pSrc[si], &prev[si], bwidth)) {
							sy2 = y; break;
						}
					}
					//so line sy2 has a change, where does it start and end?
					for(int x=x1;x<x2;x++) { //first change going from left
						const int si = sy2*stride + x*bytespp;
						if (pSrc[si] != prev[si] || pSrc[si+1] != prev[si+1] || pSrc[si+2] != prev[si+2]) {
							sx1 = x; break;
						}
					}
					sx2 = sx1;
					for(int x=x2-1; x>sx1; x--) { //last change, i.e. first change going from right
						const int si = sy2*stride + x*bytespp;
						if (pSrc[si] != prev[si] || pSrc[si+1] != prev[si+1] || pSrc[si+2] != prev[si+2]) {
							sx2 = x; break;
						}
					}
					//now sx1 and sx2 describe changed span at line sy2, what about other lines?
					for(y=sy1;y<sy2;y++) {
						const int ystride = y*stride;
						for(int x=x1;x<sx1;x++) { //is first change going from left less than known sx1?
							const int si = ystride + x*bytespp;
							if (pSrc[si] != prev[si] || pSrc[si+1] != prev[si+1] || pSrc[si+2] != prev[si+2]) {
								sx1 = x; break;
							}
						}
						for(int x=x2-1; x>sx2; x--) { //is first change from right greater than known sx2?
							const int si = ystride + x*bytespp;
							if (pSrc[si] != prev[si] || pSrc[si+1] != prev[si+1] || pSrc[si+2] != prev[si+2]) {
								sx2 = x; break;
							}
						}
					}

					sx2++; sy2++;
					if ((sx1>x1) || (sy1>y1) || (sx2<x2) || (sy2<y2)) { //changed part less than whole block
						cp = 2;
						sxy[0][bi] = sx1; sxy[1][bi] = sy1; sxy[2][bi] = sx2; sxy[3][bi] = sy2;
					} else {
						cp = 1;
						sxy[0][bi] = x1; sxy[1][bi] = y1; sxy[2][bi] = x2; sxy[3][bi] = y2;
					}

					if (FindMV(pSrc, bi, last_mvx, last_mvy, upperBI)) 
						cp += 2;
					else { //changed block 
						int n = 333;
						int lasti=0;
						int ptype = 0, lastptype = 0;
						for(int y=sy1; y<sy2; y++) {
							int i = y*stride + sx1*3;
							for(int x=sx1; x<sx2; x++) {
								const bool notedge = (x>0) && (y>0);
								if ((n<255) && (notedge ? PixelTypeFitsP(ptype, &pSrc[i], &prev[i], &pSrc[lasti], off) : PixelTypeFitsP0(ptype, &pSrc[i], &prev[i], &pSrc[lasti]))) {
									n++;		
								} else {
									if (n!=333) 
										rleData[j++] = n;
									ptype = notedge ? GetPixelTypeP(&pSrc[i], &prev[i], off) : GetPixelTypeP0(&pSrc[i], &prev[i]);
									rleData[j++] = ptype;
									n = 1;
								}	
								lasti = i;
								i += 3;
							}
						}
						rleData[j++] = n;
					}//if not found motion
					break;
				} //if memcmp
			}//for y
			bts[bi] = cp;
			if (thisBlockChanged) {
				bx1 = min(bx, bx1);
				by1 = min(by, by1);
				bx2 = max(bx, bx2);
				by2 = max(by, by2);
			}
		}// for bx
		tls[by].rleSize = j - tls[by].rleStartPos;
		justFinished = by;
	}//while have work (previously: for by)

	//initially: int bx1=nbx, bx2=-1, by1=nby, by2=-1;
	//if no changed blocks found, set all to -1
	rgn.bx1 = bx1==nbx ? -1 : bx1;  
	rgn.by1 = by1==nby ? -1 : by1; 
	rgn.bx2 = bx2;
	rgn.by2 = by2;
}

//compress RGB24 P-frame
template<class RC>
int CScreenCapt<RC>::CompressP(BYTE *pSrc, BYTE *pDST)
{
	BYTE *pDst = pDST;
	const int nThreads = pSquad->NumThreads();
	lprintf(logF, "CompressP\n");
	#ifdef TIMING
	LARGE_INTEGER t[7];
	QueryPerformanceCounter(&t[0]);
	#endif
	PrevCmpParams prevcmp(pSrc, nThreads);
	DoLoss(pSrc, &prevcmp);
	#ifdef TIMING
	QueryPerformanceCounter(&t[1]);
	#endif

	int changes=0;
	pSquad->RunParallel(CMD_CMPPREV, &prevcmp, this);
	for(int x=0; x < nThreads; x++)
		changes |= prevcmp.results[x];
	#ifdef TIMING
	QueryPerformanceCounter(&t[2]);
	#endif
	if (!changes) {
		*pDst = 0;
		return 1;
	}
	*pDst++ = 1; //changes
	ec.encodeBegin(pDst);

	for(int i=0;i<rowStates.size();i++)
		rowStates[i] = RowState::Untouched;
	// determine and encode block types, also fill tls[] rleData[]
	DecideBlocksParams blockparams(pSrc, nThreads);
	pSquad->RunParallel(CMD_BLOCKTYPE, &blockparams, this);

	#ifdef TIMING
	QueryPerformanceCounter(&t[3]);
	printf("decideblocks {");
	for(int i=0;i<runCmdTimes.size();i++) printf("%lf ", runCmdTimes[i]);
	printf("} ");
	#endif
	int bx1=-1, bx2=-1, by1=-1, by2=-1;
	for(int i=0; i<nThreads; i++) {
		if ((bx1<0) || (blockparams.regions[i].bx1 >=0 && blockparams.regions[i].bx1 < bx1))
			bx1 = blockparams.regions[i].bx1;
		if ((by1<0) || (blockparams.regions[i].by1 >=0 && blockparams.regions[i].by1 < by1))
			by1 = blockparams.regions[i].by1;
		if ((bx2<0) || (blockparams.regions[i].bx2 >=0 && blockparams.regions[i].bx2 > bx2))
			bx2 = blockparams.regions[i].bx2;
		if ((by2<0) || (blockparams.regions[i].by2 >=0 && blockparams.regions[i].by2 > by2))
			by2 = blockparams.regions[i].by2;
	}

	//encode indices of first and last blocks which differ from prev. frame
	int xx1 = by1*nbx + bx1;
	ec.encodeX(xx1 & 255, xxtab);
	ec.encodeX((xx1>>8) & 255, xxtab);
	int xx2 = by2*nbx + bx2;
	ec.encodeX(xx2 & 255, xxtab);
	ec.encodeX((xx2>>8) & 255, xxtab);
	 
	lprintf(logF, "xx1=%d xx2=%d\n",xx1,xx2);
	//encode block types for blocks between those two indices
	//RLE + Arithmetic Coding
	int oldt = -1;
	int n = -1;
	for(int x=xx1; x<=xx2; x++) {
		if ((bts[x]==oldt) && (n<255)) 
			n++;
		else {
			CheckDstLength(&ec.pDst, &pDST);
			if (n>0)
				ec.encodeBN(n, ntab2);
			ec.encodeBT(bts[x], bttab);
			oldt = bts[x];
			n = 1;
		}		
	}
	ec.encodeBN(n, ntab2);
	#ifdef TIMING
	QueryPerformanceCounter(&t[4]);
	#endif
	//encode blocks
	const int off = -stride-3;
	n = -1; 
	cx = cx1 = 0;
	int lastmx=0, lastmy=0;
	int band = 0, j = 0;
	for(uint by=0;by<nby;by++) {
		j = tls[by].rleStartPos;
		for(uint bx=0;bx<nbx;bx++) {
			int bi = by*nbx+bx;
			if (bts[bi])	{
				int x1 = sxy[0][bi];//bx*16;
				int x2 = sxy[2][bi];//bx*16+16;
				int y1 = sxy[1][bi];//by*16;
				int y2 = sxy[3][bi];//by*16+16;
				lprintf(logF, "bts[%d]=%d\n", bi, bts[bi]);
				//if block bounds are different from default (not whole block differs), encode them
				if ((bts[bi]-1)&1) {
					CheckDstLength(&ec.pDst, &pDST);
					ec.encodeSXY(x1-bx*16, sxytab[0]);
					ec.encodeSXY(y1-by*16, sxytab[1]);
					ec.encodeSXY(x2-1-bx*16, sxytab[2]);
					ec.encodeSXY(y2-1-by*16, sxytab[3]);
					lprintf(logF, "x1=%d y1=%d x2=%d y2=%d\n", x1,y1,x2,y2);
				}

				if ((bts[bi]-1)&2) { //encode motion vectors
					CheckDstLength(&ec.pDst, &pDST);
					if (RC::canEncodeBool) { //V3
						if (bi > 0 && mvs[0][bi]==lastmx && mvs[1][bi]==lastmy) { //same MV as prev block
							ec.encodeBool(true);
						} else {
							ec.encodeBool(false);
							ec.encodeMX(mvs[0][bi]+msr_x, mvtab[0]);
							ec.encodeMY(mvs[1][bi]+msr_y, mvtab[1]);
							lastmx = mvs[0][bi]; lastmy = mvs[1][bi];
						}
					} else {//V2 
						ec.encodeMX(mvs[0][bi]+msr_x, mvtab[0]);
						ec.encodeMY(mvs[1][bi]+msr_y, mvtab[1]);
					}
					lprintf(logF, "mx=%d my=%d\n", mvs[0][bi], mvs[1][bi]);
				} else { //encode data					
					int y = y1;
					int x = x1;
					int lastptype = 0, i = 0;
					CheckDstLength(&ec.pDst, &pDST);
					while(y<y2) {
						int ptype = rleData[j++];
						int n = rleData[j++];
						i = y*stride + x*3;

						WritePixel(ptype, lastptype, &pSrc[i]);
						lastptype = ptype;
						lprintf(logF, "encN n=%d ptype=%d\n", n, ptype);
						ec.encodeN(n, ntab[ptype]);

						if (n>1) {
							int t = x-x1 + n-1;
							x = t % (x2-x1) + x1;
							y += t / (x2-x1);
							i = y*stride + x*3;
						}
						cx1 = ((pSrc[i+1]>>SC_CXSHIFT)<<6)&0xFC0;
						cx = pSrc[i+2]>>SC_CXSHIFT;
						lprintf(logF, "cx = %d cx1=%d\n", cx, cx1);
						x++;
						if (x==x2) {
							x = x1;
							y++;
						}
					} //while y
				}
			} // if bts
		}//bx
	}//by
	CheckDstLength(&ec.pDst, &pDST);
	pDst = ec.encodeEnd();
	#ifdef TIMING
	QueryPerformanceCounter(&t[5]);
	#endif
	memcpy(prev, pSrc, Y*stride); //remember current frame as previous for the next one
	if (saveBuffer.size() > 0)
		saveBuffer.resize(pDst - pDST);
	#ifdef TIMING
	QueryPerformanceCounter(&t[6]);

	double frq = perfreq.QuadPart;
	printf("CP: begin=%lf cmp_prev=%lf decideblocks=%lf bts=%lf blocks=%lf memcpy_prev=%lf",
		(t[1].QuadPart - t[0].QuadPart) / frq,
		(t[2].QuadPart - t[1].QuadPart) / frq,
		(t[3].QuadPart - t[2].QuadPart) / frq,
		(t[4].QuadPart - t[3].QuadPart) / frq,
		(t[5].QuadPart - t[4].QuadPart) / frq,
		(t[6].QuadPart - t[5].QuadPart) / frq
	);
	#endif
	return pDst - pDST;
}

//decompress RGB24 P-frame
template<class RC>
int CScreenCapt<RC>::DecompressP(BYTE *pSrc, int srcLength, BYTE *pDst)
{
	lprintf(logF, "DecompressP len=%d\n", srcLength);
	int x, c, n, y;
	if (srcLength >= 10) {
		lprintf(logF, "[");
		for(int i=0;i<10;i++)
			lprintf(logF, "%d ", pSrc[i]);
		lprintf(logF, "]\n");
	}
	x = *pSrc++;
	int changes = x & 1;
	//if the frame doesn't differ from previous, just copy and exit
	if (!changes) {
		memcpy(pDst, prev, Y*stride);
		return 1;
	}
	ec.decodeBegin(pSrc, srcLength);

	
	//decode first and last indices of blocks that differ
	int t,xx1,xx2;
	t = ec.decodeX(xxtab);
	xx1 = ec.decodeX(xxtab);
	xx1 = (xx1<<8)+t;
	t = ec.decodeX(xxtab);
	xx2 = ec.decodeX(xxtab);
	xx2 = (xx2<<8)+t;

	lprintf(logF, "xx1=%d xx2=%d\n",xx1,xx2);
	//decode block types
	memset(bts,0,nbx*nby);
	x = xx1;
	while(x<=xx2) {
		c = ec.decodeBT(bttab);
		n = ec.decodeBN(ntab2);
		for(int i=0; i<n; i++)
			bts[x++] = c;		
	}

	//decode blocks
	const int off = -stride-3;
	cx = cx1 = 0;
	uint bx,by;
	int lastmx=0, lastmy=0;
	for(by=0;by<nby;by++)
		for(bx=0;bx<nbx;bx++) {
			int y16 = by*16, x16 = bx*16; 
			int x1 = x16;
			int x2 = x16+16;
			int y1 = y16;
			int y2 = y16+16;
			if (x2>X) x2 = X;
			if (y2>Y) y2 = Y;				
			int bi = by*nbx+bx;

			if (bts[bi]) {
				lprintf(logF, "bts[%d]=%d\n", bi, bts[bi]);
				if ((bts[bi]-1)&1) {
					for(y=y1;y<y2;y++) {
						const int i = y*stride + x1*3;
						memcpy(&pDst[i], &prev[i], (x2-x1)*3);
					}

					x1 = ec.decodeSXY(sxytab[0]) + x16;
					y1 = ec.decodeSXY(sxytab[1]) + y16;
					x2 = ec.decodeSXY(sxytab[2]) + x16+1;
					y2 = ec.decodeSXY(sxytab[3]) + y16+1;
					
					assert(x1<x2 && y1<y2);
					lprintf(logF, "x1=%d y1=%d x2=%d y2=%d\n", x1,y1,x2,y2);
				}
				
				if ((bts[bi]-1)&2) { //motion vec
					int mx,my;
					if (RC::canEncodeBool) { //V3
						bool same = ec.decodeBool();
						if (same) {
							mx = lastmx; my = lastmy;
						} else {
							mx = ec.decodeMX(mvtab[0]) - msr_x;
							my = ec.decodeMY(mvtab[1]) - msr_y;
						}
					} else {//V2
						mx = ec.decodeMX(mvtab[0]) - msr_x;
						my = ec.decodeMY(mvtab[1]) - msr_y;
					}
					lastmx = mx; lastmy = my;
					lprintf(logF, "mx=%d my=%d\n", mx,my);
					for(y=y1;y<y2;y++) {
						const int i = y*stride + x1*3;
						const int j = (y+my)*stride + (x1 + mx)*3;
						memcpy(&pDst[i], &prev[j], (x2-x1)*3);
					}

				} else { //data
					x = x1; y = y1; 
					int ptype = 0, lastptype = 0;
					while(y<y2)  {
						int r,g,b, i = y*stride + x*3;
						lastptype = ptype;
						ptype = ec.decodeP(ptypetab[lastptype]);
						lprintf(logF, "decP (lastptype=%d) -> ptype=%d\n", lastptype, ptype);
						if (!ptype) {
							DecodeRGB(r,g,b);
							lprintf(logF, "rgb=%d %d %d\n", r,g,b);
						}
						
						n = ec.decodeN(ntab[ptype]);
						lprintf(logF, "decN n=%d ptype=%d\n", n, ptype);
						for(c=0; c<n; c++) {
							switch(ptype) {
								case 1: 
									r = pDst[i-3]; g = pDst[i-2]; b = pDst[i-1];
									break;
								case 2:
									r = pDst[i+off+3]; g = pDst[i+off+4]; b = pDst[i+off+5];
									break;
								case 3:
									r = prev[i]; g = prev[i+1]; b = prev[i+2];
									lprintf(logF, "prev[%d]=%d,%d,%d\n", i, r,g,b);
									break;
								case 4:
									r = (int)pDst[i-3] + (int)pDst[i+off+3] - (int)pDst[i+off];
									g = (int)pDst[i-2] + (int)pDst[i+off+4] - (int)pDst[i+off+1];
									b = (int)pDst[i-1] + (int)pDst[i+off+5] - (int)pDst[i+off+2];
									break;
								case 5:
									r = pDst[i+off]; g = pDst[i+off+1]; b = pDst[i+off+2];
									break;
							}

							pDst[i] = r;
							pDst[i+1] = g;
							pDst[i+2] = b;
							i+=3; x++;
							if (x>=x2) {
								x = x1;
								y++;
								i = y*stride + x*3;
							}
						}//for c<n
						cx = g>>SC_CXSHIFT;
						MAKECX1;
						cx = b>>SC_CXSHIFT;	
						lprintf(logF, "cx = %d cx1=%d\n", cx, cx1);
					}//while y<y2
				}
			} else { //bts[] = 0
				for(y=y1;y<y2;y++) {
					const int i = y*stride + x1*3;
					memcpy(&pDst[i], &prev[i], (x2-x1)*3);
				}
			}
		}//bx
	memcpy(prev,pDst,Y*stride);
	return 1;
}

//is whole frame filled with one color?
template<class RC>
BOOL CScreenCapt<RC>::IsFlat(BYTE *pSrc)
{
	BOOL res = FALSE;
	if (X & 3) 
		res = !memcmp(pSrc, &pSrc[bytespp], (X-1)*bytespp) && !memcmp(pSrc, &pSrc[stride], (Y-1)*stride);
	else
		res = !memcmp(pSrc, &pSrc[bytespp], X*Y*bytespp-bytespp);
	return res;
}

//template<class T> int GetSPVersion() { return 0; }

//template<> int GetSPVersion<RangeCoderC>() { return 1; }
//template<> int GetSPVersion<RangeCoderSub>() { return 2; }
//template<> int GetSPVersion<UseRC>() { return 2; }
//template<> int GetSPVersion<UseANS>() { return 3; }

//compress a frame
//works in any colorspace because calls virtual methods
template<class RC>
int CScreenCapt<RC>::CompressFrame(BYTE *pSrc, BYTE *pDst, int dstLength, int &ftype) //frame type 0-I, 1-P
{
	if (!pSquad) {
		SYSTEM_INFO info;
		GetSystemInfo(&info);
		pSquad = new CSquad(info.dwNumberOfProcessors);
		tls.resize(nby); 
		rowStates.resize(nby);
		#ifdef TIMING
		runCmdTimes.resize(pSquad->NumThreads());
		#endif
	}

	const int version = myVersion;// GetSPVersion<RC>();

	const int saved = saveBuffer.size();
	if (saved > 0) { // return previously saved data
		ftype = last_ftype;
		int real_size = saved + (last_ftype==0 ? 1 : 0);
		if (dstLength >= real_size) {
			if (last_ftype==0) {
				*pDst++ = 2 + (version-1)*16; 
			}
			memcpy(pDst, &saveBuffer[0], saved);
			saveBuffer.resize(0);
		} 
		return real_size; 
	}

	pDstEnd = pDst + dstLength - 32; // if pDst goes past this point, switch to larger buffer

	// if it's filled with one color, just mark so and store this color. It's an I-frame! 
	if (IsFlat(pSrc)) {
		last_ftype = ftype = 0;
		if (!(last_was_flat && 0==memcmp(pSrc, &last_flat_clr[0], bytespp))) {
			memcpy(prev,pSrc,Y*stride);
			RenewI();
			memcpy(&last_flat_clr[0], pSrc, bytespp);
		}
		*pDst++ = 1 + (version-1)*16;
		memcpy(pDst, pSrc, bytespp);
		last_was_flat = true;		
		return 1+bytespp;
	} else
		last_was_flat = false;
	
	int csz = 0;

	if (fn && ftype) { //if it's not first frame and we're asked to make a P-frame, compress it as P-frame
		last_ftype = ftype = 1; fn++;
		csz = CompressP(pSrc, pDst);
	} else { //otherwise compress as I-frame
		last_ftype = ftype = 0; fn++;		
		*pDst++ = 2 + (version-1)*16; 
		csz = CompressI(pSrc, pDst)+1;
	}

	if (csz <= dstLength && saveBuffer.size() > 0) { //switched to buffer but not really needed
		memcpy(pDst, &saveBuffer[0], csz);
		saveBuffer.resize(0);
	}
	return csz;
}

//decompress a frame
template<class RC>
int CScreenCapt<RC>::DecompressFrame(BYTE *pSrc, int srcLength, BYTE *pDst, int ftype)
{
	if (X & 3) {		
		const int pad = stride - X * bytespp;
		for(int y=0; y<Y; y++)
			memset(&pDst[y*stride+X*bytespp], 0, pad);
	}
	lprintf(logF, "DecompressFrame fn=%d len=%d\n", fn, srcLength);
	fn++;
	if (ftype)  {//P
		last_was_flat = false;
		return DecompressP(pSrc, srcLength, pDst);
	}
	// I
	int alg = (*pSrc++) & 0x0F;
	if (alg==1) {
		lprintf(logF, "alg==1 \n");
		for(int x=0;x<X;x++) 
			memcpy(&pDst[x*bytespp], pSrc, bytespp);

		for(int y=1;y<Y;y++)
			memcpy(&pDst[y*stride], pDst, bytespp*X);
			
		bool sameclr = 0==memcmp(&last_flat_clr[0], pSrc, bytespp);
		lprintf(logF, " last_was_flat=%d sameclr=%d\n", last_was_flat, sameclr);
		if (!(last_was_flat && 0==memcmp(&last_flat_clr[0], pSrc, bytespp))) {
			memcpy(prev, pDst, Y*stride);
			RenewI();
		}
		last_was_flat = true;
		memcpy(&last_flat_clr[0],pSrc, bytespp);
		return 1;
	} else
		last_was_flat = false;
	return DecompressI(pSrc, srcLength, pDst);
}
///////////////////////////////////////////////////////////////////////

ScreenCodec::ScreenCodec()
: pSC(NULL), rgb32(false), rgb16(false), bufsize(0), 
  X(0), Y(0), stride(0), crashed(false), last_loss(0)
{ }

void ScreenCodec::Init(CodecParameters *pParams)
{
	X = pParams->width; Y = pParams->height;
	bpp = pParams->bits_per_pixel / 8;
	stride = (pParams->width * bpp + 3) & (~3);
	memcpy(&params, pParams, sizeof(CodecParameters));
	rgb32 = pParams->bits_per_pixel==32;
	rgb16 = pParams->bits_per_pixel==16;
	last_loss = pParams->loss;

	redshift = 0; greenshift = 0; blueshift = 0;
	if (rgb16) {
		while(!((1<<redshift) & params.redmask))
			redshift++;
		while(!((1<<greenshift) & params.greenmask))
			greenshift++;
		while(!((1<<blueshift) & params.bluemask))
			blueshift++;
	}
}

//init pSC, params must be filled in. version: 1 for old RC, 2 for RCSub
void ScreenCodec::CreateCodec(int version) 
{
	if (version < 2 || version > 4)
		throw BadVersionException(version);
	// CreateCodec is called from (De)CompressFrame, after Init, so we know stride here
	const int stride24 = (X * 3 + 3) & (~3);
	switch(params.bits_per_pixel) {
	case 16:
		rgb16 = true;
		bufsize = stride24 * Y; 
		rgb_buffer.resize(bufsize, 0);
		params.bits_per_pixel = 24;
		break;
	case 32:
		rgb32 = true;
		bufsize = stride24 * Y;
		rgb_buffer.resize(bufsize, 0);
		params.bits_per_pixel = 24;
		break;
	case 24:	break; // nothing special to do
	default:
		printf("Incorrect bits_per_pixel value!\n");
		throw BadVersionException(48);
	}
	switch(version) {
		case 2: pSC = new CScreenCapt<UseRC>(version); break;
		case 3: pSC = new CScreenCapt<UseANS>(version); pSC->setCx6f0(64); break;
		case 4: pSC = new CScreenCapt<UseANS>(version); pSC->setCx6f0(32); break;
	}
	pSC->Init(&params);
}

void ScreenCodec::Deinit()
{
	if (crashed) return;
	if (pSC) {
		pSC->Deinit();
		delete pSC;
		pSC = NULL;
	}
	rgb_buffer.clear();
	rgb32 = false; rgb16 = false;
}

//convert from RGB32 if necessary and call the compressor
int ScreenCodec::CompressFrame(BYTE *pSrc, BYTE *pDst, int dstLength, int &ftype, int loss) //frame type 0-I, 1-P
{
	if (crashed) return 0;
	if (loss != last_loss) {
		pSC->SetupLossMask(loss);
		last_loss = loss;
	}

	#ifdef TIMING
	LARGE_INTEGER t0, t1, t2, t3;
	QueryPerformanceFrequency(&t0);
	double freq = t0.QuadPart;
	QueryPerformanceCounter(&t0);
	#endif
	if (!pSC) {
		CreateCodec(4);
	}
	#ifdef TIMING
	QueryPerformanceCounter(&t1);
	#endif
	if (rgb32) {	
		const int stride24 = (X * 3 + 3) & (~3);
		for(uint y=0;y<Y; y++) {
			uint i = y*X*4, j = y*stride24;
			for(int x=0; x < X; x++) {
				rgb_buffer[j]   = pSrc[i];
				rgb_buffer[j+1] = pSrc[i+1];
				rgb_buffer[j+2] = pSrc[i+2];
				i+=4; j+=3;
			}
		}
		pSrc = &rgb_buffer[0];
	}
	if (rgb16) {
		const int stride24 = (X * 3 + 3) & (~3);
		for(uint y=0;y<Y; y++) {
			uint i = y*X*2, j = y*stride24;
			for(int x=0; x < X; x++) {
				const WORD w = *((WORD*)&pSrc[i]);
				rgb_buffer[j]   = (w & params.redmask) >> redshift;
				rgb_buffer[j+1] = (w & params.greenmask) >> greenshift;
				rgb_buffer[j+2] = (w & params.bluemask) >> blueshift;
				i+=2; j+=3;
			}
		}
		pSrc = &rgb_buffer[0];
	}
	#ifdef TIMING
	QueryPerformanceCounter(&t2);
	#endif
	auto ret = pSC->CompressFrame(pSrc, pDst, dstLength, ftype);
	#ifdef TIMING
	QueryPerformanceCounter(&t3);
	printf(" CpFr: Create=%lf rgb24=%lf CF=%lf ", 
		(t1.QuadPart - t0.QuadPart) / freq,
		(t2.QuadPart - t1.QuadPart) / freq,
		(t3.QuadPart - t2.QuadPart) / freq
	);
	#endif
	return ret;
}

// call the decompressor and convert to RGB32 if necessary
int ScreenCodec::DecompressFrame(BYTE *pSrc, int srcLength, BYTE *pDst, int pitch, int ftype)
{
	if (crashed && ftype > 0) return 0;
	if (!pSC) {
		if (ftype > 0) return 0; //P frame before any I
		int version = (pSrc[0] >> 4) + 1;
		CreateCodec(version);
	}

	bool useBuffer = rgb32 || rgb16;
	if (pitch != stride) {
		rgb_buffer.resize(stride*Y, 0);
		useBuffer = true;
	}
	
	crashed = false;
	if (useBuffer) {
		int ret = pSC->DecompressFrame(pSrc, srcLength, &rgb_buffer[0], ftype);
		if (bpp == 4) {
			const int stride24 = (X * 3 + 3) & (~3);
			for(uint y=0;y<Y; y++) {
				uint i=y*pitch, j=y*stride24;
				for(int x=0; x< X; x++)	{
					pDst[i]   = rgb_buffer[j];
					pDst[i+1] = rgb_buffer[j+1];
					pDst[i+2] = rgb_buffer[j+2];
					pDst[i+3] = 255;
					i+=4, j+=3;
				}
			}
		} else
		if (bpp==2) {
			const int stride24 = (X * 3 + 3) & (~3);
			for(uint y=0;y<Y; y++) {
				uint i=y*pitch, j=y*stride24;
				for(int x=0; x< X; x++)	{
					*((WORD*)&pDst[i]) = (rgb_buffer[j]<<redshift) + (rgb_buffer[j+1]<<greenshift) + (rgb_buffer[j+2]<<blueshift);
					i+=2, j+=3;
				}
			}
		} else {
			for(uint y=0;y<Y;y++)
				memcpy(&pDst[y*pitch], &rgb_buffer[y*stride], X*bpp);
		}
		return ret;
	} else {
		return pSC->DecompressFrame(pSrc, srcLength, pDst, ftype);
	}
}
