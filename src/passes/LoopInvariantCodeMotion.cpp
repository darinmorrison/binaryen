/*
 * Copyright 2018 WebAssembly Community Group participants
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

//
// Simple loop invariant code motion (licm): for every none-typed
// expression in a loop, see if it conflicts with the body of the
// loop minus itself. If not, it can be hoisted out.
//
// Flattening is not necessary here, but may help (as separating
// out expressions may allow hoisting at least part of a larger whole).
//
// TODO: Loops may have "tails" - code at the end that cannot actually
//       branch back to the loop top. We should ignore invalidations
//       with that (and can ignore hoisting it too).
//
// TODO: Even things with side effects can be hoisted out of a loop -
//       if something would trap, and can otherwise be hoisted, hoisting
//       it just means the trap happens earlier, which is fine.
//
// TODO: This is O(N^2) now, which we can fix with an Effect analyzer
//       which can add and subtract.
//
// TODO: Multiple passes? A single loop may in theory allow hoisting of
//       X after Y is hoisted, and we may want to hoist A out of one
//       loop, then another.
//

#include <unordered_map>

#include "wasm.h"
#include "pass.h"
#include "wasm-builder.h"
#include "ir/effects.h"

namespace wasm {

struct LoopInvariantCodeMotion : public WalkerPass<ControlFlowWalker<LoopInvariantCodeMotion, UnifiedExpressionVisitor<LoopInvariantCodeMotion>>> {
  bool isFunctionParallel() override { return true; }

  Pass* create() override { return new LoopInvariantCodeMotion; }

  // Maps each loop to code we have managed to hoist out of it.
  std::unordered_map<Loop*, std::vector<Expression*>> hoisted;

  void visitExpression(Expression* curr) {
    // Don't try to hoist loops themselves out of loops, keep the general structure.
    if (auto* loop = curr->dynCast<Loop>()) {
      auto& currHoisted = hoisted[loop];
      if (!currHoisted.empty()) {
        // Finish the hoisting by emitting the code outside
        Builder builder(*getModule());
        auto* ret = builder.makeBlock(currHoisted);
        ret->list.push_back(loop);
        ret->finalize(loop->type);
        replaceCurrent(ret);
      }
    } else if (curr->type == none) {
      // This has the right type to try to hoist it. See if there is a loop above.
      Loop* loop = nullptr;
      auto i = controlFlowStack().size();
      if (i == 0) return;
      i--;
      while (1) {
        if (loop = controlFlowStack[i]->dynCast<Loop>()) {
          break;
        }
        if (i == 0) {
          break;
        }
        i--;
      }
      if (!loop) return;
      // There is a loop, check the effects of curr versus the loop
      // without curr.
      EffectAnalyzer myEffects(curr);
      auto* currp = getCurrentPointer();
      Nop nop;
      *currp = &nop;
      EffectAnalyzer loopEffects(loop);
      if (loopEffects.invalidates(myEffects)) {
        // We can't do it, undo.
        *currp = curr;
        return;
      }
      // We can do it!
      hoisted[loop].push_back(curr);
      // Allocate a proper nop
      *currp = Builder(*getModule()).makeNop();
    }
  }
};

Pass *createLoopInvariantCodeMotionPass() {
  return new LoopInvariantCodeMotion();
}

} // namespace wasm
