// TOOD Rename File with more appropriate name
#ifndef LOW_LEVEL_H_
#define LOW_LEVEL_H_

#include <stddef.h>

#include <mpi.h>

// TODO clean includes
#include <ucp/api/ucp.h>

// why does other header miss this include?
#include <stdbool.h>

#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/osc/base/osc_base_obj_convert.h"
#include "ompi/mca/osc/osc.h"
#include "opal/mca/common/ucx/common_ucx.h"

#include "ompi/mca/osc/ucx/osc_ucx.h"
#include "ompi/mca/osc/ucx/osc_ucx_request.h"

// TODO is there an openmpi internal value for tag UB
// we can set tag UB to TAG_UB/2
#define COMM_BEGIN_TAG 32767
// count is not part of the message envelope

#define RECV_REQUEST_TYPE 1
#define SEND_REQUEST_TYPE 2
#define SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION 3
#define RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION 4
#define SEND_REQUEST_TYPE_USE_FALLBACK 5
#define Recv_REQUEST_TYPE_USE_FALLBACK 6

struct mpiopt_request {
  // this way it it can be used as a normal request ptr as well
  struct ompi_request_t original_request;
  int flag;
  int flag_buffer;
  uint64_t remote_data_addr;
  uint64_t remote_flag_addr;
  ucp_rkey_h remote_data_rkey;
  ucp_rkey_h remote_flag_rkey;
  void *buf;
  size_t size;
  // initialized locally
  void *ucx_request_data_transfer;
  void *ucx_request_flag_transfer;
  int operation_number;
  int type;
  ucp_mem_h mem_handle_data;
  ucp_mem_h mem_handle_flag;
  ucp_ep_h
      ep; // save used endpoint, so we dont have to look it up over and over
  // necessary for backup in case no other persistent op matches
  MPI_Request backup_request;
  int tag;
  int dest;
  MPI_Comm comm;
  MPI_Request rdma_exchange_request;
  MPI_Request rdma_exchange_request_send;
  // struct mpiopt_request* rdma_exchange_buffer;
};

/*
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
*/

// TODO the compiler does know if it should start a send or recv, test if it
// makes a difference, if we do not need this if in the code

typedef struct mpiopt_request MPIOPT_Request;

int MPIOPT_Start(MPI_Request *request);
int MPIOPT_Wait(MPI_Request *request, MPI_Status *status);
int MPIOPT_Test(MPI_Request *request, int *flag, MPI_Status *status);
int MPIOPT_Send_init(const void *buf, int count, MPI_Datatype datatype,
                     int dest, int tag, MPI_Comm comm, MPI_Request *request);
int MPIOPT_Recv_init(void *buf, int count, MPI_Datatype datatype, int source,
                     int tag, MPI_Comm comm, MPI_Request *request);
int MPIOPT_Request_free(MPI_Request *request);

void MPIOPT_INIT();
void MPIOPT_FINALIZE();

#define RDMA_SPIN_WAIT_THRESHOLD 32

#endif /* LOW_LEVEL_H_ */
