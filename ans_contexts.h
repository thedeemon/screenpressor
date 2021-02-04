#ifndef ANS_CONTEXTS_H
#define ANS_CONTEXTS_H

#include <vector>
#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include "defines.h"
#include "logging.h"

/*
Here we define several kinds of "contexts", statistical models that record how many times
each symbol was seen in given context and assign intervals [a,b) to each symbol
such that 0 <= a < b <= PROB_SCALE .
These intervals are used by rANS entropy coder such that the larger the interval
the less bits are used to encode it, so ideally interval size must correspond
to symbol's probability.
These context models are mostly used for RGB data, so symbols are 0..255.
As usual in PPM methods, context is chosen by adjacent data. 

The core interface of a context class is a pair of methods:
	bool encode(BYTE c, Freq &interval); 
    bool decode(int someFreq, BYTE &c, Freq & interval);  

When encoding, we must be able to give it a symbol and a context model
would give us either an interval for entropy coder or an indication that
it has no valuable stats yet and we should store the symbol uncompressed.
After a symbol is processes this way context model updates itself, sometimes
upgrading its kind, changing underlying data structure.

When decoding, we must be able to give a context model some value
in working range and it should find an interval where this value belongs,
and the symbol corresponding to the interval.

With this outer simple interface, underlying data structure can be very different
depending on the stats, on how many and how different symbols were met.
Depending on the data some contexts are met often, some are quite rare and unpopulated.
In many contexts just a few different symbols are met, so there's no sense in storing
256 counters and intervals for such unpopulated contexts, we can store it more compactly,
waste less memory and use cache better.
In some contexts no symbol is met more than once, i.e. each symbol is unpredictable,
such symbols get stored without attempting to compress them and we just record
which symbols were seen, no need to store counts.  When we finally meet some symbol 
for the second time we promote the context to a different kind, with counters.

RGB stats are stored in array of Context structs, one for each PPM-style context.
Each Context contains a CxU union, one of 7 kinds of stats model, for different 
number of symbols in a context. When compiling to x86, each Context is just 16 bytes,
and some kinds of contexts are stored right there, without additional allocations
and indirections.
*/

//Model update agressiveness (adaptivity) parameters for different kinds of contexts.
//The higher such value, the faster model learns new data and forgets old data,
//and the more often model rescaling happens.
#define STEP_CX5 50 //also for Cx4
#define STEP_CX6 25 
#define STEP_CX7 16 
#define STEP_FX 16 

//Interval for symbol encoding by an entropy coder. [cumFreq, cumFreq + freq)
struct Freq {
	uint16_t freq, cumFreq;
}; //to pass byte without compressing: freq=0, cumFreq=c

#define PROB_BITS 12
#define PROB_SCALE (1 << PROB_BITS)

enum FindRes { Found, Added, NoRoom };

//used in contexts where no symbol appeared twice yet
template <int sz>
struct SymbolList {
	BYTE symbols[sz];
	static const int size = sz;

	template<int S> void copyFrom(SymbolList<S> *lst, int d, BYTE c) {
		for(int i=0;i<d;i++) 
			symbols[i] = lst->symbols[i];
		symbols[d] = c;
	}

	FindRes findOrAdd(BYTE c, int d) {
		for(int i=0; i<d; i++)
			if (symbols[i]==c) return Found;
		if (d < sz) {
			symbols[d] = c; return Added;
		}
		return NoRoom;
	}
};

//Structs Cx1 .. Cx7 contain information about symbols met in a given context.
//Described by values n and d: n is total number of symbols met, d is number of different symbols met,
//so d <= n and if n=d it means every stored symbol was met once. 

//n=d, 1..14 unique symbols. Fits inside a Context, no indirections and no allocations needed.
struct Cx1 {
	BYTE kind, d;
	SymbolList<14> lst;

	void create(BYTE c) {
		kind = 1; d = 1; lst.symbols[0] = c;
	}

	void free() {}

	void show() { 
		lprintf(logF,"d=%d [", d);
		for(int i=0;i<d;i++) lprintf(logF,"%d ", lst.symbols[i]);
		lprintf(logF,"] ");
	}
};


struct Cx2 { //15 - 64 unique symbols
	BYTE kind, d;
	SymbolList<64> *symb; 

	void create(Cx1 &c1, BYTE c) {
		kind = 2; d = c1.d + 1;
		symb = new SymbolList<64>;
		symb->copyFrom( &c1.lst, c1.d, c);
	}

	void free() {
		delete symb; symb = NULL;
	}

	void show() { 
		lprintf(logF,"d=%d [", d);
		for(int i=0;i<d;i++) lprintf(logF,"%d ", symb->symbols[i]);
		lprintf(logF,"] ");
	}
};

struct Cx3 {//65 - 256 unique symbols
	BYTE kind;
	uint16_t d;
	SymbolList<256> *symb; 

	void create(Cx2 &c2, BYTE c) {
		kind = 3;  d = c2.d + 1;
		symb = new SymbolList<256>();
		symb->copyFrom(c2.symb, c2.d, c);
	}
	void free() {
		delete symb; symb = NULL;
	}
};

//This struct can store a few symbols and their counters.
//Uses linear search for finding intervals and symbols.
template<int S>
struct SmallContext { 	
	BYTE d, maxpos; //maxpos is position of symbol with max freq value
	BYTE symbols[S];   //symbols met, sorted
	uint16_t freqs[S];
	static const int f0 = STEP_CX5;

	void create(Cx1 &c1, BYTE c) { // c is one of existing symbols
		d = c1.d;
		for(int i=0;i<d;i++) symbols[i] = c1.lst.symbols[i];			
		std::sort(symbols, symbols + d);
		for(int i=0;i<d;i++) {
			if (symbols[i]==c) {
				freqs[i] = 2*f0; maxpos = i;
			} else
				freqs[i] = f0;
		}
		for(int i=d; i<S; i++) freqs[i] = 0;
	}

	bool addSymb(int pos, BYTE c, uint16_t &totFr) { 
		if (d == S) return false;
		for(int i=d-1; i >= pos; i--) {
			symbols[i+1] = symbols[i]; freqs[i+1] = freqs[i];
		}
		symbols[pos] = c; freqs[pos] = f0; d++;
		if (maxpos >= pos) maxpos++; //most probable symbol shifted too
		totFr += f0;
		if (totFr + f0 > PROB_SCALE) rescale(totFr);
		return true;
	}

	void rescale(uint16_t &totFr) {
		int s = 256 - d;
		for(int i=0;i<d;i++) {
			freqs[i] -= freqs[i] >> 1;
			s += freqs[i];
		}
		totFr = s;
	}

	bool encode(BYTE c, Freq &interval, uint16_t &totFr) { // also updates stats, true if ok, false if need upgrade to Cx5
		int shift = 0, tot = totFr;
		while (tot <= PROB_SCALE/2) {
			tot <<= 1; shift++;
		}
		const int bonus = (PROB_SCALE - tot) >> shift; // unused code space, let's give it to most probable symbol
		const uint16_t maxFreq = freqs[maxpos];
		freqs[maxpos] += bonus; // temporary change
		int cumFr = 0, cfr = 0, lastSymb = 0, pos = 0;
		while(pos < d) {
			auto s = symbols[pos];
			if (s==c) { // found it
				cumFr += c - lastSymb;
				cfr = freqs[pos];
				interval.cumFreq = cumFr << shift;	interval.freq = cfr << shift;
				freqs[maxpos] = maxFreq;
				freqs[pos] += f0; totFr += f0;
				if (pos != maxpos && freqs[pos] > freqs[maxpos])
					maxpos = pos;
				if (totFr + f0 > PROB_SCALE) rescale(totFr);
				return true;
			}
			if (c < s) { //c is a new symbol before s
				cumFr += c - lastSymb;
				interval.cumFreq = cumFr << shift;	interval.freq = 1 << shift;
				freqs[maxpos] = maxFreq;
				return addSymb(pos, c, totFr);
			}
			// c > s, continue
			cumFr += s - lastSymb + freqs[pos];
			lastSymb = s + 1;
			pos++;
		}
		freqs[maxpos] = maxFreq;
		if (pos==d) { // still not found 
			cumFr += c - lastSymb;
			interval.cumFreq = cumFr << shift;	interval.freq = 1 << shift;
			return addSymb(pos, c, totFr);
		}
		assert(0); //must be unreachable
		return false;
	}

	bool decode(int someFreq, BYTE &c, Freq & interval, uint16_t &totFr) { // false if we need upgrade to update
		int shift = 0, tot = totFr;
		while (tot <= PROB_SCALE/2) {
			tot <<= 1; shift++;
		}
		someFreq >>= shift;
		int bonus = (PROB_SCALE - tot) >> shift; // unused code space, let's give it to most probable symbol
		const uint16_t maxFreq = freqs[maxpos];
		freqs[maxpos] += bonus; // temporary change
		int cumFr = 0, cfr = 0, lastSymb = 0, pos = 0;
		while(pos < d) {
			auto s = symbols[pos];
			int startFr = cumFr + s - lastSymb;
			if (someFreq < startFr) { //c < s
				c = someFreq - cumFr + lastSymb;
				cumFr = someFreq;	
				interval.cumFreq = cumFr << shift;	interval.freq = 1 << shift;
				freqs[maxpos] = maxFreq;
				return addSymb(pos, c, totFr);
			}
			const int fr = freqs[pos];
			if (startFr + fr > someFreq) { //s=c
				c = s; 
				cumFr += c - lastSymb;
				interval.cumFreq = cumFr << shift;	interval.freq = fr << shift;
				freqs[maxpos] = maxFreq;
				freqs[pos] += f0; totFr += f0;
				if (pos != maxpos && freqs[pos] > freqs[maxpos])
					maxpos = pos;
				if (totFr + f0 > PROB_SCALE) rescale(totFr);
				return true;
			}
			// c > s, continue
			cumFr += s - lastSymb + fr;
			lastSymb = s + 1;
			pos++;
		}//while pos
		freqs[maxpos] = maxFreq;
		if (pos==d) { // still not found
			c = lastSymb + someFreq - cumFr;
			interval.cumFreq = someFreq << shift;	interval.freq = 1 << shift;
			return addSymb(pos, c, totFr);
		}
		assert(0 && "unreachable");
		return true;
	}//decode

	void show() { 
		lprintf(logF,"d=%d maxpos=%d [", d, maxpos);
		for(int i=0;i<d;i++) lprintf(logF,"%d:%d ", symbols[i], freqs[i]);
		lprintf(logF,"] ");
	}
};

//n > d,  d <= 4; fits inside a Context
struct Cx4 {
	BYTE kind;
	SmallContext<4> cxdata;

	void create(Cx1 &c1, BYTE c) { // c is one of existing symbols
		kind = 4; cxdata.create(c1, c);
	}
	void free() {}

	bool encode(BYTE c, Freq &interval) { // also updates stats, true if ok, false if need upgrade to Cx5
		uint16_t totFr = cxdata.freqs[0] + cxdata.freqs[1] + cxdata.freqs[2] + cxdata.freqs[3] + 256 - cxdata.d;
		return cxdata.encode(c, interval, totFr);
	}

	bool decode(int someFreq, BYTE &c, Freq & interval) { // false if we need upgrade to update
		uint16_t totFr = cxdata.freqs[0] + cxdata.freqs[1] + cxdata.freqs[2] + cxdata.freqs[3] + 256 - cxdata.d;
		return cxdata.decode(someFreq, c, interval, totFr);
	}//decode

	void show() { 
		cxdata.show();
	}
};

// 5 .. 16 symbols
struct Cx5 { 
	BYTE kind;
	uint16_t cntsum; //todo: use it instead of recalc
	static const int maxD = 16;
	SmallContext<maxD> *cxdata;

	void create(Cx1 &c1, BYTE c) { // c is one of existing symbols
		kind = 5; cxdata = new SmallContext<maxD>();
		cxdata->create(c1, c);
		calcSum();
	}

	void free() {
		delete cxdata; cxdata = NULL;
	}

	void calcSum() {
		int totFr = 256 - cxdata->d;
		for(int i=0; i < cxdata->d; i++) totFr += cxdata->freqs[i];
		cntsum = totFr;
	}

	bool encode(BYTE c, Freq &interval) { // also updates stats, true if ok, false if need upgrade to Cx5
		return cxdata->encode(c, interval, cntsum);
	}

	bool decode(int someFreq, BYTE &c, Freq & interval) { // false if we need upgrade to update
		return cxdata->decode(someFreq, c, interval, cntsum);
	}//decode

	void show() { cxdata->show(); }

	void create(Cx4 &c4, BYTE c) { //add new symbol
		kind = 5; cxdata = new SmallContext<maxD>();
		int i = 0, d = c4.cxdata.d, j=0, totFr=0;
		while(i < d && c4.cxdata.symbols[i] < c) {
			cxdata->symbols[j] = c4.cxdata.symbols[i];
			totFr += cxdata->freqs[j] = c4.cxdata.freqs[i];
			i++; j++;
		}
		cxdata->symbols[j] = c;
		totFr += cxdata->freqs[j] = SmallContext<maxD>::f0;
		j++;
		while(i < d) {
			cxdata->symbols[j] = c4.cxdata.symbols[i];
			totFr += cxdata->freqs[j] = c4.cxdata.freqs[i];
			i++; j++;
		}
		cxdata->d = d + 1;
		if (totFr > PROB_SCALE) cxdata->rescale(cntsum);
		calcSum();
	}
};

extern int GetThreadLocalInt(); // used for Cx6.f0, set in encodeBegin/decodeBegin

//17 - 40 symbols with counters
//Linear search of SmallContext is not fast enough here, so we use RobinHood-like hash table when encoding
//and linear search among frequency-sorted data when decoding
struct Cx6 {
	BYTE kind, d, S, fshift; // S is size of table (32 / 64 )
	//linear probing hash table...
	BYTE *symbols;
	Freq *freqs;
	uint16_t *cnts;
	static const int empty = 1;
	static const int Step = STEP_CX6;
	static const int MaxD6 = 40; // don't store more than MaxD6 symbols

	int add(BYTE c, Freq fr) { // => pos or -1 if err
		const int mask = S-1;
		int p0 = c & mask;
		int posPlaced = -1;
		assert(fr.freq > 0);
		assert(fr.cumFreq <= PROB_SCALE);
		assert(fr.cumFreq + fr.freq <= PROB_SCALE);
		uint16_t mycnt = fr.freq - (fr.freq >> 1);
		for(int j=0;;j++) { //find an empty slot
			int pos = (p0 + j) & mask;
			if (cnts[pos]==0) { //empty
				if (posPlaced < 0) posPlaced = pos; 
				symbols[pos] = c;
				freqs[pos] = fr;
				cnts[pos] = mycnt;
				d++;
				return posPlaced;
			} else {//someone here
				if (cnts[pos] < mycnt) { //evict them and take the place
					if (posPlaced < 0) posPlaced = pos; //this is where original c was put
					std::swap(symbols[pos], c);
					std::swap(freqs[pos], fr);
					std::swap(cnts[pos], mycnt);
				}
			}
		}
		assert(0 && "must be unreachable");
		return -1;
	}

	int addDec(BYTE c, Freq fr) { // => pos or -1 if full
		if (d >= MaxD6 || d >= S) return -1;
		assert(fr.freq > 0);
		const int pos = d;
		symbols[pos] = c;
		freqs[pos] = fr;
		cnts[pos] = fr.freq - (fr.freq >> 1);
		d++;
		return pos;
	}

	void sortByFreqs() {
		for(int i=0;i<S-1;i++) 
			for(int j=i+1;j<S;j++)
				if (freqs[j].freq > freqs[i].freq) {
					std::swap(freqs[i], freqs[j]);
					std::swap(cnts[i], cnts[j]);
					std::swap(symbols[i], symbols[j]);
				}
	}

	void init(int S0) {
		kind = 6; S = S0;
		symbols = new BYTE[S0];
		freqs = new Freq[S0];
		cnts = new uint16_t[S0+1]; //cnts[S] == cntsum
		for(int i=0; i<S0; i++) 
			symbols[i] = empty; 		
		memset(freqs, 0, S0*sizeof(Freq)); memset(cnts, 0, (S0+1)*sizeof(uint16_t));
	}

	void free() {
		delete[] symbols; symbols = NULL;
		delete[] freqs; freqs = NULL;
		delete[] cnts; cnts = NULL;
	}

	void create(Cx5 &c5, BYTE c) { //add new symbol
		const int S0 = 32; //initial size
		init(S0);
		const int mask = S0-1;
		int oldd = c5.cxdata->d;

		int totFr = 256 - oldd;
		for(int i=0;i<oldd;i++) totFr += c5.cxdata->freqs[i];
		d = 0;

		int shift = 0, tot = totFr;
		while (tot <= PROB_SCALE/2) {
			tot <<= 1; shift++;
		}
		int cumFr = 0, cfr = 0, lastSymb = 0;
		
		for(int pos=0; pos < oldd; pos++) {
			auto s = c5.cxdata->symbols[pos];
			int startFr = cumFr + s - lastSymb;

			cumFr += s - lastSymb;
			cfr = c5.cxdata->freqs[pos];
			Freq interval;
			interval.cumFreq = cumFr << shift;	interval.freq = cfr << shift;
			//write interval to the hash table...
			add(s, interval);
			cumFr += cfr;
			lastSymb = s + 1;
		}
		fshift = shift; 
		// find interval for c and add it too
		auto fr = unmetSymbolInterval(c);
		int p = add(c, fr);
		incrCnt(p);
		calcSum();
	}//create

	template<class C> //C is Cx2 or Cx3 (originally, now Cx3 not used)
	void create23(C &cx, BYTE c, int S0) {		
		lprintf(logF, "Cx6::create23\n");
		init(S0);
		const int mask = S0-1, f0 = GetThreadLocalInt(); // 32 for v4.0;  v3.0 had f0=64
		int oldd = cx.d; // up to 64

		int totFr = 256 - oldd;
		totFr += oldd * f0 + f0; // +f0 for the c which is met 2nd time
		d = 0;
		assert(totFr <= PROB_SCALE);

		int shift = 0, tot = totFr;
		while (tot <= PROB_SCALE/2) {
			tot <<= 1; shift++;
		}
		if (logF) {
			lprintf(logF, "totFr=%d tot=%d shift=%d\n", totFr, tot, shift);
			fflush(logF);
		}
		int cumFr = 0, cfr = 0, lastSymb = 0;
		std::sort(cx.symb->symbols, cx.symb->symbols + oldd);
		for(int pos=0; pos < oldd; pos++) {
			auto s = cx.symb->symbols[pos];
			int startFr = cumFr + s - lastSymb;

			cumFr += s - lastSymb;
			cfr = s==c ? f0*2 : f0;
			Freq interval;
			interval.cumFreq = cumFr << shift;	interval.freq = cfr << shift;
			lprintf(logF, "  pos=%d s=%d startFr=%d cumFr=%d cfr=%d\n", pos, s, startFr, cumFr, cfr);
			if (cumFr + cfr > PROB_SCALE) fflush(logF);
			//write interval to the hash table...
			add(s, interval); 
			cumFr += cfr;
			lastSymb = s + 1;
		}
		fshift = shift; 
		//assert(cumFr + ((256-d) << fshift) <= PROB_SCALE); 
		calcSum();
	}

	void create(Cx2 &c2, BYTE c) {	create23(c2, c, c2.d <= 32 ? 32 : 64);	}
	//void create(Cx3 &c3, BYTE c) {	create23(c3, c, 128);	}

	void show() const {
		lprintf(logF,"Cx6 k=%d d=%d S=%d fshift=%d ", kind, d, S, fshift);
		for(int i=0; i<S;i++) 
			if (cnts[i] > 0) {
				int c = symbols[i];
				int p0 = c & (S-1);
				int dist = i - p0;
				if (dist < 0) dist += S;
				lprintf(logF,"tab[%d]={ c=%d dist=%d p0=%d fr=%d,%d cnt=%d}\n ", i, c, dist, p0, freqs[i].freq, freqs[i].cumFreq, cnts[i] );
			}
		lprintf(logF,"cntsum=%d\n", cnts[S]);
	}

	void calcSum() {
		int shft = fshift > 0 ? fshift-1 : 0; //?
		int sum = (256 - d) << shft;
		for(int i=0;i<S;i++)
			sum += cnts[i];
		cnts[S] = sum;
	}

	void grow(BYTE * old_symbols, Freq* old_freqs, uint16_t* old_cnts) {
		int oldS = S;
		S *= 2; d = 0;
		int mask = S - 1;
		symbols = new BYTE[S];
		freqs = new Freq[S];
		cnts = new uint16_t[S+1]; //cnts[S] == cntsum
		for(int i=0; i<S; i++) 
			symbols[i] = empty; 		
		memset(freqs, 0, S*sizeof(Freq)); memset(cnts, 0, (S+1)*sizeof(uint16_t));
		for(int i=0; i<oldS; i++) {
			if (old_cnts[i] > 0) {
				int pos = add(old_symbols[i], old_freqs[i]);
				cnts[pos] = old_cnts[i];
			}
		}
		calcSum();
		delete[] old_symbols; delete[] old_freqs; delete[] old_cnts;
	}

	void growDec(BYTE * old_symbols, Freq* old_freqs, uint16_t* old_cnts) {
		int oldS = S;
		S *= 2; 
		symbols = new BYTE[S];
		freqs = new Freq[S];
		cnts = new uint16_t[S+1]; //cnts[S] == cntsum
		memcpy(symbols, old_symbols, d);
		memcpy(freqs, old_freqs, d*sizeof(Freq));
		memcpy(cnts, old_cnts, d*sizeof(uint16_t));
		const Freq fz = {0,0};
		for(int i=d; i<S; i++) { 
			symbols[i] = empty; 
			freqs[i] = fz;
			cnts[i] = 0;
		}
		cnts[S] = old_cnts[oldS];
		delete[] old_symbols; delete[] old_freqs; delete[] old_cnts;
	}

	Freq unmetSymbolInterval(BYTE c) {
		Freq fr;
		fr.freq = 1 << fshift;
		fr.cumFreq = 0; // for c == 0
		if (c > 0) {
			int lowerSym = -1;
			Freq lfr = {0,0};
			for(int i=0;i<S;i++) if (cnts[i] > 0)  {
				int s = symbols[i];
				if (s > lowerSym && s < c) {
					lowerSym = s; lfr = freqs[i];
				}
			}
			if (lfr.freq > 0) {// found some lower neighbor
				fr.cumFreq = lfr.cumFreq + lfr.freq + ((c - lowerSym - 1) << fshift);
			} else  // c > 0 but lower than all others
				fr.cumFreq = c << fshift;
		}
		//assert(cnts[S] + ((256-d) << fshift) <= PROB_SCALE);
		assert(cnts[S] <= PROB_SCALE);
		assert(fr.cumFreq <= PROB_SCALE);
		assert(fr.cumFreq + fr.freq <= PROB_SCALE);
		return fr;
	}

	bool placeSymbol(BYTE c, int pos, Freq &interval) {
		interval = unmetSymbolInterval(c);
		assert(interval.cumFreq <= PROB_SCALE);
		assert(interval.freq <= PROB_SCALE);
		if (S==32 && d >= 24) {
			grow(symbols, freqs, cnts);
			int p = add(c, interval);
			incrCnt(p);
			return true;
		} 
		if (d >= MaxD6) return false;
		freqs[pos] = interval;
		symbols[pos] = c;
		cnts[pos] = interval.freq - (interval.freq >> 1);
		d++;
		incrCnt(pos);
		return true;
	}

	bool encode(BYTE c, Freq &interval) {
		const int mask = S-1;
		int p0 = c & mask;
		if (symbols[p0]==c) { //either we found our symbol or c==empty and this slot is empty
			if (cnts[p0]==0) //slot is indeed empty				
				return placeSymbol(c, p0, interval); // c==empty, c not in table and this is the place for it
			else return found(p0, interval); // found at p0			
		} else { // not found at p0, go find it; s0 != c
			if (c != empty) {
				if (cnts[p0]==0)
					return placeSymbol(c, p0, interval); // c not in table and this is the place for it
				for(int j=1;j<S;j++) {
					int pos = (p0 + j) & mask;
					BYTE s = symbols[pos];
					if (s==c)  return found(pos, interval);// found our symbol 
					if (s==empty && cnts[pos]==0) return placeSymbol(c, pos, interval); // c not in table, and this is the place for it
				}				
			} else { // c == empty, use cnts to differ empty slots
				for(int j=0;j<S;j++) {
					int pos = (p0 + j) & mask;
					auto s = symbols[pos];
					if (s==empty) {
						if (cnts[pos] > 0) return found(pos, interval); 
						// here s==c && cnts[pos]==0 => c not in table and this is the place for it
						return placeSymbol(c, pos, interval);
					}
				}				
			}
			//  c not in table and there's no room, grow the table
			auto fr = interval = unmetSymbolInterval(c);
			if (S >= 64) return false; // we need upgrade to another Cx kind
			grow(symbols, freqs, cnts);
			int p = add(c, fr);
			incrCnt(p);
			return true;
		}
		assert(0 == "unreachable");
		return false;
	}

	bool found(int pos, Freq &interval) {
		interval = freqs[pos];
		incrCnt(pos);
		return true;				
	}

	void incrCnt(int pos) {
		int step = Step << fshift;
		cnts[pos] += step;
		cnts[S] += step;
		if (cnts[S] + step > PROB_SCALE) rescale();
	}

	void incrCntDec(int pos) {
		int step = Step << fshift;
		cnts[pos] += step;
		cnts[S] += step;
		if (pos > 0 && cnts[pos] > cnts[pos-1]) {
			std::swap(cnts[pos], cnts[pos-1]);
			std::swap(freqs[pos], freqs[pos-1]);
			std::swap(symbols[pos], symbols[pos-1]);
		}
		if (cnts[S] + step > PROB_SCALE) rescaleDec();
	}

	bool decode(int someFreq, BYTE &c, Freq & interval) {
		Freq lfr = {0,0}; BYTE lowerSym = 0;
		for(int i=0;i<d;i++) {
			int cf = freqs[i].cumFreq;
			if (cf <= someFreq) {
				if (cf + freqs[i].freq > someFreq) {//found
					c = symbols[i]; interval = freqs[i];
					incrCntDec(i); return true;
				}
				if (cf >= lfr.cumFreq) {
					lfr = freqs[i]; lowerSym = symbols[i];
				}
			}
		}
		//symbol not in table
		Freq fr;
		fr.freq = 1 << fshift;
		if (lfr.freq) {//lfr is closest lower one, c = lowerSym + ..
			int cumFr = lfr.cumFreq + lfr.freq;
			int x = (someFreq - cumFr) >> fshift; //x = c - lowerSym - 1
			c = x + lowerSym + 1;
			fr.cumFreq = lfr.cumFreq + lfr.freq + (x << fshift);
		} else { // c < all known
			c = someFreq >> fshift;
			fr.cumFreq = c << fshift;
		}
		interval = fr;
		int p = addDec(c, fr); 
		if (p < 0) {
			if (S==64) return false;
			growDec(symbols, freqs, cnts);
			p = addDec(c, fr); 
		}
		incrCntDec(p);
		return true;
	}//decode

	void rescale() { 
		uint16_t _cnts[256];
		Freq _freqs[256];
		assert(cnts[S] <= PROB_SCALE);
		const int mask = S-1;
		int sh = fshift > 0 ? fshift-1 : 0;
		int c0 = 1 << sh;
		for(int i=0;i<256;i++) 
			_cnts[i] = c0; 
		memset(_freqs, 0, sizeof(_freqs));
		bool needReordering = false;
		for(int i=0;i<S;i++)
			if (cnts[i] > 0) {
				const int s = symbols[i];
				_cnts[s] = cnts[i];	
				const int p0 = s & mask;
				if (i != p0) { //this symbol is not at its best position					
					needReordering = needReordering || (cnts[p0] < cnts[i]); //the one on its best place deserves it less
				}
			}
		int cumFr = 0;
		for(int i=0;i<256;i++) {
			_freqs[i].freq = _cnts[i];
			_freqs[i].cumFreq = cumFr;
			cumFr += _cnts[i];
		}
		if (fshift > 0) fshift--;

		if (!needReordering) {
			const int shft = fshift > 0 ? fshift-1 : 0;
			int cntsum = (256 - d) << shft;

			for(int i=0;i<S;i++) if (cnts[i]) {
				cnts[i] -= cnts[i] >> 1;
				cntsum += cnts[i];
				freqs[i] = _freqs[ symbols[i] ];
			}
			cnts[S] = cntsum;
		} else { //rehash  
			SymInfo syms[256]; 
			int sp = 0; 
			for(int i=0;i<S;i++) if (cnts[i]) {
				auto s = symbols[i];
				SymInfo si = { _freqs[s], s };
				syms[sp++] = si;
				cnts[i] = 0;
			}
			assert(sp==d);
			d = 0;
			for(int i=0;i<sp;i++) 
				add(syms[i].s, syms[i].fr);
			assert(d==sp);
			calcSum();
		}
	}

	struct SymInfo { Freq fr; BYTE s; };

	void rescaleDec() { //all data in first d elements of freqs/cnts/symbols
		uint16_t _cnts[256];
		Freq _freqs[256];
		assert(cnts[S] <= PROB_SCALE);
		const int sh = fshift > 0 ? fshift-1 : 0;
		const int c0 = 1 << sh;
		for(int i=0;i<256;i++) 
			_cnts[i] = c0; 
		memset(_freqs, 0, sizeof(_freqs));
		for(int i=0;i<d;i++) 
			_cnts[ symbols[i] ] = cnts[i];		
		int cumFr = 0;
		for(int i=0;i<256;i++) {
			_freqs[i].freq = _cnts[i];
			_freqs[i].cumFreq = cumFr;
			cumFr += _cnts[i];
		}

		if (fshift > 0) fshift--;
		int shft = fshift > 0 ? fshift-1 : 0;
		int cntsum = (256 - d) << shft;

		for(int i=0;i<d;i++) {
			cnts[i] -= cnts[i] >> 1;
			cntsum += cnts[i];
			freqs[i] = _freqs[ symbols[i] ];
		}
		cnts[S] = cntsum;
	}
};//Cx6

//store both counters and precalculated intervals, 
//don't store symbols since all symbols are possible, we just use them as indices
template<int N>
struct BigContext {
	Freq freqs[N];
	uint16_t cnts[N];

	BigContext() { memset(this, 0, sizeof(BigContext<N>)); }
};

//41 - 256 symbols
//Have an array of precalculated intervals, update it when rescaling counters,
//i.e. when new portion of stats accumulated.
//For decoding, have an array for mapping range values to symbols, but decimated,
//so to find the symbol and interval corresponding to some range value,
//scale it down, look up in decTable, then from that value search forward.
struct Cx7 {
	BYTE kind;
	int cntsum;
	BigContext<256> *cxdata;
	BYTE *decTable; //cumFreq / D => symbol; for Dshift=7 decTable is just 32 bytes
	static const int step = STEP_CX7;
	static const int Dshift = 7; 
	static const int D = 1 << Dshift;

	void init(bool decoding) {
		kind = 7;
		cxdata = new BigContext<256>();
		if (decoding) decTable = new BYTE[PROB_SCALE / D];
		else decTable = NULL;
	}

	void free() {
		delete cxdata; cxdata = NULL;
		delete[] decTable; decTable = NULL;	
	}

	void create(const Cx6 &c6, BYTE c, bool decoding) {
		init(decoding);
		cntsum = c6.cnts[c6.S];
		assert(cntsum <= PROB_SCALE);
		
		for(int i=0; i < c6.S; i++) if (c6.cnts[i]) {
			BYTE c = c6.symbols[i];
			cxdata->freqs[c] = c6.freqs[i];
			cxdata->cnts[c] = c6.cnts[i];

			//if (cxdata->freqs[c].cumFreq + cxdata->freqs[c].freq > PROB_SCALE) {
			//	c6.show();
			//	fflush(logF);
			//}
			assert(cxdata->freqs[c].cumFreq <= PROB_SCALE);
			assert(cxdata->freqs[c].cumFreq + cxdata->freqs[c].freq <= PROB_SCALE);
		}
		int funmet = 1 << c6.fshift;
		//assert( cntsum + (256 - c6.S) * funmet <= PROB_SCALE ); 
		// it seems cntsum already includes probs for unmet symbols
		int cntUnmet = funmet - (funmet >> 1);
		int cumFr = 0;
		for(int i=0;i<256;i++) {
			int fr = 0;
			if (cxdata->freqs[i].freq) {
				assert(cumFr == cxdata->freqs[i].cumFreq);
				fr = cxdata->freqs[i].freq;
			} else {
				cxdata->freqs[i].freq = funmet;
				cxdata->freqs[i].cumFreq = cumFr; 
				cxdata->cnts[i] = cntUnmet;
				fr = funmet;

				assert(cxdata->freqs[i].cumFreq <= PROB_SCALE);
			}
			if (decoding) {
				//for(int j=cumFr; j<cumFr + fr; j++)
				//	if ((j & (D-1)) == 0) decTable[j >> Dshift] = i;
				const int k0 = (cumFr + D-1) >> Dshift;//first z >= cf, such that z = D*k
				const int k1 = ((cumFr + fr - 1) >> Dshift) + 1;
				for(int k=k0; k<k1; k++)
					decTable[k] = i;
			}
			cumFr += fr;
		}
		assert(cumFr <= PROB_SCALE);
		assert(cntsum <= PROB_SCALE);
	}

	void create(Cx3 &c3, BYTE c, bool decoding) {
		init(decoding);
		for(int i=0;i<256;i++) {
			cxdata->freqs[i].freq = 1;
			cxdata->cnts[i] = 1;
		}
		int d = c3.d;
		int f0 = (PROB_SCALE - (256-d)) / (d+1);
		int c0 = f0 - (f0 >> 1);
		for(int i=0;i<d;i++) {
			auto s = c3.symb->symbols[i];
			cxdata->freqs[s].freq = f0;
			cxdata->cnts[s] = c0;
		}
		cxdata->freqs[c].freq += f0;
		cxdata->cnts[c] += step;
		cntsum = 0; int cf = 0;
		for(int i=0;i<256;i++) {
			cntsum += cxdata->cnts[i];
			cxdata->freqs[i].cumFreq = cf;
			assert(cxdata->freqs[i].cumFreq <= PROB_SCALE);
			int fr = cxdata->freqs[i].freq;
			if (decoding) {
				//for(int j=cf; j<cf + fr; j++)
				//	if ((j & (D-1)) == 0) decTable[j >> Dshift] = i;
				const int k0 = (cf + D-1) >> Dshift;//first z >= cf, such that z = D*k
				const int k1 = ((cf + fr - 1) >> Dshift) + 1;
				for(int k=k0; k<k1; k++)
					decTable[k] = i;
			}
			cf += fr;
		}
		assert(cf <= PROB_SCALE);
		assert(cntsum <= PROB_SCALE);
	}

	void encode(BYTE c, Freq &interval) {
		interval = cxdata->freqs[c];
		assert(cxdata->freqs[c].cumFreq <= PROB_SCALE);
		incrCnt<false>(c);
	}

	template<bool decoding>
	void incrCnt(BYTE c) {
		cxdata->cnts[c] += step; cntsum += step;
		assert(cntsum <= PROB_SCALE);
		if (cntsum + step > PROB_SCALE) {
			cntsum = 0; int cf = 0;
			for(int j=0;j<256;j++) {
				cxdata->freqs[j].cumFreq = cf;
				int fr = cxdata->freqs[j].freq = cxdata->cnts[j];
				if (decoding) {
					const int k0 = (cf + D-1) >> Dshift;//first z >= cf, such that z = D*k
					const int k1 = ((cf + fr - 1) >> Dshift) + 1;
					//for(int z=cf; z<cf + fr; z++)
					//	if ((z & 7) == 0) decTable[z >> 3] = j;
					for(int k=k0; k<k1; k++)
						decTable[k] = j;
				}
				cf += fr;
				cxdata->cnts[j] -= fr >> 1;
				cntsum += cxdata->cnts[j];
			}
		}
	}

	void decode(int someFreq, BYTE &c, Freq & interval) {
		int c0 = decTable[someFreq >> Dshift];
		for(int j=c0; j<255; j++) {
			assert(cxdata->freqs[j].cumFreq <= someFreq); //should be true by design
			assert(cxdata->freqs[j].cumFreq + cxdata->freqs[j].freq == cxdata->freqs[j+1].cumFreq);
			if (cxdata->freqs[j+1].cumFreq > someFreq) {
				c = j; interval = cxdata->freqs[j];					
				incrCnt<true>(c);
				return;
			}
		}
		//if we're here then c = 255
		c = 255; interval = cxdata->freqs[255];					
		incrCnt<true>(255);
	}
};

union CxU { // with n - number of symbols met and d - number of different symbols:
	Cx1 c1; //n=d <= 14
	Cx2 c2; //n=d <= 64
	Cx3 c3; //n=d <= 256
	Cx4 c4; //n>d <= 4
	Cx5 c5; //n>d <= 16 
	Cx6 c6; //n>d <= 40
	Cx7 c7; //n>d <= 256
};

template<class From, class To> To upgrade(From &old, BYTE c) {
	To cx; cx.create(old, c); old.free(); return cx;
}

template<class From> Cx7 upgradeTo7(From &old, BYTE c, bool decoding) {
	Cx7 cx; cx.create(old, c, decoding); old.free(); return cx;
}

class Context { //16 bytes when compiled in 32-bit mode
public:
	CxU u;

	Context() { u.c1.kind = 0; }
	int kind() const { return u.c1.kind; }
	bool encode(BYTE c, Freq &interval); // also updates stats, false means Skip, write raw byte
    bool decode(int someFreq, BYTE &c, Freq & interval);  // updates stats, if true returns c and interval                     

	void show() {
		lprintf(logF,"knd=%d ", u.c1.kind);
		switch(u.c1.kind) {
		case 1: u.c1.show(); break;
		case 2: u.c2.show(); break;
		case 4: u.c4.show(); break;
		case 5: u.c5.show(); break;
		case 6: u.c6.show(); break;
		}
	}

	int d() {
		switch(u.c1.kind) {
		case 6: return u.c6.d; 
		}
		return 0;
	}

	void update(BYTE c);
	void updateC1(BYTE c);
	void updateC2(BYTE c, bool decoding);
	void updateC3(BYTE c, bool decoding);
	void free();
	void renew() { free(); u.c1.kind = 0; }
};

template<int NSym>
struct FixedSizeRansCtx {
	static const int step = STEP_FX; 
	static const int Dshift = 7; 
	static const int D = 1 << Dshift;

	int cntsum;
	BigContext<NSym> cxdata; // Nsym * 6 bytes, 1.5k for NSym=256
	BYTE decTable[PROB_SCALE / D]; //32 bytes for current values of PROB_SCALE=4k and Dshift=7

	void encode(int c, Freq &interval) {
		assert(c >= 0);
		assert(c < NSym);
		interval = cxdata.freqs[c];
		incrCnt<false>(c);
	}

	template<bool decoding>
	void incrCnt(int c) {
		assert(c >= 0);
		assert(c < NSym);
		cxdata.cnts[c] += step; cntsum += step;
		if (cntsum + step > PROB_SCALE) {
			cntsum = 0; int cf = 0;
			for(int j=0;j<NSym;j++) {
				cxdata.freqs[j].cumFreq = cf;
				int fr = cxdata.freqs[j].freq = cxdata.cnts[j];
				if (decoding) {
					const int k0 = (cf + D-1) >> Dshift;//first z >= cf, such that z = 8*k
					const int k1 = ((cf + fr - 1) >> Dshift) + 1;
					for(int k=k0; k<k1; k++)
						decTable[k] = j;
				}
				cf += fr;
				cxdata.cnts[j] -= fr >> 1;
				cntsum += cxdata.cnts[j];
			}
		}
	}

	int decode(int someFreq, Freq & interval) {
		assert(someFreq >= 0);
		assert(someFreq < PROB_SCALE);
		int c0 = decTable[someFreq >> Dshift];
		assert(c0 >= 0);
		assert(c0 < NSym);
		for(int j=c0; j<NSym-1; j++) {
			assert(cxdata.freqs[j].cumFreq <= someFreq); //should be true by design
			assert(cxdata.freqs[j].cumFreq + cxdata.freqs[j].freq == cxdata.freqs[j+1].cumFreq);
			if (cxdata.freqs[j+1].cumFreq > someFreq) {
				interval = cxdata.freqs[j];					
				incrCnt<true>(j);
				return j;
			}
		}
		//if we're here then c = last symbol
		interval = cxdata.freqs[NSym-1];					
		incrCnt<true>(NSym-1);
		return NSym-1;
	}

	void renew(bool decoding) { //set equal probs
		int cf = 0;
		const int fr = PROB_SCALE / NSym;
		const int c0 = fr - (fr >> 1);
		cntsum = c0 * NSym;
		for(int i=0;i<NSym;i++) {
			cxdata.freqs[i].freq = fr;
			cxdata.freqs[i].cumFreq = cf;
			cxdata.cnts[i] = c0;
			if (decoding) {
				const int k0 = (cf + D-1) >> Dshift;//first z >= cf, such that z = 8*k
				const int k1 = ((cf + fr - 1) >> Dshift) + 1;
				for(int k=k0; k<k1; k++)
					decTable[k] = i;
			}
			cf += fr;
		}
	}
};

#endif