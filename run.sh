#!/bin/bash

#using openmpi:
#OMPI_CXX=clang++ mpicxx -fopenmp -Xclang -load -Xclang build/experimentpass/libexperimentpass.so  $1

OMPI_CC=clang mpicxx

# using mpich:
if [ ${1: -2} == ".c" ]; then
OMPI_CC=clang $MPICC -O2 -fopenmp -Xclang -load -Xclang build/mpi_compiler_assistance_matching_pass/libmpi_compiler_assistance_matching_pass.so  $1
#$MPICC -cc=clang -O2 -fopenmp -Xclang -load -Xclang build/mpi_assertion_checker/libmpi_assertion_checker.so  -ftime-report $1
#$MPICC -cc=clang -O2 -fopenmp $1
elif [ ${1: -4} == ".cpp" ]; then
$MPICXX -cxx=clang++ -O2  -fopenmp -Xclang -load -Xclang build/mpi_assertion_checker/libmpi_assertion_checker.so  $1
else
echo "Unknown file suffix, use this script with .c or .cpp files"
fi
