#!/bin/bash

# same as -n
#SBATCH --ntasks 2

#SBATCH --mem-per-cpu=10
#same as -t
#SBATCH --time 00:10:00



#same as -c
#SBATCH --cpus-per-task 96
# opne node per process

#same as -j
#SBATCH --job-name ucx_Testing
#same as -o
#SBATCH --output job.out
#same as -e
#SBATCH --error job.err

#here the jobscript starts


srun hostname


#ml gcc openmpi/test hwloc/2.5.0

export OMPI_MCA_opal_warn_on_missing_libcuda=0
export OMPI_MCA_opal_common_ucx_opal_mem_hooks=1
export OMPI_MCA_osc=ucx
export OMPI_MCA_pml=ucx
export UCX_WARN_UNUSED_ENV_VARS=n
export UCX_UNIFIED_MODE=y

srun ./a.out
srun ./a.out
srun ./a.out
#srun ./example
#srun ./example
#srun ./example
#srun ./example
#srun ./example
#srun ./example
#srun ./example

echo "done"

