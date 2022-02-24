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

#ifndef MACH_REPLACEMENT_H_
#define MACH_REPLACEMENT_H_

#include "llvm/IR/InstrTypes.h"

#include <vector>

// true if something changed
bool add_init(llvm::Module &M);
bool add_finalize(llvm::Module &M);

void replace_communication_calls(std::vector<llvm::CallBase *> init_send_calls,
                                 std::vector<llvm::CallBase *> init_recv_calls);

#endif /* MACH_REPLACEMENT_H_ */
