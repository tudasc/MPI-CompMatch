#!/bin/bash

#using openmpi:

INCLUDE="-I/home/tj75qeje/openmpi-4.1.1/ -I/home/tj75qeje/openmpi-4.1.1/opal/include -I/home/tj75qeje/openmpi-4.1.1/ompi/include/ -I/home/tj75qeje/openmpi-4.1.1/orte/include"

CFLAGS="-std=c11 -O3 ${INCLUDE}"
#CFLAGS="-std=c11 -O0 -g ${INCLUDE}"
LIBS="-lopen-pal -lucp -lm"

#
if [ ${1: -2} == ".c" ]; then
OMPI_CC=clang $MPICC $CFLAGS -Xclang -load -Xclang build/mpi_compiler_assistance_matching_pass/libmpi_compiler_assistance_matching_pass.so  $1 $LIBS
#OMPI_CC=clang $MPICC $CFLAGS $1 $LIBS
#$MPICC -cc=clang -O2 -fopenmp -Xclang -load -Xclang build/mpi_compiler_assistance_matching_pass/mpi_compiler_assistance_matching_pass.so  -ftime-report $1
elif [ ${1: -4} == ".cpp" ]; then
OMPI_CXX=clang++ $MPICXX $CFLAGS -Xclang -load -Xclang build/mpi_compiler_assistance_matching_pass/libmpi_compiler_assistance_matching_pass.so  $1 $LIBS
else
echo "Unknown file suffix, use this script with .c or .cpp files"
fi
