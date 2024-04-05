/*******************************************************************************
 * Copyright IBM Corp. and others 1991
 *
 * This program and the accompanying materials are made available under
 * the terms of the Eclipse Public License 2.0 which accompanies this
 * distribution and is available at https://www.eclipse.org/legal/epl-2.0/
 * or the Apache License, Version 2.0 which accompanies this distribution and
 * is available at https://www.apache.org/licenses/LICENSE-2.0.
 *
 * This Source Code may also be made available under the following
 * Secondary Licenses when the conditions for such availability set
 * forth in the Eclipse Public License, v. 2.0 are satisfied: GNU
 * General Public License, version 2 with the GNU Classpath
 * Exception [1] and GNU General Public License, version 2 with the
 * OpenJDK Assembly Exception [2].
 *
 * [1] https://www.gnu.org/software/classpath/license.html
 * [2] https://openjdk.org/legal/assembly-exception.html
 *
 * SPDX-License-Identifier: EPL-2.0 OR Apache-2.0 OR GPL-2.0-only WITH Classpath-exception-2.0 OR GPL-2.0-only WITH OpenJDK-assembly-exception-1.0
 *******************************************************************************/


/**
 * @file
 * @ingroup GC_Base
 */

#if !defined(PARALLEL_DISPATCHER_HPP_)
#define PARALLEL_DISPATCHER_HPP_

#include "omrcfg.h"

#include "modronopt.h"
#include "modronbase.h"

#include "BaseVirtual.hpp"
#include "EnvironmentBase.hpp"
#include "GCExtensionsBase.hpp"

class MM_EnvironmentBase;

class MM_ParallelDispatcher : public MM_BaseVirtual
{
	/*
	 * Data members
	 */
private:
protected:
	MM_Task *_task;
	MM_GCExtensionsBase *_extensions;

	enum {
		worker_status_inactive = 0,	/* Must be 0 - set at initialization time by memset */
		worker_status_waiting,
		worker_status_reserved,
		worker_status_active,
		worker_status_dying
	};

	uintptr_t _threadShutdownCount;
	omrthread_t *_threadTable;
	uintptr_t *_statusTable;
	MM_Task **_taskTable;
	
	omrthread_monitor_t _workerThreadMutex;
	omrthread_monitor_t _dispatcherMonitor; /**< Provides signalling between threads for startup and shutting down as well as the thread that initiated the shutdown */

	/* The synchronize mutex should eventually be a table of mutexes that are distributed to each */
	/* Task as they are dispatched.  For now, since there is only one task active at any time, a */
	/* single mutex is sufficient */
	omrthread_monitor_t _synchronizeMutex;
	
	bool _workerThreadsReservedForGC;  /**< States whether or not the worker threads are currently taking part in a GC */
	bool _inShutdown;  /**< Shutdown request is received */

	uintptr_t _threadCountMaximum; /**< maximum threadcount - this is the size of the thread tables etc */
	uintptr_t _threadCount; /**< number of threads currently forked */
	uintptr_t _activeThreadCount; /**< number of threads actively running a task */
	uintptr_t _threadsToReserve; /**< Indicates number of threads remaining to dispatch tasks upon notify. Must be exactly 0 after tasks are dispatched. */

	omrsig_handler_fn _handler;
	void* _handler_arg;
	uintptr_t _defaultOSStackSize; /**< default OS stack size */

#if defined(J9VM_OPT_CRIU_SUPPORT)
	uintptr_t _poolMaxCapacity;  /**< Size of the dispatcher tables: _taskTable, _statusTable & _threadTable. */
#endif /* defined(J9VM_OPT_CRIU_SUPPORT) */

public:
	/*
	 * Function members
	 */
private:
#if defined(J9VM_OPT_CRIU_SUPPORT)
	/**
	 * Expand/fill the thread pool by starting up threads based on what H/W supports.
	 * This API is capped by the initial thread pool size, i.e expanding
	 * past _poolMaxCapacity is not possible
	 * (expanding the dispatcher tables is currently not supported).
	 *
	 * This API assumes CRIU is the only consumer (not tested for general use).
	 * The following conditions are required while expanding the thread pool:
	 *     1) Caller is NOT holding exclusive VM access.
	 *     2) Dispatcher is idle (no task can be dispatched).
	 *     3) Dispatcher can't be in/enter shutdown.
	 *
	 * @param[in] env the current environment.
	 * @return boolean indicating if threads started up successfully.
	 */
	bool expandThreadPool(MM_EnvironmentBase *env);

	/**
	 * Contract the thread pool by shutting down threads in the pool to obtain newThreadCount.
	 *
	 * This API assumes  CRIU is the only consumer (not tested for general use).
	 * The following conditions are assumed while contracting the thread pool:
	 *     1) API can only be called once during VM lifetime.
	 *     2) Caller is NOT holding exclusive VM access.
	 *     3) Dispatcher is idle (no task can be dispatched).
	 *     4) Another party can't enter Dispatcher shutdown.
	 *
	 * @param[in] env the current environment.
	 * @param[in] newThreadCount the number of threads to keep in the thread pool.
	 * @return void
	 */
	void contractThreadPool(MM_EnvironmentBase *env, uintptr_t newThreadCount);

	/**
	 * Reinitialize (resize and allocate) the dispatcher tables so that the thread pool
	 * can be expanded beyond the initial size at startup.
	 *
	 * @param[in] env the current environment.
	 * @param[in] newPoolSize the number of threads that need to be accommodated by the dispatcher.
	 * @return bool indicating if the thread pool tables can accommodate the newPoolSize (
	 * i.e., successfully reallocated or already have capacity).
	 */
	bool reinitializeThreadPool(MM_EnvironmentBase *env, uintptr_t newPoolSize);
#endif /* defined(J9VM_OPT_CRIU_SUPPORT) */
protected:
	virtual void workerEntryPoint(MM_EnvironmentBase *env);
	virtual void mainEntryPoint(MM_EnvironmentBase *env);

	bool initialize(MM_EnvironmentBase *env);
	
	virtual void prepareThreadsForTask(MM_EnvironmentBase *env, MM_Task *task, uintptr_t threadCount);
	void cleanupAfterTask(MM_EnvironmentBase *env);
	virtual uintptr_t getThreadPriority();

	/**
	 * Decides whether the dispatcher also start a separate thread to be the main
	 * GC thread. Usually no, because the main thread will be the thread that
	 * requested the GC.
	 */  
	virtual bool useSeparateMainThread() { return false; }
	
	virtual void acceptTask(MM_EnvironmentBase *env);
	virtual void completeTask(MM_EnvironmentBase *env);
	virtual void wakeUpThreads(uintptr_t count);
	
	virtual uintptr_t recomputeActiveThreadCountForTask(MM_EnvironmentBase *env, MM_Task *task, uintptr_t newThreadCount); 

	void setThreadInitializationComplete(MM_EnvironmentBase *env);
	
	uintptr_t adjustThreadCount();
	
	/**
	 * Main routine to fork and startup GC threads.
	 *
	 * @param[in] workerThreadCount the thread pool index to start at.
	 * @param[in] maxWorkerThreadIndex the max thread pool index.
	 * @return boolean indicating if threads started up successfully.
	 */
	bool internalStartupThreads(uintptr_t workerThreadCount, uintptr_t maxWorkerThreadIndex);

public:
	virtual bool startUpThreads();
	virtual void shutDownThreads();

#if defined(J9VM_OPT_CRIU_SUPPORT)
	/**
	 * Reinitialize the dispatcher (i.e thread pool) to accommodate the change
	 * in restore environment.
	 *
	 * @param[in] env the current environment.
	 * @return boolean indicating whether the dispather was successfully updated.
	 */
	virtual bool reinitializeForRestore(MM_EnvironmentBase *env);

	/**
	 * Release dispatcher threads to improve the overall memory usage and
	 * speed up restore times that occur due to GC.
	 *
	 * @param[in] env the current environment.
	 * @param[in] newThreadCount the number of threads to keep in the thread pool.
	 * @return void
	 */
	virtual void prepareForCheckpoint(MM_EnvironmentBase *env, uintptr_t newThreadCount);

	/**
	 * Fetch the size allocated for the thread pool (max threads supported)
	 * (i.e., array size for dispatcher tables: _taskTable, _statusTable & _threadTable)
	 *
	 * @return uintptr_t indicating the thread pool size.
	 */
	MMINLINE uintptr_t getPoolMaxCapacity() { return _poolMaxCapacity; }
#endif /* defined(J9VM_OPT_CRIU_SUPPORT) */

	virtual bool condYieldFromGCWrapper(MM_EnvironmentBase *env, uint64_t timeSlack = 0) { return false; }
	
	MMINLINE uintptr_t threadCount() { return _threadCount; }
	MMINLINE uintptr_t threadCountMaximum() { return _threadCountMaximum; }
	MMINLINE omrthread_t *getThreadTable() { return _threadTable; }
	MMINLINE uintptr_t activeThreadCount() { return _activeThreadCount; }

	MMINLINE omrsig_handler_fn getSignalHandler() {return _handler;}
	MMINLINE void *getSignalHandlerArg() {return _handler_arg;}

	virtual void run(MM_EnvironmentBase *env, MM_Task *task, uintptr_t threadCount = UDATA_MAX);

	static MM_ParallelDispatcher *newInstance(MM_EnvironmentBase *env, omrsig_handler_fn handler, void* handler_arg, uintptr_t defaultOSStackSize);
	virtual void kill(MM_EnvironmentBase *env);

	MM_ParallelDispatcher(MM_EnvironmentBase *env, omrsig_handler_fn handler, void* handler_arg, uintptr_t defaultOSStackSize) :
		MM_BaseVirtual()
		,_task(NULL)
		,_extensions(MM_GCExtensionsBase::getExtensions(env->getOmrVM()))
		,_threadShutdownCount(0)
		,_threadTable(NULL)
		,_statusTable(NULL)
		,_taskTable(NULL)
		,_workerThreadMutex(NULL)
		,_dispatcherMonitor(NULL)
		,_synchronizeMutex(NULL)
		,_workerThreadsReservedForGC(false)
		,_inShutdown(false)
		,_threadCountMaximum(1)
		,_threadCount(1)
		,_activeThreadCount(1)
		,_threadsToReserve(0)		
		,_handler(handler)
		,_handler_arg(handler_arg)
		,_defaultOSStackSize(defaultOSStackSize)
#if defined(J9VM_OPT_CRIU_SUPPORT)
		,_poolMaxCapacity(0)
#endif /* defined(J9VM_OPT_CRIU_SUPPORT) */
	{
		_typeId = __FUNCTION__;
	}

	/*
	 * Friends
	 */
	friend class MM_Task;
	friend uintptr_t dispatcher_thread_proc2(OMRPortLibrary* portLib, void *info);
};

#endif /* PARALLEL_DISPATCHER_HPP_ */
