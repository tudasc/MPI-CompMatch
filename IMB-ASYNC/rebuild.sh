#!/bin/bash


make clean
OMPI_CXX=clang++ CXX=mpicxx CXXFLAGS="" make

mv IMB-ASYNC IMB-ASYNC_orig

make clean
OMPI_CXX=clang++ CXX=mpicxx CXXFLAGS="-Xclang -load -Xclang /home/tj75qeje/mpi-comp-match/build/mpi_compiler_assistance_matching_pass/libmpi_compiler_assistance_matching_pass.so" make

# sample settings:
#LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./src_cpp/ASYNC/thirdparty/lib/ mpirun -n 2 ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 61 -workload calc -calctime 1 -ncycles 1000 -nwarmup 100 -thread_level single -len 8,32,1024,16384,262144,4194304
# there is a small performance benefit


