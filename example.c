#define _GNU_SOURCE         /* needed for some ompi internal headers*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <math.h>
#include <malloc.h>
#include <sys/time.h>

#include "low_level.h"

#include <execinfo.h>

/* ************************************************************************ */
/*  main                                                                    */
/* ************************************************************************ */

#define N 1000

void use_self_implemented_comm() {
	int send_list[2] = { 1, 1 };
	int recv_list[2] = { 1, 1 };

	struct global_information *global_info = Init(&send_list, &recv_list);

	int rank, numtasks;
	// Welchen rang habe ich?
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	// wie viele Tasks gibt es?
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);

	int *buffer = malloc(N * sizeof(int));

	if (rank == 0) {
		struct recv_info *info = match_Receive(global_info, buffer,
		N * sizeof(int), MPI_BYTE, 1, 42, MPI_COMM_WORLD,
		MPI_STATUS_IGNORE);

		for (int n = 0; n < 1000; ++n) {

			for (int i = 0; i < N; ++i) {
				buffer[i] = rank * i * n;
			}

			start_Receive(global_info, info);
			end_Receive(global_info, info);
		}
		// free ressources
		free_receive_info(info);

		// after rdma get
		/*
		for (int i = 0; i < N; ++i) {
			printf("%i,", buffer[i]);
		}
		printf("\n");
		*/
	} else {
		match_send(global_info, 0, buffer, sizeof(int) * N, MPI_BYTE, 0, 42,
		MPI_COMM_WORLD);

		for (int n = 0; n < 1000; ++n) {
			for (int i = 0; i < N; ++i) {
				buffer[i] = rank * i * n;
			}
			begin_send(global_info, 0);
			end_send(global_info, 0);
		}
	}

	free(buffer);
	finalize(global_info);
}

void use_standard_comm() {

	int rank, numtasks;
// Welchen rang habe ich?
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
// wie viele Tasks gibt es?
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
	int *buffer = malloc(N * sizeof(int));

	if (rank == 0) {

		for (int n = 0; n < 1000; ++n) {
			for (int i = 0; i < N; ++i) {
				buffer[i] = rank * i * n;
			}
			MPI_Send(buffer, sizeof(int) * N, MPI_BYTE, 1, 42,
			MPI_COMM_WORLD);

		}
	} else {
		for (int n = 0; n < 1000; ++n) {

			for (int i = 0; i < N; ++i) {
				buffer[i] = rank * i * n;
			}

			MPI_Recv(buffer, sizeof(int) * N, MPI_BYTE, 0, 42,
			MPI_COMM_WORLD, MPI_STATUS_IGNORE);
		}

		// after comm
		/*
		for (int i = 0; i < N; ++i) {
			printf("%i,", buffer[i]);
		}
		printf("\n");
*/
	}
}

int main(int argc, char **argv) {

	struct timeval start_time; /* time when program started                      */
	struct timeval stop_time; /* time when calculation completed                */

//Initialisiere Alle Prozesse
	MPI_Init(&argc, &argv);
	gettimeofday(&start_time, NULL); /*  start timer         */
	use_self_implemented_comm();
	gettimeofday(&stop_time, NULL); /*  stop timer          */
	double time = (stop_time.tv_sec - start_time.tv_sec) + (stop_time.tv_usec - start_time.tv_usec) * 1e-6;

		printf("Self Implemented:    %f s \n", time);
	gettimeofday(&start_time, NULL); /*  start timer         */
	use_standard_comm();
	gettimeofday(&stop_time, NULL); /*  stop timer          */
	time = (stop_time.tv_sec - start_time.tv_sec) + (stop_time.tv_usec - start_time.tv_usec) * 1e-6;

		printf("Standard:    %f s \n", time);

	MPI_Finalize();
	return 0;
}

