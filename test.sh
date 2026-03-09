#!/bin/bash
set -ex
cd /app && rm -rf build && mkdir build && cd build && cmake .. && make && ctest
