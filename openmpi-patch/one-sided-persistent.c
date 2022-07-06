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
#define RDMA_SPIN_WAIT_THRESHOLD 32

#define STATISTIC_PRINTING
#define BUFFER_CONTENT_CHECKING

// end config

#define RECV_REQUEST_TYPE 1
#define SEND_REQUEST_TYPE 2
#define SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION 3
#define RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION 4
#define SEND_REQUEST_TYPE_USE_FALLBACK 5
#define Recv_REQUEST_TYPE_USE_FALLBACK 6

struct list_elem_ptr {
	void *value;
	struct list_elem_int *next;
};

struct mpiopt_request {
	// this way it it can be used as a normal request ptr as well
	struct ompi_request_t original_request;
	int flag;
	int flag_buffer;
	long AM_ID;
	void *buf;
	size_t size;
	// initialized locally
	void *ucx_request_data_transfer;
	void *ucx_request_flag_transfer;
	int operation_number;
	int type;
	ucp_mem_h mem_handle_data;
	ucp_mem_h mem_handle_flag;
	ucp_am_handler_param_t am_handler;
	ucp_request_param_t ucp_request_param;
	void *list_of_pending_msgs;
	ucp_ep_h ep; // save used endpoint, so we dont have to look it up over and over
	// necessary for backup in case no other persistent op matches
	MPI_Request backup_request;
	int tag;
	int dest;
	MPI_Comm comm;
	// MPI_Request rdma_exchange_request;
	MPI_Request rdma_exchange_request;
	void *rdma_info_buf;
	// struct mpiopt_request* rdma_exchange_buffer;
#ifdef BUFFER_CONTENT_CHECKING
	void *checking_buf;
	MPI_Request chekcking_request;
#endif
};
typedef struct mpiopt_request MPIOPT_Request;

struct list_elem_int {
	int value;
	struct list_elem_int *next;
};

// globals
// TODO refactor and have one struct for globals?
MPI_Win global_comm_win;
MPI_Comm handshake_communicator;
MPI_Comm handshake_response_communicator;
// we need a different comm here, so that send a handshake response (recv
// handshake) cannot be mistaken for another handshake-request from a send with
// the same tag
#ifdef BUFFER_CONTENT_CHECKING
MPI_Comm checking_communicator;
#endif

int dummy_int = 0;

void empty_function(void *request, ucs_status_t status) {
	// callback if flush is completed
}

// linked list of all requests that we have, so that we can progress them in
// case we get stuck
struct list_elem {
	MPIOPT_Request *elem;
	struct list_elem *next;
};
struct list_elem *request_list_head;

// requests to free (defer free of some ressources until finalize, so that
// request free is local operation)
struct list_elem *to_free_list_head;
// tell other rank, that it should post a matching receive for all unsuccessful
// handshakes
struct list_elem_int *msg_send;
unsigned long AM_tag;

static void b_send(MPIOPT_Request *request);
static void b_recv(MPIOPT_Request *request);
static void receive_rdma_info(MPIOPT_Request *request);
static int MPIOPT_Start_internal(MPIOPT_Request *request);
static int MPIOPT_Wait_internal(MPIOPT_Request *request, MPI_Status *status);
static int MPIOPT_Test_internal(MPIOPT_Request *request, int *flag,
		MPI_Status *status);
static int init_request(const void *buf, int count, MPI_Datatype datatype,
		int dest, int tag, MPI_Comm comm, MPIOPT_Request *request);
static int MPIOPT_Recv_init_internal(void *buf, int count,
		MPI_Datatype datatype, int source, int tag, MPI_Comm comm,
		MPIOPT_Request *request);
static int MPIOPT_Request_free_internal(MPIOPT_Request *request);

static void add_to_list_of_incoming_msg(MPIOPT_Request *request, void *data) {
	void *new_elem = malloc(request->size + sizeof(void*));
	*(void**) new_elem = NULL;
	memcpy(new_elem+sizeof(void*), data, request->size);

	// enque in list

	if (request->list_of_pending_msgs == NULL) {
#ifdef STATISTIC_PRINTING
		printf("Recv msg, bufferd it\n");
#endif
		request->list_of_pending_msgs = new_elem;
	} else {
		// traverse the list, we need to enqueue at the end to keep order
#ifdef STATISTIC_PRINTING
		printf(
				"Need to enqueue into list of pending Msg, this may degrade performance\n");
#endif

		void *cur_elem = request->list_of_pending_msgs;
		void *nxt_elem = *(void**) cur_elem;
		while (nxt_elem != NULL) {
			cur_elem = nxt_elem;
			nxt_elem = *(void**) cur_elem;
		}
		*(void**) cur_elem = new_elem;
	}
}

static void dequeue_from_list_of_incoming_msg(MPIOPT_Request *request) {
	assert(request->list_of_pending_msgs!=NULL);
	void *cur_elem = request->list_of_pending_msgs;
	void *nxt_elem = *(void**) cur_elem;
	request->list_of_pending_msgs = nxt_elem;

	memcpy(request->buf, cur_elem+sizeof(void*), request->size);
}

ucs_status_t incoming_am_msg_handler(void *arg, const void *header,
		size_t header_length, void *data, size_t length,
		const ucp_am_recv_param_t *param) {

	MPIOPT_Request *request = (MPIOPT_Request*) arg;

	assert(request->size == length && "Wrong message size");

	if (param->recv_attr & UCP_AM_RECV_ATTR_FLAG_RNDV) {
		/* Rendezvous request arrived, data contains an internal UCX descriptor,
		 * which has to be passed to ucp_am_recv_data_nbx function to confirm
		 * data transfer.
		 */
		//TODO implement
#ifdef STATISTIC_PRINTING
		printf("Recv Rendezvous msg\n");
#endif
		assert(false && "Currently not implemented");
		return UCS_INPROGRESS;
	}
	// else
	/* Message delivered with eager protocol, data should be available
	 * immediately
	 */

	if (param->recv_attr & UCP_AM_RECV_ATTR_FIELD_REPLY_EP) {

		assert(
				false && "What is this field about? we dont want to reply to a msg");
	}

	/*	if(param->recv_attr & UCP_AM_RECV_ATTR_FLAG_DATA  )
	 {

	 In this Case we can keep UCS's memory around and call ucp_am_data_release when the data was copied into the correct buffer

	 return UCS_INPROGRESS;
	 }
	 else{*/
// WHY is this necessary and we cannot keep UCS's memory around?
	if (request->operation_number % 2 == 0) {
		//request is not active
		add_to_list_of_incoming_msg(request, data);

	} else {
#ifdef STATISTIC_PRINTING
		printf("Recv msg without the need of buffering it\n");
#endif
		// recv is active, we can override the data buffer
		//TODO atomic inrement for multi threading
		request->operation_number++;
		memcpy(request->buf, data, request->size);
	}

	return UCS_OK;

	//} // end if(param->recv_attr & UCP_AM_RECV_ATTR_FLAG_DATA  )
}

// add it at beginning of list
static void add_request_to_list(MPIOPT_Request *request) {
	struct list_elem *new_elem = malloc(sizeof(struct list_elem));
	new_elem->elem = request;
	new_elem->next = request_list_head->next;
	request_list_head->next = new_elem;
}

static void remove_request_from_list(MPIOPT_Request *request) {
	struct list_elem *previous_elem = request_list_head;
	struct list_elem *current_elem = request_list_head->next;
	assert(current_elem != NULL);
	while (current_elem->elem != request) {
		previous_elem = current_elem;
		current_elem = previous_elem->next;
		assert(current_elem != NULL);
	}
	// remove elem from list
	previous_elem->next = current_elem->next;
	free(current_elem);
}

static void wait_for_completion_blocking(void *request) {
	assert(request != NULL);
	ucs_status_t status;
	do {
		ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
		status = ucp_request_check_status(request);
	} while (status == UCS_INPROGRESS);
	ucp_request_free(request);
}
// operation_number*2= op has not started on remote
// operation_number*2 +1= op has started on remote, we should initiate
// data-transfer operation_number*2 + 2= op has finished on remote

static void b_send(MPIOPT_Request *request) {

	request->ucx_request_data_transfer = ucp_am_send_nbx(request->ep,
			request->AM_ID, NULL, 0, request->buf, request->size,
			&request->ucp_request_param);
	ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

}

static void e_send(MPIOPT_Request *request) {

	// ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
	// will call progres only if this is necessary

	// we need to wait until the op has finished on the remote before re-using the
	// data buffer

	// busy wait
	if (__builtin_expect(request->ucx_request_data_transfer != NULL, 0)) {
		wait_for_completion_blocking(request->ucx_request_data_transfer);
		request->ucx_request_data_transfer = NULL;
	}
}

static void b_recv(MPIOPT_Request *request) {

	ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
	// nothing to do
}

static void e_recv(MPIOPT_Request *request) {
	if (__builtin_expect(request->operation_number % 2 != 0, 0)) {

		// wait for msg to arrive if it hasnt already
		while (request->operation_number % 2 != 0
				&& request->list_of_pending_msgs == NULL) {
			ucp_worker_progress(mca_osc_ucx_component.ucp_worker);
		}

		// receive msg from queue
		if (__builtin_expect(request->operation_number % 2 != 0, 1)) {
			request->operation_number++;
			dequeue_from_list_of_incoming_msg(request);
		}

	}	// else msg has arrived, nothing to do
}

// exchanges the RDMA info and maps all mem for RDMA op
static void send_rdma_info(MPIOPT_Request *request) {

	assert(
			request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION || request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION);

	if (request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {

		//ATOMIC in case of Multi threading
		request->AM_ID = AM_tag;
		AM_tag++;

		ucp_am_handler_param_t param;
		request->am_handler.id = request->AM_ID;
		request->am_handler.cb = incoming_am_msg_handler;
		request->am_handler.arg = (void*) request;
		request->am_handler.field_mask = UCP_AM_HANDLER_PARAM_FIELD_ID
				| UCP_AM_HANDLER_PARAM_FIELD_CB
				| UCP_AM_HANDLER_PARAM_FIELD_ARG;
		// register the handler
		ucp_worker_set_am_recv_handler(mca_osc_ucx_component.ucp_worker,
				&request->am_handler);
		ucp_worker_progress(mca_osc_ucx_component.ucp_worker);

		MPI_Issend(&request->AM_ID, 1, MPI_LONG, request->dest, request->tag,
				handshake_communicator, &request->rdma_exchange_request);
	} else {
		MPI_Irecv(&request->AM_ID, 1, MPI_LONG, request->dest, request->tag,
				handshake_communicator, &request->rdma_exchange_request);
	}

}

static void receive_rdma_info(MPIOPT_Request *request) {

	assert(
			request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION || request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION);

	// nothing to do

}

static void start_send_when_searching_for_connection(MPIOPT_Request *request) {

//assert(request->operation_number == 1);
	int flag;
	MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
	if (flag) {
		assert(request->rdma_exchange_request==MPI_REQUEST_NULL);
		request->type = SEND_REQUEST_TYPE;
		b_send(request);
	} else {
		// always post a normal msg, in case of fallback to normal comm is needed
		// for the first time, the receiver will post a matching recv
		assert(request->backup_request == MPI_REQUEST_NULL);
		MPI_Isend(request->buf, request->size, MPI_BYTE, request->dest,
				request->tag, request->comm, &request->backup_request);
	}
}

static void start_recv_when_searching_for_connection(MPIOPT_Request *request) {
	//assert(request->operation_number == 1);

	int flag;
	MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
	if (flag) {
		assert(request->rdma_exchange_request==MPI_REQUEST_NULL);
		request->type = RECV_REQUEST_TYPE;
		b_recv(request);
	} else {
		MPI_Irecv(request->buf, request->size, MPI_BYTE, request->dest,
				request->tag, request->comm, &request->backup_request);
	}

}

// TODO return proper error codes

static int MPIOPT_Start_send_internal(MPIOPT_Request *request) {

	// TODO atomic increment for multi threading
	request->operation_number++;

	if (__builtin_expect(request->type == SEND_REQUEST_TYPE, 1)) {
		b_send(request);

	} else if (request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
		start_send_when_searching_for_connection(request);
	} else if (request->type == SEND_REQUEST_TYPE_USE_FALLBACK) {
		assert(request->backup_request == MPI_REQUEST_NULL);
		MPI_Isend(request->buf, request->size, MPI_BYTE, request->dest,
				request->tag, request->comm, &request->backup_request);

	} else {
		assert(false && "Error: uninitialized Request");
	}
#ifdef BUFFER_CONTENT_CHECKING
	MPI_Isend(request->buf, request->size, MPI_BYTE, request->dest,
			request->tag, checking_communicator, &request->chekcking_request);

#endif
}

static int MPIOPT_Start_recv_internal(MPIOPT_Request *request) {

	// TODO atomic increment for multi threading
	request->operation_number++;

	if (__builtin_expect(request->type == RECV_REQUEST_TYPE, 1)) {
		b_recv(request);

	} else if (request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
		start_recv_when_searching_for_connection(request);

	} else if (request->type == Recv_REQUEST_TYPE_USE_FALLBACK) {
		assert(request->backup_request == MPI_REQUEST_NULL);
		MPI_Irecv(request->buf, request->size, MPI_BYTE, request->dest,
				request->tag, request->comm, &request->backup_request);
	} else {
		assert(false && "Error: uninitialized Request");
	}

#ifdef BUFFER_CONTENT_CHECKING
	MPI_Irecv(request->checking_buf, request->size, MPI_BYTE, request->dest,
			request->tag, checking_communicator, &request->chekcking_request);

#endif
}

static int MPIOPT_Start_internal(MPIOPT_Request *request) {

	if (request->type == SEND_REQUEST_TYPE
			|| request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION
			|| request->type == SEND_REQUEST_TYPE_USE_FALLBACK) {
		return MPIOPT_Start_send_internal(request);
	} else {
		return MPIOPT_Start_recv_internal(request);
	}
}

static void wait_send_when_searching_for_connection(MPIOPT_Request *request) {

	MPI_Wait(&request->backup_request, MPI_STATUS_IGNORE);
}

static void wait_recv_when_searching_for_connection(MPIOPT_Request *request) {

	// wait for either the handshake or the payload data to arrive
	int flag = 0;
	while (!flag) {

		MPI_Test(&request->rdma_exchange_request, &flag, MPI_STATUS_IGNORE);
		if (flag) {
			// handshake successful
			assert(request->rdma_exchange_request==MPI_REQUEST_NULL);
			request->type = RECV_REQUEST_TYPE;
			b_recv(request);
			e_recv(request);

		} else {
			MPI_Test(&request->backup_request, &flag, MPI_STATUS_IGNORE);
		}

	}
}

static int MPIOPT_Wait_send_internal(MPIOPT_Request *request,
		MPI_Status *status) {

	// TODO implement MPI status?
	assert(status == MPI_STATUS_IGNORE);

	if (__builtin_expect(request->type == SEND_REQUEST_TYPE, 1)) {
		e_send(request);
	} else if (request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
		wait_send_when_searching_for_connection(request);
	} else if (request->type == SEND_REQUEST_TYPE_USE_FALLBACK) {
		MPI_Wait(&request->backup_request, status);
	} else {
		assert(false && "Error: uninitialized Request");
	}
}

static int MPIOPT_Wait_recv_internal(MPIOPT_Request *request,
		MPI_Status *status) {

	// TODO implement MPI status?
	assert(status == MPI_STATUS_IGNORE);

	if (__builtin_expect(request->type == RECV_REQUEST_TYPE, 1)) {
		e_recv(request);
	} else if (request->type == RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION) {
		wait_recv_when_searching_for_connection(request);
	} else if (request->type == Recv_REQUEST_TYPE_USE_FALLBACK) {

		MPI_Wait(&request->backup_request, status);
	} else {
		assert(false && "Error: uninitialized Request");
	}

#ifdef BUFFER_CONTENT_CHECKING
	MPI_Wait(&request->chekcking_request, MPI_STATUS_IGNORE);

	// compare buffer
	int buffer_has_unexpected_content = memcmp(request->checking_buf,
			request->buf, request->size);

	if (buffer_has_unexpected_content) {
		printf("Content:\n");
		char *b = (char*) request->buf;
		for (unsigned long i = 0; i < request->size; ++i) {
			printf("\\%02hhx", (unsigned char) b[i]);
		}
		printf("\n");
		b = (char*) request->checking_buf;
		for (unsigned long i = 0; i < request->size; ++i) {
			printf("\\%02hhx", (unsigned char) b[i]);
		}
		printf("\nBut above was expected\n");
	}

	assert(
			buffer_has_unexpected_content == 0
					&& "Error, The buffer has not the content of the message");

#endif
}

static int MPIOPT_Wait_internal(MPIOPT_Request *request, MPI_Status *status) {

	// TODO implement MPI status?
	assert(status == MPI_STATUS_IGNORE);

	if (request->type == SEND_REQUEST_TYPE
			|| request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION
			|| request->type == SEND_REQUEST_TYPE_USE_FALLBACK) {
		return MPIOPT_Wait_send_internal(request, status);
	} else {
		return MPIOPT_Wait_recv_internal(request, status);
	}
}

static int MPIOPT_Test_internal(MPIOPT_Request *request, int *flag,
		MPI_Status *status) {
	assert(false);
	// TODO implement
}

static int init_request(const void *buf, int count, MPI_Datatype datatype,
		int dest, int tag, MPI_Comm comm, MPIOPT_Request *request) {

	// TODO support other dtypes as MPI_BYTE
	assert(datatype == MPI_BYTE);
	assert(comm == MPI_COMM_WORLD); // currently only works for comm_wolrd
	int rank, numtasks;
	// Welchen rang habe ich?
	MPI_Comm_rank(comm, &rank);
	// wie viele Tasks gibt es?
	MPI_Comm_size(comm, &numtasks);

	uint64_t buffer_ptr = buf;

	ompi_osc_ucx_module_t *module =
			(ompi_osc_ucx_module_t*) global_comm_win->w_osc_module;
	ucp_ep_h ep = OSC_UCX_GET_EP(module->comm, dest);

	request->ep = ep;
	request->buf = buf;
	request->dest = dest;
	request->size = count;
	request->tag = tag;
	request->comm = comm;
	request->backup_request = MPI_REQUEST_NULL;
	request->AM_ID = 0;
	request->list_of_pending_msgs = NULL;

	memset(&request->ucp_request_param, 0, sizeof(ucp_request_param_t));
	request->ucp_request_param.flags = UCP_AM_SEND_FLAG_EAGER;
	request->ucp_request_param.op_attr_mask = UCP_OP_ATTR_FIELD_FLAGS;

#ifdef BUFFER_CONTENT_CHECKING
	request->checking_buf = malloc(count);
#endif

	send_rdma_info(request);

	// add request to list, so that it is progressed, if other requests have to
	// wait
	add_request_to_list(request);

	return MPI_SUCCESS;
}

static int MPIOPT_Recv_init_internal(void *buf, int count,
		MPI_Datatype datatype, int source, int tag, MPI_Comm comm,
		MPIOPT_Request *request) {
#ifdef STATISTIC_PRINTING
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	printf("Rank %d: Init RECV from %d\n", rank, source);
#endif

	memset(request, 0, sizeof(MPIOPT_Request));
	request->type = RECV_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION;
	return init_request(buf, count, datatype, source, tag, comm, request);
}

static int MPIOPT_Send_init_internal(void *buf, int count,
		MPI_Datatype datatype, int source, int tag, MPI_Comm comm,
		MPIOPT_Request *request) {
#ifdef STATISTIC_PRINTING
	int rank;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	printf("Rank %d: Init SEND to %d with msg size %d\n", rank, source, count);
#endif
	memset(request, 0, sizeof(MPIOPT_Request));
	request->type = SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION;
	return init_request(buf, count, datatype, source, tag, comm, request);
}

static int MPIOPT_Request_free_internal(MPIOPT_Request *request) {

#ifdef STATISTIC_PRINTING
	int rank = 0;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	if (request->type == SEND_REQUEST_TYPE_SEARCH_FOR_RDMA_CONNECTION
			|| request->type == SEND_REQUEST_TYPE
			|| request->type == SEND_REQUEST_TYPE_USE_FALLBACK) {
		printf("Rank %d: SENDING: Request Free\n", rank);
	} else {
		printf("Rank %d: RECV: Request Free \n", rank);
	}
#endif
	remove_request_from_list(request);

	if (request->type == RECV_REQUEST_TYPE) {
		// De-Register message handler callback
		request->am_handler.cb = NULL;
		//request->am_handler.field_mask is still setup correctly
		ucp_worker_set_am_recv_handler(mca_osc_ucx_component.ucp_worker,
				&request->am_handler);
	}

#ifdef BUFFER_CONTENT_CHECKING
	free(request->checking_buf);
#endif

	return MPI_SUCCESS;
}

void MPIOPT_INIT() {
	// create the global win used for rdma transfers
	// TODO maybe we need less initialization to initialize the RDMA component?
	MPI_Win_create(&dummy_int, sizeof(int), 1, MPI_INFO_NULL, MPI_COMM_WORLD,
			&global_comm_win);
	request_list_head = malloc(sizeof(struct list_elem));
	request_list_head->elem = NULL;
	request_list_head->next = NULL;
	to_free_list_head = malloc(sizeof(struct list_elem));
	to_free_list_head->elem = NULL;
	to_free_list_head->next = NULL;

	AM_tag = 1;

	MPI_Comm_dup(MPI_COMM_WORLD, &handshake_communicator);
	MPI_Comm_dup(MPI_COMM_WORLD, &handshake_response_communicator);
#ifdef BUFFER_CONTENT_CHECKING
	MPI_Comm_dup(MPI_COMM_WORLD, &checking_communicator);
#endif
}
void MPIOPT_FINALIZE() {
	MPI_Win_free(&global_comm_win);
	assert(request_list_head->next == NULL); // list should be empty
	free(request_list_head);

#ifdef STATISTIC_PRINTING
	int rank = 0;
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
	printf("Rank %d: Finalize\n", rank);
#endif

	ucp_context_h context = mca_osc_ucx_component.ucp_context;

	/*
	 struct list_elem *elem = to_free_list_head;
	 while (elem != NULL) {
	 MPIOPT_Request *req = elem->elem;

	 if (req != NULL) {
	 free(req->rdma_info_buf);
	 // release all RDMA ressources

	 if (req->type == RECV_REQUEST_TYPE || req->type == SEND_REQUEST_TYPE) {
	 // otherwise all these resources where never acquired
	 ucp_mem_unmap(context, req->mem_handle_flag);
	 ucp_rkey_destroy(req->remote_flag_rkey);

	 // ucp_mem_unmap(context, req->mem_handle_data); // was freed before
	 }
	 free(req);
	 }
	 struct list_elem *nxt_elem = elem->next;
	 free(elem);
	 elem = nxt_elem;
	 }
	 */

	// TODO receive all pending messages from unsuccessful handshakes
	MPI_Comm_free(&handshake_communicator);
	MPI_Comm_free(&handshake_response_communicator);
#ifdef BUFFER_CONTENT_CHECKING
	MPI_Comm_free(&checking_communicator);
#endif
}

int MPIOPT_Start(MPI_Request *request) {
	return MPIOPT_Start_internal((MPIOPT_Request*) *request);
}

int MPIOPT_Start_send(MPI_Request *request) {
	return MPIOPT_Start_send_internal((MPIOPT_Request*) *request);
}

int MPIOPT_Start_recv(MPI_Request *request) {
	return MPIOPT_Start_recv_internal((MPIOPT_Request*) *request);
}
int MPIOPT_Wait_send(MPI_Request *request, MPI_Status *status) {
	return MPIOPT_Wait_send_internal((MPIOPT_Request*) *request, status);
}
int MPIOPT_Wait_recv(MPI_Request *request, MPI_Status *status) {
	return MPIOPT_Wait_recv_internal((MPIOPT_Request*) *request, status);
}

int MPIOPT_Wait(MPI_Request *request, MPI_Status *status) {
	return MPIOPT_Wait_internal((MPIOPT_Request*) *request, status);
}
int MPIOPT_Test(MPI_Request *request, int *flag, MPI_Status *status) {
	return MPIOPT_Test_internal((MPIOPT_Request*) *request, flag, status);
}
int MPIOPT_Send_init(const void *buf, int count, MPI_Datatype datatype,
		int dest, int tag, MPI_Comm comm, MPI_Request *request) {

	*request = malloc(sizeof(MPIOPT_Request));

	return MPIOPT_Send_init_internal(buf, count, datatype, dest, tag, comm,
			(MPIOPT_Request*) *request);
}

int MPIOPT_Recv_init(void *buf, int count, MPI_Datatype datatype, int source,
		int tag, MPI_Comm comm, MPI_Request *request) {
	*request = malloc(sizeof(MPIOPT_Request));
	return MPIOPT_Recv_init_internal(buf, count, datatype, source, tag, comm,
			(MPIOPT_Request*) *request);
}

int MPIOPT_Request_free(MPI_Request *request) {
	int retval = MPIOPT_Request_free_internal((MPIOPT_Request*) *request);
	*request = NULL;
	// free(*request);
	return retval;
}
