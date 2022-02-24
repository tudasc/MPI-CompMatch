// TOOD Rename File with more appropriate name
#ifndef LOW_LEVEL_H_
#define LOW_LEVEL_H_

#include <mpi.h>

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

#endif /* LOW_LEVEL_H_ */
