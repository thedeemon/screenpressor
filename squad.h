#ifndef _SQUAD_H_
#define _SQUAD_H_

/*
Some helpers for organizing parallel computations. 
CSquad runs and manages a bunch of threads, squad workers.
When there is some work to do in parallel, codec tells 
the CSquad to perform some operation, it signals the workers,
they run the operation each in its own thread. 
Corresponding CSquadWorker object, one for each thread, knows its number 
and can tell them which part of the work they need to do.
*/

#if WINVER < 0x0400
#define WINVER 0x0400
#define _WIN32_WINNT 0x0400
#endif

#include "windows.h"
#include <vector>
class CSquadWorker;

//Callback used by worker threads. 
//Different threads will call RunCommand with same values of `command` and `params`
//but with different values of CSquadWorker.
class ISquadJob {
public:
	virtual void RunCommand(int command, void *params, CSquadWorker *sqworker)=0 ;
};

class CSquad {
	friend class CSquadWorker;

	std::vector<CSquadWorker*> workers;
	int nw; //number of workers
	std::vector<HANDLE> ev_free, ev_havejob, ev_sync;

	int cur_command;
	void *cur_params;
	ISquadJob *cur_job;

	void Sync(int mynum);
	void ThreadProc(CSquadWorker *sqworker);
	void WaitTillAllFree();
	void SignalJob(); //signal to workers they have a job

public:
	CSquad(int nThreads);
	~CSquad();

	int NumThreads() { return nw; }
	void RunParallel(int command, void *params, ISquadJob *job);
};

class CSquadWorker {
	CSquad *pSquad;
	int myNum;

public:
	HANDLE thread_handle;

	//called from Squad
	CSquadWorker(CSquad *squad, int mynum) : pSquad(squad), myNum(mynum), thread_handle(NULL) {}
	void ThreadProc() {	pSquad->ThreadProc(this); };

	//called from Job
	void GetSegment(int totalsize, int &segstart, int &segsize);
	void Sync()  { pSquad->Sync(myNum); }
	int NumThreads() { return pSquad->NumThreads(); }
	int MyNum() { return myNum; }

};


#endif