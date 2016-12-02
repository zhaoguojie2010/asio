//
// blocking_tcp_echo_client.cpp
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~
//
// Copyright (c) 2003-2016 Christopher M. Kohlhoff (chris at kohlhoff dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include <cstdlib>
#include <cstring>
#include <iostream>
#include "asio.hpp"

#include <sys/time.h>
#include <atomic>

using asio::ip::tcp;

enum { 
    max_length = 1024,
    interval = 50000,
};

int main(int argc, char* argv[])
{
  try
  {
    if (argc != 3)
    {
      std::cerr << "Usage: blocking_tcp_echo_client <host> <port>\n";
      return 1;
    }
    std::cout << "dd " << ASIO_CONCURRENCY_HINT_IS_LOCKING(
                      SCHEDULER, 0) << std::endl;

    asio::io_context io_context;

    tcp::socket s(io_context);
    tcp::resolver resolver(io_context);
    asio::connect(s, resolver.resolve(argv[1], argv[2]));

    char request[max_length] = "hello world";
    char reply[max_length];
    size_t request_length = std::strlen(request);
    std::cout << "length= " << request_length << std::endl;
    int start = 0;
    struct timeval time_start, time_end;
    while(true){
        if (start == 0)
        {
            gettimeofday(&time_start, NULL);
        }
        asio::write(s, asio::buffer(request, request_length));
        size_t reply_length = asio::read(s,
            asio::buffer(reply, request_length));
        if (++start == interval)
        {
            gettimeofday(&time_end, NULL);
            double span = 1000000*(time_end.tv_sec-time_start.tv_sec)+time_end.tv_usec-time_start.tv_usec;
            std::cout << "process " << interval/span*1000000 << "/s" << std::endl;
            start = 0;
        }
    }
  }
  catch (std::exception& e)
  {
    std::cerr << "Exception: " << e.what() << "\n";
  }

  return 0;
}
