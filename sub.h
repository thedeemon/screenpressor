//---------------------------------------------------------------------------
//  Part of ScreenPressor lossless video codec
//  (C) Infognition Co. Ltd.
//---------------------------------------------------------------------------

// Header for range coder class which performs arithmetic coding.
// This range coder is used in version 2.0.

#ifndef _SUBB_
#define _SUBB_

#include <stdexcept>
typedef unsigned int uint;

#define TOP (1<<24)

#define TOP_C         (1<<24)
#define BOT_C         (1<<16)

class RangeCoderSub {
	uint code, range, FFNum, Cache;
public:
	__int64 low; 
	BYTE* inputEnd;
	
	void EncodeBegin() {
		/*low=*/FFNum=Cache=0; range=(uint)-1;		
	}

	BYTE* DecodeBegin(BYTE* pSrc, int inputLength) 
	{
		if (pSrc == NULL)
			throw std::invalid_argument("pSrc is NULL!");
		if (inputLength < 5)
			throw std::length_error("inputLength too small");
		code=0;
		range=(uint)-1;
		inputEnd = pSrc + inputLength;
		for (int i=0; i<5; i++) 
			code=(code<<8) | *pSrc++;
		return pSrc;
	}

	BYTE* EncodeEnd(BYTE* pDst);
	void DecodeEnd() {	}

	BYTE* Encode(uint cumFreq, uint freq, uint totFreq, BYTE*pDst); 
	BYTE* ShiftLow(BYTE* pDst); 
	uint GetFreq (uint totFreq);
	BYTE* Decode(uint cumFreq, uint freq, uint totFreq, BYTE* pSrc);

	////////////////////////////////////////////////////////
	BYTE* EncodeVal(int c, uint *cnt, uint &totfr, uint maxc, uint step, BYTE *pDst);
	BYTE* DecodeVal(int &c, uint *cnt, uint &totfr, uint maxc, uint step, BYTE *pSrc);
	BYTE* EncodeValUni(int c, uint *cnt, uint &totfr, uint step, BYTE *pDst);
	BYTE* DecodeValUni(int &c, uint *cnt, uint &totfr, uint step, BYTE *pSrc);

};//class

#endif