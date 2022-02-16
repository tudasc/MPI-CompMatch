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
int main(int argc, char **argv) {

	int N = 10;
//Initialisiere Alle Prozesse
	MPI_Init(&argc, &argv);

	int send_list[2] = { 1, 1 };
	int recv_list[2] = { 1, 1 };

	struct global_information *global_info = Init(&send_list, &recv_list);

	int rank, numtasks;
// Welchen rang habe ich?
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
// wie viele Tasks gibt es?
	MPI_Comm_size(MPI_COMM_WORLD, &numtasks);

	int *buffer = malloc(N * sizeof(int));

	printf("Rank %i starts\n", rank);
	//old_demo(rank, N);

	if (rank == 0) {
		struct recv_info *info = match_Receive(global_info, buffer,
				N * sizeof(int), MPI_BYTE, 1, 42, MPI_COMM_WORLD,
				MPI_STATUS_IGNORE);

		for (int n = 0; n < 1000; ++n) {

			printf("%d\n", n);
			for (int i = 0; i < N; ++i) {
				buffer[i] = rank * i * n;
			}

			start_Receive(global_info, info);
			end_Receive(global_info, info);
		}
		// free ressources
		free_receive_info(info);

		// after rdma get
		for (int i = 0; i < N; ++i) {
			printf("%i,", buffer[i]);
		}
		printf("\n");
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
	MPI_Finalize();
	return 0;
}

