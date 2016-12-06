//
// detail/reactive_socket_accept_op.hpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef ASIO_DETAIL_REACTIVE_SOCKET_ACCEPT_OP_HPP
#define ASIO_DETAIL_REACTIVE_SOCKET_ACCEPT_OP_HPP

#if defined(_MSC_VER) && (_MSC_VER >= 1200)
# pragma once
#endif // defined(_MSC_VER) && (_MSC_VER >= 1200)

#include "asio/detail/config.hpp"
#include "asio/detail/bind_handler.hpp"
#include "asio/detail/buffer_sequence_adapter.hpp"
#include "asio/detail/fenced_block.hpp"
#include "asio/detail/memory.hpp"
#include "asio/detail/reactor_op.hpp"
#include "asio/detail/socket_holder.hpp"
#include "asio/detail/socket_ops.hpp"

#include "asio/detail/push_options.hpp"

namespace asio {
namespace detail {

template <typename Socket, typename Protocol>
class reactive_socket_accept_op_base : public reactor_op
{
public:
  reactive_socket_accept_op_base(socket_type socket,
      socket_ops::state_type state, Socket& peer, const Protocol& protocol,
      typename Protocol::endpoint* peer_endpoint, func_type complete_func)
    : reactor_op(&reactive_socket_accept_op_base::do_perform, complete_func),
      stage_(STAGE0),
      mode_(NORMAL),
      socket_(socket),
      state_(state),
      peer_(peer),
      protocol_(protocol),
      peer_endpoint_(peer_endpoint),
      addrlen_(peer_endpoint ? peer_endpoint->capacity() : 0)
  {
  }

  static status do_perform(reactor_op* base)
  {
    reactive_socket_accept_op_base* o(
        static_cast<reactive_socket_accept_op_base*>(base));

    socket_type new_socket = invalid_socket;
    status result = socket_ops::non_blocking_accept(o->socket_,
        o->state_, o->peer_endpoint_ ? o->peer_endpoint_->data() : 0,
        o->peer_endpoint_ ? &o->addrlen_ : 0, o->ec_, new_socket)
    ? done : not_done;
    o->new_socket_.reset(new_socket);
    ASIO_HANDLER_REACTOR_OPERATION((*o, "non_blocking_accept", o->ec_));
    o->next_stage();
    return result;
  }

  void do_assign()
  {
    if (new_socket_.get() != invalid_socket)
    {
      if (peer_endpoint_)
        peer_endpoint_->resize(addrlen_);
      peer_.assign(protocol_, new_socket_.get(), ec_);
      if (!ec_)
        new_socket_.release();
    }
  }

public:
  enum Mode {NORMAL, LOCK_FREE};
  void set_mode(Mode mode)
  {
    mode_ = mode;
  }

protected:
  // STAGE1: push the op into distribute_queue_
  void assign_stage1(void* owner)
  {
    if (new_socket_.get() != invalid_socket)
    {
      scheduler* sched(static_cast<scheduler*>(owner));

      // has to change to next stage before distribute, otherwise
      // the spawned scheduler might consume a op still in stage 1
      next_stage();
      sched->distribute(this);
    }
  }

  // STAGE2: now the op has been extracted from the father io_context's distribute_queue_
  //         either by father io_context or the spawned io_context, it'll be registered
  //         to the current io_context's reactor
  void assign_stage2(void* owner)
  {
    // reset the service first
    scheduler* sched(static_cast<scheduler*>(owner));
    io_context* io_ctx(static_cast<io_context*>(&sched->context()));
    Socket tmp(*io_ctx);
    peer_ = ASIO_MOVE_CAST(Socket)(tmp);

    do_assign();
  }
  Mode mode_;
  // STAGE0: do accept
  // STAGE1: push the op into distribute_queue_
  // STAGE2: now the op has been extracted from the father io_context's distribute_queue_
  //         either by father io_context or the spawned io_context, it'll be registered
  //         to the current io_context's reactor
  enum Stage {STAGE0=0, STAGE1, STAGE2};
  Stage stage_;

private:
  void next_stage()
  {
    switch (stage_)
    {
      case STAGE0:
        stage_ = STAGE1;
        break;
      case STAGE1:
        stage_ = STAGE2;
        break;
      default:
        return;
    }
  }

private:
  socket_type socket_;
  socket_ops::state_type state_;
  socket_holder new_socket_;
  Socket& peer_;
  Protocol protocol_;
  typename Protocol::endpoint* peer_endpoint_;
  std::size_t addrlen_;
};

template <typename Socket, typename Protocol, typename Handler>
class reactive_socket_accept_op :
  public reactive_socket_accept_op_base<Socket, Protocol>
{
public:
  ASIO_DEFINE_HANDLER_PTR(reactive_socket_accept_op);

  reactive_socket_accept_op(socket_type socket,
      socket_ops::state_type state, Socket& peer, const Protocol& protocol,
      typename Protocol::endpoint* peer_endpoint, Handler& handler)
    : reactive_socket_accept_op_base<Socket, Protocol>(socket, state, peer,
        protocol, peer_endpoint, &reactive_socket_accept_op::do_complete),
      handler_(ASIO_MOVE_CAST(Handler)(handler))
  {
    handler_work<Handler>::start(handler_);
  }

  static void do_complete(void* owner, operation* base,
      const asio::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
    // Take ownership of the handler object.
    reactive_socket_accept_op* o(static_cast<reactive_socket_accept_op*>(base));
    ptr p = { asio::detail::addressof(o->handler_), o, o };
    handler_work<Handler> w(o->handler_);

    // On success, assign new connection to peer socket object.
    if (owner)
      o->do_assign();

    ASIO_HANDLER_COMPLETION((*o));

    // Make a copy of the handler so that the memory can be deallocated before
    // the upcall is made. Even if we're not about to make an upcall, a
    // sub-object of the handler may be the true owner of the memory associated
    // with the handler. Consequently, a local copy of the handler is required
    // to ensure that any owning sub-object remains valid until after we have
    // deallocated the memory here.
    detail::binder1<Handler, asio::error_code>
      handler(o->handler_, o->ec_);
    p.h = asio::detail::addressof(handler.handler_);
    p.reset();

    // Make the upcall if required.
    if (owner)
    {
      fenced_block b(fenced_block::half);
      ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_));
      w.complete(handler, handler.handler_);
      ASIO_HANDLER_INVOCATION_END;
    }
  }

private:
  Handler handler_;
};

#if defined(ASIO_HAS_MOVE)

template <typename Protocol, typename Handler>
class reactive_socket_move_accept_op :
  private Protocol::socket,
  public reactive_socket_accept_op_base<typename Protocol::socket, Protocol>
{
public:
  ASIO_DEFINE_HANDLER_PTR(reactive_socket_move_accept_op);

  reactive_socket_move_accept_op(io_context& ioc, socket_type socket,
      socket_ops::state_type state, const Protocol& protocol,
      typename Protocol::endpoint* peer_endpoint, Handler& handler)
    : Protocol::socket(ioc),
      reactive_socket_accept_op_base<typename Protocol::socket, Protocol>(
        socket, state, *this, protocol, peer_endpoint,
        &reactive_socket_move_accept_op::do_complete),
      handler_(ASIO_MOVE_CAST(Handler)(handler))
  {
    handler_work<Handler>::start(handler_);
  }

  static void do_complete(void* owner, operation* base,
      const asio::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
    reactive_socket_move_accept_op* o(
        static_cast<reactive_socket_move_accept_op*>(base));

    // On success, assign new connection to peer socket object.
    if (owner)
    {
      if (o->mode_ == base::LOCK_FREE)
      {
        if (o->stage_ == base::STAGE1) {
          o->assign_stage1(owner);
          return;
        }
        else if (o->stage_ == base::STAGE2)
        {
          o->assign_stage2(owner);
        }
      }
      else
      {
        o->do_assign();
      }
    }
    ASIO_HANDLER_COMPLETION((*o));

    // Take ownership of the handler object.
    ptr p = { asio::detail::addressof(o->handler_), o, o };
    handler_work<Handler> w(o->handler_);

    // Make a copy of the handler so that the memory can be deallocated before
    // the upcall is made. Even if we're not about to make an upcall, a
    // sub-object of the handler may be the true owner of the memory associated
    // with the handler. Consequently, a local copy of the handler is required
    // to ensure that any owning sub-object remains valid until after we have
    // deallocated the memory here.
    detail::move_binder2<Handler,
      asio::error_code, typename Protocol::socket>
        handler(0, ASIO_MOVE_CAST(Handler)(o->handler_), o->ec_,
          ASIO_MOVE_CAST(typename Protocol::socket)(*o));
    p.h = asio::detail::addressof(handler.handler_);
    p.reset();

    // Make the upcall if required.
    if (owner)
    {
      fenced_block b(fenced_block::half);
      ASIO_HANDLER_INVOCATION_BEGIN((handler.arg1_, "..."));
      w.complete(handler, handler.handler_);
      ASIO_HANDLER_INVOCATION_END;
    }
  }


private:
  typedef reactive_socket_accept_op_base<typename Protocol::socket, Protocol> base;
  Handler handler_;
};

#endif // defined(ASIO_HAS_MOVE)

} // namespace detail
} // namespace asio

#include "asio/detail/pop_options.hpp"

#endif // ASIO_DETAIL_REACTIVE_SOCKET_ACCEPT_OP_HPP
