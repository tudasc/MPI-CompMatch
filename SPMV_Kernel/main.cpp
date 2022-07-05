#ifndef HPCG_NO_MPI
#include <mpi.h>
#endif

#include <fstream>
#include <iostream>
#include <cstdlib>
#ifdef HPCG_DETAILED_DEBUG
using std::cin;
#endif
using std::endl;

#include <vector>

#include "GenerateGeometry.hpp"
#include "GenerateProblem.hpp"
#include "ComputeSPMV.hpp"

#include "SetupHalo.hpp"
#include "OptimizeProblem.hpp"

#include "ExchangeHalo.hpp"
#include "Geometry.hpp"
#include "SparseMatrix.hpp"
#include "Vector.hpp"


int main(int argc, char * argv[]) {

#ifndef HPCG_NO_MPI
  MPI_Init(&argc, &argv);
#endif

//TODO  READ PARAMETERS


  int size , rank; // Number of MPI processes, My process ID
  MPI_Comm_size(MPI_COMM_WORLD,&size);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);

  //TODO
  local_int_t nx,ny,nz;
  nx = 10;
  ny = 10;
  nz = 10;

  int ierr = 0;  // Used to check return codes on function calls

  // Construct the geometry and linear system
  SparseMatrix A;
  Vector b, x, xexact;
  Geometry * geom = new Geometry;
  GenerateGeometry(size, rank, 0, 0, 0, nx, ny, nz, 0, 0, 0, geom);

  InitializeSparseMatrix(A, geom);
  GenerateProblem(A, &b, &x, &xexact);

  SetupHalo(A);
  OptimizeProblem(A);

//TODO call spmv for a number of times

  RegisterHaloVector(A,x);

  BeginExchangeHaloSend(A,x);
  BeginExchangeHaloRecv(A,x);
  ComputeSPMV(A,x,b,true);

  unsigned long flop = GetNumber_of_flop_for_SPMV(A, x);
  std::cout <<"Calculated SPMV with" << flop << "flops \n";


  DeRegisterHaloVector(A,x);
  // Clean up
  DeleteMatrix(A); // This delete will recursively delete all coarse grid data

  DeleteVector(x);
  DeleteVector(b);
  DeleteVector(xexact);

  MPI_Finalize();

  return 0;
}
