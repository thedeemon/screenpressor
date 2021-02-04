//---------------------------------------------------------------------------
//  Part of ScreenPressor lossless video codec
//  (C) Infognition Co. Ltd.
//---------------------------------------------------------------------------
// Headers for ScreenCodec, CScreenCapt classes.

// CScreenCapt performs compression and decompression in RGB24 format 
// using Range Coder or ANS Coder
// ScreenCodec calls one of these versions
// and performs RGB32 <-> RGB24 <-> RGB16 conversion when necessary.

#ifndef SCREENCAPH
#define SCREENCAPH

#include <vector>
#include "squad.h"
#include "sub.h"
#include "logging.h"
#include "ans_contexts.h"
#include "ransmt.h"

#define NOPROTECT

#ifndef NOPROTECT
#include "vm.h"
#endif

// Some constants for compression algorithm.
// They determine speed of updating statistics for different kinds of data 
// and stats table sizes.
// Two codecs where at least one of these numbers differs become binary incompatible.

#define SC_STEP 400  
#define SC_NSTEP 400 
#define SC_CXSHIFT 2
#define MAKECX1 cx1 = (cx<<6)&0xFC0;

#define SC_CXMAX 4096
#define SC_BTSTEP 10 
#define SC_NCXMAX 6
#define SC_BTNSTEP 20
#define SC_SXYSTEP 100 
#define SC_MSTEP 100 
#define SC_UNSTEP 1000 
#define SC_XXSTEP 1

//#define TIMING

struct CodecParameters {
	uint width, height; //image size
	BYTE bits_per_pixel; //16, 24 or 32
	WORD redmask, greenmask, bluemask; // color masks for 16 bit mode, like 0x7C00, 0x3E0, 0x1F
	uint high_range_x, high_range_y, low_range_x, low_range_y; //motion search range, like 256,256, 8,8
	uint loss; // in bits (0..5)
};

//bounds of changed blocks in a part of frame processed by one worker
struct BlockRegion {
	int bx1, bx2, by1, by2;
	BlockRegion() : bx1(-1), bx2(-1), by1(-1), by2(-1) {};
};

struct DecideBlocksParams {
	BYTE* pSrc;
	std::vector<BlockRegion> regions;

	DecideBlocksParams(BYTE *pSrc_, int nThreads) : pSrc(pSrc_)	{
		regions.resize(nThreads);
	}
};

struct PrevCmpParams {
	BYTE *pSrc;
	std::vector<int> results;

	PrevCmpParams(BYTE *pSrc_, int nThreads) : pSrc(pSrc_)
	{
		results.resize(nThreads);
	}
};

struct WorkerData { // thread-local data for worker threads
	int rleStartPos, rleSize; // slice in rleData
};

class BadVersionException {
public:
	BadVersionException(int v) : version(v) { }
	int version;
};

//common interface for different versions of the codec
class IScreenCapt {
public:
	virtual void Init(CodecParameters *pParams)=0; 
	virtual void Deinit()=0;
	virtual int CompressFrame(BYTE *pSrc, BYTE *pDst, int dstLength, int &ftype)=0; 
	virtual int DecompressFrame(BYTE *pSrc, int srcLength, BYTE *pDst, int ftype)=0;
	virtual ~IScreenCapt() {};
	virtual void SetupLossMask(int loss)=0;
	virtual void setCx6f0(int f0)=0;
};

// strategy for using range coder and its tables, this is compatible with v2
struct UseRC {
	BYTE *pDst; // when decoding pDst is used as pSrc
	RangeCoderSub rc;
	uint msr_x, msr_y;
	int f0val; // for Cx6, not used here

	void encodeBegin(BYTE *pDest) {
		pDst = pDest;
#ifdef NOPROTECT
		rc.low = 0;
#endif
		rc.EncodeBegin();
	}

	BYTE* encodeEnd() {
		return pDst = rc.EncodeEnd(pDst);
	}
	
	void decodeBegin(BYTE* pSrc, int srcLength) {
		pDst = rc.DecodeBegin(pSrc, srcLength);
	}

#ifndef NOPROTECT
	void* lowPtr() { return &rc.low; }
	int vmAction() { return 2; 	} //write 2 zeroes (64 bits in total)
#endif
	void setMotionRange(uint msrX, uint msrY) { msr_x = msrX; msr_y = msrY; }

	void stop() {}

	typedef uint* CtxN;
	static const bool CtxNalloc = true; //need to call createN?
	void encodeN(int n, CtxN &ntab) {
		pDst = rc.EncodeVal(n, ntab, ntab[256], 256, SC_NSTEP, pDst);
	}
	int decodeN(CtxN& ntab) {
		int n;
		pDst = rc.DecodeVal(n, ntab, ntab[256], 256, SC_NSTEP, pDst);
		return n;
	}
	CtxN createN() { return (uint*)calloc(257,  sizeof(uint)); }
	void freeN(CtxN &ntab) { free(ntab); }
	void renewN(CtxN &ntab) {
		for(int i=0;i<256;i++) ntab[i] = 1;
		ntab[256] = 256;
	}

	template<int L> struct FixedTab {
		uint tab[L];

		void renew() {
			for(int i=0;i<L-1;i++) tab[i] = 1;
			tab[L-1] = L-1;
		}
	};

	typedef FixedTab<7> CtxP;
	void encodeP(int ptype, CtxP& ptab) {
		pDst = rc.EncodeVal(ptype, ptab.tab, ptab.tab[6], 6, SC_UNSTEP, pDst);
	}
	int decodeP(CtxP& ptab) {
		int ptype;
		pDst = rc.DecodeVal(ptype, ptab.tab, ptab.tab[6], 6, SC_UNSTEP, pDst);
		return ptype;
	}
	void renewP(CtxP &ptab) { ptab.renew(); }

	typedef uint* CtxC; //color: r, g or b
	void encodeC(int c, CtxC& cntab) {
		pDst = rc.EncodeValUni(c, cntab, cntab[256+16], SC_STEP, pDst);
	}
	int decodeC(CtxC& cntab) {
		int c;
		pDst = rc.DecodeValUni(c, cntab, cntab[256+16], SC_STEP, pDst);
		return c;
	}
	CtxC createC() { return (uint*)calloc(256+16+1, sizeof(uint)); }
	void freeC(CtxC &cntab) { free(cntab); }
	void renewC(CtxC &cntab) {
		for(int n=0;n<256;n++)
			cntab[n] = 1;
		for(int n=0;n<16;n++)
			cntab[256+n] = 16;
		cntab[256+16] = 256;//totfr
	}

	typedef FixedTab<257> CtxX;
	void encodeX(int xx, CtxX& xxtab) {
		pDst = rc.EncodeVal(xx, xxtab.tab, xxtab.tab[256], 256, SC_XXSTEP,pDst);
	}
	int decodeX(CtxX& xxtab) {
		int xx;
		pDst = rc.DecodeVal(xx, xxtab.tab, xxtab.tab[256], 256, SC_XXSTEP,pDst);
		return xx;
	}
	void renewX(CtxX &xxtab) { xxtab.renew(); }

	typedef FixedTab<257> CtxBN;
	void encodeBN(int n, CtxBN& ntab2) {
		pDst = rc.EncodeVal(n, ntab2.tab, ntab2.tab[256], 256, SC_BTNSTEP, pDst);
	}
	int decodeBN(CtxBN& ntab2) {
		int n;
		pDst = rc.DecodeVal(n, ntab2.tab, ntab2.tab[256], 256, SC_BTNSTEP, pDst);
		return n;
	}
	void renewBN(CtxBN &ntab2) { ntab2.renew(); }

	typedef FixedTab<6> CtxBT;
	void encodeBT(int bt, CtxBT& bttab) {
		pDst = rc.EncodeVal(bt, bttab.tab, bttab.tab[5], 5, SC_BTSTEP, pDst);
	}
	int decodeBT(CtxBT& bttab) {
		int bt;
		pDst = rc.DecodeVal(bt, bttab.tab, bttab.tab[5], 5, SC_BTSTEP, pDst);
		return bt;
	}
	void renewBT(CtxBT& bttab) { bttab.renew(); }

	typedef FixedTab<17> CtxSXY;
	void encodeSXY(int x, CtxSXY& sxytab) {
		pDst = rc.EncodeVal(x, sxytab.tab, sxytab.tab[16], 16, SC_SXYSTEP, pDst);
	}
	int decodeSXY(CtxSXY& sxytab) {
		int x;
		pDst = rc.DecodeVal(x, sxytab.tab, sxytab.tab[16], 16, SC_SXYSTEP, pDst);
		return x;
	}
	void renewSXY(CtxSXY& sxytab) { sxytab.renew(); }

	typedef uint* CtxM;
	static const bool CtxMalloc = true; //need to call createM*?
	void encodeMX(int x, CtxM& mvtab) {
		pDst = rc.EncodeVal(x, mvtab, mvtab[msr_x*2], msr_x*2, SC_MSTEP, pDst);
	}
	int decodeMX(CtxM& mvtab) {
		int x;
		pDst = rc.DecodeVal(x, mvtab, mvtab[msr_x*2], msr_x*2, SC_MSTEP, pDst);
		return x;
	}
	void encodeMY(int x, CtxM& mvtab) {
		pDst = rc.EncodeVal(x, mvtab, mvtab[msr_y*2], msr_y*2, SC_MSTEP, pDst);
	}
	int decodeMY(CtxM& mvtab) {
		int x;
		pDst = rc.DecodeVal(x, mvtab, mvtab[msr_y*2], msr_y*2, SC_MSTEP, pDst);
		return x;
	}
	CtxM createMX() { return (uint*) calloc((msr_x * 2 + 1) , sizeof(uint)); }
	CtxM createMY() { return (uint*) calloc((msr_y * 2 + 1) , sizeof(uint)); }
	void freeM(CtxM &mvtab) { free(mvtab); }
	void renewM(CtxM &mvtab, bool xdimension) {
		const int mx = xdimension ? msr_x*2 : msr_y*2;
		for(int i=0;i<mx;i++) mvtab[i] = 1;
		mvtab[mx] = mx;
	}

	static const bool canEncodeBool = false;
	void encodeBool(bool flag) { }
	bool decodeBool() {return false; }
};

extern void SetThreadLocalInt(int v);

//strategy for using ANS entropy coder and context tables, this is v3
struct UseANS {
	BYTE *pDst; // when decoding pDst is used as pSrc
	RansMTCoder rmtc;
	RansState ransDec; //for decoding
	int nDec;
	bool decoding;
	int f0val; // for Cx6

	UseANS() : decoding(true) {} //init just in case we call renew before decodeBegin

	void stop() { rmtc.stop(); } //stop the thread

	void encodeBegin(BYTE *pDest) {
		pDst = pDest;
		decoding = false;
#ifdef NOPROTECT
		rmtc.ransInitState = RANS_BYTE_L;
#endif
		rmtc.start(pDest);

		SetThreadLocalInt(f0val);
	}
	BYTE* encodeEnd() {
		return pDst = rmtc.finish();
	}
	void decodeBegin(BYTE* pSrc, int srcLength) {
		pDst = pSrc;
		decoding = true;
		nDec = 0;
		RansDecInit(&ransDec, &pDst);
		SetThreadLocalInt(f0val);
	}

	#ifndef NOPROTECT
	void* lowPtr() { return &rmtc.ransInitState; }
	int vmAction() { return 3; } //init with RANS_BYTE_L
    #endif
	void setMotionRange(uint msrX, uint msrY) {} //in v3 motion range is 128 now

	//types: CtxC CtxN CtxBN CtxBT CtxSXY CtxM CtxP CtxX 
	typedef Context CtxC;
	void encodeC(int c, CtxC& cntab) {
		Freq fr;
		if (!cntab.encode(c, fr)) { //false => bypass
			fr.freq = 0; fr.cumFreq = c;
		} 
		rmtc.put(fr);
	}
	int decodeC(CtxC& cntab) {
		Freq fr;
		BYTE c;
		if (cntab.decode( RansDecGet(&ransDec, PROB_BITS), c, fr))  {
			RansDecAdvance(&ransDec, &pDst, fr.cumFreq, fr.freq, PROB_BITS);
		} else {
			c = *pDst++;
			cntab.update(c);
		}		
		nDec++;
		if (nDec==RansMTCoder::B) {
			RansDecInit(&ransDec, &pDst);
			nDec = 0;
		}
		return c;
	}

	CtxC createC() { Context c; return c; }	
	void freeC(CtxC &cntab) { cntab.free(); }
	void renewC(CtxC &cntab) { cntab.renew(); }

	template<int NSym>
	void encodeF(int n, FixedSizeRansCtx<NSym> &cx) {
		Freq fr;
		cx.encode(n, fr);
		rmtc.put(fr);
	}

	template<int NSym>
	int decodeF(FixedSizeRansCtx<NSym> &cx) {
		Freq fr; 
		int c = cx.decode(RansDecGet(&ransDec, PROB_BITS), fr);
		assert(c >= 0);
		assert(c < NSym);
		RansDecAdvance(&ransDec, &pDst, fr.cumFreq, fr.freq, PROB_BITS);
		nDec++;
		if (nDec==RansMTCoder::B) {
			RansDecInit(&ransDec, &pDst);
			nDec = 0;
		}
		return c;
	}

	typedef FixedSizeRansCtx<256> CtxN;
	static const bool CtxNalloc = false; //need to call createN?

	void encodeN(int n, CtxN &ntab) { encodeF(n, ntab);	}
	int decodeN(CtxN& ntab) { return decodeF(ntab); }
	CtxN createN() { CtxN c; assert(0 && "should not be called"); return c; }
	void freeN(CtxN &ntab) {}
	void renewN(CtxN &ntab) { ntab.renew(decoding); }
	
	typedef FixedSizeRansCtx<6> CtxP;
	void encodeP(int ptype, CtxP& ptab) { encodeF(ptype, ptab); }
	int decodeP(CtxP& ptab) { return decodeF(ptab); }
	void renewP(CtxP &ptab) { ptab.renew(decoding); }

	typedef FixedSizeRansCtx<256> CtxX;
	void encodeX(int xx, CtxX& xxtab) { encodeF(xx, xxtab); }
	int decodeX(CtxX& xxtab) { return decodeF(xxtab); }
	void renewX(CtxX &xxtab) { xxtab.renew(decoding); }

	typedef FixedSizeRansCtx<256> CtxBN;
	void encodeBN(int n, CtxBN& ntab2) { encodeF(n, ntab2); }
	int decodeBN(CtxBN& ntab2) { return decodeF(ntab2); }
	void renewBN(CtxBN &ntab2) { ntab2.renew(decoding); }

	typedef FixedSizeRansCtx<5> CtxBT;
	void encodeBT(int bt, CtxBT& bttab) { encodeF(bt, bttab); }
	int decodeBT(CtxBT& bttab) { return decodeF(bttab); }
	void renewBT(CtxBT& bttab) { bttab.renew(decoding); }

	typedef FixedSizeRansCtx<16> CtxSXY;
	void encodeSXY(int x, CtxSXY& sxytab) { encodeF(x, sxytab); }
	int decodeSXY(CtxSXY& sxytab) { return decodeF(sxytab); }
	void renewSXY(CtxSXY& sxytab) { sxytab.renew(decoding); }

	static const bool CtxMalloc = false; //need to call createMX/MY?
	typedef FixedSizeRansCtx<512> CtxM;
	void encodeMX(int x, CtxM& mvtab) { encodeF(x, mvtab); }
	int decodeMX(CtxM& mvtab) { return decodeF(mvtab); }
	void encodeMY(int x, CtxM& mvtab) { encodeF(x, mvtab); }
	int decodeMY(CtxM& mvtab) { return decodeF(mvtab); }
	CtxM createMX() { CtxM c; assert(0 && "Should not be called"); return c; }
	CtxM createMY() { CtxM c; assert(0 && "Should not be called"); return c; }
	void freeM(CtxM &mvtab) {}
	void renewM(CtxM &mvtab, bool xdimension) { mvtab.renew(decoding); }

	static const bool canEncodeBool = true;
	void encodeBool(bool flag) { // P=0.5
		Freq fr = { PROB_SCALE/2, (flag ? PROB_SCALE/2 : 0)};
		rmtc.put(fr);
	}
	bool decodeBool() {
		auto f = RansDecGet(&ransDec, PROB_BITS);
		bool flag = f >= PROB_SCALE/2;
		RansDecAdvance(&ransDec, &pDst, (flag ? PROB_SCALE/2 : 0) , PROB_SCALE/2, PROB_BITS);
		nDec++;
		if (nDec==RansMTCoder::B) {
			RansDecInit(&ransDec, &pDst);
			nDec = 0;
		}
		return flag;
	}
};

//state of a row of 16x16 blocks during processing, used for work stealing between threads 
enum RowState { Untouched, Processing, Done };

// RGB24 codec parameterized by entropy coder 
template<class RC>
class CScreenCapt : public IScreenCapt, public ISquadJob {
protected:
	RC ec; //entropy coder: range coder for v2, ANS coder for v3

	bool init; // is data initialized?

	//statistics tables for different kinds of data
	typename RC::CtxC cntab[3][SC_CXMAX]; //colors
	typename RC::CtxN ntab[SC_NCXMAX]; //numbers of repetitions in RLE
	typename RC::CtxBN ntab2; //RLE lengths for block types
	typename RC::CtxBT bttab; //block types counters tab
	typename RC::CtxSXY sxytab[4]; //changed block paddings
	typename RC::CtxM mvtab[2]; //motion vectors table
	typename RC::CtxP ptypetab[6]; //pixel type, context = previous value
	typename RC::CtxX xxtab; //changed blocks indices

	BYTE *prev;
	int X,Y, stride;
	uint cx, cx1, nbx,nby, fn;
	BYTE *bts; //block types
	int *sxy[4]; //sx1, sy1, sx2, sy2 for each block
	int *mvs[2]; //motion vectors
	static const uint bytespp = 3; //bytes per pixel: 2 or 3
	uint msr_x, msr_y, msrlow_x, msrlow_y; //motion search ranges 
	CSquad *pSquad;
#ifndef NOPROTECT
	LVM2 vm;
#endif
	
	int loss_mask, corr_mask; //4 bytes in all modes
	bool last_was_flat;
	BYTE last_flat_clr[4];
	BYTE *pDstEnd;
	std::vector<BYTE> saveBuffer;
	int last_ftype;	

	std::vector<WorkerData> tls; // with work stealing this must have nby entries
	std::vector<BYTE> rleData;

	CRITICAL_SECTION rowsCritSec;
	std::vector<RowState> rowStates;

	int myVersion;

#ifdef TIMING
	LARGE_INTEGER perfreq; //for speed testing
	std::vector<double> runCmdTimes;
#endif
	bool FindMV(BYTE *pSrc, int bi, int &last_mvx, int &last_mvy, int upperBI); //find motion vector
	bool SameBlocks(BYTE *pSrc, int i, int ip, int width_bytes, int height);
	BOOL IsFlat(BYTE *pSrc); //is image filled with one color?

	//compress/decompress one I/P frame
	virtual int CompressI(BYTE *pSrc, BYTE *pDST);
	virtual int DecompressI(BYTE *pSrc, int srcLength, BYTE *pDst);
	virtual int CompressP(BYTE *pSrc, BYTE *pDST);
	virtual int DecompressP(BYTE *pSrc, int srcLength, BYTE *pDST);

	void RenewI(); //reinit stats for compressing/decompressing I-frame
	void DoLoss(BYTE *pSrc, PrevCmpParams* pcparams);

	//pixel encoding / decoding
	void EncodeRGB(BYTE *pSrc);
	void DecodeRGB(int &r, int &g, int &b);
	int GetPixelType(BYTE* pSrc, BYTE* pSrclast, const int off);
	bool PixelTypeFits(int ptype, BYTE *pSrc, BYTE* pSrclast, const int off);
	int GetPixelTypeP(BYTE* pSrc, BYTE* pr, const int off);
	bool PixelTypeFitsP(int ptype, BYTE *pSrc, BYTE* pr, BYTE* pSrclast, const int off);
	int GetPixelTypeP0(BYTE* pSrc, BYTE* pr);
	bool PixelTypeFitsP0(int ptype, BYTE *pSrc, BYTE* pr, BYTE* pSrclast);
	void WritePixel(int ptype, int lastptype, BYTE* pSrc);

	void ClassifyPixelsI(int myNum, int y0, int ysize, BYTE *pSrc);
	void DecideBlockTypes(int by_start, int by_size, BYTE *pSrc, BlockRegion &rgn, int myNum);
	virtual void RunCommand(int command, void *params, CSquadWorker *sqworker);

	void CheckDstLength(BYTE **ppDst, BYTE **ppDstStart);

public:
	CScreenCapt(int ver);
	~CScreenCapt();
	virtual void Init(CodecParameters *pParams); 
	virtual void Deinit();
	virtual int CompressFrame(BYTE *pSrc, BYTE *pDst, int dstLength, int &ftype); //frame type 0-I, 1-P
	virtual int DecompressFrame(BYTE *pSrc, int srcLength, BYTE *pDst, int ftype);
	virtual void SetupLossMask(int loss);
	virtual void setCx6f0(int f0);
};

//instance of a codec
class ScreenCodec {
	IScreenCapt *pSC;
	bool rgb32; // are we working with RGB32?
	bool rgb16;
	std::vector<BYTE> rgb_buffer; // buffer for RGB16 <-> RGB24 <-> RGB32 conversion
	uint bufsize;
	uint X,Y, stride, bpp; //bpp = 2,3 or 4
	CodecParameters params;
	bool crashed;
	int redshift, greenshift, blueshift;
	int last_loss;

	void CreateCodec(int version); //init pSC, params must be filled in. version: 1 was for old RC, 2 for RCSub, 3 for ANS

public:
	ScreenCodec();
	~ScreenCodec() { Deinit(); }
	void Init(CodecParameters *pParams); 
	void Deinit();
	int CompressFrame(BYTE *pSrc, BYTE *pDst, int dstLength, int &ftype, int loss); //frame type 0-I, 1-P
	int DecompressFrame(BYTE *pSrc, int srcLength, BYTE *pDst, int pitch, int ftype);
	void CrashHappened() { crashed = true; }
};

#endif