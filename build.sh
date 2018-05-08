#!/bin/bash

g++ --std=c++11 -DCPP -DGPP -I/usr/csshare/pkgs/csim_cpp-19.0/lib -m32 $1 /usr/csshare/pkgs/csim_cpp-19.0/lib/csim.cpp.a
