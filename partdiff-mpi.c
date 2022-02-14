/****************************************************************************/
/****************************************************************************/
/**                                                                        **/
/**                 TU München - Institut für Informatik                   **/
/**                                                                        **/
/** Copyright: Prof. Dr. Thomas Ludwig                                     **/
/**            Andreas C. Schmidt                                          **/
/**                                                                        **/
/** File:      partdiff-seq.c                                              **/
/**                                                                        **/
/** Purpose:   Partial differential equation solver for Gauss-Seidel and   **/
/**            Jacobi method.                                              **/
/**                                                                        **/
/****************************************************************************/
/****************************************************************************/

/* ************************************************************************ */
/* Include standard header file.                                            */
/* ************************************************************************ */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <malloc.h>
#include <sys/time.h>

#include <mpi.h>

#include "partdiff-par.h"

struct calculation_arguments {
	uint64_t N; /* number of spaces between lines (lines=N+1)     */
	uint64_t num_matrices; /* number of matrices                             */
	double h; /* length of a space between two lines            */
	double ***Matrix; /* index matrix used for addressing M             */
	double *M; /* two matrices with real values                  */
};

struct calculation_results {
	uint64_t m;
	uint64_t stat_iteration; /* number of current iteration                    */
	double stat_precision; /* actual precision of all slaves in iteration    */
};

/* ************************************************************************ */
/* Global variables                                                         */
/* ************************************************************************ */

/* time measurement variables */
struct timeval start_time; /* time when program started                      */
struct timeval comp_time; /* time when calculation completed                */

// Für die Paralelisierung:
int rank;
int numtasks;

static int tag = 1; // tag für alle versendeten Nachrichten

int size; // größe des Eigenen abschnitts
int from;	// Globale Zeilenindizes, die diesem Prozess zugeteilt werden
int to;
int pre; // Vorgänger Wert -1 steht dabei für nicht Vorhanden
int nxt; //Nachfolger

/* ************************************************************************ */
/* initVariables: Initializes some global variables                         */
/* ************************************************************************ */
static
void initVariables(struct calculation_arguments* arguments, struct calculation_results* results,
		struct options const* options) {
	arguments->N = (options->interlines * 8) + 9 - 1;
	arguments->num_matrices = (options->method == METH_JACOBI) ? 2 : 1;
	arguments->h = 1.0 / arguments->N;

	results->m = 0;
	results->stat_iteration = 0;
	results->stat_precision = 0;
}

/* ************************************************************************ */
/* freeMatrices: frees memory for matrices                                  */
/* ************************************************************************ */
static
void freeMatrices(struct calculation_arguments* arguments) {
	uint64_t i;

	for (i = 0; i < arguments->num_matrices; i++) {
		free(arguments->Matrix[i]);
	}

	free(arguments->Matrix);
	free(arguments->M);
}

/* ************************************************************************ */
/* allocateMemory ()                                                        */
/* allocates memory and quits if there was a memory allocation problem      */
/* ************************************************************************ */
static
void*
allocateMemory(size_t size) {
	void *p;

	if ((p = malloc(size)) == NULL) {
		printf("Speicherprobleme! (%" PRIu64 " Bytes)\n", size);
		/* exit program */
		exit(1);
	}

	return p;
}

/* ************************************************************************ */
/* allocateMatrices: allocates memory for matrices                          */
/* ************************************************************************ */
static
void allocateMatrices(struct calculation_arguments* arguments) {
	uint64_t i, j;

	uint64_t const zeilen = size + 1; //Wir Berauchen in Jedem Prozess noch 2 Zeilen mehr
	//ansonnsten darf natürlich nur der Lokale Bereich gespeichert werden
	uint64_t const spalten = arguments->N;

	arguments->M = allocateMemory(arguments->num_matrices * (zeilen + 1) * (spalten + 1) * sizeof(double));
	arguments->Matrix = allocateMemory(arguments->num_matrices * sizeof(double**));

	for (i = 0; i < arguments->num_matrices; i++) {
		arguments->Matrix[i] = allocateMemory((zeilen + 1) * sizeof(double*));

		for (j = 0; j <= zeilen; j++) {
			arguments->Matrix[i][j] = arguments->M + (i * (zeilen + 1) * (spalten + 1)) + (j * (spalten + 1));
		}
	}
}

/* ************************************************************************ */
/* initMatrices: Initialize matrix/matrices and some global variables       */
/* ************************************************************************ */
static
void initMatrices(struct calculation_arguments* arguments, struct options const* options) {
	uint64_t g, i, j; /*  local variables for loops   */

	uint64_t const zeilen = size + 1;	//Weider nur über den Lokalen bereich gehen
	uint64_t const spalten = arguments->N;
	double const h = arguments->h;
	double*** Matrix = arguments->Matrix;

	/* initialize matrix/matrices with zeros */
	for (g = 0; g < arguments->num_matrices; g++) {
		for (i = 0; i <= zeilen; i++) {
			for (j = 0; j <= spalten; j++) {
				Matrix[g][i][j] = 0.0;
			}
		}
	}

	/* initialize borders, depending on function (function 2: nothing to do) */
	if (options->inf_func == FUNC_F0) {
		for (g = 0; g < arguments->num_matrices; g++) {
			for (i = 0; i <= zeilen; i++) {
				Matrix[g][i][0] = 1.0 - (h * (i + from - 1));//Grenze Links i auf Globales I umrechnen from-1, da erste Zeile ja eigentlich zum anderen Prozess gehört
				Matrix[g][i][spalten] = h * (i + from - 1);	//Grenze rechts
			}

			if ((rank == 0) || (rank == numtasks - 1)) {
				for (i = 0; i <= spalten; i++) {
					if (rank == 0) {
						Matrix[g][0][i] = 1.0 - (h * i);//Grenze oben muss nur vom ersten Prozess initialisiert werden
					}
					if (rank == numtasks - 1) {
						Matrix[g][zeilen][i] = h * i;	//Grenze unten muss nur vom letzten Prozess initialisiert werden
					}
				}
				if (rank == numtasks - 1) {
					Matrix[g][zeilen][0] = 0.0;	//Rinke untere ecke
				}
				if (rank == 0) {
					Matrix[g][0][spalten] = 0.0;	//rechte obere ecke
				}
			}
		}
	}
}

//Zur besseren Lesbarkeit: Berechnung für eine Zeile in extra Funktion ausgegliedert
// Der Compiler wird diesen Aufruf sowieso wieder inlinen, sodass kein performance-verlust entsteht
//Return wert ist das Maxresiduum dieser Zeile
static double calculateRow(int i, int spalten, double** Matrix_In, double** Matrix_Out, double fpisin, double pih,
		struct options const* options, int term_iteration) {
	int j; /* local variables for loops  */
	//int m1, m2; /* used as indices for old and new matrices       */
	double star; /* four times center value minus 4 neigh.b values */
	double residuum; /* residuum of current iteration                  */
	double maxresiduum = 0; /* maximum residuum value of a slave in iteration */

	double fpisin_i = 0.0;

	if (options->inf_func == FUNC_FPISIN) {
		fpisin_i = fpisin * sin(pih * (double) (i + from - 1));	//Hier Globales I einsetzen
	}

	/* over all columns */
	for (j = 1; j < spalten; j++) {
		star = 0.25 * (Matrix_In[i - 1][j] + Matrix_In[i][j - 1] + Matrix_In[i][j + 1] + Matrix_In[i + 1][j]);

		if (options->inf_func == FUNC_FPISIN) {
			star += fpisin_i * sin(pih * (double) j);
		}

		if (options->termination == TERM_PREC || term_iteration == 1) {
			residuum = Matrix_In[i][j] - star;
			residuum = (residuum < 0) ? -residuum : residuum;
			maxresiduum = (residuum < maxresiduum) ? maxresiduum : residuum;
		}

		Matrix_Out[i][j] = star;
	}
	return maxresiduum;
}
/* ************************************************************************ */
/* calculate: solves the equation                                           */
/* ************************************************************************ */

static
void calculate_GAUSSSEIDEL(struct calculation_arguments const* arguments, struct calculation_results *results,
		struct options const* options, int rank, int numtasks) {
	int i; /* local variables for loops  */
	int m1, m2; /* used as indices for old and new matrices       */
	//double star; /* four times center value minus 4 neigh.b values */
	double residuum; /* residuum of current iteration                  */
	double maxresiduum; /* maximum residuum value of a slave in iteration */

	// Speicher, damit der Status der Verschiedenen Kommunikationsvorgänge am ende abgefragt werden kann
	MPI_Request reqSendPre;
	MPI_Request reqSendNxt;
	//MPI_Request reqRcvPre; //Nicht mehr bednötigt
	MPI_Request reqRcvNxt;

	//MPI_Status status;

	int stop_iter = -1;
	//Iteration, nach der Alle Prozesse anhalten 
	//Wird nur bei abbruch nach genauigkeit verwendet.

	int const zeilen = size;
	int const spalten = arguments->N;
	double const h = arguments->h;

	double pih = 0.0;
	double fpisin = 0.0;

	int term_iteration = options->term_iteration;

	/* initialize m1 and m2 depending on algorithm */
	// Algotithmus muss hier Gauß-Seidel sein	
	m1 = 0;
	m2 = 0;

	if (options->inf_func == FUNC_FPISIN) {
		pih = PI * h;
		fpisin = 0.25 * TWO_PI_SQUARE * h * h;
	}

	while (term_iteration > 0) {
		double** Matrix_Out = arguments->Matrix[m1];
		double** Matrix_In = arguments->Matrix[m2];

		maxresiduum = 0;

		//Die Iteration muss Erstmal damit Beginnen, die aktuelle Vorgängerzeile zu erhalten
		if (pre != -1) {	//Also Falls es einen Vorgänger gibt
			MPI_Recv(Matrix_In[0], spalten, MPI_DOUBLE, pre, tag,
			MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}
		//Blocked ist hier Natürlich essensiell, man kann ja erst mit seiner Iteration anfangen
		//Wenn die Vorherige zeile "Fertig" ist

		//Zeile 1 Berechnen
		maxresiduum = calculateRow(1, spalten, Matrix_In, Matrix_Out, fpisin, pih, options, term_iteration);
		//Berechnetes Ergebnis versenden
		if (pre != -1) {
			MPI_Isend(Matrix_Out[1], spalten, MPI_DOUBLE, pre, tag,
			MPI_COMM_WORLD, &reqSendPre);

		}
		//Bei Der Berechnung von Zeile 2 wird noch LESEND auf Zeile 1 zugegriffen
		//Da Der Sendebuffer dabei nicht Modifiziert wird, ist dieses Verhalten mit dem Standard Konform

		/* over all rows */
		for (i = 2; i <= zeilen; i++) {
			residuum = calculateRow(i, spalten, Matrix_In, Matrix_Out, fpisin, pih, options, term_iteration);
			maxresiduum = (residuum < maxresiduum) ? maxresiduum : residuum;

		}
		//Versende die Letzte Zeile
		// Und empfange die unterste vom Nachfolger für die nächste Iteration
		i = zeilen;
		if (nxt != -1) {
			MPI_Isend(Matrix_Out[i], spalten, MPI_DOUBLE, nxt, tag,
			MPI_COMM_WORLD, &reqSendNxt);

			MPI_Irecv(Matrix_Out[i + 1], spalten, MPI_DOUBLE, nxt, tag,
			MPI_COMM_WORLD, &reqRcvNxt);

		}

		results->stat_iteration++;

		if (options->termination == TERM_PREC) {
			// nur wenn im erlaubten bereich allreduce rufen
			// wenn stop_iter !=-1 dann wurde bereits Festgestellt, wann Terminiert wird
			if ((results->stat_iteration > (unsigned) (numtasks - rank)) && (stop_iter == -1)) {
				// hier nur numtasks - rank , da iterationen direkt vorher hochgezählt wurden
				//cast zu unsigned damit compiler warnung verschwindet, 
				// Es Gilt immer, dass numtasks >= rank
				residuum = maxresiduum;
				MPI_Allreduce(&residuum, &maxresiduum, 1, MPI_DOUBLE, MPI_MAX,
				MPI_COMM_WORLD);
				if (maxresiduum < options->term_precision) {

					stop_iter = results->stat_iteration;
					MPI_Bcast(&stop_iter, 1, MPI_INT, 0, MPI_COMM_WORLD);
					// Finde Die Iteration, die am weitesten Vorne ist
					//Alle Prozesse rechnen nun bis zu dieser Iteration weiter
					// rank 0 sagt diese einfach an, da er sowieso immer am weitesten vorne ist
				}
			}
			if ((signed) results->stat_iteration == stop_iter) {
				term_iteration = 0;
			}

			// hier die richtige iterationszahl zum leeren der pipeline einsetzen
			//if (term_iteration > rank - 1) {// nur neu setzen, wenn Iterationen dardurch nicht erhöt werden
			//	term_iteration = rank;

		}

		//results->stat_precision = maxresiduum;
		// Wird erst nach allen Iterationen gesetzt

		//Macht bei Gaus-Seidel einfach gar nichts, daher auskommentiert
		/* exchange m1 and m2 */
		//i = m1;
		//m1 = m2;
		//m2 = i;
		/* check for stopping calculation, depending on termination method */
		if (options->termination == TERM_ITER) {
			term_iteration--;
		}
		// Warte auf abschluss der Kommunikation
		if (pre != -1) {
			MPI_Wait(&reqSendPre, MPI_STATUS_IGNORE);
			//MPI_Wait(&reqRcvPre, MPI_STATUS_IGNORE);
			//Wurde schon Bei beginn der Iteration ausgeführt
		}
		if (nxt != -1) {
			MPI_Wait(&reqSendNxt, MPI_STATUS_IGNORE);
			MPI_Wait(&reqRcvNxt, MPI_STATUS_IGNORE);
		}

	}
// end while
// Nach allen Iterationen nochmal allreduce
// um die geltende genauigkeit festzustellen
	residuum = maxresiduum;
	MPI_Allreduce(&residuum, &maxresiduum, 1, MPI_DOUBLE, MPI_MAX,
	MPI_COMM_WORLD);
	results->stat_precision = maxresiduum;

	results->m = m2;
}

static
void calculate_JACOBI(struct calculation_arguments const* arguments, struct calculation_results *results,
		struct options const* options) {
	int i; /* local variables for loops  */
	int m1, m2; /* used as indices for old and new matrices       */
//double star; /* four times center value minus 4 neigh.b values */
	double residuum; /* residuum of current iteration                  */
	double maxresiduum; /* maximum residuum value of a slave in iteration */

// Speicher, damit der Status der Verschiedenen Kommunikationsvorgänge am ende abgefragt werden kann
	MPI_Request reqSendPre;
	MPI_Request reqSendNxt;
	MPI_Request reqRcvPre;
	MPI_Request reqRcvNxt;

	MPI_Status status;

	int const zeilen = size;
	int const spalten = arguments->N;
	double const h = arguments->h;

	double pih = 0.0;
	double fpisin = 0.0;

	int term_iteration = options->term_iteration;

	m1 = 0;
	m2 = 1;

	if (options->inf_func == FUNC_FPISIN) {
		pih = PI * h;
		fpisin = 0.25 * TWO_PI_SQUARE * h * h;
	}

	while (term_iteration > 0) {
		double** Matrix_Out = arguments->Matrix[m1];
		double** Matrix_In = arguments->Matrix[m2];

		maxresiduum = 0;

		//Berechne Zeile1
		i = 1;
		maxresiduum = calculateRow(i, spalten, Matrix_In, Matrix_Out, fpisin, pih, options, term_iteration);

		// Sende Das Ergebnis und empfange die Zeile für die Nächste iteration
		// Das geht an dieseer Stelle non blocked, da die Buffer verschieden sind und im Weiteren Verlauf der Iteration nihct mehr benutzt werden müssen
		if (pre != -1) {
			MPI_Isend(Matrix_Out[i], spalten, MPI_DOUBLE, pre, tag,
			MPI_COMM_WORLD, &reqSendPre);
			//Empfangene Nachicht= 0te Zeile der Neuen Matrix
			MPI_Irecv(Matrix_Out[i - 1], spalten, MPI_DOUBLE, pre, tag,
			MPI_COMM_WORLD, &reqRcvPre);
		}
		//Berechne Zeile N-1
		i = zeilen;
		residuum = calculateRow(i, spalten, Matrix_In, Matrix_Out, fpisin, pih, options, term_iteration);

		if (nxt != -1) {
			MPI_Isend(Matrix_Out[i], spalten, MPI_DOUBLE, nxt, tag,
			MPI_COMM_WORLD, &reqSendNxt);

			MPI_Irecv(Matrix_Out[i + 1], spalten, MPI_DOUBLE, nxt, tag,
			MPI_COMM_WORLD, &reqRcvNxt);
		}
		maxresiduum = (residuum < maxresiduum) ? maxresiduum : residuum;

		/* over all rows */
		for (i = 2; i < zeilen; i++) {
			residuum = calculateRow(i, spalten, Matrix_In, Matrix_Out, fpisin, pih, options, term_iteration);
			maxresiduum = (residuum < maxresiduum) ? maxresiduum : residuum;
		}

		// Warte auf abschluss der Kommunikation
		if (pre != -1) {
			MPI_Wait(&reqSendPre, &status);
			MPI_Wait(&reqRcvPre, &status);
		}
		if (nxt != -1) {
			MPI_Wait(&reqSendNxt, &status);
			MPI_Wait(&reqRcvNxt, &status);
		}

		results->stat_iteration++;

		if (options->termination == TERM_PREC) {

			residuum = maxresiduum;
			MPI_Allreduce(&residuum, &maxresiduum, 1, MPI_DOUBLE, MPI_MAX,
			MPI_COMM_WORLD);
		}
		results->stat_precision = maxresiduum;

		/* exchange m1 and m2 */
		i = m1;
		m1 = m2;
		m2 = i;

		/* check for stopping calculation, depending on termination method */
		if (options->termination == TERM_PREC) {
			if (maxresiduum < options->term_precision) {
				term_iteration = 0;
			}
		} else if (options->termination == TERM_ITER) {
			term_iteration--;
		}
	}
	// Nach allen Iterationen nochmal allreduce
	// um die geltende genauigkeit festzustellen
		residuum = maxresiduum;
		MPI_Allreduce(&residuum, &maxresiduum, 1, MPI_DOUBLE, MPI_MAX,
		MPI_COMM_WORLD);
		results->stat_precision = maxresiduum;

	results->m = m2;
}

/* ************************************************************************ */
/*  displayStatistics: displays some statistics about the calculation       */
/* ************************************************************************ */
static
void displayStatistics(struct calculation_arguments const* arguments, struct calculation_results const* results,
		struct options const* options) {
	int N = arguments->N;
	double time = (comp_time.tv_sec - start_time.tv_sec) + (comp_time.tv_usec - start_time.tv_usec) * 1e-6;

	printf("Berechnungszeit:    %f s \n", time);
	printf("Speicherbedarf:     %f MiB\n",
			(N + 1) * (N + 1) * sizeof(double) * arguments->num_matrices / 1024.0 / 1024.0);
	printf("Berechnungsmethode: ");

	if (options->method == METH_GAUSS_SEIDEL) {
		printf("Gauss-Seidel");
	} else if (options->method == METH_JACOBI) {
		printf("Jacobi");
	}

	printf("\n");
	printf("Interlines:         %" PRIu64 "\n",options->interlines);
	printf("Stoerfunktion:      ");

	if (options->inf_func == FUNC_F0) {
		printf("f(x,y) = 0");
	} else if (options->inf_func == FUNC_FPISIN) {
		printf("f(x,y) = 2pi^2*sin(pi*x)sin(pi*y)");
	}

	printf("\n");
	printf("Terminierung:       ");

	if (options->termination == TERM_PREC) {
		printf("Hinreichende Genaugkeit");
	} else if (options->termination == TERM_ITER) {
		printf("Anzahl der Iterationen");
	}

	printf("\n");
	printf("Anzahl Iterationen: %" PRIu64 "\n", results->stat_iteration);
	printf("Norm des Fehlers:   %e\n", results->stat_precision);
	printf("\n");
}

/**
 * rank and size are the MPI rank and size, respectively.
 * from and to denote the global(!) range of lines that this process is responsible for.
 *
 * Example with 9 matrix lines and 4 processes:
 * - rank 0 is responsible for 1-2, rank 1 for 3-4, rank 2 for 5-6 and rank 3 for 7.
 *   Lines 0 and 8 are not included because they are not calculated.
 * - Each process stores two halo lines in its matrix (except for ranks 0 and 3 that only store one).
 * - For instance: Rank 2 has four lines 0-3 but only calculates 1-2 because 0 and 3 are halo lines for other processes. It is responsible for (global) lines 5-6.
 */
static
void DisplayMatrix(struct calculation_arguments* arguments, struct calculation_results* results,
		struct options* options, int rank, int size, int from, int to) {
	int const elements = 8 * options->interlines + 9;

	int x, y;
	double** Matrix = arguments->Matrix[results->m];
	MPI_Status status;

	/* first line belongs to rank 0 */
	if (rank == 0)
		from--;

	/* last line belongs to rank size - 1 */
	if (rank + 1 == size)
		to++;

	if (rank == 0)
		printf("Matrix:\n");

	for (y = 0; y < 9; y++) {
		int line = y * (options->interlines + 1);

		if (rank == 0) {
			/* check whether this line belongs to rank 0 */
			if (line < from || line > to) {
				/* use the tag to receive the lines in the correct order
				 * the line is stored in Matrix[0], because we do not need it anymore */
				MPI_Recv(Matrix[0], elements, MPI_DOUBLE, MPI_ANY_SOURCE, 42 + y, MPI_COMM_WORLD, &status);
			}
		} else {
			if (line >= from && line <= to) {
				/* if the line belongs to this process, send it to rank 0
				 * (line - from + 1) is used to calculate the correct local address */
				MPI_Send(Matrix[line - from + 1], elements, MPI_DOUBLE, 0, 42 + y, MPI_COMM_WORLD);
			}
		}

		if (rank == 0) {
			for (x = 0; x < 9; x++) {
				int col = x * (options->interlines + 1);

				if (line >= from && line <= to) {
					/* this line belongs to rank 0 */
					printf("%7.4f", Matrix[line][col]);
				} else {
					/* this line belongs to another rank and was received above */
					printf("%7.4f", Matrix[0][col]);
				}
			}

			printf("\n");
		}
	}

	fflush(stdout);
}

/* ************************************************************************ */
/*  main                                                                    */
/* ************************************************************************ */
int main(int argc, char** argv) {

//Initialisiere Alle Prozesse
	MPI_Init(&argc, &argv);

// Welchen rang habe ich?
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
// wie viele Tasks gibt es?
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);

	struct options options;
	struct calculation_arguments arguments;
	struct calculation_results results;

	/* get parameters */

	AskParams(rank, &options, argc, argv); /* ************************* */

	initVariables(&arguments, &results, &options); /* ******************************************* */

	int rest;
// Der Rest wird bei Uns Zeilenweise auf die Ersten Prozesse aufgeteilt
// Das Bedeutet bei rest X, dass die Ersten X Prozesse eine Zeile mehr bekommen
// beispiel: 3 Prozesse 8 Zeilen: Prozess0: 3 Zeilen Prozess1: 3 Zeilen Prozess2: 2 Zeilen

	size = (arguments.N - 1) / numtasks;
	rest = (arguments.N - 1) % numtasks;
//-1, da die Letzte Zeile zu keinem Prozess gehört (die 0te Zeile gehört auch keinem, wird abewr nicht mitgezählt)
	from = rank * size + rest + 1;
//+1 bei, da die erste Zeile zu keinem Prozess gehört

	if (rest > rank) {
		size++;
		from = rank * size + 1;
	}
	to = from + size - 1;

	if (size <= 1) {
		printf("Zu Wenige Zeilen, um sie sinvoll aufzuteilen, Bitte Starten sie das Programm mit weniger Prozessen");
		return EXIT_FAILURE;
	}

//Nachfolger und vorgänger initialisieren
	if (numtasks == 1)	//Dann Keine Kommunikation
			{
		pre = -1;
		nxt = -1;
	} else {
		pre = rank - 1;	//Funktioniert auch für den Sonderfall Rank=0
		nxt = rank + 1;
		if (nxt >= numtasks) {
			nxt = -1;	//Sonderfall: Letzter Task
		}
	}

	allocateMatrices(&arguments); /*  get and initialize variables and matrices  */
	initMatrices(&arguments, &options); /* ******************************************* */

	if (rank == 0) { // Nur rank 0 ist für den Statistikkram zuständig
		gettimeofday(&start_time, NULL); /*  start timer         */
	}
	MPI_Barrier(MPI_COMM_WORLD); //Barrieren hier nur um die Zeitmessung von allen Prozessen zu haben
	if (options.method == METH_JACOBI) {
		calculate_JACOBI(&arguments, &results, &options); /*  solve the equation  */ // Alle Berechnen, auch Rang 0, es gibt für die berechnung keinen master
	} else {
		calculate_GAUSSSEIDEL(&arguments, &results, &options, rank, numtasks); /*  solve the equation  */
	}

	MPI_Barrier(MPI_COMM_WORLD);
	if (rank == 0) {
		gettimeofday(&comp_time, NULL); /*  stop timer          */
	}

	if (rank == 0) {
		displayStatistics(&arguments, &results, &options);
	}
	DisplayMatrix(&arguments, &results, &options, rank, numtasks, from, to);

	freeMatrices(&arguments); /*  free memory     */

	MPI_Finalize();
	return 0;
}
