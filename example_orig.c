#define _GNU_SOURCE /* needed for some ompi internal headers*/

#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>

#include <execinfo.h>

#include <math.h>
#include <mpi.h>


/* ************************************************************************ */
/*  main                                                                    */
/* ************************************************************************ */

#define DUMMY_WLOAD_TIME 7

#define STATISTIC_PRINTING

// bufsize and num iter have to be large to get performance benefit, otherwise
// slowdown occur
// KB
//#define BUFFER_SIZE 1000
// MB
//#define BUFFER_SIZE 1000000

// 10KB
//#define BUFFER_SIZE 10000
//#define NUM_ITERS 100000
//#define NUM_ITERS 25000

#define BUFFER_SIZE 10
#define NUM_ITERS 30

#define N BUFFER_SIZE

void dummy_workload(double *buf) {
  for (int j = 0; j < DUMMY_WLOAD_TIME ; ++j) {
  for (int i = 0; i < N - 1; ++i) {
    buf[i] = sin(buf[i + 1]);
  }}
}

void check_buffer_content(int *buf,int peer, int n) {
  int not_correct = 0;

  for (int i = 0; i < N; ++i) {
    if (buf[i] != peer * i * n) {
      not_correct++;
    }

  }

  if (not_correct != 0) {
    printf("ERROR: %d: buffer has unexpected content (%d/%d errors)\n", n,not_correct,N);
    // exit(-1);
  }

}

#define tag_entry 42
#define tag_rkey_data 43
#define tag_rkey_flag 44

double use_persistent_comm() {
  int rank, numtasks;
  // Welchen rang habe ich?
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  // wie viele Tasks gibt es?
  MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
  int *buffer_r = malloc(N * sizeof(int));
  int *buffer_s = malloc(N * sizeof(int));
  double *work_buffer = calloc(sizeof(double),N);
  work_buffer[N - 1] = 0.6;

  struct timeval start_time; /* time when program started */
  struct timeval stop_time; /* time when calculation completed                */

  // start timer after allocation of the data-buffers

  gettimeofday(&start_time, NULL); /*  start timer         */


  MPI_Request req_s;
  MPI_Request req_r;

  int peer=1;
  if (rank == 1) {
	  peer=0;}

    MPI_Send_init(buffer_s, sizeof(int) * N, MPI_BYTE, peer, 42, MPI_COMM_WORLD,
                  &req_s);
    MPI_Recv_init(buffer_r, sizeof(int) * N, MPI_BYTE, peer, 42, MPI_COMM_WORLD,
                      &req_r);

    for (int n = 0; n < NUM_ITERS; ++n) {
      for (int i = 0; i < N; ++i) {
    	  buffer_s[i] = rank * i * n;
      }
      MPI_Start(&req_r);
      MPI_Start(&req_s);

      dummy_workload(work_buffer);

      //MPI_Wait(&req_s, MPI_STATUS_IGNORE);
      //MPI_Wait(&req_r, MPI_STATUS_IGNORE);
      MPI_Wait(&req_s, MPI_STATUS_IGNORE);
      MPI_Wait(&req_r, MPI_STATUS_IGNORE);

#ifdef STATISTIC_PRINTING
      check_buffer_content(buffer_r,peer, n);
#endif

    }

  MPI_Request_free(&req_r);
  MPI_Request_free(&req_s);

  gettimeofday(&stop_time, NULL); /*  stop timer          */
  return (stop_time.tv_sec - start_time.tv_sec) +
         (stop_time.tv_usec - start_time.tv_usec) * 1e-6;
}


double without_comm() {

  double *work_buffer = calloc(sizeof(double),N);
  work_buffer[N - 1] = 0.6;

  struct timeval start_time; /* time when program started */
  struct timeval stop_time; /* time when calculation completed                */

  // start timer after allocation of the data-buffers

  gettimeofday(&start_time, NULL); /*  start timer         */

    for (int n = 0; n < NUM_ITERS; ++n) {
      dummy_workload(work_buffer);
    }

  gettimeofday(&stop_time, NULL); /*  stop timer          */
  return (stop_time.tv_sec - start_time.tv_sec) +
         (stop_time.tv_usec - start_time.tv_usec) * 1e-6;
}


//TODO use Reduce to only report max time
// only use persistent and compare transformed VS non-transormed for fair comparision
int main(int argc, char **argv) {
	  // Initialisiere Alle Prozesse
	  MPI_Init(&argc, &argv);




  int rank;
  // Welchen rang habe ich?
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);

  MPI_Barrier(MPI_COMM_WORLD);

  double time_without_comm = without_comm();
 
  MPI_Barrier(MPI_COMM_WORLD);

  double time_with_comm = use_persistent_comm();

  MPI_Barrier(MPI_COMM_WORLD);


  double max_time =0.0;
  MPI_Reduce(&time_with_comm, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if(rank==0){
	  printf("Bufsize:    %lu B \n", BUFFER_SIZE);
	  printf("repetitions:    %d \n", NUM_ITERS);
  printf("Total Time:    %f s \n", max_time);
  printf("Comp. Time:    %f s \n", time_without_comm);
  printf("Overhead:    %f s \n", max_time-time_without_comm);
  printf("MPI Timer Res:  %.7e s \n",MPI_Wtick());
  }

  MPI_Finalize();
  return 0;
}
