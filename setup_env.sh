#!/bin/bash

ml gcc/8.3

ml openmpi/test ucx/1.12.1 hwloc/2.5.0 clang/11.1.0

# use cluster version of ucx for production run
ml unload ucx
ml openucx/1.12.0
ml openmpi/rendevouz2

export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/home/tj75qeje/mpi-comp-match/IMB-ASYNC/src_cpp/ASYNC/thirdparty/lib/

export OMPI_MCA_opal_warn_on_missing_libcuda=0
export OMPI_MCA_opal_common_ucx_opal_mem_hooks=1
export OMPI_MCA_osc=ucx
export OMPI_MCA_pml=ucx
export UCX_WARN_UNUSED_ENV_VARS=n
export UCX_UNIFIED_MODE=y
