#ifndef RANSMT_H
#define RANSMT_H
#include <vector>
#include <Windows.h>
#include "rans_byte.h"
#include "ans_contexts.h"

/*
RansMTCoder class manages a worker thread that's used to encode blocks of data
(symbol intervals) with rANS entropy coder, in parallel, 
while next portion of data is being produced.

The codec decides what data needs to be encoded (block types, motion vectors, 
colors, pixel types, pixel counts etc.) and for each kind of value there is
a statistical model assigning an interval [a,b) to each possible value,
where 0 <= a < b <= PROB_SCALE. Codec produces a sequence of such intervals
and feeds them to RansMTCoder.put().

rANS coder can turn a sequence of such intervals in reverse order into
a compressed chunk of binary data. Since it must be encoded in reverse order,
we need to accumulate a block of data, block of intervals, before compressing
them. Then this block of intervals can be encoded independently and in parallel
to producing and accumulating next block, this is a pipeline.

This parallel processing is used when there is a lot of data in one frame, 
more than one block. Block size is currently 128k intervals. If it's a simple
frame with not a lot of data (less than one 128k block) it is easier and cheaper 
to compress them in the same thread, there is no more work in current frame to
do in parallel to this entropy compression.
*/
struct RansMTCoder {
	//two buffers: we write to one in main thread and compress the other in another thread
	std::vector<Freq> ranges[2]; 
	int writingTo; // 0 or 1
	HANDLE haveJob, ready, done;
	bool quit;
	BYTE *dst; //where rans writes to
	static const int B = 128*1024; 
	CRITICAL_SECTION critsec;
	RansState ransInitState; //uint32_t, must be RANS_BYTE_L (1<<23)

	RansMTCoder() {
		ranges[0].reserve(B);
		ranges[1].reserve(B);
		writingTo = 0; quit = false; 
		haveJob = CreateEvent(NULL, FALSE, FALSE, NULL); //auto reset, initial=false
		ready = CreateEvent(NULL, FALSE, FALSE, NULL); //initial=false
		done = CreateEvent(NULL, FALSE, FALSE, NULL); //auto reset, initial=false
		InitializeCriticalSection(&critsec);

		DWORD tid=0;		
		CreateThread(NULL, 512*1024, RansWorkerThread, this, 0, &tid);
	}

	static DWORD WINAPI RansWorkerThread(void* lpParameter) {
		((RansMTCoder*)lpParameter)->threadProc();
		return 0;
	}

	~RansMTCoder() {
		CloseHandle(haveJob); CloseHandle(ready); CloseHandle(done);
		DeleteCriticalSection(&critsec);
	}

	//remember where to write the compressed data, prepare to start
	void start(BYTE *pDst) {
		dst = pDst;
		quit = false;
		ranges[0].resize(0); ranges[1].resize(0);
		ResetEvent(ready); ResetEvent(haveJob); ResetEvent(done);
	}

	void put(Freq fr) { //called from main thread
		assert(ranges[writingTo].size() < B);

		ranges[writingTo].push_back(fr);
		if (ranges[writingTo].size()==B) { //filled the block
			SetEvent(haveJob); //tell the worker thread to compress it
			WaitForSingleObject(ready, INFINITE); //wait until we're ready to write to another buffer
		}
	}

	BYTE* finish() { //data ended, compress what's left in ranges
		EnterCriticalSection(&critsec); //make sure worker ended its current work piece
		if (ranges[writingTo].size() > 0) {
			dst = writeBlock(&ranges[writingTo][0], ranges[writingTo].size(), dst); 
		}
		LeaveCriticalSection(&critsec);
		return dst;
	}

	void threadProc() {//stack size must be more than B*2 i.e. Stack > 256k for B=128k
		while(!quit) {
			DWORD res = WaitForSingleObject(haveJob, INFINITE);
			if (res==WAIT_OBJECT_0) {
				if (quit) break;
				//got haveJob event, ranges[writingTo] is the one filled with work
				int idx = writingTo;
				writingTo ^= 1;
				ranges[writingTo].resize(0);
				EnterCriticalSection(&critsec);
				SetEvent(ready); //ranges[writingTo] is ready to accept new data
				dst = writeBlock(&ranges[idx][0], ranges[idx].size(), dst); //meanwhile we're compressing previously filled buffer
				LeaveCriticalSection(&critsec);
			}
		}
		SetEvent(done);
	}

	void stop() {
		quit = true;
		SetEvent(haveJob);
		WaitForSingleObject(done, INFINITE);
	}

	BYTE* writeBlock(Freq *ranges, int len, BYTE *dst) {
		RansState rans;
		//RansEncInit(&rans);
		rans = ransInitState;
		BYTE tmpbuf[B*2];
		BYTE *ptr = tmpbuf + B*2 - 4;
		BYTE *ptr0 = ptr;

		for(int i=len-1; i>=0; i--) { //rANS encodes in reverse order
			if (ranges[i].freq) //encode an interval
				RansEncPut(&rans, &ptr, ranges[i].cumFreq, ranges[i].freq, PROB_BITS);
			else
				*--ptr = ranges[i].cumFreq; //store a symbol without compression
		}
		RansEncFlush(&rans, &ptr);
		size_t sz = ptr0 - ptr;
		memcpy(dst, ptr, sz);
		return dst + sz;
	}
};//RansMTCoder

#endif