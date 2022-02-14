# Common definitions
#CC = clang
#CC = gcc
#MPICC = mpicc
#MPICC = scorep mpicc

# module load openmpi/test hwloc/2.5.0

# Compiler flags, paths and libraries
# Include npath showing to openmpoi internals
INCLUDE=-I/home/tj75qeje/openmpi-4.1.1/ -I/home/tj75qeje/openmpi-4.1.1/opal/include -I/home/tj75qeje/openmpi-4.1.1/ompi/include/ -I/home/tj75qeje/openmpi-4.1.1/orte/include

CFLAGS = -std=c11 -Og -g $(INCLUDE) -Wall -Wextra -Wno-unused-parameter
#CFLAGS = -std=c11 -O2 $(INCLUDE)
LFLAGS = $(CFLAGS)
LIBS   = -lopen-pal -lucp 

TGTS = example
OBJS = example.o

# Targets ...
all: $(TGTS)

example: $(OBJS2) example.o
	$(MPICC) $(LFLAGS) -fopenmp -o $@ $(OBJS) $(LIBS)
	
example.o: example.c 
	$(MPICC) -c $(CFLAGS) -fopenmp $<

clean:
	$(RM) $(OBJS)
	$(RM) $(TGTS)