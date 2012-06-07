/*
  TODO: MP AB Copyright
*/

/*
  Interface
  ~~~~~~~~~
   (
    - This is an APC request queue
    - We assume there is a particular owner thread which periodically calls
      process_apc_requests() to serve the call requests.
    - Other threads can post call requests, and block until they are exectued.
  )

  Implementation
  ~~~~~~~~~~~~~~
  - The target has a mutex-guarded request queue.

  - After the request has been put into queue, the requestor waits for request
    to be satisfied. The worker satisifes the request and signals the
    requestor.
*/

/*
  Target for asynchronous procedue calls (APCs).
*/
class Apc_target
{
  mysql_mutex_t *LOCK_thd_data_ptr;
public:
  Apc_target() : enabled(0), apc_calls(NULL) /*, call_queue_size(0)*/ {} 
  ~Apc_target() { DBUG_ASSERT(!enabled && !apc_calls);}

  void init(mysql_mutex_t *target_mutex);
  void destroy();
  void enable();
  void disable();
  
  void process_apc_requests();

  typedef void (*apc_func_t)(void *arg);
  
  /*
    Make an APC call: schedule it for execution and wait until the target
    thread has executed it. This function must not be called from a thread
    that's different from the target thread.

    @retval FALSE - Ok, the call has been made
    @retval TRUE  - Call wasnt made (either the target is in disabled state or
                    timeout occured)
  */
  bool make_apc_call(apc_func_t func, void *func_arg, 
                     int timeout_sec, bool *timed_out);

#ifndef DBUG_OFF
  int n_calls_processed; /* Number of calls served by this target */
#endif
private:
  class Call_request;

  /* 
    Non-zero value means we're enabled. It's an int, not bool, because one can
    call enable() N times (and then needs to call disable() N times before the 
    target is really disabled)
  */
  int enabled;

  /* 
    Circular, double-linked list of all enqueued call requests. 
    We use this structure, because we 
     - process requests sequentially (i.e. they are removed from the front)
     - a thread that has posted a request may time out (or be KILLed) and 
       cancel the request, which means we need a fast request-removal
       operation.
  */
  Call_request *apc_calls;
 

  /*
    This mutex is used to
    - make queue put/remove operations atomic (one must be in posession of the
      mutex when putting/removing something from the queue)

    - make sure that nobody enqueues a request onto an Apc_target which has 
      disabled==TRUE. The idea is:
      = requestor must be in possession of the mutex and check that
        disabled==FALSE when he is putting his request into the queue.
      = When the owner (ie. service) thread changes the Apc_target from 
        enabled to disabled, it will acquire the mutex, disable the 
        Apc_target (preventing any new requests), and then serve all pending 
        requests. 
      That way, we will never have the situation where the Apc_target is 
      disabled, but there are some un-served requests.
  */
  //pthread_mutex_t LOCK_apc_queue;

  class Call_request
  {
  public:
    apc_func_t func; /* Function to call */
    void *func_arg;  /* Argument to pass it */
    bool processed;

    //pthread_mutex_t LOCK_request;
    //pthread_cond_t COND_request;

    /* Condition that will be signalled when the request has been served */
    mysql_cond_t COND_request;

    Call_request *next;
    Call_request *prev;
    
    const char *what; /* (debug) state of the request */
  };

  void enqueue_request(Call_request *qe);
  void dequeue_request(Call_request *qe);

  /* return the first call request in queue, or NULL if there are none enqueued */
  Call_request *get_first_in_queue()
  {
    return apc_calls;
  }
};

///////////////////////////////////////////////////////////////////////

