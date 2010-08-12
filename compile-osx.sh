#!/bin/bash
libev='-I/opt/local/include -L/opt/local/lib -lev'
#libev='-I/Users/hongli/Projects/passenger/ext/libev -L/Users/hongli/Projects/passenger/ext/libev/.libs -lev'
command="g++ -Wall -g threadtest.cpp -o threadtest -lpthread $libev"
echo $command
exec $command