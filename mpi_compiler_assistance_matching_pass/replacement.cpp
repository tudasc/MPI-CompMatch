/*
 Copyright 2022 Tim Jammer

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

#include "replacement.h"
#include "analysis_results.h"
#include "conflict_detection.h"

#include "implementation_specific.h"
#include "mpi_functions.h"

#include "llvm/IR/IRBuilder.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "debug.h"

using namespace llvm;

void add_init(llvm::Module &M) {

  for (auto *u : mpi_func->mpi_init->users()) {
    if (auto *call = dyn_cast<CallBase>(u)) {
      // MPI_Init should not be in an invoke inst
      IRBuilder<> builder(call->getNextNode());
      builder.CreateCall(mpi_func->optimized.init);
    }
  }
}

void add_finalize(llvm::Module &M) {
  for (auto *u : mpi_func->mpi_finalize->users()) {
    if (auto *call = dyn_cast<CallBase>(u)) {
      IRBuilder<> builder(call);
      builder.CreateCall(mpi_func->optimized.finalize);
    }
  }
}

void replace_call(CallBase *call, Function *func) {

  assert(call->getFunctionType() == func->getFunctionType());

  IRBuilder<> builder(call->getContext());

  std::vector<Value *> args;

  for (unsigned int i = 0; i < call->getNumArgOperands(); ++i) {
    args.push_back(call->getArgOperand(i));
  }

  auto *new_call = builder.CreateCall(func, args);

  ReplaceInstWithInst(call, new_call);

  // call->replaceAllUsesWith(new_call);
  // call->eraseFromParent();
}

bool check_if_all_usages_of_ptr_are_lifetime(Value *ptr) {

  for (auto *u : ptr->users()) {
    if (auto *call = dyn_cast<CallBase>(u)) {
      if (!call->getCalledFunction()->isIntrinsic()) {
        return false;
      }

    } else {
      return false;
    }
  }
  return true;
}

std::vector<CallBase *> get_usage_of_request(CallBase *call) {

  // get the request
  unsigned int req_arg_pos = 6;

  assert(call->getCalledFunction() == mpi_func->mpi_send_init ||
         call->getCalledFunction() == mpi_func->mpi_recv_init);
  assert(call->getNumArgOperands() == 7);

  Value *req = call->getArgOperand(req_arg_pos);

  std::vector<CallBase *> calls_to_replace;

  for (auto *u : req->users()) {

    assert(u != nullptr);
    if (auto *call = dyn_cast<CallBase>(u)) {
      if (is_mpi_call(call)) {
        calls_to_replace.push_back(call);

      } else {
        errs() << "NOT SUPPORTED: Request is used in non MPI call:\n";
        u->dump();
        assert(false);
      }
    } else if (auto *cast = dyn_cast<BitCastInst>(u)) {
      assert(check_if_all_usages_of_ptr_are_lifetime(cast));

      // this is allowed:
      //%14 = bitcast %struct.ompi_request_t** %3 to i8*
      // call void @llvm.lifetime.start.p0i8(i64 8, i8* nonnull %14) #8

    } else {
      errs() << "NOT SUPPORTED: Request is used in non MPI call:\n";
      u->dump();
      assert(false);
    }
  }

  return calls_to_replace;
}

void replace_communication_calls(std::vector<CallBase *> init_send_calls,
                                 std::vector<CallBase *> init_recv_calls) {

  std::set<CallBase *> calls_to_replace;
  std::copy(init_send_calls.begin(), init_send_calls.end(),
            std::inserter(calls_to_replace, calls_to_replace.begin()));
  std::copy(init_recv_calls.begin(), init_recv_calls.end(),
            std::inserter(calls_to_replace, calls_to_replace.begin()));

  // first gather everythin to replace, than replace, otherwise we might break
  // some def use chains in between, e.g. e a free is used by send_init and revc
  // init

  for (auto *call : init_send_calls) {
    auto temp = get_usage_of_request(call);
    std::copy(temp.begin(), temp.end(),
              std::inserter(calls_to_replace, calls_to_replace.begin()));
  }

  for (auto *call : init_recv_calls) {
    auto temp = get_usage_of_request(call);
    std::copy(temp.begin(), temp.end(),
              std::inserter(calls_to_replace, calls_to_replace.begin()));
  }

  // do the actual replacement
  for (auto *call : calls_to_replace) {
    if (call->getCalledFunction() == mpi_func->mpi_wait) {
      replace_call(call, mpi_func->optimized.mpi_wait);
    } else if (call->getCalledFunction() == mpi_func->mpi_start) {
      replace_call(call, mpi_func->optimized.mpi_start);
    } else if (call->getCalledFunction() == mpi_func->mpi_request_free) {
      replace_call(call, mpi_func->optimized.mpi_request_free);
    } else if (call->getCalledFunction() == mpi_func->mpi_send_init) {
      replace_call(call, mpi_func->optimized.mpi_send_init);
    } else if (call->getCalledFunction() == mpi_func->mpi_recv_init) {
      replace_call(call, mpi_func->optimized.mpi_recv_init);
    } else {

      errs() << "This MPI call is currently NOT supported\n";
      call->dump();

      assert(false);
    }
  }
}
