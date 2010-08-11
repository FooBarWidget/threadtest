#!/bin/bash
set -ev
g++ -Wall -g threadtest.cpp -o threadtest -I/opt/local/include -L/opt/local/lib -lpthread -lev
