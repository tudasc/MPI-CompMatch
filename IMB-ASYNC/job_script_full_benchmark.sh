#!/bin/bash

# same as -n
#SBATCH --ntasks 2

#SBATCH --mem-per-cpu=3800
#same as -t
###SBATCH --time 00:30:00
# approx half a minute per benchmark, 4 benchmarks * 28 parameters
# approx 2h
#SBATCH --time 0:30:00

#same as -c
#SBATCH --cpus-per-task 96
# one node per process

###SBATCH --array 0-1
#SBATCH --array 1-100
###SBATCH --array 0-10

#same as -j
#SBATCH --job-name MPI-ASYNC-BENCHMARK
#same as -o
#SBATCH --output output/job_%a.out
## The real output will be saved into yml files
####SBATCH --output /dev/null



# config
OUTPATH=/work/scratch/tj75qeje/mpi-comp-match/output/$SLURM_NPROCS
# limited number of buffer lengths for shorter benchmark time
# after a warmup period
#LEN="-len 8,1024,16384,65536,262144,1048576,4194304,16777216 -ncycles 256 -nwarmup 64  -datatype char "
# only the warmup phase
#LEN2="-len 8,1024,16384,65536,262144,1048576,4194304,16777216 -ncycles 64 -nwarmup 0 -datatype char "
# Full configuration
# after a warmup period
#LEN="-len 4,8,32,512,1024,4096,16384,65536,262144,1048576,4194304,16777216 -ncycles 256 -nwarmup 64  -datatype char "
# only the warmup phase
#LEN2="-len 4,8,32,512,1024,4096,16384,65536,262144,1048576,4194304,16777216 -ncycles 64 -nwarmup 0 -datatype char "

TIMEOUT_CMD="/usr/bin/timeout -k 90 90"
#here the jobscript starts
#srun hostname

ml purge
ml gcc/8.3.1
ml hwloc/2.5.0 clang/11.1.0
ml openucx/1.12.0

#export OMPI_MCA_opal_warn_on_missing_libcuda=0
#export OMPI_MCA_opal_common_ucx_opal_mem_hooks=1
export OMPI_MCA_osc=ucx
export OMPI_MCA_pml=ucx
export UCX_WARN_UNUSED_ENV_VARS=n
export UCX_UNIFIED_MODE=y

I=0
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/tj75qeje/mpi-comp-match/IMB-ASYNC/src_cpp/ASYNC/thirdparty/lib/

mkdir -p $OUTPATH

# random parameterorder, to add random disturbance (e.g. the slurm controler will interrupt quite regularly)
RAND_PARAMS=$(shuf /home/tj75qeje/mpi-comp-match/IMB-ASYNC/parameters.txt)
# change delimiter (IFS) to new line.
IFS_BAK=$IFS
IFS=$'\n'
for PARAM in $RAND_PARAMS; do

# return delimiter to previous value
IFS=$IFS_BAK
IFS_BAK=

((I=I+1))

# get calctime out of param, as calctime will not be included inside the yml, we need to include it in the filename
CALCTIME=$(echo $PARAM | cut -d' ' -f2)

ml openmpi/normal

$TIMEOUT_CMD srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single -datatype char -ncycles 256 -nwarmup 64 $PARAM -output $OUTPATH/normal_calctime_${CALCTIME}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.$I.yaml >& /dev/null

ml openmpi/rendevouz1

$TIMEOUT_CMD srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single -datatype char -ncycles 256 -nwarmup 64 $PARAM -output $OUTPATH/rendevouz1_calctime_${CALCTIME}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.$I.yaml >& /dev/null

ml openmpi/rendevouz2

$TIMEOUT_CMD srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single -datatype char -ncycles 256 -nwarmup 64 $PARAM -output $OUTPATH/rendevouz2_calctime_${CALCTIME}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.$I.yaml >& /dev/null

ml openmpi/eager

$TIMEOUT_CMD srun ./IMB-ASYNC_orig async_persistentpt2pt -cper10usec 64 -workload calc -thread_level single -datatype char -ncycles 256 -nwarmup 64 $PARAM -output $OUTPATH/eager_calctime_${CALCTIME}.$SLURM_JOB_ID.$SLURM_ARRAY_TASK_ID.$I.yaml >& /dev/null

# change delimiter (IFS) to new line.
IFS_BAK=$IFS
IFS=$'\n'

done 

# return delimiter to previous value
IFS=$IFS_BAK
IFS_BAK=

echo "done"

