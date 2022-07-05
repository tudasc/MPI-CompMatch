#
# HPCG Benchmark CMake configuration
#
cmake_minimum_required( VERSION 3.0 FATAL_ERROR )

project( spmv_kernel
    LANGUAGES CXX
    )

#
# Options
#
option(HPCG_ENABLE_CONTIGUOUS_ARRAYS "Enable contiguous arrays for better cache pre-fetch" OFF)
option(HPCG_ENABLE_CUBIC_RADICAL_SEARCH "Enable faster search for optimal 3D process grid" OFF)
option(HPCG_ENABLE_DEBUG "Enable debug build" OFF)
option(HPCG_ENABLE_DETAILED_DEBUG "Enable detailed debug build" OFF)
option(HPCG_ENABLE_MPI "Enable MPI support" OFF)
option(HPCG_ENABLE_LONG_LONG "Enable use of 'long long' type for global indices" ON)
option(HPCG_ENABLE_OPENMP "Enable OpenMP support" OFF)

add_executable( spmv main.cpp ExchangeHalo.cpp GenerateGeometry.cpp MixedBaseCounter.cpp OptimizeProblem.cpp
    GenerateProblem.cpp SetupHalo.cpp ComputeSPMV.cpp ComputeOptimalShapeXYZ.cpp)

if (HPCG_ENABLE_CONTIGUOUS_ARRAYS)
    target_compile_definitions(spmv PRIVATE HPCG_CONTIGUOUS_ARRAYS)
endif ()

if (HPCG_ENABLE_CUBIC_RADICAL_SEARCH)
    target_compile_definitions(spmv PRIVATE HPCG_CUBIC_RADICAL_SEARCH)
endif ()

if (HPCG_ENABLE_DEBUG)
    target_compile_definitions(spmv PRIVATE HPCG_DEBUG)
endif ()

if (HPCG_ENABLE_DETAILED_DEBUG)
    target_compile_definitions(spmv PRIVATE HPCG_DETAILED_DEBUG)
endif ()

if (HPCG_ENABLE_MPI)
    set(MPI_CXX_SKIP_MPICXX ON)
    find_package(MPI REQUIRED)
    target_link_libraries(spmv ${MPI_CXX_LIBRARIES})
else ()
    target_compile_definitions(spmv PRIVATE HPCG_NO_MPI)
endif ()

if (NOT HPCG_ENABLE_LONG_LONG)
    target_compile_definitions(spmv PRIVATE HPCG_NO_LONG_LONG)
endif ()

if (HPCG_ENABLE_OPENMP)
    find_package(OpenMP REQUIRED)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
    target_link_libraries(spmv ${OpenMP_CXX_LIBRARIES})
else ()
    target_compile_definitions(spmv PRIVATE HPCG_NO_OPENMP)
endif ()
