// Filename		: ThreadPool.cpp
// Author		: Siddharth Barman
// Date			: 18 Sept 2005
// Description	: Implementation file for CThreadPool class. 
//------------------------------------------------------------------------------
#include "stdafx.h"
#include "ThreadPool.h"
#include <process.h>

#ifdef _DEBUG
#undef THIS_FILE
static char THIS_FILE[]=__FILE__;
#define new DEBUG_NEW
#endif
//------------------------------------------------------------------------------

/* Parameters	: pointer to a _threadData structure.
   Description	: This is the internal thread function which will run 
				  continuously till the Thread Pool is deleted. Any user thread
				  functions will run from within this function.
*/
#ifdef USE_WIN32API_THREAD
DWORD WINAPI CThreadPool::_ThreadProc(LPVOID pParam)
#else
UINT __stdcall CThreadPool::_ThreadProc(LPVOID pParam)
#endif
{
	DWORD					dwWait;
	CThreadPool*			pool;
  	HANDLE					hThread = GetCurrentThread();
	LPTHREAD_START_ROUTINE  proc;
	LPVOID					data;
	DWORD					dwThreadId = GetCurrentThreadId();
	HANDLE					hWaits[2];
	IRunObject*				runObject;
	bool					bAutoDelete;

//	ASSERT(pParam != NULL);
	if(NULL == pParam)
	{
		return -1;
	}

	pool = static_cast<CThreadPool*>(pParam);
	hWaits[0] = pool->GetWaitHandle(dwThreadId);
	hWaits[1] = pool->GetShutdownHandle();
	
	loop_here:

	dwWait = WaitForMultipleObjects(2, hWaits, FALSE, INFINITE);

	if(dwWait - WAIT_OBJECT_0 == 0)
	{
		// a new function was added, go and get it
		if(pool->GetThreadProc(dwThreadId, proc, &data, &runObject))
		{
			pool->BusyNotify(dwThreadId);
			
			if(proc == NULL)
			{
				// a function object is being used instead of 
				// a function pointer.
				bAutoDelete = runObject->AutoDelete();
				
				runObject->Initialize();
				runObject->Run();

				// see if we need to free this object
				if(bAutoDelete)
				{/*
					#ifdef _DEBUG
					TCHAR szMessage[256];
					_stprintf(szMessage, _T("Deleting Run Object of thread , handle = %d, id = %d\n"), 
					 hThread, dwThreadId);
					TRACE(szMessage);
					#endif
				*/
					runObject->DeleteInstance();
				}
				else
				{/*
					#ifdef _DEBUG
					TCHAR szMessage[256];
					_stprintf(szMessage, _T("Not Deleted Run Object of thread , handle = %d, id = %d\n"), 
					 hThread, dwThreadId);
					TRACE(szMessage);
					#endif
					*/
				}
			}
			else
			{
				proc(data);
			}

			pool->FinishNotify(dwThreadId); // tell the pool, i am now free
		}

		goto loop_here;
	}	
		
	return 0;
}

//------------------------------------------------------------------------------
/* Parameters	: Pool size, indicates the number of threads that will be 
				  available in the queue.
*******************************************************************************/
CThreadPool::CThreadPool(int nPoolSize, int nPoolMaxSize, bool bCreateNow)
{
	m_nPoolSize = nPoolSize;
	m_nPoolMaxSize = nPoolMaxSize;

	if(bCreateNow)
	{
		if(!Create())
		{
			throw 1;
		}
	}
}
//------------------------------------------------------------------------------

/* Description	: Use this method to create the thread pool. The constructor of
				  this class by default will create the pool. Make sure you 
				  do not call this method without first calling the Destroy() 
				  method to release the existing pool.
   Returns		: true if everything went fine else returns false.
  *****************************************************************************/
bool CThreadPool::Create()
{
/*	HANDLE		hThread;
	DWORD		dwThreadId;
	_ThreadData ThreadData;
	TCHAR		szEvtName[20];
	UINT		uThreadId;	
	*/
	InitializeCriticalSection(&m_cs); // this is used to protect the shared 
									  // data like the list and map
	
	// create the event which will signal the threads to stop
	m_hNotifyShutdown = CreateEvent(NULL, TRUE, FALSE, SHUTDOWN_EVT_NAME);
//	ASSERT(m_hNotifyShutdown != NULL);
	if(!m_hNotifyShutdown)
	{
		return false;
	}

	// create the threads
	for(int nIndex = 0; nIndex < m_nPoolSize; nIndex++)
	{
		if(!CreateThread(true))
			break;
		/*
		_stprintf(szEvtName, _T("Thread%d"), nIndex);
				
		#ifdef USE_WIN32API_THREAD
		hThread = CreateThread(NULL, 0, CThreadPool::_ThreadProc, 
							   this, CREATE_SUSPENDED, &dwThreadId);
		#else
		hThread = (HANDLE)_beginthreadex(NULL, 0, CThreadPool::_ThreadProc, this,  
								 CREATE_SUSPENDED, (UINT*)&uThreadId);
		dwThreadId = uThreadId;
		#endif
		ASSERT(NULL != hThread);
		
		if(hThread)
		{
			// add the entry to the map of threads
			ThreadData.bFree		= true;
			ThreadData.WaitHandle	= CreateEvent(NULL, TRUE, FALSE, szEvtName);
			ThreadData.hThread		= hThread;
			ThreadData.dwThreadId	= dwThreadId;
		
			m_threads.insert(ThreadMap::value_type(dwThreadId, ThreadData));		

			ResumeThread(hThread); 
		
			#ifdef _DEBUG
			TCHAR szMessage[256];
			_stprintf(szMessage, _T("Thread created, handle = %d, id = %d\n"), 
					  hThread, dwThreadId);
			TRACE(szMessage);
			#endif
		}
		else
		{
			return false;
		}*/
	}

	return true;
}
//------------------------------------------------------------------------------

CThreadPool::~CThreadPool()
{
	Destroy();
}
//------------------------------------------------------------------------------

/* Description	: Use this method to destory the thread pool. The destructor of
				  this class will destory the pool anyway. Make sure you 
				  this method before calling a Create() when an existing pool is 
				  already existing.
   Returns		: void
  *****************************************************************************/
void CThreadPool::Destroy()
{
	// tell all threads to shutdown.
	SetEvent(m_hNotifyShutdown);

	// lets give the thread one second atleast to terminate
	Sleep(1000);
	
	ThreadMap::iterator iter;
	_ThreadData ThreadData;
	
	// walk through the events and threads and close them all
	for(iter = m_threads.begin(); iter != m_threads.end(); iter++)
	{
		ThreadData = (*iter).second;		
		CloseHandle(ThreadData.WaitHandle);
		CloseHandle(ThreadData.hThread);
	}

	// close the shutdown event
	CloseHandle(m_hNotifyShutdown);

	// delete the critical section
	DeleteCriticalSection(&m_cs);

	// empty all collections
	m_functionList.clear();
	m_threads.clear();
}
//------------------------------------------------------------------------------

int CThreadPool::GetPoolSize()
{
	return m_nPoolSize;
}
//------------------------------------------------------------------------------

/* Parameters	: nSize - number of threads in the pool.   
   ****************************************************************************/
void CThreadPool::SetPoolSize(int nSize)
{
//	ASSERT(nSize > 0);
	if(nSize <= 0)
	{
		return;
	}

	m_nPoolSize = nSize;
}

//------------------------------------------------------------------------------
HANDLE CThreadPool::GetShutdownHandle()
{
	return m_hNotifyShutdown;
}
//------------------------------------------------------------------------------

/* Parameters	: hThread - Handle of the thread that is invoking this function.
   Return		: A ThreadProc function pointer. This function pointer will be 
			      executed by the actual calling thread.
				  NULL is returned if no functions list is empty.
																			  */
bool CThreadPool::GetThreadProc(DWORD dwThreadId, LPTHREAD_START_ROUTINE& Proc, 
								LPVOID* Data, IRunObject** runObject)
{
	LPTHREAD_START_ROUTINE  lpResult = NULL;
	_FunctionData			FunctionData;
	FunctionList::iterator	iter;

	// get the first function info in the function list
	EnterCriticalSection(&m_cs);
	
	iter = m_functionList.begin();

	if(iter != m_functionList.end())
	{
		FunctionData = (*iter);

		Proc = FunctionData.lpStartAddress;
		
		if(NULL == Proc) // is NULL for function objects
		{		
			*runObject = static_cast<IRunObject*>(FunctionData.pData);
		}
		else		
		{
			// this is a function pointer
			*Data		= FunctionData.pData;
			runObject	= NULL;
		}		

		// 从任务队列中删除
		m_functionList.pop_front(); // remove the function from the list
	
		LeaveCriticalSection(&m_cs);
		return true;	
	}
	else
	{
		LeaveCriticalSection(&m_cs);
		return false;
	}
}
//------------------------------------------------------------------------------

/* Parameters	: hThread - Handle of the thread that is invoking this function.
   Description	: When ever a thread finishes executing the user function, it 
				  should notify the pool that it has finished executing.      
																			  */
void CThreadPool::FinishNotify(DWORD dwThreadId)
{
	ThreadMap::iterator iter;
	
	EnterCriticalSection(&m_cs);
	iter = m_threads.find(dwThreadId);

	if(iter == m_threads.end())	// if search found no elements
	{			
		LeaveCriticalSection(&m_cs);
//		ASSERT(!("No matching thread found."));
		return;
	}
	else
	{	
		m_threads[dwThreadId].bFree = true;

		#ifdef _DEBUG
		TCHAR szMessage[256];
		sprintf(szMessage, ("Thread free, thread id = %d\n"), dwThreadId);
//		TRACE(szMessage);
		#endif

		if(!m_functionList.empty())
		{
			// there are some more functions that need servicing, lets do that.
			// By not doing anything here we are letting the thread go back and
			// check the function list and pick up a function and execute it.
			LeaveCriticalSection(&m_cs);
			return;
		}		
		else
		{
			// back to sleep, there is nothing that needs servicing.
			LeaveCriticalSection(&m_cs);
			ResetEvent(m_threads[dwThreadId].WaitHandle);
		}
	}	
}
//------------------------------------------------------------------------------

void CThreadPool::BusyNotify(DWORD dwThreadId)
{
	ThreadMap::iterator iter;
	
	EnterCriticalSection(&m_cs);

	iter = m_threads.find(dwThreadId);

	if(iter == m_threads.end())	// if search found no elements
	{
		LeaveCriticalSection(&m_cs);
//		ASSERT(!_T("No matching thread found."));
	}
	else
	{		
		m_threads[dwThreadId].bFree = false;		

		#ifdef _DEBUG
		TCHAR szMessage[256];
		sprintf(szMessage, ("Thread busy, thread id = %d\n"), dwThreadId);
//		TRACE(szMessage);
		#endif

		LeaveCriticalSection(&m_cs);
	}	
}
//------------------------------------------------------------------------------

/* Parameters	: pFunc - function pointer of type ThreadProc
				  pData - An LPVOID pointer
   Decription	: This function is to be called by clients which want to make 
				  use of the thread pool.
  *****************************************************************************/
void CThreadPool::Run(LPTHREAD_START_ROUTINE pFunc, LPVOID pData, 
					  ThreadPriority priority)
{
	_FunctionData funcdata;

	funcdata.lpStartAddress = pFunc;
	funcdata.pData			= pData;

	// add it to the list
	EnterCriticalSection(&m_cs);
	if(priority == Low)
	{
		m_functionList.push_back(funcdata);
	}
	else
	{
		m_functionList.push_front(funcdata);
	}
	LeaveCriticalSection(&m_cs);

	// See if any threads are free
	ThreadMap::iterator iter;
	_ThreadData ThreadData;

	EnterCriticalSection(&m_cs);
	bool bStarted = false;
	for(iter = m_threads.begin(); iter != m_threads.end(); iter++)
	{
		ThreadData = (*iter).second;
		
		if(ThreadData.bFree)
		{
			// here is a free thread, put it to work
			m_threads[ThreadData.dwThreadId].bFree = false;			
			SetEvent(ThreadData.WaitHandle); 
			// this thread will now call GetThreadProc() and pick up the next
			// function in the list.
			bStarted = true;
			break;
		}
	}

	if(!bStarted)
		CreateThread(false);

	LeaveCriticalSection(&m_cs);
}
//------------------------------------------------------------------------------

/* Parameters	: runObject - Pointer to an instance of class which implements
							  IRunObject interface.
				  priority  - Low or high. Based on this the object will be
							  added to the front or back of the list.
   Decription	: This function is to be called by clients which want to make 
				  use of the thread pool.
  *****************************************************************************/
void CThreadPool::Run(IRunObject* runObject, ThreadPriority priority)
{
//	ASSERT(runObject != NULL);
		
	_FunctionData funcdata;

	funcdata.lpStartAddress = NULL; // NULL indicates a function object is being
									// used instead.
	funcdata.pData			= runObject; // the function object

	// add it to the list
	EnterCriticalSection(&m_cs);
	if(priority == Low)
	{
		m_functionList.push_back(funcdata);
	}
	else
	{
		m_functionList.push_front(funcdata);
	}
	LeaveCriticalSection(&m_cs);

	// See if any threads are free
	ThreadMap::iterator iter;
	_ThreadData ThreadData;

	EnterCriticalSection(&m_cs);
	bool bStarted = false;
	for(iter = m_threads.begin(); iter != m_threads.end(); iter++)
	{
		ThreadData = (*iter).second;
		
		if(ThreadData.bFree)
		{
			// here is a free thread, put it to work
			m_threads[ThreadData.dwThreadId].bFree = false;			
			SetEvent(ThreadData.WaitHandle); 
			// this thread will now call GetThreadProc() and pick up the next
			// function in the list.
			bStarted = true;
			break;
		}
	}

	// 线程不足，添加线程
	if(!bStarted)
		CreateThread(false);
	
	LeaveCriticalSection(&m_cs);


}
//------------------------------------------------------------------------------

/* Parameters	: ThreadId - the id of the thread for which the wait handle is 
							 being requested.
   Returns		: NULL if no mathcing thread id is present.
				  The HANDLE which can be used by WaitForXXXObject API.
  *****************************************************************************/
HANDLE CThreadPool::GetWaitHandle(DWORD dwThreadId)
{
	HANDLE hWait;
	ThreadMap::iterator iter;
	
	EnterCriticalSection(&m_cs);
	iter = m_threads.find(dwThreadId);
	
	if(iter == m_threads.end())	// if search found no elements
	{
		LeaveCriticalSection(&m_cs);
		return NULL;
	}
	else
	{		
		hWait = m_threads[dwThreadId].WaitHandle;
		LeaveCriticalSection(&m_cs);
	}	

	return hWait;
}
//------------------------------------------------------------------------------

void CThreadPool::SetPoolMaxSize(int nMaxSize)
{
	m_nPoolMaxSize = nMaxSize;
}

// 创建线程，调用时需用临界区保护
// bSuspend:是否暂时挂起

bool CThreadPool::CreateThread(bool bSuspend)
{
	HANDLE		hThread;
	DWORD		dwThreadId;
	_ThreadData ThreadData;
	TCHAR		szEvtName[20];
	UINT		uThreadId;

	if(m_threads.size() >= m_nPoolMaxSize)
		return false;

	#ifdef USE_WIN32API_THREAD
	hThread = ::CreateThread(NULL, 0, CThreadPool::_ThreadProc, 
			this, CREATE_SUSPENDED, &dwThreadId);
	#else
		hThread = (HANDLE)_beginthreadex(NULL, 10, CThreadPool::_ThreadProc, this,  
			CREATE_SUSPENDED, (UINT*)&uThreadId);
		dwThreadId = uThreadId;
	#endif
//	ASSERT(NULL != hThread);
	
	if(hThread)
	{
		// add the entry to the map of threads
		ThreadData.bFree		= bSuspend;
		ThreadData.WaitHandle	= CreateEvent(NULL, TRUE, !bSuspend, szEvtName);
		ThreadData.hThread		= hThread;
		ThreadData.dwThreadId	= dwThreadId;
		
		m_threads.insert(ThreadMap::value_type(dwThreadId, ThreadData));		
		
		ResumeThread(hThread); 
		
		#ifdef _DEBUG
			TCHAR szMessage[256];
			sprintf(szMessage, ("Thread created, handle = %d, id = %d\n"),
				hThread, dwThreadId);
//			TRACE(szMessage);
		#endif
	}
	else
	{
		return false;
	}

	return true;
}
