
#include <mpi.h>
#include <ucp/api/ucp.h>

// why does other header miss this include?
#include <stdbool.h>

#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/osc/base/osc_base_obj_convert.h"
#include "ompi/mca/osc/osc.h"
#include "opal/mca/common/ucx/common_ucx.h"

#include "ompi/mca/osc/ucx/osc_ucx.h"
#include "ompi/mca/osc/ucx/osc_ucx_request.h"

// config :
// TODO is there an openmpi internal value for tag UB
// we can set tag UB to TAG_UB/2
#define COMM_BEGIN_TAG 32767
// count is not part of the message envelope
#define RDMA_SPIN_WAIT_THRESHOLD 32

//#define STATISTIC_PRINTING

// end config

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
typedef struct mpiopt_request MPIOPT_Request;

MPI_Win global_comm_win;
int dummy_int = 0;

struct matching_info {
  int dummy;
};

void empty_function(void *request, ucs_status_t status) {
  // callback if flush is completed
}

static int MPIOPT_Start_internal(MPIOPT_Request *request);
static int MPIOPT_Wait_internal(MPIOPT_Request *request, MPI_Status *status);
static int MPIOPT_Test_internal(MPIOPT_Request *request, int *flag,
                                MPI_Status *status);
static int MPIOPT_Send_init_internal(const void *buf, int count,
                                     MPI_Datatype datatype, int dest, int tag,
                                     MPI_Comm comm, MPIOPT_Request *request);
static int MPIOPT_Recv_init_internal(void *buf, int count,
                                     MPI_Datatype datatype, int source, int tag,
                                     MPI_Comm comm, MPIOPT_Request *request);
static int MPIOPT_Request_free_internal(MPIOPT_Request *request);

static void wait_for_completion_blocking(void *request) {
  assert(request != NULL);
  ucs_status_t status;
  do {
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
    status = ucp_request_check_status(request);
  } while (status == UCS_INPROGRESS);
  ucp_request_free(request);
}

static void acknowlege_Request_free(MPIOPT_Request *request) {
  // wait for any outstanding RDMA Operation (i.e. transfer of flag that comm
  // is finished)
  if (__builtin_expect(request->ucx_request_data_transfer != NULL, 0)) {
    wait_for_completion_blocking(request->ucx_request_flag_transfer);
    request->ucx_request_flag_transfer = NULL;
  }
  if (__builtin_expect(request->ucx_request_flag_transfer != NULL, 0)) {
    wait_for_completion_blocking(request->ucx_request_flag_transfer);
    request->ucx_request_flag_transfer = NULL;
  }

  request->flag_buffer = -1;

  MPI_Send(&request->operation_number, sizeof(int), MPI_BYTE, request->dest,
           COMM_BEGIN_TAG + request->tag, request->comm);

  // receive the Msg that the communication will end
  MPI_Recv(&request->flag_buffer, sizeof(int), MPI_BYTE, request->dest,
           COMM_BEGIN_TAG + request->tag, request->comm, MPI_STATUS_IGNORE);

  // we need to be in the same msg as the other rank
  assert(request->flag_buffer == request->operation_number);

  // release all RDMA ressources
  ucp_context_h context = mca_osc_ucx_component.ucp_context;

  ucp_mem_unmap(context, request->mem_handle_flag);
  ucp_mem_unmap(context, request->mem_handle_data);
#ifdef STATISTIC_PRINTING
  printf("RDMA  connection closed\n");
#endif
}

// operation_number*2= op has not started on remote
// operation_number*2 +1= op has started on remote, 	we should initiate data
// transfer operation_number*2 + 2= op has finished on remote

static void b_send(MPIOPT_Request *request) {

  if (__builtin_expect(request->flag == request->operation_number * 2 + 1, 0)) {
    // increment: signal that WE finish the operation on the remote
    request->flag++;
    // no possibility of data-race, the remote will wait for us to put the data
    assert(request->flag == request->operation_number * 2 + 2);
    // start rdma data transfer
#ifdef STATISTIC_PRINTING
    printf("send pushes data\n");
#endif
    ucs_status_t status =
        ucp_put_nbi(request->ep, request->buf, request->size,
                    request->remote_data_addr, request->remote_data_rkey);
    // ensure order:
    status = ucp_worker_fence(mca_osc_ucx_component.ucp_worker);
    status = ucp_put_nbi(request->ep, &request->flag_buffer, sizeof(int),
                         request->remote_flag_addr, request->remote_flag_rkey);
    request->ucx_request_data_transfer =
        ucp_ep_flush_nb(request->ep, 0, empty_function);

    // TODO do I call progress here?
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

  } else {
    request->flag_buffer = request->operation_number * 2 + 1;
    // give him the flag that we are ready: he will RDMA get the data
    ucs_status_t status =
        ucp_put_nbi(request->ep, &request->flag_buffer, sizeof(int),
                    request->remote_flag_addr, request->remote_flag_rkey);

    request->ucx_request_flag_transfer =
        ucp_ep_flush_nb(request->ep, 0, empty_function);
    // TODO do I call progress here?
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
  }
}

static void e_send_with_comm_abort_test(MPIOPT_Request *request) {
  int flag_comm_abort = 0;

  // busy wait
  while (__builtin_expect(request->flag < request->operation_number * 2 + 2 &&
                              !flag_comm_abort,
                          0)) {

    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

    MPI_Iprobe(request->dest, COMM_BEGIN_TAG + request->tag, request->comm,
               &flag_comm_abort, MPI_STATUS_IGNORE);
  }

  if (__builtin_expect(flag_comm_abort, 0)) {
    // other rank has freed the corresponding request
    acknowlege_Request_free(request);
    request->type = SEND_REQUEST_TYPE_USE_FALLBACK;
    // check if we need to send the current msg via fallback:
    if (request->flag < request->operation_number * 2 + 2) {
      MPI_Send(request->buf, request->size, MPI_BYTE, request->dest,
               request->tag, request->comm);
    }
  }
}

static void e_send(MPIOPT_Request *request) {

  // ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
  // will call progres only if this is necessary

  if (__builtin_expect(request->ucx_request_flag_transfer != NULL, 0)) {
    wait_for_completion_blocking(request->ucx_request_flag_transfer);
    request->ucx_request_flag_transfer = NULL;
  }

  // same for data transfer
  if (__builtin_expect(request->ucx_request_data_transfer != NULL, 0)) {
    wait_for_completion_blocking(request->ucx_request_data_transfer);
    request->ucx_request_data_transfer = NULL;
  }

  // we need to wait until the op has finished on the remote before re-using the
  // data buffer
  int count = 0;
  // busy wait
  while (__builtin_expect(request->flag < request->operation_number * 2 + 2 &&
                              count < RDMA_SPIN_WAIT_THRESHOLD,
                          0)) {
    ++count;
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
  }

  if (__builtin_expect(request->flag < request->operation_number * 2 + 2, 0)) {
    // after some time: also test if the other rank has freed the request in
    // between
    e_send_with_comm_abort_test(request);
  }
}

static void b_recv(MPIOPT_Request *request) {
  if (__builtin_expect(request->flag == request->operation_number * 2 + 1, 0)) {

    request->flag++; // recv is done at our side
    // no possibility of data race, WE will advance the comm
    assert(request->flag == request->operation_number * 2 + 2);
    // start rdma data transfer
#ifdef STATISTIC_PRINTING
    printf("recv fetches data\n");
#endif
    ucs_status_t status =
        ucp_get_nbi(request->ep, (void *)request->buf, request->size,
                    request->remote_data_addr, request->remote_data_rkey);

    assert(status == UCS_OK || status == UCS_INPROGRESS);
    /*
    if (status != UCS_OK && status != UCS_INPROGRESS) {
      printf("ERROR in RDMA GET\n");
    }*/
    // ensure order:
    status = ucp_worker_fence(mca_osc_ucx_component.ucp_worker);
    assert(status == UCS_OK || status == UCS_INPROGRESS);

    request->flag_buffer = request->operation_number * 2 + 2;
    status = ucp_put_nbi(request->ep, &request->flag_buffer, sizeof(int),
                         request->remote_flag_addr, request->remote_flag_rkey);
    assert(status == UCS_OK || status == UCS_INPROGRESS);

    request->ucx_request_data_transfer =
        ucp_ep_flush_nb(request->ep, 0, empty_function);

    // TODO do I call progress here?
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

  } else {
    // request->flag = READY_TO_RECEIVE;
    request->flag_buffer = request->operation_number * 2 + 1;
    // give him the flag that we are ready: he will RDMA put the data
    ucs_status_t status =
        ucp_put_nbi(request->ep, &request->flag_buffer, sizeof(int),
                    request->remote_flag_addr, request->remote_flag_rkey);
    assert(status == UCS_OK || status == UCS_INPROGRESS);

    request->ucx_request_flag_transfer =
        ucp_ep_flush_nb(request->ep, 0, empty_function);
    // TODO do I call progress here?
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
  }
}

static void e_recv_with_comm_abort_test(MPIOPT_Request *request) {
  int flag_comm_abort = 0;

  // busy wait
  while (__builtin_expect(request->flag < request->operation_number * 2 + 1 &&
                              !flag_comm_abort,
                          0)) {

    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

    MPI_Iprobe(request->dest, COMM_BEGIN_TAG + request->tag, request->comm,
               &flag_comm_abort, MPI_STATUS_IGNORE);
  }

  if (__builtin_expect(flag_comm_abort, 0)) {
    // other rank has freed the corresponding request
    acknowlege_Request_free(request);
    request->type = Recv_REQUEST_TYPE_USE_FALLBACK;
    // check if we need to send the current msg via fallback:
    if (request->flag < request->operation_number * 2 + 2) {
      MPI_Recv(request->buf, request->size, MPI_BYTE, request->dest,
               request->tag, request->comm, MPI_STATUS_IGNORE);
    }
  }
}

static void e_recv(MPIOPT_Request *request) {
  // ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

  if (__builtin_expect(request->ucx_request_flag_transfer != NULL, 0)) {
    wait_for_completion_blocking(request->ucx_request_flag_transfer);
    request->ucx_request_flag_transfer = NULL;
  }

  // same for data transfer
  if (__builtin_expect(request->ucx_request_data_transfer != NULL, 0)) {
    wait_for_completion_blocking(request->ucx_request_data_transfer);
    request->ucx_request_data_transfer = NULL;
  }

  int count = 0;
  // busy wait
  while (__builtin_expect(request->flag < request->operation_number * 2 + 1 &&
                              count < RDMA_SPIN_WAIT_THRESHOLD,
                          0)) {
    ++count;
    ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
  }

  e_recv_with_comm_abort_test(request);

  if (__builtin_expect(request->flag == request->operation_number * 2 + 1, 0)) {
#ifdef STATISTIC_PRINTING
    printf("crosstalk detected\n");
#endif
    // fetch the data
    b_recv(request);
    // and block until transfer finished
    if (request->ucx_request_data_transfer != NULL) {
      wait_for_completion_blocking(request->ucx_request_data_transfer);
      request->ucx_request_data_transfer = NULL;
    }

  } // else: nothing to do, the op has finished
}

// exchanges the RDMA info
static void exchange_rdma_info(MPIOPT_Request *request) {
#ifdef STATISTIC_PRINTING
  printf("RDMA  connection established\n");
#endif

  assert(request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION ||
         request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION);

  uint64_t flag_ptr = &request->flag;
  // MPIOPT_Request info_to_send;

  ompi_osc_ucx_module_t *module =
      (ompi_osc_ucx_module_t *)global_comm_win->w_osc_module;
  ucp_ep_h ep = request->ep;

  // TODO sendrecv to avoid deadlock
  MPI_Send(&flag_ptr, sizeof(uint64_t), MPI_BYTE, request->dest,
           COMM_BEGIN_TAG + request->tag, MPI_COMM_WORLD);
  MPI_Recv(&request->remote_flag_addr, sizeof(uint64_t), MPI_BYTE,
           request->dest, COMM_BEGIN_TAG + request->tag, MPI_COMM_WORLD,
           MPI_STATUS_IGNORE);

  int tag_rkey_data = 32767 + 32767 + request->tag;
  int tag_rkey_flag = tag_rkey_data;

  ucp_context_h context = mca_osc_ucx_component.ucp_context;
  // prepare buffer for RDMA access:
  ucp_mem_map_params_t mem_params;
  // ucp_mem_attr_t mem_attrs;
  ucs_status_t ucp_status;
  // init mem params
  memset(&mem_params, 0, sizeof(ucp_mem_map_params_t));

  mem_params.address = request->buf;
  mem_params.length = request->size;
  // we need to tell ucx what fields are valid
  mem_params.field_mask =
      UCP_MEM_MAP_PARAM_FIELD_ADDRESS | UCP_MEM_MAP_PARAM_FIELD_LENGTH;

  ucp_status = ucp_mem_map(context, &mem_params, &request->mem_handle_data);
  assert(ucp_status == UCS_OK && "Error in register mem for RDMA operation");

  void *rkey_buffer;
  size_t rkey_size;

  // pack a remote memory key
  ucp_status = ucp_rkey_pack(context, request->mem_handle_data, &rkey_buffer,
                             &rkey_size);
  assert(ucp_status == UCS_OK && "Error in register mem for RDMA operation");

  MPI_Send(rkey_buffer, rkey_size, MPI_BYTE, request->dest, tag_rkey_data,
           MPI_COMM_WORLD);

  // free temp buf
  ucp_rkey_buffer_release(rkey_buffer);

  memset(&mem_params, 0, sizeof(ucp_mem_map_params_t));

  mem_params.address = flag_ptr;
  mem_params.length = sizeof(int);
  // we need to tell ucx what fields are valid
  mem_params.field_mask =
      UCP_MEM_MAP_PARAM_FIELD_ADDRESS | UCP_MEM_MAP_PARAM_FIELD_LENGTH;

  ucp_status = ucp_mem_map(context, &mem_params, &request->mem_handle_flag);
  assert(ucp_status == UCS_OK && "Error in register mem for RDMA operation");

  // pack a remote memory key
  ucp_status = ucp_rkey_pack(context, request->mem_handle_flag, &rkey_buffer,
                             &rkey_size);
  assert(ucp_status == UCS_OK && "Error in register mem for RDMA operation");

  MPI_Send(rkey_buffer, rkey_size, MPI_BYTE, request->dest, tag_rkey_flag,
           MPI_COMM_WORLD);

  // free temp buf
  ucp_rkey_buffer_release(rkey_buffer);

  void *temp_buf;
  MPI_Status status;
  int temp_buf_count;
  MPI_Probe(request->dest, tag_rkey_data, MPI_COMM_WORLD, &status);
  MPI_Get_count(&status, MPI_BYTE, &temp_buf_count);
  temp_buf = calloc(temp_buf_count, 1);
  MPI_Recv(temp_buf, temp_buf_count, MPI_BYTE, request->dest, tag_rkey_data,
           MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  ucp_ep_rkey_unpack(ep, temp_buf, &request->remote_data_rkey);
  free(temp_buf);

  // TODO use MPROBE for multi threading
  MPI_Probe(request->dest, tag_rkey_flag, MPI_COMM_WORLD, &status);
  MPI_Get_count(&status, MPI_BYTE, &temp_buf_count);
  temp_buf = calloc(temp_buf_count, 1);
  MPI_Recv(temp_buf, temp_buf_count, MPI_BYTE, request->dest, tag_rkey_flag,
           MPI_COMM_WORLD, MPI_STATUS_IGNORE);
  ucp_ep_rkey_unpack(ep, temp_buf, &request->remote_flag_rkey);
  free(temp_buf);

  // printf("Rank %d: buffer: %p
  // remote:%p\n",rank,buffer,info.remote_data_addr); printf("Rank %d:
  // flagbuffer: %p flagremote:%p\n",rank,&info,info.remote_flag_addr);

#ifndef DNDEBUG
  // probe for the Msg that the communication will end
  int flag;
  MPI_Iprobe(request->dest, COMM_BEGIN_TAG + request->tag, request->comm, &flag,
             MPI_STATUS_IGNORE);
  assert(flag == 0);
  // nice for attaching the debugger:
  MPI_Barrier(MPI_COMM_WORLD);
#endif
}

static void start_send_when_searching_for_connection(MPIOPT_Request *request) {
  int flag;
  MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
  if (flag) {
    // found matching counterpart
    exchange_rdma_info(request);
    request->type = SEND_REQUEST_TYPE;
    b_send(request); // start RDMA
  } else {
    // use Fallback: post Isend
    MPI_Isend(request->buf, request->size, MPI_BYTE, request->dest,
              request->tag, request->comm, &request->backup_request);
  }
}

static void start_recv_when_searching_for_connection(MPIOPT_Request *request) {
  int flag;
  MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
  if (flag) {
    // found matching counterpart
    exchange_rdma_info(request);
    request->type = RECV_REQUEST_TYPE;
    b_recv(request); // start RDMA
  } else {
    // use Fallback: post Irecv
    MPI_Irecv(request->buf, request->size, MPI_BYTE, request->dest,
              request->tag, request->comm, &request->backup_request);
  }
}

// TODO return proper error codes
static int MPIOPT_Start_internal(MPIOPT_Request *request) {

  // TODO atomic increment for multi thhreading
  request->operation_number++;

  // TODO switch ?

  if (request->type == SEND_REQUEST_TYPE) {
    b_send(request);
  } else if (request->type == RECV_REQUEST_TYPE) {
    b_recv(request);
  } else if (request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
    start_send_when_searching_for_connection(request);
  } else if (request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
    start_recv_when_searching_for_connection(request);
  } else if (request->type == SEND_REQUEST_TYPE_USE_FALLBACK) {
    MPI_Isend(request->buf, request->size, MPI_BYTE, request->dest,
              request->tag, request->comm, &request->backup_request);
  } else if (request->type == Recv_REQUEST_TYPE_USE_FALLBACK) {
    MPI_Irecv(request->buf, request->size, MPI_BYTE, request->dest,
              request->tag, request->comm, &request->backup_request);
  } else {
    assert(false && "Error: uninitialized Request");
  }
}

static void wait_send_when_searching_for_connection(MPIOPT_Request *request) {
  int flag;
  MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
  while (!flag) {
    MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
    if (flag) {
      // found matching counterpart
      exchange_rdma_info(request);
      request->type = SEND_REQUEST_TYPE;
      MPI_Cancel(
          &request->backup_request); // we have issued this operation before
      MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
      b_send(request);
      e_send(request);
    } else {
      MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
      // TODO sleep? and let MPI progress
    }
  }
}
// end while, either backup comm finished, or RDMA connection was}
void wait_recv_when_searching_for_connection(MPIOPT_Request *request) {
  int flag;
  MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
  while (!flag) {
    MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
    if (flag) {
      // found matching counterpart
      exchange_rdma_info(request);
      request->type = RECV_REQUEST_TYPE;
      MPI_Cancel(
          &request->backup_request); // we have issued this operation before
      MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
      b_recv(request);
      e_recv(request);
    } else {
      MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
      // TODO sleep? and let MPI progress
    }
  }
  // end while, either backup comm finished, or RDMA connection was}
}

static int MPIOPT_Wait_internal(MPIOPT_Request *request, MPI_Status *status) {

  // TODO implement MPI status?
  assert(status == MPI_STATUS_IGNORE);

  // TODO switch ?

  if (request->type == SEND_REQUEST_TYPE) {
    e_send(request);
  } else if (request->type == RECV_REQUEST_TYPE) {
    e_recv(request);
  } else if (request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
    wait_send_when_searching_for_connection(request);
  } else if (request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
    wait_recv_when_searching_for_connection(request);
  } else if (request->type == SEND_REQUEST_TYPE_USE_FALLBACK ||
             request->type == Recv_REQUEST_TYPE_USE_FALLBACK) {
    MPI_Wait(&request->backup_request, status);
  } else {
    assert(false && "Error: uninitialized Request");
  }
}

static int MPIOPT_Test_internal(MPIOPT_Request *request, int *flag,
                                MPI_Status *status) {
  assert(false);
  // TODO implement
}

static int MPIOPT_Send_init_internal(const void *buf, int count,
                                     MPI_Datatype datatype, int dest, int tag,
                                     MPI_Comm comm, MPIOPT_Request *request) {

  // TODO support other dtypes as MPI_BYTE
  assert(datatype == MPI_BYTE);
  assert(tag < 32767); // 32767 is the minimum value of MPI_TAG_UB required by
                       // mpi standard

  memset(request, 0, sizeof(MPIOPT_Request));
  int rank, numtasks;
  // Welchen rang habe ich?
  MPI_Comm_rank(comm, &rank);
  // wie viele Tasks gibt es?
  MPI_Comm_size(comm, &numtasks);

  int max_tag_allowed, bool_flag;

  MPI_Attr_get(comm, MPI_TAG_UB, &max_tag_allowed, &bool_flag);
  assert(bool_flag);
  // maximum tag used for communication when we exchange the rdma data
  assert(32767 + 32767 + tag < max_tag_allowed);

  uint64_t buffer_ptr = buf;

  ompi_osc_ucx_module_t *module =
      (ompi_osc_ucx_module_t *)global_comm_win->w_osc_module;
  ucp_ep_h ep = OSC_UCX_GET_EP(module->comm, dest);

  // TODO IBSEND == to cancel it
  MPI_Send(&buffer_ptr, sizeof(uint64_t), MPI_BYTE, dest, COMM_BEGIN_TAG + tag,
           MPI_COMM_WORLD);
  // TODO USE request->rdma_exchange_request_send
  MPI_Irecv(&request->remote_data_addr, sizeof(uint64_t), MPI_BYTE, dest,
            COMM_BEGIN_TAG + tag, MPI_COMM_WORLD,
            &request->rdma_exchange_request);

  request->ep = ep;
  request->buf = buf;
  request->dest = dest;
  request->size = count;
  request->tag = tag;
  request->comm = comm;
  request->backup_request = MPI_REQUEST_NULL;

  request->type = SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION;

  int flag;
  // MPI_Status stat;
  MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
  if (flag) {
    // found matching counterpart
    exchange_rdma_info(request);
    request->type = SEND_REQUEST_TYPE;
  }
}

static int MPIOPT_Recv_init_internal(void *buf, int count,
                                     MPI_Datatype datatype, int source, int tag,
                                     MPI_Comm comm, MPIOPT_Request *request) {
  // it does the same as send_init (exchange RDMA parameters to setup comm)

  MPIOPT_Send_init_internal(buf, count, datatype, source, tag, comm, request);

  if (request->type == SEND_REQUEST_TYPE) {
    request->type = RECV_REQUEST_TYPE;
  } else if (request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
    request->type = RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION;
  } else {
    assert(false && "ERROR");
  }
}

// TODO this is blocking: we may want to do it non-blocking and free all
// leftover ressources at the end?
static int MPIOPT_Request_free_internal(MPIOPT_Request *request) {

  if (request->type == RECV_REQUEST_TYPE ||
      request->type == SEND_REQUEST_TYPE) {
    // otherwise all these ressources where never aquired

    acknowlege_Request_free(request);
  }

  // cancel any search for RDMA connection, if necessary
  int flag;
  MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
  if (!flag) {

    MPI_Cancel(&request->rdma_exchange_request);
    MPI_Wait(&request->rdma_exchange_request, MPI_STATUS_IGNORE);
  }

  // cancel backup comm, if it was initialized when waiting for RDMa connection
  // it is safe to cancel it, as the request must be inactive by now
  MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
  if (!flag) {

    MPI_Cancel(&request->backup_request);
    MPI_Wait(&request->backup_request, MPI_STATUS_IGNORE);
  }

  request->type = 0; // uninitialized
}

void MPIOPT_INIT() {
  // create the global win used for rdma transfers
  // TODO maybe we need less initializatzion to initioaize the RDMA component?
  MPI_Win_create(&dummy_int, sizeof(int), 1, MPI_INFO_NULL, MPI_COMM_WORLD,
                 &global_comm_win);
}
void MPIOPT_FINALIZE() { MPI_Win_free(&global_comm_win); }

int MPIOPT_Start(MPI_Request *request) {
  return MPIOPT_Start_internal((MPIOPT_Request *)*request);
}
int MPIOPT_Wait(MPI_Request *request, MPI_Status *status) {
  return MPIOPT_Wait_internal((MPIOPT_Request *)*request, status);
}
int MPIOPT_Test(MPI_Request *request, int *flag, MPI_Status *status) {
  return MPIOPT_Test_internal((MPIOPT_Request *)*request, flag, status);
}
int MPIOPT_Send_init(const void *buf, int count, MPI_Datatype datatype,
                     int dest, int tag, MPI_Comm comm, MPI_Request *request) {

  *request = malloc(sizeof(MPIOPT_Request));

  return MPIOPT_Send_init_internal(buf, count, datatype, dest, tag, comm,
                                   (MPIOPT_Request *)*request);
}

int MPIOPT_Recv_init(void *buf, int count, MPI_Datatype datatype, int source,
                     int tag, MPI_Comm comm, MPI_Request *request) {
  *request = malloc(sizeof(MPIOPT_Request));
  return MPIOPT_Recv_init_internal(buf, count, datatype, source, tag, comm,
                                   (MPIOPT_Request *)*request);
}

int MPIOPT_Request_free(MPI_Request *request) {
  int retval = MPIOPT_Request_free_internal((MPIOPT_Request *)*request);
  free(*request);
  return retval;
}
