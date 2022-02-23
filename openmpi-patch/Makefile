# Common definitions
#CC = clang
#CC = gcc
#MPICC = mpicc
#MPICC = scorep mpicc

# module load openmpi/test hwloc/2.5.0

# Compiler flags, paths and libraries
# Include npath showing to openmpoi internals
INCLUDE=-I/home/tj75qeje/openmpi-4.1.1/ -I/home/tj75qeje/openmpi-4.1.1/opal/include -I/home/tj75qeje/openmpi-4.1.1/ompi/include/ -I/home/tj75qeje/openmpi-4.1.1/orte/include

#CFLAGS = -std=c11 -Og -g $(INCLUDE) -Wall -Wextra -Wno-unused-parameter
CFLAGS = -std=c11 -O3 -DNDEBUG $(INCLUDE) -flto
LFLAGS = $(CFLAGS)
LIBS   = -lopen-pal -lucp -lm

TGTS = example
OBJS = example.o low-level.o

# Targets ...
all: $(TGTS)

example: $(OBJS)
	$(MPICC) $(LFLAGS) -o $@ $(OBJS) $(LIBS)
	
low-level.o: low-level.c 
	$(MPICC) -c $(CFLAGS) $<

example.o: example.c 
	$(MPICC) -c $(CFLAGS) $<

clean:
	$(RM) $(OBJS)
	$(RM) $(TGTS)