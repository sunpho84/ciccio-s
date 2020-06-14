#ifndef _POOL_HPP
#define _POOL_HPP

#ifdef HAVE_CONFIG_H
 #include "config.hpp"
#endif

#include <atomic>
#include <functional>
#include <omp.h>
#include <tuple>
#include <vector>

//#include <threads/mutex.hpp>
//#include <threads/tBarrier.hpp>

#include <base/debug.hpp>

//#include <external/inplace_function.h>

#ifndef EXTERN_POOL
 #define EXTERN_POOL extern
#define INIT_POOL_TO(...)
#else
 #define INIT_POOL_TO(...) (__VA_ARGS__)
#endif

namespace ciccios
{
  /// Thread id of master thread
  [[ maybe_unused ]]
  constexpr int masterThreadId=0;
  
  namespace resources
    {
      /// Get the total number of threads
      inline int getNThreads()
      {
	/// Output
	int res;
	
#pragma omp parallel
	res=omp_get_num_threads();
	
	return res;
      }
      
      /// Number of threads
      ///
      /// Internal storage, not to be accessed
      EXTERN_POOL int nThreads INIT_POOL_TO(resources::getNThreads());
  }
  
  /// Number of threads
  static const int& nThreads=resources::nThreads;
  
  /// Contains a thread pool
  namespace ThreadPool
  {
    /// Maximal size of the stack used for thw work
    //static constexpr int MAX_POOL_FUNCTION_SIZE=128;
    
    /// Number of threads waiting for work
    EXTERN_POOL std::atomic<int> nThreadsWaitingForWork INIT_POOL_TO(0);
    
    EXTERN_POOL std::atomic<int> nWorksAssigned INIT_POOL_TO(0);
    
    /// States if the pool is started
    EXTERN_POOL bool poolIsStarted INIT_POOL_TO(false);
    
    /// Type to encapsulate the work to be done
    using Work=
      std::function<void(int)>;
    //stdext::inplace_function<void(int),MAX_POOL_FUNCTION_SIZE>;
    
    /// Work to be done in the pool
    ///
    /// This incapsulates a function returning void, and getting an
    /// integer as an argument, corresponding to the thread
    EXTERN_POOL Work work;
    
    /// Assert that only the pool is accessing
    inline void assertPoolOnly(const int& threadId) ///< Calling thread
    {
      if(threadId==masterThreadId)
	CRASHER<<"Only pool threads are allowed"<<endl;
    }
    
    /// Assert that only the master thread is accessing
    INLINE_FUNCTION
    void assertMasterOnly(const int& threadId) ///< Calling thread
    {
      if(threadId!=masterThreadId)
	CRASHER<<"Only master thread is allowed, but thread "<<threadId<<" is trying to act"<<endl;
    }
    
    /// Get the thread id of the current thread
    INLINE_FUNCTION
    int getThreadId()
    {
      return omp_get_thread_num();
    }
    
    /// Compares the thread tag with the master one
    INLINE_FUNCTION
    bool isMasterThread(const int& threadId)
    {
      return (threadId==masterThreadId);
    }
    
    /// Wait all workers are waiting for work
    INLINE_FUNCTION
    void waitThatAllWorkersWaitForWork()
    {
      while(poolIsStarted and nThreadsWaitingForWork!=nThreads-1);
    }
    
    namespace resources
    {
      /// Starts a parallel section, if pool is started
      template <typename F>
      INLINE_FUNCTION void parallelWhenPoolStarted(F&& f) ///< Function embedding the work
      {
	waitThatAllWorkersWaitForWork();
	work=std::move(f);
	
	nThreadsWaitingForWork=0;
	nWorksAssigned.store(nWorksAssigned+1,std::memory_order_release);
	
	work(masterThreadId);
      }
      
      /// Main thread part of the pool
      template <typename F,
		typename...Args>
      INLINE_FUNCTION
      void poolMaster(const F& f,
		      Args&&...args)
      {
	f(std::forward<Args>(args)...);
	
	waitThatAllWorkersWaitForWork();
	
	poolIsStarted=false;
	
	parallelWhenPoolStarted([](const int&){});
      }
      
      /// Wait that the count of the number of work has been increased
      INLINE_FUNCTION
      void waitThatMasterSignalsNewWork(const int prevNWorkAssigned)
      {
	// The work will be assigned only when the master sees all workers waiting
	while(nWorksAssigned.load(std::memory_order_relaxed)==prevNWorkAssigned);
      }
      
      /// Wait for the work to come
      INLINE_FUNCTION
      void waitForWork()
      {
	/// Previous numebr fo work assigned
	const int prevNWorkAssigned=nWorksAssigned;
	
	// Increase the number of threads waiting for work
	nThreadsWaitingForWork.fetch_add(1);
	
	/// Wait that the master gives signal that work has been assigned
	waitThatMasterSignalsNewWork(prevNWorkAssigned);
	
	// Cache synchronization, possibly useless
	std::atomic_thread_fence(std::memory_order_acquire);
      }
      
      /// Other thread part of the pool
      INLINE_FUNCTION
      void poolWorkerLoop(const int& threadId)
      {
	do
	  {
	    waitForWork();
	    
	    work(threadId);
	  }
	while(poolIsStarted);
      }
    }
    
    /// Starts the pool thread taking a function and arguments as argument
    template <typename F,
	      typename...Args>
    void poolLoop(F&& f,
		  Args&&...args)
    {
      // Checks that the pool is not filled, to avoid recursive call
      if(poolIsStarted)
	CRASHER<<"Cannot fill again the pool!"<<endl;
      else
	poolIsStarted=true;
      
#pragma omp parallel
	{
	  /// Gets the thread id
	  const int threadId=getThreadId();
	  
	  if(isMasterThread(threadId))
	    resources::poolMaster(std::forward<F>(f),std::forward<Args>(args)...);
	  else
	    resources::poolWorkerLoop(threadId);
	}
    }
    
    /// Starts a parallel section
    ///
    /// The object \c f must be callable, returning void and getting
    /// an integer as a parameter, representing the thread id
    template <typename F>
    INLINE_FUNCTION void parallel(F&& f) ///< Function embedding the work
    {
      if(poolIsStarted)
	resources::parallelWhenPoolStarted(f);
      else
	poolLoop(resources::parallelWhenPoolStarted<F>,std::forward<F>(f));
    }
    
    /// Split a loop into \c nTrheads chunks, giving each chunk as a work for a corresponding thread
    template <typename Size,           // Type for the range of the loop
	      typename F>              // Type of the function
    INLINE_FUNCTION
    void loopSplit(const Size& beg,  ///< Beginning of the loop
		   const Size& end,  ///< End of the loop
		   const F& f)       ///< Function to be called, accepting two integers: the first is the thread id, the second the loop argument
    {
      parallel([beg,end,nPieces=nThreads,&f](const int& threadId) INLINE_ATTRIBUTE
	     {
	       /// Workload for each thread, taking into account the remainder
	       const Size threadLoad=
		 (end-beg+nPieces-1)/nPieces;
	       
	       /// Beginning of the chunk
	       const Size threadBeg=
		 threadLoad*threadId;
	       
	       /// End of the chunk
	       const Size threadEnd=
		 std::min(end,threadBeg+threadLoad);
	       
	       for(Size i=threadBeg;i<threadEnd;i++)
		 f(threadId,i);
	     });
    }
  }
}

#undef EXTERN_POOL
#undef INIT_POOL_TO

#endif