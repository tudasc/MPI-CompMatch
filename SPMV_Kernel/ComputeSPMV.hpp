
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

#ifndef COMPUTESPMV_HPP
#define COMPUTESPMV_HPP
#include "Vector.hpp"
#include "SparseMatrix.hpp"

/*!
 Routine to compute sparse matrix vector product y = Ax where:
 Precondition: First call exchange_externals to get off-processor values of x

 This routine calls the reference SpMV implementation by default, but
 can be replaced by a custom, optimized routine suited for
 the target system.

 @param[in]  A the known system matrix
 @param[in]  x the known vector
 @param[out] y the On exit contains the result: Ax.

 @return returns 0 upon success and non-zero otherwise

 @see ComputeSPMV_ref
 */


inline int ComputeSPMV( const SparseMatrix & A, Vector & x, Vector & y,bool use_non_blocking_halo_exchange=false){

	assert(x.localLength >= A.localNumberOfColumns); // Test vector lengths
	assert(y.localLength >= A.localNumberOfRows);

	// Begin halo exchange was called before entering if use_non_blocking_halo_exchange

	const double *const xv = x.values;
	double *const yv = y.values;
	const local_int_t nrow = A.localNumberOfRows;

	// init result
	for (local_int_t i = 0; i < nrow; ++i) {
		yv[i] = 0;
	}

	// local part
#ifndef HPCG_NO_OPENMP
#pragma omp parallel for
#endif
	for (local_int_t i = 0; i < A.local_local_NumberOfColumns; i++) {

		const double *const cur_vals = A.matrixValuesCSC[i];
		const local_int_t *const cur_inds = A.mtxCSCIndL[i];
		const int cur_nnz = A.nonzerosInCol[i];

		for (int j = 0; j < cur_nnz; j++) {
			local_int_t row = cur_inds[j];
#ifndef HPCG_NO_OPENMP
#pragma omp atomic
#endif
			yv[row] += cur_vals[j] * xv[i];
		}
	}

	if (use_non_blocking_halo_exchange) {
		EndExchangeHaloRecv(A, x);
	}

	// non-local part
	// local part
#ifndef HPCG_NO_OPENMP
#pragma omp parallel for
#endif
	for (local_int_t i = A.local_local_NumberOfColumns;
			i < A.localNumberOfColumns; i++) {

		const double *const cur_vals = A.matrixValuesCSC[i];
		const local_int_t *const cur_inds = A.mtxCSCIndL[i];
		const int cur_nnz = A.nonzerosInCol[i];

		for (int j = 0; j < cur_nnz; j++) {
			local_int_t row = cur_inds[j];
#ifndef HPCG_NO_OPENMP
#pragma omp atomic
#endif
			yv[row] += cur_vals[j] * xv[i];
		}
	}

	// we need to make shure all sends are completed as well
	// as Send has started before entering this func, the recvs will complete and no deadlock will be present
	// during send, the values to send where collected into a seperate buffer, so no isuses with override
	if (use_non_blocking_halo_exchange) {
		EndExchangeHaloSend(A, x);
	}

	return 0;
}

inline const unsigned long GetNumber_of_flop_for_SPMV(const SparseMatrix & A, Vector & x){
	// one Multiply and one add per nonzero
	return A.localNumberOfNonzeros*2;
}

#endif  // COMPUTESPMV_HPP
