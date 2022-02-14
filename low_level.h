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
void RDMA_Put(void*buffer,size_t size,int target,MPI_Aint target_displacement,MPI_Win win);
// RDMA_Get
void RDMA_Get(void*buffer,size_t size,int target,MPI_Aint target_displacement,MPI_Win win);

#define DEFAULT_STATUS 0
#define DATA_READY_TO_SEND 1
#define DATA_RECEIVED 1

struct matching_queue_entry{
	int flag;
	int tag;
	MPI_Aint data_addr;
	MPI_Aint flag_addr;
	size_t size;
};

struct send_info{
	int flag;// set by remote
	int dest;
	MPI_Aint flag_addr;
	struct matching_queue_entry remote_entry;
};

struct recv_info{
	int dest;
	struct matching_queue_entry* matching_entry;
	void* data_buf;
};


struct global_information{
int num_ranks;
struct matching_queue_entry** matching_queues;
int* local_matching_queue_count;
int* remote_matching_queue_count;
int* local_matching_queue_size;
int* remote_matching_queue_size;

MPI_Aint* remote_matching_queue_addresses;
ompi_win_t * win; // same as: MPI_Win win;
};

void spin_wait(int* flag, int condition){
	while(*flag!=condition){
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

struct send_info* match_send(struct global_information *global_info, const void *buf,
		int count, MPI_Datatype datatype, int dest, int tag, MPI_Comm comm) ;

void begin_send(struct global_information *global_info,struct send_info* info);


void end_send(struct global_information *global_info,struct send_info* info);

struct recv_info* match_Receive(struct global_information *global_info,void *buf, int count, MPI_Datatype datatype, int source, int tag,
        MPI_Comm comm, MPI_Status *status);

void start_Receive(struct global_information *global_info,struct recv_info *info);


void end_Receive(struct global_information *global_info,struct recv_info *info);

#endif /* LOW_LEVEL_H_ */
