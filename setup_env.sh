#!/bin/bash

ml openmpi/test hwloc/2.5.0

export OMPI_MCA_opal_warn_on_missing_libcuda=0
export OMPI_MCA_opal_common_ucx_opal_mem_hooks=1
export OMPI_MCA_osc=ucx
export OMPI_MCA_pml=ucx
export UCX_WARN_UNUSED_ENV_VARS=n
export UCX_UNIFIED_MODE=y
