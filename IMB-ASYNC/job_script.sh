#!/bin/bash

# same as -n
#SBATCH --ntasks 2

#SBATCH --mem-per-cpu=10
#same as -t
#SBATCH --time 00:10:00

#same as -c
#SBATCH --cpus-per-task 96
# opne node per process

###SBATCH --array 0-1
###SBATCH --array 0-11
###SBATCH --array 0-1

#same as -j
#SBATCH --job-name MPI-ASYNC-BENCHMARK
#same as -o
##SBATCH --output output/job_%a.out
#SBATCH --output job.out
#same as -e
#SBATCH --error err/job_%a.err

#here the jobscript starts

srun hostname

ml gcc/8.3.1 openmpi/test hwloc/2.5.0 clang/11.1.0

#export OMPI_MCA_opal_warn_on_missing_libcuda=0
#export OMPI_MCA_opal_common_ucx_opal_mem_hooks=1
export OMPI_MCA_osc=ucx
export OMPI_MCA_pml=ucx
export UCX_WARN_UNUSED_ENV_VARS=n
export UCX_UNIFIED_MODE=y

readarray -t PARAMS < /home/tj75qeje/mpi-comp-match/IMB-ASYNC/parameters.txt

#PARAM=${PARAMS[$SLURM_ARRAY_TASK_ID]}
PARAM=${PARAMS[0]}

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./src_cpp/ASYNC/thirdparty/lib/

echo "Original $PARAM"
srun ./IMB-ASYNC_orig calc_calibration
srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -nwarmup 100 -thread_level single $PARAM

echo "Compiler Assisted $PARAM"
srun ./IMB-ASYNC calc_calibration
srun ./IMB-ASYNC async_persistentpt2pt -cper10usec 64 -workload calc -nwarmup 100 -thread_level single $PARAM

echo "done"

