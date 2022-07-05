
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

int ComputeSPMV( const SparseMatrix & A, Vector & x, Vector & y,bool use_non_blocking_halo_exchange=false);

inline const unsigned long GetNumber_of_flop_for_SPMV(const SparseMatrix & A, Vector & x){
	// one Multiply and one add per nonzero
	return A.localNumberOfNonzeros*2;
}

#endif  // COMPUTESPMV_HPP
