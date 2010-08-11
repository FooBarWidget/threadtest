#!/bin/bash
set -ev
g++ -Wall -g threadtest.cpp -o threadtest -lpthread -lev
