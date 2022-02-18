//TOOD Rename File with more appropriate name
#ifndef LOW_LEVEL_H_
#define LOW_LEVEL_H_

#include <stddef.h>

#include <mpi.h>

//TODO clean includes
#include <ucp/api/ucp.h>

// why does other header miss this include?
#include <stdbool.h>

#include "ompi/mca/osc/osc.h"
#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/osc/base/osc_base_obj_convert.h"
#include "opal/mca/common/ucx/common_ucx.h"

#include "ompi/mca/osc/ucx/osc_ucx.h"
#include "ompi/mca/osc/ucx/osc_ucx_request.h"

// small RDMA_Put is unlikely to return a request object
void RDMA_Put(void *buffer, size_t size, int target,
		MPI_Aint target_displacement, MPI_Win win);
// RDMA_Get
void RDMA_Get(void *buffer, size_t size, int target,
		MPI_Aint target_displacement, MPI_Win win);

#define RKEY_SPACE 50

#define DEFAULT_STATUS 0
#define DATA_READY_TO_SEND 1
#define PREVIOUS_RKEY_IS_VALID 1 << 1
#define DATA_RECEIVED 1<< 2
#define CHECK_FOR_PREVIOUS_RKEY 1<< 3
#define MATCHING_REQUEST_INITIALIZED 1<< 4
#define SEND_OP_FINISHED 1<< 5

struct matching_queue_entry {
	int flag;
	int tag;
	size_t size;
	MPI_Aint data_addr; // where to pull data from
	MPI_Aint flag_addr; // where to signal that data was received
	char rkey[RKEY_SPACE]; // space for rkey to access remote data
};

struct send_info {
	int flag; // set by remote
	int dest;
	ucp_mem_h mem_handle; // ucp mem handle of the data buffer
	MPI_Aint flag_addr; // where the remote matching_queue_entry starts
	struct matching_queue_entry remote_entry; // copy of remote's matching_queue_entry (used as buffer to push it to remote)
};

struct recv_info {
	int dest; // source rank == dest of RDMA fetch
	struct matching_queue_entry *matching_entry; // ptr to the matching entry
	ucp_rkey_h rkey; // rkey to access remote data
	void *data_buf; // ptr to data buf
};

struct global_information {
	int num_ranks;
	struct matching_queue_entry **matching_queues;
	int *local_matching_queue_count;
	int *remote_matching_queue_count;
	int *local_matching_queue_size;
	int *remote_matching_queue_size;

	MPI_Aint *remote_matching_queue_addresses;
//ompi_win_t * win; // same as: MPI_Win win;
	MPI_Win win;
};

static inline void spin_wait_for(int *flag, int condition) {
	while (*flag != condition) {
		//TODO sleep?
		ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
	}
	return;
}


// and flag and condition
static inline void spin_wait_for_and(int *flag, int condition) {
	while (!((*flag) & condition)) {
		//TODO sleep
	}
	return;
}

static inline void spin_wait_until_not(int *flag, int condition) {
	while (*flag == condition) {
		//TODO sleep
	}
	return;
}

// Init

// Init Send
// Init Recv

// start Send
// start Recv

// end Send
// end Recv

// finalize

struct global_information* Init(int *send_list, int *recv_list);
void finalize(struct global_information *global_info);

void match_send(struct global_information *global_info, const int send_ID,
		void *buf, int count, MPI_Datatype datatype, int dest, int tag,
		MPI_Comm comm);

void begin_send(struct global_information *global_info, const int send_ID);

void end_send(struct global_information *global_info, const int send_ID);

struct recv_info* match_Receive(struct global_information *global_info,
		void *buf, int count, MPI_Datatype datatype, int source, int tag,
		MPI_Comm comm, MPI_Status *status);

void start_Receive(struct global_information *global_info,
		struct recv_info *info);

void end_Receive(struct global_information *global_info,
		struct recv_info *info);

static inline void free_receive_info(struct recv_info *info) {
	ucp_rkey_destroy(info->rkey);
	free(info);
}

#endif /* LOW_LEVEL_H_ */
