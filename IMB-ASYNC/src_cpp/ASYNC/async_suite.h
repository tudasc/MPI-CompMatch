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

#include <mpi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <iostream>
#include <fstream>

#include "benchmark.h"
#include "benchmark_suites_collection.h"
#include "scope.h"
#include "utils.h"
#include "argsparser.h"

namespace async_suite {

    #include "benchmark_suite.h"

    DECLARE_BENCHMARK_SUITE_STUFF(BS_GENERIC, async_suite)

    template <> bool BenchmarkSuite<BS_GENERIC>::declare_args(args_parser &parser,
                                                              std::ostream &output) const {
        UNUSED(output);
        parser.set_current_group(get_name());
        parser.add_vector<int>("len", "4,128,2048,32768,524288").
                     set_mode(args_parser::option::APPLY_DEFAULTS_ONLY_WHEN_MISSING).
                     set_caption("INT,INT,...");
        parser.add<std::string>("datatype", "double").set_caption("double|float|int|char");
        parser.add_vector<int>("ncycles", "1000");
        parser.add<int>("nwarmup", 0).set_caption("INT -- number of warmup cycles [default: 0]");
        parser.add_vector<int>("calctime", "10,10,50,500,10000").
                     set_mode(args_parser::option::APPLY_DEFAULTS_ONLY_WHEN_MISSING).
                     set_caption("INT,INT,...");
        parser.add<std::string>("workload", "none").set_caption("none|calc|calc_and_progress|calc_and_mpich_progress");
        parser.add<int>("cper10usec", 0).set_caption("INT -- calibration result: measured number of calc cycles per 10 microseconds in normal mode");
        parser.add<int>("estcycles", 3).set_caption("INT -- for calibration mode: number of repeating estimation cycles [default: 3]");
        parser.add<int>("spinperiod", 50).set_caption("INT -- for calc_and_progress: time period in microseconds between sequential MPI_Test calls [default: 50]");
        parser.set_default_current_group();
        return true;
    }

    std::vector<int> len;
    std::vector<int> calctime;
    MPI_Datatype datatype;
    YAML::Emitter yaml_out;
    std::string yaml_outfile;
    std::vector<int> ncycles;
    int nwarmup;
    int cper10usec;
    int estcycles;
    int spinperiod;
    enum workload_t {
        NONE, CALC, CALC_AND_PROGRESS, CALC_AND_MPICH_PROGRESS
    } workload;

    template <> bool BenchmarkSuite<BS_GENERIC>::prepare(const args_parser &parser,
                                                         const std::vector<std::string> &,
                                                         const std::vector<std::string> &unknown_args,
                                                         std::ostream &output) {
        if (unknown_args.size() != 0) {
            output << "Some unknown options or extra arguments. Use -help for help." << std::endl;
            return false;
        }
        parser.get<int>("len", len);
        parser.get<int>("calctime", calctime);
        cper10usec = parser.get<int>("cper10usec");
        std::string str_workload = parser.get<std::string>("workload");
        if (str_workload == "none") {
            workload = workload_t::NONE; 
        } else if (str_workload == "calc") {
            workload = workload_t::CALC; 
        } else if (str_workload == "calc_and_progress") {
            workload = workload_t::CALC_AND_PROGRESS; 
        } else if (str_workload == "calc_and_mpich_progress") {
            workload = workload_t::CALC_AND_MPICH_PROGRESS;
        } else {
            output << get_name() << ": " << "Unknown workload kind in 'workload' option. Use -help for help." << std::endl;
            return false;
        }


        std::string dt = parser.get<std::string>("datatype");
        if (dt == "int") datatype = MPI_INT;
        else if (dt == "double") datatype = MPI_DOUBLE;
	else if (dt == "float") datatype = MPI_FLOAT;
        else if (dt == "char") datatype = MPI_CHAR;
        else {
            output << get_name() << ": " << "Unknown data type in 'datatype' option. Use -help for help." << std::endl;
            return false;
        }
        parser.get<int>("ncycles", ncycles);
        nwarmup = parser.get<int>("nwarmup");
        yaml_outfile = parser.get<std::string>("output");
        estcycles = parser.get<int>("estcycles");
        spinperiod = parser.get<int>("spinperiod");
        yaml_out << YAML::BeginDoc;
        yaml_out << YAML::BeginMap;
        return true;
    }

     template <> void BenchmarkSuite<BS_GENERIC>::finalize(const std::vector<std::string> &,
                          std::ostream &, int rank) {
        yaml_out << YAML::EndMap;
        yaml_out << YAML::Newline;
        if (!yaml_outfile.empty() && !rank) {
            std::ofstream ofs(yaml_outfile, std::ios_base::out | std::ios_base::trunc);
            ofs << yaml_out.c_str();
        }
    }

	template <class SUITE>
	bool is_not_default(const std::string &name) {
		std::shared_ptr<Benchmark> b = SUITE::get_instance().create(name);
		if (b.get() == nullptr) {
			return false;
		}
		return !b->is_default();
	}

    template <> void BenchmarkSuite<BS_GENERIC>::get_bench_list(std::vector<std::string> &benchs,
                                          BenchmarkSuiteBase::BenchListFilter filter) const {
		get_full_list(benchs);
		if (filter == BenchmarkSuiteBase::DEFAULT_BENCHMARKS) {
			for (size_t i = benchs.size() - 1; i != 0; i--) {
				if (is_not_default<BenchmarkSuite<BS_GENERIC>>(benchs[i]))
					benchs.erase(benchs.begin() + i);
			}
		}
	}

	template <> void BenchmarkSuite<BS_GENERIC>::get_bench_list(std::set<std::string> &benchs,
                                          BenchmarkSuiteBase::BenchListFilter filter) const {
		get_full_list(benchs);
		if (filter == BenchmarkSuiteBase::DEFAULT_BENCHMARKS) {
			for (auto &bench_name : benchs) {
				if (is_not_default<BenchmarkSuite<BS_GENERIC>>(bench_name))
					benchs.erase(bench_name);
			}
		}
	}
     

#define HANDLE_PARAMETER(TYPE, NAME) if (key == #NAME) { \
                                        result = std::shared_ptr< TYPE >(&NAME, []( TYPE *){}); \
                                     }

#define GET_PARAMETER(TYPE, NAME) TYPE *p_##NAME = suite->get_parameter(#NAME).as< TYPE >(); \
                                  assert(p_##NAME != NULL); \
                                  TYPE &NAME = *p_##NAME;

    template <> any BenchmarkSuite<BS_GENERIC>::get_parameter(const std::string &key) {
        any result;
        HANDLE_PARAMETER(std::vector<int>, len);
        HANDLE_PARAMETER(std::vector<int>, calctime);
        HANDLE_PARAMETER(MPI_Datatype, datatype);
        HANDLE_PARAMETER(YAML::Emitter, yaml_out);
        HANDLE_PARAMETER(std::vector<int>, ncycles);
        HANDLE_PARAMETER(int, nwarmup);
        HANDLE_PARAMETER(int, cper10usec);
        HANDLE_PARAMETER(int, estcycles);
        HANDLE_PARAMETER(int, spinperiod);
        HANDLE_PARAMETER(workload_t, workload);
        return result;
    }

}
