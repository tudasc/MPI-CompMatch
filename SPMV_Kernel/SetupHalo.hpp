
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
void RegisterHaloVector(const SparseMatrix & A,Vector & x);
void DeRegisterHaloVector(const SparseMatrix & A,Vector & x);

#endif // SETUPHALO_HPP
