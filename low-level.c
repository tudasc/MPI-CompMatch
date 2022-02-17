#include "low_level.h"

#define INIT_MSG_TAG 1337

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


void RDMA_Get_test(void* buffer, size_t size,ucp_rkey_h rkey,ucp_ep_h ep,uint64_t remote_addr){
	ucs_status_t status = ucp_get_nbi(ep, (void*) buffer, size, remote_addr,
				rkey);

		if (status != UCS_OK && status != UCS_INPROGRESS) {
			printf("ERROR in RDMA GET\n");
		}

		// For Testing: use blocking flush:
		blocking_ep_flush(ep, mca_osc_ucx_component.ucp_worker);


}

void RDMA_Put_test(void* buffer, size_t size,ucp_rkey_h rkey,ucp_ep_h ep,uint64_t remote_addr){
	ucs_status_t status = ucp_put_nbi(ep, (void*) buffer, size, remote_addr,
				rkey);

		if (status != UCS_OK && status != UCS_INPROGRESS) {
			printf("ERROR in RDMA PUT\n");
		}

		// For Testing: use blocking flush:
		blocking_ep_flush(ep, mca_osc_ucx_component.ucp_worker);


}


void RDMA_Get(void *buffer, size_t size, int target,
		MPI_Aint target_displacement, MPI_Win win) {
	ompi_osc_ucx_module_t *module = (ompi_osc_ucx_module_t*) win->w_osc_module;
	ucp_ep_h ep = OSC_UCX_GET_EP(module->comm, target);
	// we know displacement unit
	//uint64_t remote_addr = (module->win_info_array[target]).addr
	//		+ target_displacement;
	// we will give the direct address
	uint64_t remote_addr = target_displacement;
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
	//uint64_t remote_addr = (module->win_info_array[target]).addr
	//		+ target_displacement;
	// we will give the direct address
	uint64_t remote_addr = target_displacement;

	ucp_rkey_h rkey = (module->win_info_array[target]).rkey;

	ucs_status_t status = ucp_put_nbi(ep, (void*) buffer, size, remote_addr,
			rkey);
	if (status != UCS_OK && status != UCS_INPROGRESS) {
		printf("ERROR in RDMA GET\n");
	}

	// For Testing: use blocking flush:
	blocking_ep_flush(ep, mca_osc_ucx_component.ucp_worker);

}

struct global_information* Init(int *send_list, int *recv_list) {

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	struct global_information *result = malloc(
			sizeof(struct global_information));

	MPI_Comm_size(MPI_COMM_WORLD, &result->num_ranks);

	int total_matching_queue_entries = 0;
	int total_send_queue_entries = 0;
	for (int i = 0; i < result->num_ranks; ++i) {
		if (i != rank)
			total_matching_queue_entries += recv_list[i];
		printf("%d: Allocate Queue Size %d for msgs from rank %d\n", rank,
				recv_list[i], i);
		total_send_queue_entries += send_list[i];
	}

	size_t total_queue_mem_needed = sizeof(struct matching_queue_entry)
			* total_matching_queue_entries
			+ sizeof(struct send_info) * total_send_queue_entries;

	void *matching_queue_mem = calloc(total_queue_mem_needed, 1);

	MPI_Win_create(matching_queue_mem, total_queue_mem_needed, 1,
	MPI_INFO_NULL,
	MPI_COMM_WORLD, &result->win);

	//TODO reduce the amount of malloc calls
	//int* count_size_mem=malloc(result->num_ranks * sizeof(int)*4);

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

	struct matching_queue_entry *current_matching_mem_pos =
			(struct matching_queue_entry*) matching_queue_mem;
	for (int i = 0; i < result->num_ranks; ++i) {
		if (i == rank) {
			result->local_matching_queue_count[i] = 0;
			result->remote_matching_queue_count[i] = 0;
			result->local_matching_queue_size[i] = 0;
			result->remote_matching_queue_size[i] = 0;
			result->matching_queues[i] = current_matching_mem_pos;
			// advance the ptr:
			current_matching_mem_pos = (sizeof(struct send_info)
					* total_send_queue_entries)
					+ ((char*) current_matching_mem_pos);
			result->remote_matching_queue_addresses[i] = 0;

		} else {
			int this_size = recv_list[i];

			result->local_matching_queue_count[i] = 0;
			result->remote_matching_queue_count[i] = 0;
			result->local_matching_queue_size[i] = this_size;
			result->remote_matching_queue_size[i] = send_list[i]; //TODO maybe communicate with other rank?
			result->matching_queues[i] = current_matching_mem_pos;
			// advance the ptr:
			current_matching_mem_pos = &current_matching_mem_pos[this_size];

			MPI_Aint local_adress;
			MPI_Get_address(result->matching_queues[i], &local_adress);
			//TODO should use sendrecv to avoid deadlock
			MPI_Send(&local_adress, 1, MPI_AINT, i, INIT_MSG_TAG,
			MPI_COMM_WORLD);
			MPI_Recv(&result->remote_matching_queue_addresses[i], 1,
			MPI_AINT, i, INIT_MSG_TAG, MPI_COMM_WORLD,
			MPI_STATUSES_IGNORE);

		}
	}

	// pointing directly behind alloced mem
	assert(
			current_matching_mem_pos
					== matching_queue_mem + total_queue_mem_needed);

	return result;

}

//TODO  refactor global to be a global var?

//TODO pack the rkey and send it to remote alongside the local address

// maps mem for rdma and initializes the send_info accordingly
void map_mem_for_rdma(struct send_info *info, void *buf, size_t size) {

	ucp_context_h context = mca_osc_ucx_component.ucp_context;
	// prepare buffer for RDMA access:
	ucp_mem_map_params_t mem_params;
	//ucp_mem_attr_t mem_attrs;
	ucs_status_t status;
	// init mem params
	memset(&mem_params, 0, sizeof(ucp_mem_map_params_t));

	mem_params.address = buf;
	mem_params.length = size;
	// we need to tell ucx what fields are valid
	mem_params.field_mask = UCP_MEM_MAP_PARAM_FIELD_ADDRESS
			| UCP_MEM_MAP_PARAM_FIELD_LENGTH;

	status = ucp_mem_map(context, &mem_params, &info->mem_handle);
	assert(status == UCS_OK && "Error in register mem for RDMA operation");

	void *rkey_buffer;
	size_t rkey_size;

	// pack a remote memory key
	status = ucp_rkey_pack(context, info->mem_handle, &rkey_buffer, &rkey_size);
	assert(status == UCS_OK && "Error in register mem for RDMA operation");

	printf("Packed Key, size:%lu\n", rkey_size);
	assert(rkey_size<=RKEY_SPACE);

	// cpy packed rkey in proper buffer for rdma transfer
	memcpy(&info->remote_entry.rkey, rkey_buffer, rkey_size);
	// free temp buf
	ucp_rkey_buffer_release(rkey_buffer);
}

//TODO using different tags, and tgt ranks is not currently supported

_Static_assert(sizeof(struct send_info**) == sizeof(struct matching_queue_entry**),"Pointer sizes are different");

//TODO get_type_extend benutzen um andere types als byte zu erlauben
void match_send(struct global_information *global_info, int send_ID, void *buf,
		int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) {
	assert(comm==MPI_COMM_WORLD);
	assert(datatype==MPI_BYTE);
	assert(
			global_info->remote_matching_queue_size[dest]
					> global_info->remote_matching_queue_count[dest]);

	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	struct send_info *info =
			&(((struct send_info**) global_info->matching_queues)[rank][send_ID]);

	if (info->flag == 0) { // first time this operation is used
		// need to "allocate" a new space in the remote receive queue
		info->flag = CHECK_FOR_PREVIOUS_RKEY;
		info->dest = dest;

		MPI_Aint remote_queue_base =
				global_info->remote_matching_queue_addresses[dest];
		MPI_Aint remote_queue_pos = remote_queue_base
				+ (sizeof(struct matching_queue_entry)
						* global_info->remote_matching_queue_count[dest]);

		info->flag_addr = remote_queue_pos;

		info->remote_entry.tag = tag;
		info->remote_entry.data_addr = buf;
		//MPI_Get_address(buf, &info->remote_entry.data_addr);
		MPI_Get_address(&info->flag, &info->remote_entry.flag_addr);
		info->remote_entry.size = count;

		map_mem_for_rdma(info, buf, count);

		info->remote_entry.flag = MATCHING_REQUEST_INITIALIZED;

		global_info->remote_matching_queue_count[dest]++;

	} else {

		info->remote_entry.flag = MATCHING_REQUEST_INITIALIZED;

		// double check, that the op has completed
		//TODO should not be necessary
		spin_wait_for(&info->flag, DATA_RECEIVED);

		if (info->remote_entry.data_addr != buf) {
			// need to update buffer
			info->remote_entry.data_addr = buf;

			ucp_context_h context = mca_osc_ucx_component.ucp_context;

			ucp_mem_unmap(context, info->mem_handle);
			map_mem_for_rdma(info, buf, count);
		} else {
			info->remote_entry.flag = info->remote_entry.flag
					| PREVIOUS_RKEY_IS_VALID;
		}

		// just update tag, no special requirements
		info->remote_entry.tag = tag;

		if (dest != info->dest) {
			//TODO update destination
			assert(false);
		}

	}

	//TODO we need less RDMa transfer if only the flag needs to be updated
	RDMA_Put(&info->remote_entry, sizeof(struct matching_queue_entry), dest,
			info->flag_addr, global_info->win);

	return;

}

void begin_send(struct global_information *global_info, const int send_ID) {
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	struct send_info *info =
			&(((struct send_info**) global_info->matching_queues)[rank][send_ID]);

	info->remote_entry.flag = DATA_READY_TO_SEND;

	if (info->flag == SEND_OP_FINISHED) {
		info->remote_entry.flag = info->remote_entry.flag
				| PREVIOUS_RKEY_IS_VALID;
	}

	RDMA_Put(&info->remote_entry.flag, sizeof(int), info->dest, info->flag_addr,
			global_info->win);
}

void end_send(struct global_information *global_info, const int send_ID) {
	//nothing to do
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	struct send_info *info =
			&(((struct send_info**) global_info->matching_queues)[rank][send_ID]);

	spin_wait_for(&info->flag, DATA_RECEIVED);
	info->flag = SEND_OP_FINISHED;

}

struct recv_info* match_Receive(struct global_information *global_info,
		void *buf, int count, MPI_Datatype datatype, int source, int tag,
		MPI_Comm comm, MPI_Status *status) {
	assert(comm==MPI_COMM_WORLD);
	assert(datatype==MPI_BYTE);

	struct recv_info *info = malloc(sizeof(struct recv_info));

	// currently there will be only one op, so we will math this

	info->dest = source;
	info->matching_entry = &global_info->matching_queues[source][0]; //TODO IMPLEMENT ACTUAL MESSAGE MATCHING
	info->data_buf = buf;

	return info;
}

void start_Receive(struct global_information *global_info,
		struct recv_info *info) {

	ompi_osc_ucx_module_t *module =
			(ompi_osc_ucx_module_t*) global_info->win->w_osc_module;
	ucp_ep_h ep = OSC_UCX_GET_EP(module->comm, info->dest);

	spin_wait_for_and(&info->matching_entry->flag, DATA_READY_TO_SEND);

	if (!(info->matching_entry->flag & PREVIOUS_RKEY_IS_VALID)) {

		//TODO free previous rkey if any
		// unpack rkey
		ucs_status_t status = ucp_ep_rkey_unpack(ep,
				&info->matching_entry->rkey, &info->rkey);
		assert(status == UCS_OK && "ERROR in unpacking the RKEY");
	}

	// RDMA GET
	ucs_status_t status = ucp_get_nbi(ep, info->data_buf,
			info->matching_entry->size, info->matching_entry->data_addr,
			info->rkey);
	if (status != UCS_OK && status != UCS_INPROGRESS) {
		printf("ERROR in RDMA GET\n");
	}

	// For Testing: use blocking flush:
	blocking_ep_flush(ep, mca_osc_ucx_component.ucp_worker);

}

void end_Receive(struct global_information *global_info, struct recv_info *info) {

	info->matching_entry->flag = DATA_RECEIVED;
	RDMA_Put(&info->matching_entry->flag, sizeof(int), info->dest,
			info->matching_entry->flag_addr, global_info->win);

}

void finalize(struct global_information *global_info) {

	//TODO this should be a param instead of a define
#define MAX_NUM_OF_SENDS 1
	//release all ressources
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);

	ucp_context_h context = mca_osc_ucx_component.ucp_context;

	for (int i = 0; i < MAX_NUM_OF_SENDS; ++i) {
		struct send_info *info =
				&(((struct send_info**) global_info->matching_queues)[rank][i]);

		// otherwise op was not used
		if (info->remote_entry.flag != 0) {
			ucp_mem_unmap(context, info->mem_handle);
		}
	}

	MPI_Win_free(&global_info->win);
	free(global_info->local_matching_queue_count);
	free(global_info->local_matching_queue_size);
	free(global_info->remote_matching_queue_count);
	free(global_info->remote_matching_queue_size);

	free(global_info->remote_matching_queue_addresses);
	free(global_info->matching_queues[0]);
	free(global_info->matching_queues);
	free(global_info);

}

