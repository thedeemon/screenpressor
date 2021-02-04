//---------------------------------------------------------------------------
//  Part of ScreenPressor lossless video codec
//  (C) Infognition Co. Ltd.
//---------------------------------------------------------------------------
// Implementation of classes used to perform computations in parallel worker threads.

#include "squad.h"

DWORD WINAPI SquadWorkerThreadProc(LPVOID lpParameter)
{
	((CSquadWorker*)lpParameter)->ThreadProc();
	return 0;
}

//which part of work should be done by this worker?
void CSquadWorker::GetSegment(int totalsize, int &segstart, int &segsize)
{
	if (totalsize >= pSquad->nw) {
		segstart = totalsize * myNum / pSquad->nw;
		int segend = totalsize * (myNum+1) / pSquad->nw;
		if (segend > totalsize)
			segend = totalsize;
		segsize = segend - segstart;
	} else { //totalsize < nw
		if (myNum < totalsize) {
			segstart = myNum; segsize = 1;
		} else {
			segstart = segsize = 0;
		}
	}
}

/////////////////////////////////////////////////////////////////////

//create a bunch of worker threads
CSquad::CSquad(int nThreads)
{
	if (nThreads<1) 
		nThreads = 1;
	nw = nThreads;
	workers.resize(nThreads);
	for(int i=0;i<nThreads; i++)
		workers[i] = new CSquadWorker(this, i);
	if (nThreads>1) {
		ev_free.resize(nThreads);
		ev_havejob.resize(nThreads);
		ev_sync.resize(nThreads);
		DWORD tid = 0;
		for(int i=0;i<nThreads; i++) {
			ev_free[i] =  CreateEvent(NULL, TRUE/*manual*/, FALSE/*initial*/, NULL);
			ev_havejob[i] = CreateEvent(NULL, FALSE/*auto*/, FALSE, NULL);
			ev_sync[i] =    CreateEvent(NULL, FALSE/*auto*/, FALSE, NULL);
			workers[i]->thread_handle = CreateThread(NULL,256*1024, SquadWorkerThreadProc, workers[i], 0, &tid);
		}
	}
}

//end all work with worker threads
CSquad::~CSquad()
{
	if (nw>1) { //stop threads
		WaitTillAllFree();
		cur_command = -1;		
		SignalJob();
		std::vector<HANDLE> ths;  //= (HANDLE*)calloc(nw, sizeof(HANDLE));
		for(int i=0;i<nw;i++)
			ths.push_back(workers[i]->thread_handle);
		WaitForMultipleObjects(nw, &ths[0], TRUE, INFINITE);
		//free(ths);

		for(int i=0;i<nw;i++) {
			CloseHandle(ev_free[i]);
			CloseHandle(ev_havejob[i]);
			CloseHandle(ev_sync[i]);
		}
		//free(ev_free); free(ev_havejob); free(ev_sync);
	}
	for(int i=0;i<nw; i++) {
		if (workers[i]->thread_handle)
			CloseHandle(workers[i]->thread_handle);
		delete workers[i];
	}
	//free(workers);
}

//wait until all workers are free
void CSquad::WaitTillAllFree()
{
	if (nw<2) return;
	WaitForMultipleObjects(nw, &ev_free[0], TRUE/*all*/, INFINITE);
}

//signal to workers that they have a job
void CSquad::SignalJob() 
{
	if (nw<2) return;
	for(int i=0;i<nw;i++)
		SetEvent(ev_havejob[i]);
}

//wait until all workers come to this point
void CSquad::Sync(int mynum)
{
	if (nw<2) return;	
	if (mynum > 0) {
		SignalObjectAndWait(ev_sync[mynum], ev_havejob[mynum], INFINITE, FALSE);
	} else {
		WaitForMultipleObjects(nw-1, &ev_sync[1], TRUE/*all*/, INFINITE);
		for(int i=1;i<nw;i++)
			SetEvent(ev_havejob[i]);
	}
}

//run some task in parallel threads
//each worker will call ISquadJob->RunCommand from its thread with command and params set here
void CSquad::RunParallel(int command, void *params, ISquadJob *job)
{
	cur_command = command;
	cur_params = params;
	cur_job = job;

	if (nw>1) { //run in worker threads
		WaitTillAllFree();
		for(int i=0;i<nw;i++)
			ResetEvent(ev_free[i]);
		SignalJob();
		WaitTillAllFree();
	} else  //run in this thread
		job->RunCommand(command, params, workers[0]);
}

//main loop of each worker thread
void CSquad::ThreadProc(CSquadWorker *sqworker)
{	
	const int mynum = sqworker->MyNum();
	while(1) {
		DWORD waitres = SignalObjectAndWait(ev_free[mynum], ev_havejob[mynum], INFINITE, FALSE);
		if (waitres==WAIT_OBJECT_0) {//signaled
			if (cur_command < 0) {//stop
				break; 
			}
			cur_job->RunCommand(cur_command, cur_params, sqworker);
		} 
	}
}
