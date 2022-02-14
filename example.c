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

/*
 #include <mpi.h>
 #include <ucp/api/ucp.h>

 // why does other header miss this include?
 #include <stdbool.h>

 #include "ompi/mca/osc/osc.h"
 #include "ompi/mca/osc/base/base.h"
 #include "ompi/mca/osc/base/osc_base_obj_convert.h"
 #include "opal/mca/common/ucx/common_ucx.h"

 #include "ompi/mca/osc/ucx/osc_ucx.h"
 #include "ompi/mca/osc/ucx/osc_ucx_request.h"
 */

// from openucx doku:
void empty_function(void *request, ucs_status_t status) {
	// callback if flush is completed
}

ucs_status_t blocking_ep_flush(ucp_ep_h ep, ucp_worker_h worker) {
	void *request;
	request = ucp_ep_flush_nb(ep, 0, empty_function);
	if (request == NULL) {
		return UCS_OK;
	} else if (UCS_PTR_IS_ERR(request)) {
		return UCS_PTR_STATUS(request);
	} else {
		ucs_status_t status;
		do {
			ucp_worker_progress(worker);
			status = ucp_request_check_status(request);
		} while (status == UCS_INPROGRESS);
		ucp_request_free(request);
		return status;
	}
}

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

	printf("Rank %i starts\n", rank);

	int *buffer = malloc(N * sizeof(int));
	//typedef struct ompi_win_t *MPI_Win;

	if (rank == 0) {

		struct recv_info* info = match_Receive(global_info, buffer, N *sizeof(int), MPI_BYTE,1, 42, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

		for (int i = 0; i < N; ++i) {
			buffer[i] = rank * i;
		}

		start_Receive(global_info, info);
		end_Receive(global_info, info);
		free(info);



// after rdma get
		for (int i = 0; i < N; ++i) {
			printf("%i,", buffer[i]);
		}
		printf("\n");

	} else {
		struct send_info* info = match_send(global_info, buffer, sizeof(int)*N, MPI_BYTE, 0, 42, MPI_COMM_WORLD);

		for (int i = 0; i < N; ++i) {
			buffer[i] = rank * i;
		}
		begin_send(global_info, info);
		end_send(global_info, info);
		free(info);
	}


	printf("%i: Done\n", rank);

	MPI_Finalize();
	return 0;
}

void RDMA_Get(void *buffer, size_t size, int target,
		MPI_Aint target_displacement, MPI_Win win) {
	ompi_osc_ucx_module_t *module = (ompi_osc_ucx_module_t*) win->w_osc_module;
	ucp_ep_h ep = OSC_UCX_GET_EP(module->comm, target);
	// we know displacement unit
	uint64_t remote_addr = (module->win_info_array[target]).addr
			+ target_displacement;

	ucp_rkey_h rkey = (module->win_info_array[target]).rkey;

	ucs_status_t status = ucp_get_nbi(ep, (void*) buffer, size, remote_addr,
			rkey);
	if (status != UCS_OK && status != UCS_INPROGRESS) {
		printf("ERROR in RDMA GET\n");
	}

	// For Testing: use blocking flush:
	blocking_ep_flush(ep, mca_osc_ucx_component.ucp_worker);

}

void RDMA_Put(void *buffer, size_t size, int target,
		MPI_Aint target_displacement, MPI_Win win) {
	ompi_osc_ucx_module_t *module = (ompi_osc_ucx_module_t*) win->w_osc_module;
	ucp_ep_h ep = OSC_UCX_GET_EP(module->comm, target);
	// we know displacement unit
	uint64_t remote_addr = (module->win_info_array[target]).addr
			+ target_displacement;

	ucp_rkey_h rkey = (module->win_info_array[target]).rkey;

	ucs_status_t status = ucp_put_nbi(ep, (void*) buffer, size, remote_addr,
			rkey);
	if (status != UCS_OK && status != UCS_INPROGRESS) {
		printf("ERROR in RDMA GET\n");
	}

	// For Testing: use blocking flush:
	blocking_ep_flush(ep, mca_osc_ucx_component.ucp_worker);

}

#define INIT_MSG_TAG 1337

struct global_information* Init(int *send_list, int *recv_list) {

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	struct global_information *result = malloc(
			sizeof(struct global_information));

	MPI_Win_create_dynamic(
	MPI_INFO_NULL,
	MPI_COMM_WORLD, &result->win);
	MPI_Comm_size(MPI_COMM_WORLD, &result->num_ranks);

	result->local_matching_queue_count = malloc(
			result->num_ranks * sizeof(int));
	result->remote_matching_queue_count = malloc(
			result->num_ranks * sizeof(int));
	result->local_matching_queue_size = malloc(result->num_ranks * sizeof(int));
	result->remote_matching_queue_size = malloc(
			result->num_ranks * sizeof(int));

	result->matching_queues = malloc(
			result->num_ranks * sizeof(struct matching_queue_entry*));
	result->remote_matching_queue_addresses = malloc(
			result->num_ranks * sizeof(MPI_Aint));

	for (int i = 0; i < result->num_ranks; ++i) {
		if (i == rank) {
			result->local_matching_queue_count[i] = 0;
			result->remote_matching_queue_count[i] = 0;
			result->local_matching_queue_size[i] = 0;
			result->remote_matching_queue_size[i] = 0;
			result->matching_queues[i] = NULL;
			result->remote_matching_queue_addresses[i] = 0;

		} else {
			result->local_matching_queue_count[i] = 0;
			result->remote_matching_queue_count[i] = 0;
			result->local_matching_queue_size[i] = recv_list[i];
			result->remote_matching_queue_size[i] = send_list[i];
			result->matching_queues[i] = malloc(
					recv_list[i] * sizeof(struct matching_queue_entry));
			MPI_Win_attach(result->win, &result->matching_queues[i],
					recv_list[i] * sizeof(struct matching_queue_entry));
			MPI_Aint local_adress;
			MPI_Get_address(&result->matching_queues[i], &local_adress);
			//TODO should use sendrecv to avoid deadlock
			MPI_Send(&local_adress, 1, MPI_AINT, i, INIT_MSG_TAG,
			MPI_COMM_WORLD);
			//result->remote_matching_queue_addresses[i] = 0;
			MPI_Recv(&result->remote_matching_queue_addresses[i], 1,
			MPI_AINT, i, INIT_MSG_TAG, MPI_COMM_WORLD,
			MPI_STATUSES_IGNORE);

		}
	}

	return result;

}

//TODO  refactor global to be a global var?

//TODO get_type_extend benutzen um andere types als byte zu erlauben
struct send_info* match_send(struct global_information *global_info, const void *buf,
		int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
	assert(comm==MPI_COMM_WORLD);
	assert(datatype==MPI_BYTE);
	assert(global_info->remote_matching_queue_size[dest]>global_info->remote_matching_queue_count[dest]);

	struct send_info* info=malloc(sizeof (struct send_info));

	info->flag=0;
	info->dest=dest;

	MPI_Aint remote_queue_base =
			global_info->remote_matching_queue_addresses[dest];
	MPI_Aint remote_queue_pos = remote_queue_base
			+ (sizeof(struct matching_queue_entry)
					* global_info->remote_matching_queue_count[dest]);

	info->flag_addr=remote_queue_pos;
					//TODO double attach?
	MPI_Win_attach(global_info->win, buf, count);
	MPI_Win_attach(global_info->win, &info, sizeof(int));


	info->remote_entry.flag = 0;
	info->remote_entry.tag = tag;
	MPI_Get_address(buf, &info->remote_entry.data_addr);
	MPI_Get_address(&info->flag, &info->remote_entry.flag_addr);
	info->remote_entry.size = count;

RDMA_Put(&info->remote_entry, sizeof(struct matching_queue_entry), dest, remote_queue_pos, global_info->win);

global_info->remote_matching_queue_count[dest]++;

return info;

}

void begin_send(struct global_information *global_info,struct send_info* info){
	info->remote_entry.flag =DATA_READY_TO_SEND;
	RDMA_Put(&info->remote_entry.flag, sizeof(int), info->dest, info->flag_addr, global_info->win);
}


void end_send(struct global_information *global_info,struct send_info* info){
	//nothing to do

	spin_wait(&info->flag,DATA_RECEIVED);

}

struct recv_info* match_Receive(struct global_information *global_info,void *buf, int count, MPI_Datatype datatype, int source, int tag,
        MPI_Comm comm, MPI_Status *status){
	assert(comm==MPI_COMM_WORLD);
	assert(datatype==MPI_BYTE);

	struct recv_info* info = malloc(sizeof(struct recv_info));

	//TODO IMPLEMENT ACTUAL MESSAGE MATCHING

	// currently ther will be only one op, so we will math this

	info->dest=source;
	info->matching_entry= &global_info->matching_queues[source][0];
	info->data_buf=buf;


}

void start_Receive(struct global_information *global_info,struct recv_info *info){

	spin_wait(&info->matching_entry->flag, DATA_READY_TO_SEND);

	RDMA_Get(info->data_buf, info->matching_entry->size, info->dest, info->matching_entry->data_addr, global_info->win);

}

void end_Receive(struct global_information *global_info,struct recv_info *info){
	info->matching_entry->flag=DATA_RECEIVED;
	RDMA_Put(&info->matching_entry->flag, sizeof(int), info->dest, info->matching_entry->flag_addr, global_info->win);
}
