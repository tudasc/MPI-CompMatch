
//@HEADER
// ***************************************************
//
// HPCG: High Performance Conjugate Gradient Benchmark
//
// Contact:
// Michael A. Heroux ( maherou@sandia.gov)
// Jack Dongarra     (dongarra@eecs.utk.edu)
// Piotr Luszczek    (luszczek@eecs.utk.edu)
//
// ***************************************************
//@HEADER

#ifndef SETUPHALO_HPP
#define SETUPHALO_HPP
#include "SparseMatrix.hpp"

void SetupHalo(SparseMatrix & A);
/*!
  Register a Halo Vector to use for halo exchange.
  Registration is necessary if one wants to use non-blocking halo exchange

  @param[inout] A    The known system matrix
  @param[inout] x    The vector that will be used for halo exchange, will be resized to also fit remote elems

  @see BeginExchangeHalo
  @see EndExchangeHalo
*/

inline void RegisterHaloVector(const SparseMatrix & A,Vector & x)
{
	  ResizeVector(x, A.localNumberOfColumns);

	  assert(A.halo_exchange_vector==NULL);

	  int MPI_MY_TAG = 99;

	  //
	  // Externals are at end of locals
	  //
	  double * x_external = (double *) x.values + A.localNumberOfRows;
	  double* sendBuffer= A.sendBuffer;

	  for (local_int_t i = 0; i < A.numberOfSendNeighbors; ++i) {
		  local_int_t n_recv = A.receiveLength[i];
#ifdef ENABLE_MPIOPT
		  MPIOPT_Recv_init(x_external, n_recv*sizeof(double), MPI_BYTE, A.neighbors[i], MPI_MY_TAG, MPI_COMM_WORLD, &A.halo_requests[i]);
#else
		  MPI_Recv_init(x_external, n_recv, MPI_DOUBLE, A.neighbors[i], MPI_MY_TAG, MPI_COMM_WORLD, &A.halo_requests[i]);
#endif
		  x_external += n_recv;

		  local_int_t n_send = A.sendLength[i];
#ifdef ENABLE_MPIOPT
		  MPIOPT_Send_init(sendBuffer, n_send*sizeof(double), MPI_BYTE, A.neighbors[i], MPI_MY_TAG, MPI_COMM_WORLD,&A.halo_requests[i+A.numberOfSendNeighbors]);
#else
		  MPI_Send_init(sendBuffer, n_send, MPI_DOUBLE, A.neighbors[i], MPI_MY_TAG, MPI_COMM_WORLD,&A.halo_requests[i+A.numberOfSendNeighbors]);
#endif
		  sendBuffer += n_send;
	}

	  A.halo_exchange_vector=&x;
}


/*!
  De-Registers a Halo Vector to use for halo exchange.

  @param[inout] A    The known system matrix
  @param[in] x    The vector that will be used for halo exchange,

  @see BeginExchangeHalo
  @see EndExchangeHalo
*/
inline void DeRegisterHaloVector(const SparseMatrix & A,Vector & x)
{
	  assert(A.halo_exchange_vector==&x);

	  // free old requests
	  for (int i = 0; i < A.numberOfSendNeighbors*2; ++i) {
#ifdef ENABLE_MPIOPT
			MPIOPT_Request_free(&A.halo_requests[i]);
#else
	  	MPI_Request_free(&A.halo_requests[i]);
#endif
	  }
	  A.halo_exchange_vector=NULL;
}


#endif // SETUPHALO_HPP
