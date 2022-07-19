# Compiler Assisted Message Matching for Persistent MPI operations
This Repository contains our LLVM analysis pass, to reduce the amount of message matching necessary for MPI persistent Operations, as well as three different implementations of possible communication schemes.

## Prerequisites
For this Project, we used clang/llvm 11, openmpi 4.1.1 openucx 12.0.0 (also refer `setup_env.sh`)

## Compiler analysis pass
The directory `mpi_compiler_assistance_matching_pass` contains the source code of our pass.
For building our pass please refer to `build.sh` in the main directory
For Usage of our pass Refer to `run.sh` in the main directory.

## Communication schemes
Implementations of different communication schemes are contained in the openmpi-patch directory.

Instructions on how to build openmpi with our communication methods enabled can be found in `openmpi-patch/ompi_build.txt` 
openmpi-patch contains different Implementations: `openmpi-patch/one-sided-persistent.c` contains the usage of ucx active messages, via a define one can switch between an eager and a rendezvous protocol.
`openmpi-patch/one-sided-persistent_two_sided_rendezvous.c` contains our proposed two-sided rendezvous protocol.

## Performance evaluation
For a  initial assesment of the performance gain, we included an adapded version of the IMB_ASYNC benchmark (https://github.com/a-v-medvedev/mpi-benchmarks), that also includes a testcase for persistent operations. `IMB_ASYNC/rebuild.sh` illustrates how one can build the Benchmark code with and without our compiler analysis. `visualize/generate_pots.py` contains a script to visualize the performance results.
