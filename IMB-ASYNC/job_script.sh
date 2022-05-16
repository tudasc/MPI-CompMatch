#!/bin/bash

# same as -n
#SBATCH --ntasks 2

#SBATCH --mem-per-cpu=3800
#same as -t
#SBATCH --time 00:30:00

#same as -c
#SBATCH --cpus-per-task 96
# one node per process

###SBATCH --array 0-1
#SBATCH --array 1-100
###SBATCH --array 0-1

#same as -j
#SBATCH --job-name MPI-ASYNC-BENCHMARK
#same as -o
###SBATCH --output output/job_%a.out
## The real output will be saved into yml files
#SBATCH --output /dev/null
#same as -e
#SBATCH --error err/job_%a.err

#here the jobscript starts

OUTPATH=/work/scratch/tj75qeje/mpi-comp-match/output

#srun hostname

ml gcc/8.3.1 openmpi/test hwloc/2.5.0 clang/11.1.0

#export OMPI_MCA_opal_warn_on_missing_libcuda=0
#export OMPI_MCA_opal_common_ucx_opal_mem_hooks=1
export OMPI_MCA_osc=ucx
export OMPI_MCA_pml=ucx
export UCX_WARN_UNUSED_ENV_VARS=n
export UCX_UNIFIED_MODE=y

# Fiull configuration
# after a warmup period
#LEN="-len 4,8,32,512,1024,4096,16384,65536,262144,1048576,4194304,16777216 -ncycles 256 -nwarmup 64  -datatype char "
# only the warmup phase
#LEN2="-len 4,8,32,512,1024,4096,16384,65536,262144,1048576,4194304,16777216 -ncycles 64 -nwarmup 0 -datatype char "

# limited number of buffer lengths
# after a warmup period
LEN="-len 8,1024,16384,65536,262144,1048576,4194304,16777216 -ncycles 256 -nwarmup 64  -datatype char "
# only the warmup phase
LEN2="-len 8,1024,16384,65536,262144,1048576,4194304,16777216 -ncycles 64 -nwarmup 0 -datatype char "

#readarray -t PARAMS < /home/tj75qeje/mpi-comp-match/IMB-ASYNC/parameters.txt


#echo "Original"
#srun ./IMB-ASYNC_orig calc_calibration
#echo "Compiler Assisted"
#srun ./IMB-ASYNC calc_calibration


export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:./src_cpp/ASYNC/thirdparty/lib/

# random parameterreihenfolge, im moment haben wir immer bei einem bestimmten wert einen zu hohen overhead (slrum control deamon interrupting the measurement?)
for PARAM in $(shuf /home/tj75qeje/mpi-comp-match/IMB-ASYNC/parameters.txt ) ; do

srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single $LEN -calctime $PARAM -output $OUTPATH/orig_calctime_${PARAM}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.yaml >& /dev/null

srun ./IMB-ASYNC async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single $LEN -calctime $PARAM -output $OUTPATH/modified_calctime_${PARAM}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.yaml >& /dev/null

srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single $LEN2 -calctime $PARAM -output $OUTPATH/orig_NOwarmup_calctime_${PARAM}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.yaml >& /dev/null

srun ./IMB-ASYNC async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single $LEN2 -calctime $PARAM -output $OUTPATH/modified_NOwarmup_calctime_${PARAM}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.yaml >& /dev/null


done
echo "done"

