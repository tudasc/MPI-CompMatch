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
option(ENABLE_MPIOPT "Enable OpenMP support" OFF)

add_executable( spmv main.cpp ExchangeHalo.cpp GenerateGeometry.cpp MixedBaseCounter.cpp OptimizeProblem.cpp
    GenerateProblem.cpp SetupHalo.cpp ComputeSPMV.cpp ComputeOptimalShapeXYZ.cpp)

if (ENABLE_MPIOPT)
    target_compile_definitions(spmv PRIVATE ENABLE_MPIOPT)
endif ()




set(MPI_CXX_SKIP_MPICXX ON)
find_package(MPI REQUIRED)
target_link_libraries(spmv ${MPI_CXX_LIBRARIES})

find_package(OpenMP REQUIRED)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${OpenMP_CXX_FLAGS}")
target_link_libraries(spmv ${OpenMP_CXX_LIBRARIES})
