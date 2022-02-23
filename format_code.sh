#!/bin/bash

clang-format --style=llvm -i ./openmpi-patch/*.c ./openmpi-patch/*.h
clang-format --style=llvm -i ./mpi_compiler_assistance_matching_pass/*.cpp ./mpi_compiler_assistance_matching_pass/*.h

