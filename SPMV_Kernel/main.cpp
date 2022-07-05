#ifndef HPCG_NO_MPI
#include <mpi.h>
#endif

#include <fstream>
#include <iostream>
#include <cstdlib>
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


  MPI_Init(&argc, &argv);
#ifdef ENABLE_MPIOPT
  MPIOPT_INIT();
#endif

//TODO  READ PARAMETERS


  int size , rank; // Number of MPI processes, My process ID
  MPI_Comm_size(MPI_COMM_WORLD,&size);
  MPI_Comm_rank(MPI_COMM_WORLD,&rank);


  local_int_t nx,ny,nz;
  nx = 10;
  ny = 10;
  nz = 10;
  local_int_t num_iters=10;

  //parse args
  if (argc>1){
	  nx= atoi(argv[1]);
  }
  if (argc>2){
  	  ny= atoi(argv[2]);
    }
  if (argc>3){
  	  nz= atoi(argv[3]);
    }
  if (argc>4){
    	  num_iters= atoi(argv[4]);
      }

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

  MPI_Barrier(MPI_COMM_WORLD);

  double start_time = MPI_Wtime();


  RegisterHaloVector(A,x);

  BeginExchangeHaloSend(A,x);
  BeginExchangeHaloRecv(A,x);

  for (int i = 0; i < num_iters; ++i) {
    ComputeSPMV(A,x,b,true);//ends Halo exchange
    BeginExchangeHaloRecv(A,x);
    // "compute" new x
    FillRandomVector(b);
    CopyVector(b,x);
    BeginExchangeHaloSend(A,x);
  }

  EndExchangeHaloSend(A,x);
  EndExchangeHaloSend(A,x);

  double end_time = MPI_Wtime();

  double time = end_time-start_time;

  unsigned long flop = num_iters*GetNumber_of_flop_for_SPMV(A, x);

  double flops= (double) flop / time;

  unsigned long sum_flop;
  double max_time;
  double sum_flops;
  MPI_Reduce(&time,&max_time,1,MPI_DOUBLE,MPI_MAX,0,MPI_COMM_WORLD);
  MPI_Reduce(&flops,&sum_flops,1,MPI_DOUBLE,MPI_SUM,0,MPI_COMM_WORLD);
  MPI_Reduce(&flop,&sum_flop,1,MPI_UNSIGNED_LONG,MPI_SUM,0,MPI_COMM_WORLD);

  double avg_flops= sum_flops/(double)size;

  if (rank==0){
	  std::cout << "Time: " << max_time << "\n"
			  << "Sum Flop: " << sum_flop << "\n"
			  << "Avg Flops: " << avg_flops << "\n";

  }


  DeRegisterHaloVector(A,x);
  // Clean up
  DeleteMatrix(A); // This delete will recursively delete all coarse grid data

  DeleteVector(x);
  DeleteVector(b);
  DeleteVector(xexact);

#ifdef ENABLE_MPIOPT
  MPIOPT_FINALIZE();
#endif
  MPI_Finalize();

  return 0;
}
