
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

/*!
 @file SetupHalo.cpp

 HPCG routine
 */

#ifndef HPCG_NO_MPI
#include <mpi.h>
#include <map>
#include <set>
#endif

#ifndef HPCG_NO_OPENMP
#include <omp.h>
#endif

#include "SetupHalo.hpp"

/*!
  Prepares system matrix data structure and creates data necessary necessary
  for communication of boundary values of this process.

  @param[inout] A    The known system matrix
  @param[inout] x    The vector that will be used for halo exchange, will be resiyed to also fit remote elems

  @see ExchangeHalo
*/
void SetupHalo(SparseMatrix & A) {

  // The call to this reference version of SetupHalo can be replaced with custom code.
  // However, any code must work for general unstructured sparse matrices.  Special knowledge about the
  // specific nature of the sparsity pattern may not be explicitly used.


	  // Extract Matrix pieces

	  local_int_t localNumberOfRows = A.localNumberOfRows;
	  char  * nonzerosInRow = A.nonzerosInRow;
	  global_int_t ** mtxIndG = A.mtxIndG;
	  local_int_t ** mtxIndL = A.mtxIndL;

	#ifdef HPCG_NO_MPI  // In the non-MPI case we simply copy global indices to local index storage
	#ifndef HPCG_NO_OPENMP
	  #pragma omp parallel for
	#endif
	  for (local_int_t i=0; i< localNumberOfRows; i++) {
	    int cur_nnz = nonzerosInRow[i];
	    for (int j=0; j<cur_nnz; j++) mtxIndL[i][j] = mtxIndG[i][j];
	  }

	#else // Run this section if compiling for MPI

	  // Scan global IDs of the nonzeros in the matrix.  Determine if the column ID matches a row ID.  If not:
	  // 1) We call the ComputeRankOfMatrixRow function, which tells us the rank of the processor owning the row ID.
	  //  We need to receive this value of the x vector during the halo exchange.
	  // 2) We record our row ID since we know that the other processor will need this value from us, due to symmetry.

	  std::map< int, std::set< global_int_t> > sendList, receiveList;
	  typedef std::map< int, std::set< global_int_t> >::iterator map_iter;
	  typedef std::set<global_int_t>::iterator set_iter;
	  std::map< global_int_t, local_int_t > externalToLocalMap;

	  // TODO: With proper critical and atomic regions, this loop could be threaded, but not attempting it at this time
	  for (local_int_t i=0; i< localNumberOfRows; i++) {
	    global_int_t currentGlobalRow = A.localToGlobalMap[i];
	    for (int j=0; j<nonzerosInRow[i]; j++) {
	      global_int_t curIndex = mtxIndG[i][j];
	      int rankIdOfColumnEntry = ComputeRankOfMatrixRow(*(A.geom), curIndex);
	#ifdef HPCG_DETAILED_DEBUG
	      HPCG_fout << "rank, row , col, globalToLocalMap[col] = " << A.geom->rank << " " << currentGlobalRow << " "
	          << curIndex << " " << A.globalToLocalMap[curIndex] << endl;
	#endif
	      if (A.geom->rank!=rankIdOfColumnEntry) {// If column index is not a row index, then it comes from another processor
	        receiveList[rankIdOfColumnEntry].insert(curIndex);
	        sendList[rankIdOfColumnEntry].insert(currentGlobalRow); // Matrix symmetry means we know the neighbor process wants my value
	      }
	    }
	  }

	  // Count number of matrix entries to send and receive
	  local_int_t totalToBeSent = 0;
	  for (map_iter curNeighbor = sendList.begin(); curNeighbor != sendList.end(); ++curNeighbor) {
	    totalToBeSent += (curNeighbor->second).size();
	  }
	  local_int_t totalToBeReceived = 0;
	  for (map_iter curNeighbor = receiveList.begin(); curNeighbor != receiveList.end(); ++curNeighbor) {
	    totalToBeReceived += (curNeighbor->second).size();
	  }

	#ifdef HPCG_DETAILED_DEBUG
	  // These are all attributes that should be true, due to symmetry
	  HPCG_fout << "totalToBeSent = " << totalToBeSent << " totalToBeReceived = " << totalToBeReceived << endl;
	  assert(totalToBeSent==totalToBeReceived); // Number of sent entry should equal number of received
	  assert(sendList.size()==receiveList.size()); // Number of send-to neighbors should equal number of receive-from
	  // Each receive-from neighbor should be a send-to neighbor, and send the same number of entries
	  for (map_iter curNeighbor = receiveList.begin(); curNeighbor != receiveList.end(); ++curNeighbor) {
	    assert(sendList.find(curNeighbor->first)!=sendList.end());
	    assert(sendList[curNeighbor->first].size()==receiveList[curNeighbor->first].size());
	  }
	#endif

	  // Build the arrays and lists needed by the ExchangeHalo function.
	  double * sendBuffer = new double[totalToBeSent];
	  local_int_t * elementsToSend = new local_int_t[totalToBeSent];
	  int * neighbors = new int[sendList.size()];
	  local_int_t * receiveLength = new local_int_t[receiveList.size()];
	  local_int_t * sendLength = new local_int_t[sendList.size()];
	  int neighborCount = 0;
	  local_int_t receiveEntryCount = 0;
	  local_int_t sendEntryCount = 0;
	  for (map_iter curNeighbor = receiveList.begin(); curNeighbor != receiveList.end(); ++curNeighbor, ++neighborCount) {
	    int neighborId = curNeighbor->first; // rank of current neighbor we are processing
	    neighbors[neighborCount] = neighborId; // store rank ID of current neighbor
	    receiveLength[neighborCount] = receiveList[neighborId].size();
	    sendLength[neighborCount] = sendList[neighborId].size(); // Get count if sends/receives
	    for (set_iter i = receiveList[neighborId].begin(); i != receiveList[neighborId].end(); ++i, ++receiveEntryCount) {
	      externalToLocalMap[*i] = localNumberOfRows + receiveEntryCount; // The remote columns are indexed at end of internals
	    }
	    for (set_iter i = sendList[neighborId].begin(); i != sendList[neighborId].end(); ++i, ++sendEntryCount) {
	      //if (geom.rank==1) HPCG_fout << "*i, globalToLocalMap[*i], sendEntryCount = " << *i << " " << A.globalToLocalMap[*i] << " " << sendEntryCount << endl;
	      elementsToSend[sendEntryCount] = A.globalToLocalMap[*i]; // store local ids of entry to send
	    }
	  }

	  // Convert matrix indices to local IDs
	#ifndef HPCG_NO_OPENMP
	  #pragma omp parallel for
	#endif
	  for (local_int_t i=0; i< localNumberOfRows; i++) {
	    for (int j=0; j<nonzerosInRow[i]; j++) {
	      global_int_t curIndex = mtxIndG[i][j];
	      int rankIdOfColumnEntry = ComputeRankOfMatrixRow(*(A.geom), curIndex);
	      if (A.geom->rank==rankIdOfColumnEntry) { // My column index, so convert to local index
	        mtxIndL[i][j] = A.globalToLocalMap[curIndex];
	      } else { // If column index is not a row index, then it comes from another processor
	        mtxIndL[i][j] = externalToLocalMap[curIndex];
	      }
	    }
	  }

	  // Store contents in our matrix struct
	  A.numberOfExternalValues = externalToLocalMap.size();
	  A.local_local_NumberOfColumns=A.localNumberOfRows;
	  A.localNumberOfColumns = A.localNumberOfRows + A.numberOfExternalValues;
	  A.numberOfSendNeighbors = sendList.size();
	  A.totalToBeSent = totalToBeSent;
	  A.elementsToSend = elementsToSend;
	  A.neighbors = neighbors;
	  A.receiveLength = receiveLength;
	  A.sendLength = sendLength;
	  A.sendBuffer = sendBuffer;

	#ifdef HPCG_DETAILED_DEBUG
	  HPCG_fout << " For rank " << A.geom->rank << " of " << A.geom->size << ", number of neighbors = " << A.numberOfSendNeighbors << endl;
	  for (int i = 0; i < A.numberOfSendNeighbors; i++) {
	    HPCG_fout << "     rank " << A.geom->rank << " neighbor " << neighbors[i] << " send/recv length = " << sendLength[i] << "/" << receiveLength[i] << endl;
	    for (local_int_t j = 0; j<sendLength[i]; ++j)
	      HPCG_fout << "       rank " << A.geom->rank << " elementsToSend[" << j << "] = " << elementsToSend[j] << endl;
	  }
	#endif


	  A.halo_exchange_vector=NULL;
	  A.halo_requests=new MPI_Request[A.numberOfSendNeighbors*2];
	  // we can reserve mem at this point, but the MPI init calls will take place later

	#endif
	// ifdef HPCG_NO_MPI
}
/*!
  Register a Halo Vector to use for halo exchange.
  Registration is necessary if one wants to use non-blocking halo exchange

  @param[inout] A    The known system matrix
  @param[inout] x    The vector that will be used for halo exchange, will be resized to also fit remote elems

  @see BeginExchangeHalo
  @see EndExchangeHalo
*/

//TODO we need to inline the De-Register funcs so that our compiler analysis can get the score of the requests
void RegisterHaloVector(const SparseMatrix & A,Vector & x)
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
void DeRegisterHaloVector(const SparseMatrix & A,Vector & x)
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
