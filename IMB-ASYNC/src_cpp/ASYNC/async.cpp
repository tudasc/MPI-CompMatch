/*****************************************************************************
 *                                                                           *
 * Copyright 2016-2018 Intel Corporation.                                    *
 *                                                                           *
 *****************************************************************************

This code is covered by the Community Source License (CPL), version
1.0 as published by IBM and reproduced in the file "license.txt" in the
"license" subdirectory. Redistribution in source and binary form, with
or without modification, is permitted ONLY within the regulations
contained in above mentioned license.

Use of the name and trademark "Intel(R) MPI Benchmarks" is allowed ONLY
within the regulations of the "License for Use of "Intel(R) MPI
Benchmarks" Name and Trademark" as reproduced in the file
"use-of-trademark-license.txt" in the "license" subdirectory.

THE PROGRAM IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, EITHER EXPRESS OR IMPLIED INCLUDING, WITHOUT
LIMITATION, ANY WARRANTIES OR CONDITIONS OF TITLE, NON-INFRINGEMENT,
MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE. Each Recipient is
solely responsible for determining the appropriateness of using and
distributing the Program and assumes all risks associated with its
exercise of rights under this Agreement, including but not limited to
the risks and costs of program errors, compliance with applicable
laws, damage to or loss of data, programs or equipment, and
unavailability or interruption of operations.

EXCEPT AS EXPRESSLY SET FORTH IN THIS AGREEMENT, NEITHER RECIPIENT NOR
ANY CONTRIBUTORS SHALL HAVE ANY LIABILITY FOR ANY DIRECT, INDIRECT,
INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING
WITHOUT LIMITATION LOST PROFITS), HOWEVER CAUSED AND ON ANY THEORY OF
LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OR
DISTRIBUTION OF THE PROGRAM OR THE EXERCISE OF ANY RIGHTS GRANTED
HEREUNDER, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGES.

EXPORT LAWS: THIS LICENSE ADDS NO RESTRICTIONS TO THE EXPORT LAWS OF
YOUR JURISDICTION. It is licensee's responsibility to comply with any
export regulations applicable in licensee's jurisdiction. Under
CURRENT U.S. export regulations this software is eligible for export
from the U.S. and can be downloaded by or otherwise exported or
reexported worldwide EXCEPT to U.S. embargoed destinations which
include Cuba, Iraq, Libya, North Korea, Iran, Syria, Sudan,
Afghanistan and any other country to which the U.S. has embargoed
goods and services.

 ***************************************************************************
*/

#include "async_benchmark.h"

#include <unistd.h>
#include <climits>
#include <math.h>

#if 1
namespace sys {
// NOTE: seems to be Linux-specific
static inline size_t getnumcores() {
    return sysconf(_SC_NPROCESSORS_ONLN);
}

static inline bool threadaffinityisset(int &nthreads) {
    cpu_set_t mask;
    if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
        perror("sched_getaffinity");
        assert(false && "sched_getaffinity failure");
    }
    int NC = sys::getnumcores();
    int nset = 0;
    for (int i = 0; i < NC; i++) {
        nset += (CPU_ISSET(i, &mask) ? 1 : 0);
    }
    nthreads = nset;
    // We assume OK: exact one-to-one affinity or hyperthreading/SMT affinity
    // for 2, 3 or 4 threads
    return nthreads > 0 && nthreads < 5 && nthreads != NC;
}

static inline int getthreadaffinity() {
    cpu_set_t mask;
    if (sched_getaffinity(0, sizeof(cpu_set_t), &mask) == -1) {
        perror("sched_getaffinity");
        assert(false && "sched_getaffinity failure");
    }
    int core = -1;
    for (size_t i = 0; i < sizeof(mask) * 8; i++) {
        if (CPU_ISSET(i, &mask)) {
            core = (int)i;
            break;
        }
    }
    assert(core != -1);
    assert(core < (int)sys::getnumcores());
    return core;
}
}
#endif


namespace async_suite {

    inline bool set_stride(int rank, int size, int &stride, int &group)
    {
        if (stride == 0)
            stride = size / 2;
        if (stride <= 0 || stride > size / 2)
            return false;
        group = rank / stride;
        if ((group / 2 == size / (2 * stride)) && (size % (2 * stride) != 0))
            return false;
        return true;
    }


    void AsyncBenchmark::init() {
        GET_PARAMETER(std::vector<int>, len);
        GET_PARAMETER(MPI_Datatype, datatype);
        scope = std::make_shared<VarLenScope>(len);
        MPI_Type_size(datatype, &dtsize);
        size_t size_to_alloc = (size_t)scope->get_max_len() * (size_t)dtsize * buf_size_multiplier();
        if (size_to_alloc <= ASSUMED_CACHE_SIZE * 3)
            size_to_alloc = ASSUMED_CACHE_SIZE * 3;
        rbuf = (char *)calloc(size_to_alloc, 1);
        sbuf = (char *)calloc(size_to_alloc, 1);
        if (rbuf == nullptr || sbuf == nullptr)
            throw std::runtime_error("AsyncBenchmark: memory allocation error.");
        allocated_size = size_to_alloc;
        MPI_Comm_size(MPI_COMM_WORLD, &np);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        is_rank_active = set_stride(rank, np, stride, group);
    }

    struct topohelper {
        int np, rank;
        topohelper(int _np, int _rank) : np(_np), rank(_rank) {}
        int prev(int distance) {
            distance = distance % np;
            int p = rank - distance;
            if (p < 0)
                p += np;
            return p;
        }
        int next(int distance) {
            distance = distance % np;
            int n = rank + distance;
            if (n >= np)
                n -= np;
            return n;
        }
    };

    void AsyncBenchmark_na2a::init() {
        AsyncBenchmark::init();
        topohelper topo(np, rank);
        std::vector<int> sources { topo.prev(stride), topo.next(stride) };
        std::vector<int> dests { topo.prev(stride), topo.next(stride) };
        MPI_Dist_graph_create_adjacent(MPI_COMM_WORLD,
                                       sources.size(), sources.data(), (const int *)MPI_UNWEIGHTED,
                                       dests.size(), dests.data(), (const int *)MPI_UNWEIGHTED,
                                       MPI_INFO_NULL, true,
                                       &graph_comm);
    }

    void AsyncBenchmark_ina2a::init() {
        AsyncBenchmark::init();
        calc.init();
        topohelper topo(np, rank);
        std::vector<int> sources { topo.prev(stride), topo.next(stride) };
        std::vector<int> dests { topo.prev(stride), topo.next(stride) };
        MPI_Dist_graph_create_adjacent(MPI_COMM_WORLD,
                                       sources.size(), sources.data(), (const int *)MPI_UNWEIGHTED,
                                       dests.size(), dests.data(), (const int *)MPI_UNWEIGHTED,
                                       MPI_INFO_NULL, true,
                                       &graph_comm);
    }
   
    void AsyncBenchmark_rma_pt2pt::init() {
        AsyncBenchmark::init();
        MPI_Win_create(sbuf, allocated_size, dtsize, MPI_INFO_NULL,
                       MPI_COMM_WORLD, &win);
    }

    void AsyncBenchmark_rma_ipt2pt::init() {
        AsyncBenchmark::init();
        calc.init();
        MPI_Win_create(sbuf, allocated_size, dtsize, MPI_INFO_NULL,
                       MPI_COMM_WORLD, &win);
    }

    void AsyncBenchmark::run(const scope_item &item) { 
        GET_PARAMETER(MPI_Datatype, datatype);
        GET_PARAMETER(std::vector<int>, ncycles);
        GET_PARAMETER(std::vector<int>, len);
        GET_PARAMETER(int, nwarmup);
        assert(len.size() != 0);
        assert(ncycles.size() != 0);
        int item_ncycles = ncycles[0];
        for (size_t i =0; i < len.size(); i++) {
            if (item.len == (size_t)len[i]) {
                item_ncycles = (i >= ncycles.size() ? ncycles.back() : ncycles[i]);
            }
        }
        double time, tover_comm, tover_calc;
        bool done = benchmark(item.len, datatype, nwarmup, item_ncycles, time, tover_comm, tover_calc);
        if (!done) {
            results[item.len] = result { false, 0.0, 0.0, 0.0, item_ncycles };
        }
    }

    struct YamlOutputMaker {
        std::string block;
        YamlOutputMaker(const std::string &_block) : block(_block) {}
        std::map<const std::string, double> kv;
        void add(const std::string &key, double value) { kv[key] = value; }
        void add(int key, double value) { add(std::to_string(key), value); }
        void make_output(YAML::Emitter &yaml_out) const {
            yaml_out << YAML::Key << block << YAML::Value;
            yaml_out << YAML::Flow << YAML::BeginMap;
            for (auto &item : kv) {
                yaml_out << YAML::Key << YAML::Flow << item.first << YAML::Value << item.second;
            }
            yaml_out << YAML::Flow << YAML::EndMap;
        } 
    };

    static void WriteOutYaml(YAML::Emitter &yaml_out, const std::string &bname, 
                            const std::vector<YamlOutputMaker> &makers) {
        yaml_out << YAML::Key << YAML::Flow << bname << YAML::Value;
        yaml_out << YAML::Flow << YAML::BeginMap;
        for (auto &m : makers) {
            m.make_output(yaml_out);
        }
        yaml_out << YAML::Flow << YAML::EndMap;
    }

    static inline double get_avg(double x, int nexec, int rank, int np, bool is_done) {
        double xx = x;
        std::vector<double> fromall;
        if (rank == 0)
            fromall.resize(np);
        if (!is_done) 
            xx = 0;
        MPI_Gather(&xx, 1, MPI_DOUBLE, fromall.data(), 1, MPI_DOUBLE, 0, MPI_COMM_WORLD);
        if (rank != 0)
            return 0;
        const char *avg_option = nullptr;
        if (!(avg_option = getenv("IMB_ASYNC_AVG_OPT"))) {
            avg_option = "MEDIAN";
        }
        if (std::string(avg_option) == "MEDIAN") {
            std::sort(fromall.begin(), fromall.end());
            if (nexec == 0)
                return 0;
            int off = np - nexec;
            if (nexec == 1)
                return fromall[off];
            if (nexec == 2) {
                return (fromall[off] + fromall[off+1]) / 2.0;
            }
            return fromall[off + nexec / 2];
        }
        if (std::string(avg_option) == "AVERAGE") {
            double sum = 0;
            for (auto x : fromall)
                sum += x;
            sum /= fromall.size();
            return sum;
        }
        if (std::string(avg_option) == "MAX") {
            double maxx = 0;
            for (auto x : fromall)
                maxx = std::max(x, maxx);
            return maxx;
        }
        return -1;
    }

    void AsyncBenchmark::finalize() { 
        GET_PARAMETER(YAML::Emitter, yaml_out);
        YamlOutputMaker yaml_tmin("tmin");
        YamlOutputMaker yaml_tmax("tmax");
        YamlOutputMaker yaml_tavg("tavg");
        YamlOutputMaker yaml_over_comm("over_comm");
        YamlOutputMaker yaml_over_calc("over_calc");
        YamlOutputMaker yaml_over_full("over_full");
        YamlOutputMaker yaml_topo("topo");
        for (auto it = results.begin(); it != results.end(); ++it) {
            int len = it->first;
            double time = (it->second).time, tmin = 0, tmax = 0, tavg = 0;
            double tover_comm = 0, tover_calc = 0;
            int is_done = ((it->second).done ? 1 : 0), nexec = 0;
            MPI_Reduce(&is_done, &nexec, 1, MPI_INT, MPI_SUM, 0, MPI_COMM_WORLD);
            if (!(it->second).done) time = 1e32;
            MPI_Reduce(&time, &tmin, 1, MPI_DOUBLE, MPI_MIN, 0, MPI_COMM_WORLD);
            if (!(it->second).done) time = 0.0;
            MPI_Reduce(&time, &tmax, 1, MPI_DOUBLE, MPI_MAX, 0, MPI_COMM_WORLD);
            tavg = get_avg(time, nexec, rank, np, is_done); 
            tover_comm = get_avg((it->second).overhead_comm, nexec, rank, np, is_done);
            tover_calc = get_avg((it->second).overhead_calc, nexec, rank, np, is_done);
            if (rank == 0) {
                if (nexec == 0) {
                    std::cout << get_name() << ": " << "{ " << "len: " << len << ", "
                        << " error: \"no successful executions!\"" << " }" << std::endl;
                } else {
                    std::cout << get_name() << ": " << "{ " << "len: " << len << ", "
                        << "ncycles: " << (it->second).ncycles << ", "
                        << " time: [ " << tmin << ", " 
                                      << tavg << ", " 
                                      << tmax << " ]" 
                        << ", overhead: [ " << tover_comm << " , " << tover_calc 
                                      << " ] }" << std::endl;
                    yaml_tmin.add(len, tmin);
                    yaml_tmax.add(len, tmax);
                    yaml_tavg.add(len, tavg);
                    yaml_over_comm.add(len, tover_comm); 
                    yaml_over_calc.add(len, tover_calc); 
                    yaml_over_full.add(len, tover_calc + tover_comm);
                }
            }
        }
        yaml_topo.add("np", np);
        yaml_topo.add("stride", stride);
        WriteOutYaml(yaml_out, get_name(), {yaml_tavg, yaml_over_full, yaml_topo});
    }

    AsyncBenchmark::~AsyncBenchmark() {
        free(rbuf);
        free(sbuf);
    }
    bool AsyncBenchmark_pt2pt::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        tover_comm = 0;
	    tover_calc = 0;
        if (!is_rank_active) {
            MPI_Barrier(MPI_COMM_WORLD);
            return false;
        }
        size_t b = (size_t)count * (size_t)dtsize;
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0;
        const int tag = 1;
        int pair = -1;
        if (group % 2 == 0) {
            pair = rank + stride;
            for(int i = 0; i < ncycles + nwarmup; i++) {
                if (i == nwarmup) t1 = MPI_Wtime();
                MPI_Send((char*)sbuf + (i%n)*b, count, datatype, pair, tag, MPI_COMM_WORLD);
                MPI_Recv((char*)rbuf + (i%n)*b, count, datatype, pair, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            }
            t2 = MPI_Wtime();
            time = (t2 - t1) / ncycles;
        } else {
            pair = rank - stride;
            for(int i = 0; i < ncycles + nwarmup; i++) {
                if (i == nwarmup) t1 = MPI_Wtime();
                MPI_Recv((char*)rbuf + (i%n)*b, count, datatype, pair, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
                MPI_Send((char*)sbuf + (i%n)*b, count, datatype, pair, tag, MPI_COMM_WORLD);
            }
            t2 = MPI_Wtime();
            time = (t2 - t1) / ncycles;
        } 
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, 0.0, 0.0, ncycles };
        return true;
    }

    void AsyncBenchmark_ipt2pt::init() {
        AsyncBenchmark::init();
        calc.init();
    }

    bool AsyncBenchmark_ipt2pt::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        if (!is_rank_active) {
            MPI_Barrier(MPI_COMM_WORLD);
            return false;
        }
        size_t b = (size_t)count * (size_t)dtsize;
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0, ctime = 0, total_ctime = 0, total_tover_comm = 0, total_tover_calc = 0, 
                                          local_ctime = 0, local_tover_comm = 0, local_tover_calc = 0;
        const int tag = 1;
        int pair = -1;
        MPI_Request request[2];
        calc.reqs = request;
        calc.num_requests = 2;
        if (group % 2 == 0) {
            pair = rank + stride;
            for (int i = 0; i < ncycles + nwarmup; i++) {
                if (i == nwarmup) t1 = MPI_Wtime();
                MPI_Isend((char*)sbuf  , count, datatype, pair, tag, MPI_COMM_WORLD, &request[0]);
                MPI_Irecv((char*)rbuf , count, datatype, pair, MPI_ANY_TAG, MPI_COMM_WORLD,
                          &request[1]);
                calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
                if (i >= nwarmup) {
                    total_ctime += local_ctime;
                    total_tover_comm += local_tover_comm;
                    total_tover_calc += local_tover_calc;
                }
                MPI_Waitall(2, request, MPI_STATUSES_IGNORE);
            }
            t2 = MPI_Wtime();
            time = (t2 - t1) / ncycles;
            ctime = total_ctime / ncycles;
            tover_comm = total_tover_comm / ncycles;
            tover_calc = total_tover_calc / ncycles;
        } else {
            pair = rank - stride;
            for (int i = 0; i < ncycles + nwarmup; i++) {
                if (i == nwarmup) t1 = MPI_Wtime();
                MPI_Isend((char*)sbuf , count, datatype, pair, tag, MPI_COMM_WORLD, &request[0]);
                MPI_Irecv((char*)rbuf , count, datatype, pair, MPI_ANY_TAG, MPI_COMM_WORLD,
                          &request[1]);
                calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
                if (i >= nwarmup) {
                    total_ctime += local_ctime;
                    total_tover_comm += local_tover_comm;
                    total_tover_calc += local_tover_calc;
                }
                MPI_Waitall(2, request, MPI_STATUSES_IGNORE);
            }
            t2 = MPI_Wtime();
            time = (t2 - t1) / ncycles;
            ctime = total_ctime / ncycles;
            tover_comm = total_tover_comm / ncycles;
            tover_calc = total_tover_calc / ncycles;
        } 
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, time - ctime + tover_comm, tover_calc, ncycles };
        return true;
    }

    void AsyncBenchmark_persistentpt2pt::init() {
            AsyncBenchmark::init();
            calc.init();
        }

        bool AsyncBenchmark_persistentpt2pt::benchmark(int orig_count, MPI_Datatype orig_datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
            if (!is_rank_active) {
                MPI_Barrier(MPI_COMM_WORLD);
                return false;
            }
            size_t b = (size_t)orig_count * (size_t)dtsize;
            int count =b;
            datatype=MPI_BYTE;
            size_t n = allocated_size / b;
            double t1 = 0, t2 = 0, ctime = 0, total_ctime = 0, total_tover_comm = 0, total_tover_calc = 0,
                                              local_ctime = 0, local_tover_comm = 0, local_tover_calc = 0;
            const int tag = 1;
            int pair = -1;
            MPI_Request request_s;
            MPI_Request request_r;


            calc.reqs = nullptr;
            calc.num_requests = 0;
            if (group % 2 == 0) {
                pair = rank + stride;

                MPI_Send_init((char*)sbuf  , count, datatype, pair, tag, MPI_COMM_WORLD, &request_s);
                MPI_Recv_init((char*)rbuf , count, datatype, pair, tag, MPI_COMM_WORLD, &request_r);
                for (int i = 0; i < ncycles + nwarmup; i++) {
                    if (i == nwarmup) t1 = MPI_Wtime();
MPI_Start(&request_s);
MPI_Start(&request_r);
                    calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
                    if (i >= nwarmup) {
                        total_ctime += local_ctime;
                        total_tover_comm += local_tover_comm;
                        total_tover_calc += local_tover_calc;
                    }
                    MPI_Wait(&request_s, MPI_STATUSES_IGNORE);
                    MPI_Wait(&request_r, MPI_STATUSES_IGNORE);


                }
                t2 = MPI_Wtime();
                time = (t2 - t1) / ncycles;
                ctime = total_ctime / ncycles;
                tover_comm = total_tover_comm / ncycles;
                tover_calc = total_tover_calc / ncycles;
                MPI_Request_free(&request_s);
                MPI_Request_free(&request_r);
            } else {
                pair = rank - stride;
                MPI_Send_init((char*)sbuf  , count, datatype, pair, tag, MPI_COMM_WORLD, &request_s);
                            MPI_Recv_init((char*)rbuf , count, datatype, pair, tag, MPI_COMM_WORLD, &request_r);
                for (int i = 0; i < ncycles + nwarmup; i++) {
                    if (i == nwarmup) t1 = MPI_Wtime();
                    MPI_Start(&request_s);
                    MPI_Start(&request_r);
                    calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
                    if (i >= nwarmup) {
                        total_ctime += local_ctime;
                        total_tover_comm += local_tover_comm;
                        total_tover_calc += local_tover_calc;
                    }
                    MPI_Wait(&request_s, MPI_STATUSES_IGNORE);
                    MPI_Wait(&request_r, MPI_STATUSES_IGNORE);

                }
                t2 = MPI_Wtime();
                time = (t2 - t1) / ncycles;
                ctime = total_ctime / ncycles;
                tover_comm = total_tover_comm / ncycles;
                tover_calc = total_tover_calc / ncycles;
                MPI_Request_free(&request_s);
                MPI_Request_free(&request_r);
            }
            MPI_Barrier(MPI_COMM_WORLD);
            results[count] = result { true, time, time - ctime + tover_comm, tover_calc, ncycles };
            return true;
        }

#define EXTRA_BARRIER 0

    void barrier(int rank, int np) {
#if !EXTRA_BARRIER
        (void)rank;
        (void)np;
        MPI_Barrier(MPI_COMM_WORLD);
#else

        int mask = 0x1;
        int dst, src;
        int tmp = 0;
        for (; mask < np; mask <<= 1) {
            dst = (rank + mask) % np;
            src = (rank - mask + np) % np;
            MPI_Sendrecv(&tmp, 0, MPI_BYTE, dst, 1010,
                         &tmp, 0, MPI_BYTE, src, 1010,
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        }
#endif
    }

    bool AsyncBenchmark_allreduce::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        time = 0;
        tover_comm = 0;
	    tover_calc = 0;
        size_t b = (size_t)count * (size_t)dtsize;
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0;
        for (int i = 0; i < ncycles + nwarmup; i++) {
            if (i >= nwarmup) t1 = MPI_Wtime();
            MPI_Allreduce((char *)sbuf + (i%n)*b, (char *)rbuf + (i%n)*b, count, datatype, MPI_SUM, MPI_COMM_WORLD);
            if (i >= nwarmup) {
                t2 = MPI_Wtime();
                time += (t2 - t1);
            }
            barrier(rank, np);
#if EXTRA_BARRIER
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
#endif
        }
        time /= ncycles;
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, 0, 0, ncycles };
        return true;
    }

    void AsyncBenchmark_iallreduce::init() {
        AsyncBenchmark::init();
        calc.init();
    }

    bool AsyncBenchmark_iallreduce::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        size_t b = (size_t)count * (size_t)dtsize;
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0, ctime = 0, total_ctime = 0, total_tover_comm = 0, total_tover_calc = 0,
                                          local_ctime = 0, local_tover_comm = 0, local_tover_calc = 0;
	    time = 0;
        MPI_Request request[1];
        calc.reqs = request;
        calc.num_requests = 1;
	    MPI_Status  status;
        for (int i = 0; i < ncycles + nwarmup; i++) {
            if (i >= nwarmup) t1 = MPI_Wtime();
            MPI_Iallreduce((char *)sbuf + (i%n)*b, (char *)rbuf + (i%n)*b, count, datatype, MPI_SUM, MPI_COMM_WORLD, request);
            calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
            MPI_Wait(request, &status);
            if (i >= nwarmup) {
                t2 = MPI_Wtime();
                time += (t2 - t1);
                total_ctime += local_ctime;
                total_tover_comm += local_tover_comm;
                total_tover_calc += local_tover_calc;
            }
            barrier(rank, np);
#if EXTRA_BARRIER
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
#endif
        }
        time /= ncycles;
        ctime = total_ctime / ncycles;
	    tover_comm = total_tover_comm / ncycles;
	    tover_calc = total_tover_calc / ncycles;
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, time - ctime + tover_comm, tover_calc, ncycles };
        return true;
    }

    bool AsyncBenchmark_na2a::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {          
        time = 0;
        tover_comm = 0;
	    tover_calc = 0;
        if (!is_rank_active) {
            MPI_Barrier(MPI_COMM_WORLD);
            return false;
        }
        size_t b = (size_t)count * (size_t)dtsize * buf_size_multiplier();
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0;
        for (int i = 0; i < ncycles + nwarmup; i++) {
            if (i >= nwarmup) t1 = MPI_Wtime();
            MPI_Neighbor_alltoall((char *)sbuf + (i%n)*b, count, datatype,
                                  (char *)rbuf + (i%n)*b, count, datatype,
                                  graph_comm);            
            if (i >= nwarmup) {
                t2 = MPI_Wtime();
                time += (t2 - t1);
            }
            barrier(rank, np);
#if EXTRA_BARRIER
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
#endif
        }
        time /= ncycles;
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, 0, 0, ncycles };
        return true;
    }

    bool AsyncBenchmark_ina2a::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {         
        size_t b = (size_t)count * (size_t)dtsize * buf_size_multiplier();
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0, ctime = 0, total_ctime = 0, total_tover_comm = 0, total_tover_calc = 0,
                                          local_ctime = 0, local_tover_comm = 0, local_tover_calc = 0;
	    time = 0;
        if (!is_rank_active) {
            MPI_Barrier(MPI_COMM_WORLD);
            return false;
        }
        MPI_Request request[1];
        calc.reqs = request;
        calc.num_requests = 1;
	    MPI_Status  status;
        for (int i = 0; i < ncycles + nwarmup; i++) {
            if (i >= nwarmup) t1 = MPI_Wtime();
            MPI_Ineighbor_alltoall((char *)sbuf + (i%n)*b, count, datatype,
                                   (char *)rbuf + (i%n)*b, count, datatype,
                                   graph_comm, request);
            calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
            MPI_Wait(request, &status);
            if (i >= nwarmup) {
                t2 = MPI_Wtime();
                time += (t2 - t1);
                total_ctime += local_ctime;
                total_tover_comm += local_tover_comm;
                total_tover_calc += local_tover_calc;
            }
            barrier(rank, np);
#if EXTRA_BARRIER
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
            barrier(rank, np);
#endif
        }
        time /= ncycles;
        ctime = total_ctime / ncycles;
	    tover_comm = total_tover_comm / ncycles;
	    tover_calc = total_tover_calc / ncycles;
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, time - ctime + tover_comm, tover_calc, ncycles };
        return true;
    }

    bool AsyncBenchmark_rma_pt2pt::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        tover_comm = 0;
        tover_calc = 0;
        if (!is_rank_active) {
            MPI_Barrier(MPI_COMM_WORLD);
            return false;
        }
        size_t b = (size_t)count * (size_t)dtsize;
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0;
        int pair = -1;
        if (group % 2 == 0) {
            pair = rank + stride;
        } else {
            pair = rank - stride;
        }
        for (int i = 0; i < ncycles + nwarmup; i++) {
            if (i == nwarmup) t1 = MPI_Wtime();
            MPI_Win_lock(MPI_LOCK_SHARED, pair, 0, win);
            MPI_Get((char*)rbuf + (i%n)*b, count, datatype, pair, (i%n)*b/dtsize, count, datatype, win);
            MPI_Win_unlock(pair, win);
        }
        t2 = MPI_Wtime();
        time = (t2 - t1) / ncycles;
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, 0.0, 0.0, ncycles };
        return true;
    }

    bool AsyncBenchmark_rma_ipt2pt::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        if (!is_rank_active) {
            MPI_Barrier(MPI_COMM_WORLD);
            return false;
        }
        size_t b = (size_t)count * (size_t)dtsize;
        size_t n = allocated_size / b;
        double t1 = 0, t2 = 0, ctime = 0, total_ctime = 0, total_tover_comm = 0, total_tover_calc = 0,
                                          local_ctime = 0, local_tover_comm = 0, local_tover_calc = 0;
        int pair = -1;
        MPI_Request request[1];
        calc.reqs = request;
        calc.num_requests = 1;
        MPI_Status status;
        if (group % 2 == 0) {
            pair = rank + stride;
        } else {
            pair = rank - stride;
        }
        for (int i = 0; i < ncycles + nwarmup; i++) {
            if (i == nwarmup) t1 = MPI_Wtime();
            MPI_Win_lock(MPI_LOCK_SHARED, pair, 0, win);
            MPI_Rget((char*)rbuf + (i%n)*b, count, datatype, pair, (i%n)*b/dtsize, count, datatype, win, request);
            calc.benchmark(count, datatype, 0, 1, local_ctime, local_tover_comm, local_tover_calc);
            MPI_Wait(request, &status);
            MPI_Win_unlock(pair, win);
            if (i >= nwarmup) {
                total_ctime += local_ctime;
                total_tover_comm += local_tover_comm;
                total_tover_calc += local_tover_calc;
            }
        }
        t2 = MPI_Wtime();
        time = (t2 - t1) / ncycles;
        ctime = total_ctime / ncycles;
        tover_comm = total_tover_comm / ncycles;
        tover_calc = total_tover_calc / ncycles;
        MPI_Barrier(MPI_COMM_WORLD);
        results[count] = result { true, time, time - ctime + tover_comm, tover_calc, ncycles };
        return true;
    }

    // NOTE: to ensure just calc, no manual progress call it with iters_till_test == R
    // NOTE2: tover_comm is not zero'ed here before operation!
    void AsyncBenchmark_calc::calc_and_progress_cycle(int R, int iters_till_test, double &tover_comm) {
        for (int repeat = 0, cnt = iters_till_test; repeat < R; repeat++) {
            if (--cnt == 0) { 
                double t1 = MPI_Wtime();
                if (reqs && num_requests) {
                    for (int r = 0; r < num_requests; r++) {
                        if (!stat[r]) {
                            total_tests++;
                            MPI_Test(&reqs[r], &stat[r], MPI_STATUS_IGNORE);
                            if (stat[r]) {
                                successful_tests++;
                            }
                        }
                    }
                }
                double t2 = MPI_Wtime();
                tover_comm += (t2 - t1);
                cnt = iters_till_test;
            } 
            for (int i = 0; i < SIZE; i++) {
                for (int j = 0; j < SIZE; j++) {
                    for (int k = 0; k < SIZE; k++) {
                        c[i][j] += a[i][k] * b[k][j] + repeat*repeat;
                    }
                }
            }

        }
    }

    void AsyncBenchmark_calc::calibration() {
        GET_PARAMETER(int, cper10usec);
        GET_PARAMETER(int, estcycles);
        double timings[3];
        int warmup = estcycles;
        if (warmup == 0 && cper10usec == 0) {
            throw std::runtime_error("AsyncBenchmark_calc: either -cper10usec or -estcycles option is required.");
        }
        if (warmup != 0) {        
            int Nrep = (int)(4000000000ul / (unsigned long)(SIZE*SIZE*SIZE));
            for (int k = 0; k < 3 + warmup; k++) {
                double tover = 0;
                double t1 = MPI_Wtime();
                calc_and_progress_cycle(Nrep, Nrep, tover);
                double t2 = MPI_Wtime();
                if (k >= warmup)
                    timings[k - warmup] = t2 - t1;
                else {
                    if (k > 0) {
                        if (t2 - t1 > 1.5) {
                            Nrep = (int)((double)Nrep * 1.0 / (t2 - t1));
                        } else if (t2 - t1 > 0.001 && t2 - t1 < 0.5) {
                            Nrep = (int)((double)Nrep * 1.0 / (t2 - t1));
                        } else if (t2 - t1 < 0.001) {
                            assert(0 && "cper10usec: calibration cycle error: too little measuring time");
                        }
                    }
                }
            }
            double tmedian = std::min(timings[0], timings[1]);
            if (tmedian < timings[2])
                tmedian = std::min(std::max(timings[0], timings[1]), timings[2]);
            double _10usec = 1.0e5;
            int local_cper10usec = (int)((double)Nrep / (tmedian * _10usec) + 0.999);
            MPI_Allreduce(&local_cper10usec, &cper10usec_avg, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
            MPI_Allreduce(&local_cper10usec, &cper10usec_min, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);
            MPI_Allreduce(&local_cper10usec, &cper10usec_max, 1, MPI_INT, MPI_MAX, MPI_COMM_WORLD);
            cper10usec_avg /= np;
            if (cper10usec_avg < 150 && cper10usec_avg > 10) {
                int hits = 0;
                int local_hit = ((fabs((float)local_cper10usec - (float)cper10usec_avg) > 
                                 (float)cper10usec_avg/25.0) ? 0 : 1);
                MPI_Allreduce(&local_hit, &hits, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
                if ((float)(np - hits) / (float)np > 0.1f) {
                    if (rank == 0) {
                        std::cout << ">> cper10usec: WARNING: many deviated values!" << std::endl;
                        irregularity_level++;
                    }
                }
                if (cper10usec_min == 0 || hits == 0) {
                    if (rank == 0) {
                        std::cout << ">> cper10usec: WARNING: very strange and deviated calibration results" << std::endl;
                        irregularity_level += 2;
                    }
                } else if (cper10usec_max / cper10usec_min >= 4 && cper10usec_avg / cper10usec_min >= 2) {
                    // exclude highly deviated values
                    int cleaned_local_cper10usec = (local_hit ? local_cper10usec : 0);
                    cper10usec_avg = 0;
                    MPI_Allreduce(&cleaned_local_cper10usec, &cper10usec_avg, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
                    cper10usec_avg /= hits;
                    irregularity_level++;
                }
            }
#if 0                
            char node[80];
            gethostname(node, 80-1);
            std::cout << ">> cper10usec: node: " << node << " time=" << tmedian << "; cpersec=" << (double)Nrep/tmedian << std::endl;
            std::cout << ">> cper10usec: node: " << node << " cper10usec=" << cper10usec_avg << std::endl;
#endif                
            if (rank == 0) {
                std::cout << ">> " << get_name() << ": average cper10usec=" << cper10usec_avg << " min/max=" 
                          << cper10usec_min << "/" << cper10usec_max << std::endl;
                if (cper10usec_avg > 150 || cper10usec_avg < 10) {
                    irregularity_level++;
                    std::cout << ">> cper10usec: NOTE: good value for cper10usec is [10, 150]."
                              << " You may decrease or increase SIZE constant." << std::endl;
                }
            }
        } else {
            // opt cper10usec is given, don't know what to measure...
            if (rank == 0) {
                std::cout << ">> " << get_name() << ": WARNING: cper10usec: skipped calibration procedure since -cper10usec option is given." << std::endl;
            }
        }
        MPI_Barrier(MPI_COMM_WORLD);
    }

    void AsyncBenchmark_calc::init() {
        AsyncBenchmark::init();
        GET_PARAMETER(std::vector<int>, calctime);
        GET_PARAMETER(workload_t, workload);

        wld = workload;
        for (size_t i = 0; i < len.size(); i++) {
            calctime_by_len[len[i]] = (i >= calctime.size() ? (calctime.size() == 0 ? 10000 : calctime[calctime.size() - 1]) : calctime[i]);
        }
        for (int i = 0; i < SIZE; i++) {
            x[i] = y[i] = 0.;
            for (int j=0; j< SIZE; j++) {
                a[i][j] = 1.;
            }
        }
    }

    bool AsyncBenchmark_calc::benchmark(int count, MPI_Datatype datatype, int nwarmup, int ncycles, double &time, double &tover_comm, double &tover_calc) {
        GET_PARAMETER(int, cper10usec);
        GET_PARAMETER(int, spinperiod);
        int real_cper10usec;
        (void)datatype;
        total_tests = 0;
        successful_tests = 0;
        time = 0;
        tover_comm = 0;
        tover_calc = 0;
        if (wld == workload_t::NONE) {
            time = 0;
            return true;
        }
        double t1 = 0, t2 = 0;
        if (cper10usec == 0) {
            cper10usec = cper10usec_avg;
            assert(cper10usec_avg != 0);
        }
        int R = calctime_by_len[count] * cper10usec / 10;
        if (wld == workload_t::CALC_AND_PROGRESS && reqs) {
            for (int r = 0; r < num_requests; r++) {
                stat[r] = 0;
            }
        }
        const int cnt_for_mpi_test = std::max(spinperiod * cper10usec / 10, 1);
        if (wld == workload_t::CALC_AND_PROGRESS) {
            for (int i = 0; i < ncycles + nwarmup; i++) {
                if (i == nwarmup) 
                    t1 = MPI_Wtime();
                calc_and_progress_cycle(R, cnt_for_mpi_test, tover_comm);
            }
        } else {
            for (int i = 0; i < ncycles + nwarmup; i++) {
                if (i == nwarmup) 
                    t1 = MPI_Wtime();
                calc_and_progress_cycle(R, R, tover_comm);
            }
        }
        t2 = MPI_Wtime();
        time = (t2 - t1);
#if 1        
        int pure_calc_time = int((time - tover_comm) * 1e6);
        if (!pure_calc_time)
            return true;
        real_cper10usec = R * 10 / pure_calc_time;
        if (cper10usec && real_cper10usec) {
            int R0 = pure_calc_time * cper10usec / 10;
            tover_calc = (double)(R0 - R) / (double)real_cper10usec * 1e-5;
            if (tover_calc < 1e6)
                tover_calc = 0;
        }
#endif        
        return true;
    }

    void AsyncBenchmark_calibration::init() {
        MPI_Comm_size(MPI_COMM_WORLD, &np);
        MPI_Comm_rank(MPI_COMM_WORLD, &rank);
        scope = std::make_shared<VarLenScope>(0, 0);
        calc.init();
    }

    void AsyncBenchmark_calibration::run(const scope_item &item) {
        (void)item;
        calc.calibration();
    }

    static inline bool isregular(int l, int g, int np) {
        int regular = 0, local_regular = 0;
        local_regular = (l == g ? 1 : 0);
        MPI_Allreduce(&local_regular, &regular, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        return regular == np;
    }

    void AsyncBenchmark_calibration::finalize() {
        GET_PARAMETER(YAML::Emitter, yaml_out);

        YamlOutputMaker yaml_affinity("affinity");
		int isset = 0, ncores = 0, nthreads = 0;
		int local_isset = 0, local_ncores = 0, local_nthreads = 0;
        local_isset = (int)sys::threadaffinityisset(local_nthreads);
        local_ncores = sys::getnumcores();
        MPI_Allreduce(&local_isset, &isset, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        isset /= np;
        MPI_Allreduce(&local_ncores, &ncores, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        ncores /= np;
        MPI_Allreduce(&local_nthreads, &nthreads, 1, MPI_INT, MPI_SUM, MPI_COMM_WORLD);
        nthreads /= np;
        isset = (isregular(local_isset, isset, np) ? isset : -1);
        ncores = (isregular(local_ncores, ncores, np) ? ncores : -1);
        nthreads = (isregular(local_nthreads, nthreads, np) ? nthreads : -1);
        yaml_affinity.add("isset", isset);
        yaml_affinity.add("ncores", ncores);
        yaml_affinity.add("nthreads", nthreads);
        
        YamlOutputMaker yaml_calibration("calibration");
        yaml_calibration.add("avg", calc.cper10usec_avg); 
        yaml_calibration.add("min", calc.cper10usec_min); 
        yaml_calibration.add("max", calc.cper10usec_max); 
        yaml_calibration.add("irregularity", calc.irregularity_level); 

        WriteOutYaml(yaml_out, get_name(), {yaml_affinity, yaml_calibration});
    }


    DECLARE_INHERITED(AsyncBenchmark_pt2pt, sync_pt2pt)
    DECLARE_INHERITED(AsyncBenchmark_ipt2pt, async_pt2pt)
	DECLARE_INHERITED(AsyncBenchmark_persistentpt2pt, async_persistentpt2pt)
    DECLARE_INHERITED(AsyncBenchmark_allreduce, sync_allreduce)
    DECLARE_INHERITED(AsyncBenchmark_iallreduce, async_allreduce)
    DECLARE_INHERITED(AsyncBenchmark_na2a, sync_na2a)
    DECLARE_INHERITED(AsyncBenchmark_ina2a, async_na2a)
    DECLARE_INHERITED(AsyncBenchmark_rma_pt2pt, sync_rma_pt2pt)
    DECLARE_INHERITED(AsyncBenchmark_rma_ipt2pt, async_rma_pt2pt)
    DECLARE_INHERITED(AsyncBenchmark_calc, calc)
    DECLARE_INHERITED(AsyncBenchmark_calibration, calc_calibration)
}
