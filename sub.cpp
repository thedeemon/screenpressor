//---------------------------------------------------------------------------
//  Part of ScreenPressor lossless video codec
//  (C) Infognition Co. Ltd.
//---------------------------------------------------------------------------
// Implementation of new range coder class which performs arithmetic coding.
// This range coder is used in version 2.0.
// see http://en.wikipedia.org/wiki/Range_encoding
//#include "stdafx.h"
#include "defines.h"
#include "sub.h"
#include <assert.h>

BYTE* RangeCoderSub::EncodeEnd(BYTE* pDst) 
{
		low+=1;
		for (int i=0; i<5; i++) 
			pDst = ShiftLow(pDst);
		return pDst;
}

BYTE* RangeCoderSub::Encode(uint cumFreq, uint freq, uint totFreq, BYTE*pDst) 
{
		low += cumFreq * (range/= totFreq);
		range*= freq;
		while( range<TOP ) pDst=ShiftLow(pDst), range<<=8;
		return pDst;
}

BYTE* RangeCoderSub::ShiftLow(BYTE* pDst) 
{
		if ( (low>>24)!=0xFF ) {
			*pDst++ = Cache + (low>>32);
			int c = 0xFF+(low>>32);
			while ( FFNum ) *pDst++ = c, FFNum--;
			Cache = uint(low)>>24;
		} else 
			FFNum++;
		low = uint(low)<<8;
		return pDst;
}

uint RangeCoderSub::GetFreq (uint totFreq) 
{
		return code / (range/= totFreq);
}

BYTE* RangeCoderSub::Decode(uint cumFreq, uint freq, uint totFreq, BYTE* pSrc) 
{
		code -= cumFreq*range;
		range *= freq;
		while (range < TOP) { 
			if (pSrc >= inputEnd) 
				throw std::length_error("RangeCoderSub::Decode: input buffer exhausted");
			code = (code<<8) | *pSrc++;
			range <<= 8;
		}
		return pSrc;
}

//encode a value from a known range and update stats table, renormalizing stats if necessary
BYTE* RangeCoderSub::EncodeVal(int c, uint *cnt, uint &totfr, uint maxc, uint step, BYTE *pDst)
{
	uint cumfr=0,i;

	assert((c>=0) && (c<maxc) && (totfr>0));

	for(i=0;i<c;i++)
		cumfr += cnt[i];
	pDst = Encode(cumfr, cnt[c], totfr, pDst);
	cnt[c] += step;
	totfr += step;
	if (totfr>BOT_C) {
		totfr = 0;
		for(i=0;i<maxc;i++) {
			cnt[i] = (cnt[i]>>1)+1;
			totfr += cnt[i];
		}
	}
	return pDst;
}

//decode a value from a known range and update stats table, renormalizing stats if necessary
BYTE* RangeCoderSub::DecodeVal(int &c, uint *cnt, uint &totfr, uint maxc, uint step, BYTE *pSrc)
{
	uint value, cumfr, i;
	
	value = GetFreq(totfr);
	for(cumfr=0,c=0; c<maxc; c++ ) {																					
		if (value >= cumfr + cnt[c])													
			cumfr += cnt[c];														
		else																			
	        break;																		
	}																					
	pSrc = Decode(cumfr, cnt[c], totfr, pSrc);

	cnt[c] += step;
	totfr += step;
	if (totfr>BOT_C) {
		totfr = 0;
		for(i=0;i<maxc;i++) {
			cnt[i] = (cnt[i]>>1)+1;
			totfr += cnt[i];
		}
	}
	return pSrc;
}

//encode a value from range 0..255 with close to uniform distribution
BYTE* RangeCoderSub::EncodeValUni(int c, uint *cnt, uint &totfr, uint step, BYTE *pDst)
{
	uint cumfr=0,i,x;
	const int maxc = 256;

	for(x=0;x<c/16;x++)
		cumfr += cnt[256+x];
	for(i=x*16;i<c;i++)
		cumfr += cnt[i];

	pDst = Encode(cumfr, cnt[c], totfr, pDst);
	cnt[c] += step;
	cnt[256+x] += step;
	totfr += step;
	if (totfr>BOT_C) {
		totfr = 0;
		for(i=0;i<maxc;i++) {
			cnt[i] = (cnt[i]>>1)+1;
			totfr += cnt[i];
		}
		for(i=0;i<16;i++) {
			cnt[256+i] = 0;
			for(int j=0; j<16;j++)
				cnt[256+i] += cnt[i*16+j];
		}
	}
	return pDst;
}

//decode a value from range 0..255 with close to uniform distribution
BYTE* RangeCoderSub::DecodeValUni(int &c, uint *cnt, uint &totfr, uint step, BYTE *pSrc)
{
	uint value, cumfr, i;
	const int maxc = 256;
	
	value = GetFreq(totfr);

	int x=0;
	for(cumfr=0; x<16; x++ ) {
		if (value >= cumfr + cnt[256 + x])
			cumfr += cnt[256 + x];
		else																			
	        break;																		
	}																					

	c = x * 16;
	for(; c<maxc; c++ ) {																					
		if (value >= cumfr + cnt[c])													
			cumfr += cnt[c];														
		else																			
	        break;																		
	}																					
	pSrc = Decode(cumfr, cnt[c], totfr, pSrc);
	cnt[c] += step;
	cnt[256+x] += step;
	totfr += step;
	if (totfr>BOT_C) {
		totfr = 0;
		for(i=0;i<maxc;i++) {
			cnt[i] = (cnt[i]>>1)+1;
			totfr += cnt[i];
		}
		for(i=0;i<16;i++) {
			cnt[256+i] = 0;
			for(int j=0; j<16;j++)
				cnt[256+i] += cnt[i*16+j];
		}
	}
	return pSrc;
}