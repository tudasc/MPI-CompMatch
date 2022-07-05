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
 @file OptimizeProblem.cpp

 HPCG routine
 */

#include "OptimizeProblem.hpp"
/*!
 Optimizes the data structures used for CG iteration to increase the
 performance of the benchmark version of the preconditioned CG algorithm.

 @param[inout] A      The known system matrix, also contains the MG hierarchy in attributes Ac and mgData.
 @param[inout] data   The data structure with all necessary CG vectors preallocated
 @param[inout] b      The known right hand side vector
 @param[inout] x      The solution vector to be computed in future CG iteration
 @param[inout] xexact The exact solution vector

 @return returns 0 upon success and non-zero otherwise

 @see GenerateGeometry
 @see GenerateProblem
 */
int OptimizeProblem(SparseMatrix &A) {

	// This function can be used to completely transform any part of the data structures.
	// Right now it does nothing, so compiling with a check for unused variables results in complaints

#if defined(HPCG_USE_MULTICOLORING)
  const local_int_t nrow = A.localNumberOfRows;
  std::vector<local_int_t> colors(nrow, nrow); // value `nrow' means `uninitialized'; initialized colors go from 0 to nrow-1
  int totalColors = 1;
  colors[0] = 0; // first point gets color 0

  // Finds colors in a greedy (a likely non-optimal) fashion.

  for (local_int_t i=1; i < nrow; ++i) {
    if (colors[i] == nrow) { // if color not assigned
      std::vector<int> assigned(totalColors, 0);
      int currentlyAssigned = 0;
      const local_int_t * const currentColIndices = A.mtxIndL[i];
      const int currentNumberOfNonzeros = A.nonzerosInRow[i];

      for (int j=0; j< currentNumberOfNonzeros; j++) { // scan neighbors
        local_int_t curCol = currentColIndices[j];
        if (curCol < i) { // if this point has an assigned color (points beyond `i' are unassigned)
          if (assigned[colors[curCol]] == 0)
            currentlyAssigned += 1;
          assigned[colors[curCol]] = 1; // this color has been used before by `curCol' point
        } // else // could take advantage of indices being sorted
      }

      if (currentlyAssigned < totalColors) { // if there is at least one color left to use
        for (int j=0; j < totalColors; ++j)  // try all current colors
          if (assigned[j] == 0) { // if no neighbor with this color
            colors[i] = j;
            break;
          }
      } else {
        if (colors[i] == nrow) {
          colors[i] = totalColors;
          totalColors += 1;
        }
      }
    }
  }

  std::vector<local_int_t> counters(totalColors);
  for (local_int_t i=0; i<nrow; ++i)
    counters[colors[i]]++;

  // form in-place prefix scan
  local_int_t old=counters[0], old0;
  for (local_int_t i=1; i < totalColors; ++i) {
    old0 = counters[i];
    counters[i] = counters[i-1] + old;
    old = old0;
  }
  counters[0] = 0;

  // translate `colors' into a permutation
  for (local_int_t i=0; i<nrow; ++i) // for each color `c'
    colors[i] = counters[colors[i]]++;
#endif

	// CSR Matrix to CSC for SPMV (to split global and local parts)
	// this will automatically order the columns in the way, that first come the local ones and after this the global

	// get nonzero per column
	A.nonzerosInCol = new local_int_t[A.localNumberOfColumns](); // zero initialized
	for (local_int_t row = 0; row < A.localNumberOfRows; ++row) {
		for (local_int_t j = 0; j < A.nonzerosInRow[row]; ++j) {
			A.nonzerosInCol[A.mtxIndL[row][j]] += 1;
		}
	}

	// create necessary data structures
	A.mtxCSCIndL = new local_int_t*[A.localNumberOfColumns];
	A.mtxCSCIndL[0] = new local_int_t[A.localNumberOfNonzeros];
	A.matrixValuesCSC = new double*[A.localNumberOfColumns];
	A.matrixValuesCSC[0] = new double[A.localNumberOfNonzeros];

	A.matrixDiagonalCSC = new double*[A.localNumberOfRows];

	local_int_t pos = A.nonzerosInCol[0];
	for (int col = 1; col < A.localNumberOfColumns; ++col) {
		A.mtxCSCIndL[col] = A.mtxCSCIndL[0] + pos;
		A.matrixValuesCSC[col] = A.matrixValuesCSC[0] + pos;
		pos += A.nonzerosInCol[col];
	}

	local_int_t diagonal = 0;

// and copy the matrix content
	auto current_nonzerosInCol = new local_int_t[A.localNumberOfColumns](); // zero initialized

	for (local_int_t row = 0; row < A.localNumberOfRows; ++row) {
		for (local_int_t j = 0; j < A.nonzerosInRow[row]; ++j) {
			local_int_t col = A.mtxIndL[row][j];
			local_int_t jj = current_nonzerosInCol[col];
			A.mtxCSCIndL[col][jj] = row;
			A.matrixValuesCSC[col][jj] = A.matrixValues[row][j];
			if (&A.matrixValues[row][j] == A.matrixDiagonal[diagonal]) {
				// this value is in the diagonal
				A.matrixDiagonalCSC[diagonal] = &A.matrixValuesCSC[col][jj];
				diagonal++;
				if (diagonal >= A.localNumberOfRows){
					diagonal--;	// prevent out of bound access
				}
			}
			current_nonzerosInCol[col] += 1;
		}

	}


	delete[] current_nonzerosInCol;
	return 0;
}

// Helper function (see OptimizeProblem.hpp for details)
double OptimizeProblemMemoryUse(const SparseMatrix &A) {

	return 0.0;

}
