/*
 Copyright 2020 Tim Jammer

 Licensed under the Apache License, Version 2.0 (the "License");
 you may not use this file except in compliance with the License.
 You may obtain a copy of the License at

 http://www.apache.org/licenses/LICENSE-2.0

 Unless required by applicable law or agreed to in writing, software
 distributed under the License is distributed on an "AS IS" BASIS,
 WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 See the License for the specific language governing permissions and
 limitations under the License.
 */

#include "mpi_functions.h"
#include <assert.h>

#include "llvm/IR/InstrTypes.h"
#include <llvm/Support/raw_ostream.h>

using namespace llvm;

bool is_mpi_call(CallBase *call) {
  return is_mpi_function(call->getCalledFunction());
}

bool is_mpi_function(llvm::Function *f) {
  if (f) {
    return f->getName().contains("MPI");
  } else
    return false;
}

std::vector<CallBase *> gather_all_calls(Function *f) {
  std::vector<CallBase *> result;
  if (f) {
    for (auto user : f->users()) {
      if (CallBase *call = dyn_cast<CallBase>(user)) {
        if (call->getCalledFunction() == f) {

          result.push_back(call);
        } else {
          call->dump();
          errs() << "\nWhy do you do that?\n";
        }
      }
    }
  }
  return result;
}

struct mpi_functions *get_used_mpi_functions(llvm::Module &M) {

  struct mpi_functions *result = new struct mpi_functions;
  assert(result != nullptr);

  for (auto it = M.begin(); it != M.end(); ++it) {
    Function *f = &*it;
    if (f->getName().equals("MPI_Init")) {
      result->mpi_init = f;

    } else if (f->getName().equals("MPI_Init_thread")) {
      result->mpi_init_thread = f;

      // sync functions:
    } else if (f->getName().equals("MPI_Finalize")) {
      result->mpi_finalize = f;

    } else if (f->getName().equals("MPI_Barrier")) {
      result->mpi_barrier = f;

    } else if (f->getName().equals("MPI_Ibarrier")) {
      result->mpi_Ibarrier = f;

    } else if (f->getName().equals("MPI_Allreduce")) {
      result->mpi_allreduce = f;

    } else if (f->getName().equals("MPI_Iallreduce")) {
      result->mpi_Iallreduce = f;

    }

    // different sending modes:
    else if (f->getName().equals("MPI_Send")) {
      result->mpi_send = f;

    } else if (f->getName().equals("MPI_Bsend")) {
      result->mpi_Bsend = f;

    } else if (f->getName().equals("MPI_Ssend")) {
      result->mpi_Ssend = f;

    } else if (f->getName().equals("MPI_Rsend")) {
      result->mpi_Rsend = f;

    } else if (f->getName().equals("MPI_Isend")) {
      result->mpi_Isend = f;

    } else if (f->getName().equals("MPI_Ibsend")) {
      result->mpi_Ibsend = f;

    } else if (f->getName().equals("MPI_Issend")) {
      result->mpi_Issend = f;

    } else if (f->getName().equals("MPI_Irsend")) {
      result->mpi_Irsend = f;

    } else if (f->getName().equals("MPI_Sendrecv")) {
      result->mpi_Sendrecv = f;

    } else if (f->getName().equals("MPI_Recv")) {
      result->mpi_recv = f;

    } else if (f->getName().equals("MPI_Irecv")) {
      result->mpi_Irecv = f;

      // Other MPI functions, that themselves may not yield another conflict
    } else if (f->getName().equals("MPI_Buffer_detach")) {
      result->mpi_buffer_detach = f;

    } else if (f->getName().equals("MPI_Test")) {
      result->mpi_test = f;

    } else if (f->getName().equals("MPI_Wait")) {
      result->mpi_wait = f;

    } else if (f->getName().equals("MPI_Waitall")) {
      result->mpi_waitall = f;

    } else if (f->getName().equals("MPI_Start")) {
      result->mpi_start = f;

    } else if (f->getName().equals("MPI_Recv_init")) {
      result->mpi_recv_init = f;

    } else if (f->getName().equals("MPI_Send_init")) {
      result->mpi_send_init = f;

    } else if (f->getName().equals("MPI_Request_free")) {
      result->mpi_request_free = f;
    }
  }

  // construct the optimized version of functions, if original functions where
  // used:

  if (result->mpi_wait) {
    result->optimized.mpi_wait =
        cast<Function>(M.getOrInsertFunction(
                            "MPIOPT_Wait", result->mpi_wait->getFunctionType())
                           .getCallee()
                           ->stripPointerCasts());
  }
  if (result->mpi_start) {
    result->optimized.mpi_start = cast<Function>(
        M.getOrInsertFunction("MPIOPT_Start",
                              result->mpi_start->getFunctionType())
            .getCallee()
            ->stripPointerCasts());
  }
  if (result->mpi_send_init) {
    result->optimized.mpi_send_init = cast<Function>(
        M.getOrInsertFunction("MPIOPT_Send_init",
                              result->mpi_send_init->getFunctionType())
            .getCallee()
            ->stripPointerCasts());
  }
  if (result->mpi_recv_init) {
    result->optimized.mpi_recv_init = cast<Function>(
        M.getOrInsertFunction("MPIOPT_Recv_init",
                              result->mpi_recv_init->getFunctionType())
            .getCallee()
            ->stripPointerCasts());
  }
  if (result->mpi_request_free) {
    result->optimized.mpi_request_free = cast<Function>(
        M.getOrInsertFunction("MPIOPT_Request_free",
                              result->mpi_request_free->getFunctionType())
            .getCallee()
            ->stripPointerCasts());
  }

  // construct the init and finish functions, if necessary:
  if (result->mpi_init || result->mpi_init_thread || result->mpi_finalize) {
    // void funcs that do not have params
    auto *ftype = FunctionType::get(Type::getVoidTy(M.getContext()), false);
    result->optimized.init =
        cast<Function>(M.getOrInsertFunction("MPIOPT_INIT", ftype, {})
                           .getCallee()
                           ->stripPointerCasts());
    result->optimized.finalize =
        cast<Function>(M.getOrInsertFunction("MPIOPT_FINALIZE", ftype, {})
                           .getCallee()
                           ->stripPointerCasts());
  }

  // gather all MPI send and recv calls
  /*
   // currenty unused:
          auto temp = gather_all_calls(result->mpi_send);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Bsend);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Ssend);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Rsend);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Isend);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Ibsend);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Irsend);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Sendrecv);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_send_init);
          result->send_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());

           temp = gather_all_calls(result->mpi_recv);
          result->recv_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Irecv);
          result->recv_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_recv_init);
          result->recv_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
           temp = gather_all_calls(result->mpi_Sendrecv);
          result->recv_calls.insert(result->send_calls.end(), temp.begin(),
                          temp.end());
                          */

  return result;
}

bool is_mpi_used(struct mpi_functions *mpi_func) {

  if (mpi_func->mpi_init != nullptr) {
    return mpi_func->mpi_init->getNumUses() > 0;
  } else {
    return false;
  }
}

bool is_send_function(llvm::Function *f) {
  assert(f != nullptr);
  return f == mpi_func->mpi_send || f == mpi_func->mpi_Bsend ||
         f == mpi_func->mpi_Ssend || f == mpi_func->mpi_Rsend ||
         f == mpi_func->mpi_Isend || f == mpi_func->mpi_Ibsend ||
         f == mpi_func->mpi_Irsend || f == mpi_func->mpi_Issend ||
         f == mpi_func->mpi_Sendrecv || f == mpi_func->mpi_send_init;
}

bool is_recv_function(llvm::Function *f) {
  assert(f != nullptr);
  return f == mpi_func->mpi_recv || f == mpi_func->mpi_Irecv ||
         f == mpi_func->mpi_Sendrecv || f == mpi_func->mpi_recv_init;
}
