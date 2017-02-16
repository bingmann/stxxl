/***************************************************************************
 *  include/stxxl/bits/io/request_queue_impl_1q.h
 *
 *  Part of the STXXL. See http://stxxl.sourceforge.net
 *
 *  Copyright (C) 2002 Roman Dementiev <dementiev@mpi-sb.mpg.de>
 *  Copyright (C) 2008-2009 Andreas Beckmann <beckmann@cs.uni-frankfurt.de>
 *  Copyright (C) 2009 Johannes Singler <singler@ira.uka.de>
 *
 *  Distributed under the Boost Software License, Version 1.0.
 *  (See accompanying file LICENSE_1_0.txt or copy at
 *  http://www.boost.org/LICENSE_1_0.txt)
 **************************************************************************/

#ifndef STXXL_IO_REQUEST_QUEUE_IMPL_1Q_HEADER
#define STXXL_IO_REQUEST_QUEUE_IMPL_1Q_HEADER

#include <stxxl/bits/io/request_queue_impl_worker.h>

#include <list>
#include <mutex>

namespace stxxl {

//! \addtogroup reqlayer
//! \{

//! Implementation of a local request queue having only one queue for both read
//! and write requests, thus having only one thread.
class request_queue_impl_1q : public request_queue_impl_worker
{
private:
    typedef request_queue_impl_1q self;
    typedef std::list<request_ptr> queue_type;

    std::mutex queue_mutex_;
    queue_type queue_;

    shared_state<thread_state> thread_state_;
    std::thread thread_;
    semaphore sem_;

    static const priority_op priority_op_ = WRITE;

    static void * worker(void* arg);

public:
    // \param n max number of requests simultaneously submitted to disk
    explicit request_queue_impl_1q(int n = 1);

    // in a multi-threaded setup this does not work as intended
    // also there were race conditions possible
    // and actually an old value was never restored once a new one was set ...
    // so just disable it and all it's nice implications
    void set_priority_op(const priority_op& op) final
    {
        //_priority_op = op;
        STXXL_UNUSED(op);
    }

    void add_request(request_ptr& req) final;
    bool cancel_request(request_ptr& req) final;
    ~request_queue_impl_1q();
};

//! \}

} // namespace stxxl

#endif // !STXXL_IO_REQUEST_QUEUE_IMPL_1Q_HEADER
// vim: et:ts=4:sw=4
