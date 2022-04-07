#define _GNU_SOURCE /* needed for some ompi internal headers*/

#include <inttypes.h>
#include <malloc.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <execinfo.h>

#include <math.h>
#include <mpi.h>


/* ************************************************************************ */
/*  main                                                                    */
/* ************************************************************************ */

#define DUMMY_WLOAD_TIME 7

//#define STATISTIC_PRINTING

// bufsize and num iter have to be large to get performance benefit, otherwise
// slowdown occur
// KB
//#define BUFFER_SIZE 1000
// MB
//#define BUFFER_SIZE 1000000

// 10KB
#define BUFFER_SIZE 10000
//#define NUM_ITERS 100000
#define NUM_ITERS 25000

//#define BUFFER_SIZE 10
//#define NUM_ITERS 3

#define N BUFFER_SIZE

void dummy_workload(double *buf) {
  for (int j = 0; j < DUMMY_WLOAD_TIME ; ++j) {
  for (int i = 0; i < N - 1; ++i) {
    buf[i] = sin(buf[i + 1]);
  }}
}

void check_buffer_content(int *buf, int n) {
  int not_correct = 0;

  for (int i = 0; i < N; ++i) {
    if (buf[i] != 1 * i * n) {
      not_correct++;
    }

  }

  if (not_correct != 0) {
    printf("ERROR: %d: buffer has unexpected content\n", n);
    // exit(-1);
  }
}

#define tag_entry 42
#define tag_rkey_data 43
#define tag_rkey_flag 44

void use_persistent_comm() {
  int rank, numtasks;
  // Welchen rang habe ich?
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  // wie viele Tasks gibt es?
  MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
  int *buffer_r = malloc(N * sizeof(int));
  int *buffer_s = malloc(N * sizeof(int));
  double *work_buffer = malloc(N * sizeof(double));
  work_buffer[N - 1] = 0.6;

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
      check_buffer_content(buffer_r, n);
#endif

    }

  MPI_Request_free(&req_r);
  MPI_Request_free(&req_s);
}



//TODO use Reduce to only report max time
// only use persistent and compare transformed VS non-transormed for fair comparision
int main(int argc, char **argv) {
	  // Initialisiere Alle Prozesse
	  MPI_Init(&argc, &argv);


  struct timespec start_time; /* time when program started */
  struct timespec stop_time; /* time when calculation completed                */

  int rank;
  // Welchen rang habe ich?
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);


 
  MPI_Barrier(MPI_COMM_WORLD);
  clock_gettime(CLOCK_MONOTONIC,&start_time); /*  start timer         */
  use_persistent_comm();
  clock_gettime(CLOCK_MONOTONIC,&stop_time); /*  stop timer          */
  MPI_Barrier(MPI_COMM_WORLD);

  double time_with_comm = (stop_time.tv_sec - start_time.tv_sec) +
         (stop_time.tv_nsec - start_time.tv_nsec) * 1e-9;
  double *work_buffer = (double*) malloc(N * sizeof(double));
  MPI_Barrier(MPI_COMM_WORLD);
  clock_gettime(CLOCK_MONOTONIC,&start_time); /*  start timer         */
    for (int i = 0; i < NUM_ITERS; ++i) {
    	dummy_workload(work_buffer);
	}
    clock_gettime(CLOCK_MONOTONIC,&stop_time); /*  stop timer          */
    MPI_Barrier(MPI_COMM_WORLD);
    free(work_buffer);
    double calc_only_time = (stop_time.tv_sec - start_time.tv_sec) +
           (stop_time.tv_nsec - start_time.tv_nsec) * 1e-9;

  double max_time =0.0;
  MPI_Reduce(&time_with_comm, &max_time, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);

  if(rank==0){
	  printf("Bufsize:    %d B \n", BUFFER_SIZE);
	  printf("repetitions:    %d \n", NUM_ITERS);

  printf("Total Time:    %f s \n", max_time);
  printf("Computation Time:    %f s \n", calc_only_time);
  printf("Overhead:    %f s \n", max_time-calc_only_time);
  }

  struct timespec resulution;
  clock_getres(CLOCK_MONOTONIC,&resulution);
  printf("Timer Resolution:    %f s \n", resulution.tv_sec+ resulution.tv_nsec * 1e-9);


  MPI_Finalize();
  return 0;
}
