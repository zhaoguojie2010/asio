//
// detail/scheduler.hpp
// ~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_SCHEDULER_HPP
#define ASIO_DETAIL_SCHEDULER_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"

#include "asio/error_code.hpp"
#include "asio/execution_context.hpp"
#include "asio/detail/atomic_count.hpp"
#include "asio/detail/conditionally_enabled_event.hpp"
#include "asio/detail/conditionally_enabled_mutex.hpp"
#include "asio/detail/op_queue.hpp"
#include "asio/detail/reactor_fwd.hpp"
#include "asio/detail/scheduler_operation.hpp"
#include "asio/detail/thread_context.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

struct scheduler_thread_info;

class scheduler
  : public execution_context_service_base<scheduler>,
    public thread_context
{
public:
  typedef scheduler_operation operation;

  // Constructor. Specifies the number of concurrent threads that are likely to
  // run the scheduler. If set to 1 certain optimisation are performed.
  ASIO_DECL scheduler(asio::execution_context& ctx,
      int concurrency_hint = 0);

  // Destroy all user-defined handler objects owned by the service.
  ASIO_DECL void shutdown();

  // Initialise the task, if required.
  ASIO_DECL void init_task();

  // Run the event loop until interrupted or no more work.
  ASIO_DECL std::size_t run(asio::error_code& ec);

  // Run until interrupted or one operation is performed.
  ASIO_DECL std::size_t run_one(asio::error_code& ec);

  // Run until timeout, interrupted, or one operation is performed.
  ASIO_DECL std::size_t wait_one(
      long usec, asio::error_code& ec);

  // Poll for operations without blocking.
  ASIO_DECL std::size_t poll(asio::error_code& ec);

  // Poll for one operation without blocking.
  ASIO_DECL std::size_t poll_one(asio::error_code& ec);

  // Interrupt the event processing loop.
  ASIO_DECL void stop();

  // Determine whether the scheduler is stopped.
  ASIO_DECL bool stopped() const;

  // Restart in preparation for a subsequent run invocation.
  ASIO_DECL void restart();

  // Notify that some work has started.
  void work_started()
  {
    ++outstanding_work_;
  }

  // Used to compensate for a forthcoming work_finished call. Must be called
  // from within a scheduler-owned thread.
  ASIO_DECL void compensating_work_started();

  // Notify that some work has finished.
  void work_finished()
  {
    if (--outstanding_work_ == 0)
      stop();
  }

  // Return whether a handler can be dispatched immediately.
  bool can_dispatch()
  {
    return thread_call_stack::contains(this) != 0;
  }

  // Request invocation of the given operation and return immediately. Assumes
  // that work_started() has not yet been called for the operation.
  ASIO_DECL void post_immediate_completion(
      operation* op, bool is_continuation);

  // Request invocation of the given operation and return immediately. Assumes
  // that work_started() was previously called for the operation.
  ASIO_DECL void post_deferred_completion(operation* op);

  // Request invocation of the given operations and return immediately. Assumes
  // that work_started() was previously called for each operation.
  ASIO_DECL void post_deferred_completions(op_queue<operation>& ops);

  // Enqueue the given operation following a failed attempt to dispatch the
  // operation for immediate invocation.
  ASIO_DECL void do_dispatch(operation* op);

  // Process unfinished operations as part of a shutdownoperation. Assumes that
  // work_started() was previously called for the operations.
  ASIO_DECL void abandon_operations(op_queue<operation>& ops);

  // Get the concurrency hint that was used to initialise the scheduler.
  int concurrency_hint() const
  {
    return concurrency_hint_;
  }

private:
  // The mutex type used by this scheduler.
  typedef conditionally_enabled_mutex mutex;

  // The event type used by this scheduler.
  typedef conditionally_enabled_event event;

  // Structure containing thread-specific data.
  typedef scheduler_thread_info thread_info;

  // Run at most one operation. May block.
  ASIO_DECL std::size_t do_run_one(mutex::scoped_lock& lock,
      thread_info& this_thread, const asio::error_code& ec);

  // Run at most one operation with a timeout. May block.
  ASIO_DECL std::size_t do_wait_one(mutex::scoped_lock& lock,
      thread_info& this_thread, long usec, const asio::error_code& ec);

  // Poll for at most one operation.
  ASIO_DECL std::size_t do_poll_one(mutex::scoped_lock& lock,
      thread_info& this_thread, const asio::error_code& ec);

  // Stop the task and all idle threads.
  ASIO_DECL void stop_all_threads(mutex::scoped_lock& lock);

  // Wake a single idle thread, or the task, and always unlock the mutex.
  ASIO_DECL void wake_one_thread_and_unlock(
      mutex::scoped_lock& lock);

  // Helper class to perform task-related operations on block exit.
  struct task_cleanup;
  friend struct task_cleanup;

  // Helper class to call work-related operations on block exit.
  struct work_cleanup;
  friend struct work_cleanup;

  // Whether to optimise for single-threaded use cases.
  bool one_thread_;

  // Mutex to protect access to internal data.
  mutable mutex mutex_;

  // Event to wake up blocked threads.
  event wakeup_event_;

  // The task to be run by this service.
  reactor* task_;

  // Operation object to represent the position of the task in the queue.
  struct task_operation : operation
  {
    task_operation() : operation(0) {}
  } task_operation_;

  // Whether the task has been interrupted.
  bool task_interrupted_;

  // The count of unfinished work.
  atomic_count outstanding_work_;

  // The queue of handlers that are ready to be delivered.
  op_queue<operation> op_queue_;

  // Flag to indicate that the dispatcher has been stopped.
  bool stopped_;

  // Flag to indicate that the dispatcher has been shut down.
  bool shutdown_;

  // The concurrency hint used to initialise the scheduler.
  const int concurrency_hint_;

public:
  mutex::scoped_lock distribute_lock_;
  // consume accepted connections from distribute_queue_
  void consume_accepted_conns();

  // set up relationship with root scheduler
  void relate(scheduler& root);

  // push the accept op to distribute_queue_
  void distribute(operation* op);

  // set one thread
  void set_thread_specific(bool b);

  // set root
  void set_root(void* root)
  {
    root_ = static_cast<scheduler*>(root);
  }

private:
  // spawn new scheduler
  void spawn();

  // Run at most one operation. May block. This function only
  // process the operations of certain connections which are
  // binded to the current thread(scheduler), so no lock is need
  // unless it tries to accquired new connections
  ASIO_DECL std::size_t do_run_one_thread_specific(
      const asio::error_code& ec);

  // Mutex to guard the global distribute_queue.
  static mutex distribute_mutex_;

  // Event to wake up blocked scheduler
  static event distribute_event_;

  // Queue to distribute operations to multiple schedulers
  op_queue<operation> distribute_queue_;


  // number of operations(connections) in the distribute_queue_;
  int distribute_queue_len_;

  // if true, scheduler will run in one thread only
  bool thread_specific_;

  // indicates if new io_context can be spawned from current scheduler
  bool can_spawn_;

  // we can only spawn MAXPROCS_-1 new schedulers which run in their own
  // os threads. MAXPROCS_ is the number of CPUs of the server
  const int MAXPROCS_;

  // current number of spawned schedulers
  int current_procs_;

  // number of accepted alive connections
  int conns_num_; // TODO: decrease when connection closed

  // number of connections of the current scheduler
  int priv_conns_num_;

  // root_ is the first scheduler that spawns other schedulers
  scheduler* root_;
};
conditionally_enabled_mutex scheduler::distribute_mutex_(true);
conditionally_enabled_event scheduler::distribute_event_;
//op_queue<scheduler::operation> scheduler::distribute_queue_;
//int scheduler::distribute_queue_len_ = 0;

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#if defined(ASIO_HEADER_ONLY)
# include "asio/detail/impl/scheduler.ipp"
#endif // defined(ASIO_HEADER_ONLY)

#endif // ASIO_DETAIL_SCHEDULER_HPP
