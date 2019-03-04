//=- WebAssemblyFixIrreducibleControlFlow.cpp - Fix irreducible control flow -//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that removes irreducible control flow. Irreducible
/// control flow means multiple-entry loops, which this pass transforms to have a
/// single entry.
///
/// Note that LLVM has a generic pass that lowers irreducible control flow, but
/// it linearizes control flow, turning diamonds into two triangles, which is
/// both unnecessary and undesirable for WebAssembly.
///
/// The big picture: We recursively process each "region",
/// defined as a group of blocks with a single entry and no branches back to that entry.
/// A region may be the entire function body, or the inner part of a loop, i.e.,
/// the loop's body without branches back to the loop entry. In each region we fix
/// up multi-entry loops by
/// adding a new block that can dispatch to each of the loop entries, based on the
/// value of a label "helper" variable, and we replace direct branches to the
/// entries with assignments to the label variable and a branch to the dispatch
/// block. Then the dispatch block is the single entry in the loop containing the
/// previous multiple entries.
/// After ensuring all the loops in a region are reducible, we recurse into them.
/// The total time complexity of this pass is:
///   O(NumBlocks * NumNestedLoops * NumIrreducibleLoops +
///     NumLoops * NumLoops)
///
/// This pass is similar to what the Relooper [1] does. Both identify looping code
/// that requires multiple entries, and resolve it in a similar way (in
/// Relooper terminology, we implement a Multiple shape in a Loop shape). Note
/// also that like the Relooper, we implement a "minimal" intervention: we only
/// use the "label" helper for the blocks we absolutely must and no others. We
/// also prioritize code size and do not
/// duplicate code in order to resolve irreducibility. The graph algorithms for
/// finding loops and entries and so forth are also similar to the Relooper.
/// The main differences between this pass and the Relooper are:
///  * We just care about irreducibility, so we just look at loops.
///  * The Relooper emits structured control flow (with ifs etc.), while we
///    emit a CFG.
///
/// [1] Alon Zakai. 2011. Emscripten: an LLVM-to-JavaScript compiler. In
/// Proceedings of the ACM international conference companion on Object oriented
/// programming systems languages and applications companion (SPLASH '11). ACM,
/// New York, NY, USA, 301-312. DOI=10.1145/2048147.2048224
/// http://doi.acm.org/10.1145/2048147.2048224
///
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/WebAssemblyMCTargetDesc.h"
#include "WebAssembly.h"
#include "WebAssemblyMachineFunctionInfo.h"
#include "WebAssemblySubtarget.h"
#include "llvm/ADT/PriorityQueue.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/Passes.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "wasm-fix-irreducible-control-flow"

namespace {

using BlockVector = SmallVector<MachineBasicBlock *, 4>;
using BlockSet = SmallPtrSet<MachineBasicBlock *, 4>;

// Calculates reachability in a region. Ignores branches to blocks outside of
// the region, and ignores branches to the region entry (for the case where
// the region is the inner part of a loop).
class ReachabilityGraph {
public:
  ReachabilityGraph(MachineBasicBlock *Entry, const BlockSet &Blocks)
    : Entry(Entry), Blocks(Blocks) {
#ifndef NDEBUG
    // The region must have a single entry.
    for (auto *MBB : Blocks) {
      if (MBB != Entry) {
        for (auto *Pred : MBB->predecessors()) {
          assert(inRegion(Pred));
        }
      }
    }
#endif
    calculate();
  }

  bool canReach(MachineBasicBlock *From, MachineBasicBlock *To) {
    assert(inRegion(From) && inRegion(To));
    return Reachable[From].count(To);
  }

  // Get all blocks that are in a loop.
  const BlockSet &getLoopers() { return Loopers; }

  // Get all blocks that are loop entries.
  const BlockSet &getLoopEntries() { return LoopEntries; }

  // Get all blocks that enter a particular loop from outside.
  const BlockSet &getLoopEnterers(MachineBasicBlock *LoopEntry) {
    assert(inRegion(LoopEntry));
    return LoopEnterers[LoopEntry];
  }

private:
  MachineBasicBlock *Entry;
  const BlockSet &Blocks;

  BlockSet Loopers, LoopEntries;
  DenseMap<MachineBasicBlock *, BlockSet> LoopEnterers;

  bool inRegion(MachineBasicBlock *MBB) {
    return Blocks.count(MBB);
  }

  // Maps a block to all the other blocks it can reach.
  DenseMap<MachineBasicBlock *, BlockSet> Reachable;

  void calculate() {
    // Reachability computation work list. Contains pairs of recent additions (A, B) where we just
    // added a link A => B.
    using BlockPair = std::pair<MachineBasicBlock *, MachineBasicBlock *>;
    SmallVector<BlockPair, 4> WorkList;

    // Add all relevant direct branches.
    for (auto *MBB : Blocks) {
      for (auto *Succ : MBB->successors()) {
        if (Succ != Entry && inRegion(Succ)) {
          Reachable[MBB].insert(Succ);
          WorkList.emplace_back(MBB, Succ);
        }
      }
    }

    while (!WorkList.empty()) {
      MachineBasicBlock *MBB;
      MachineBasicBlock *Succ;
      std::tie(MBB, Succ) = WorkList.pop_back_val();
      assert(inRegion(MBB) && Succ != Entry && inRegion(Succ));
      if (MBB != Entry) {
        // We recently added MBB => Succ, and that means we may have enabled
        // Pred => MBB => Succ.
        for (auto *Pred : MBB->predecessors()) {
          if (Reachable[Pred].insert(Succ).second) {
            WorkList.emplace_back(Pred, Succ);
          }
        }
      }
    }

    // Blocks that can return to themselves are in a loop.
    for (auto *MBB : Blocks) {
      if (canReach(MBB, MBB)) {
        Loopers.insert(MBB);
      }
    }
    assert(!Loopers.count(Entry));

    // Find the loop entries - loopers reachable from non-loopers - and their
    // loop enterers.
    for (auto *Looper : Loopers) {
      for (auto *Pred : Looper->predecessors()) {
// Looper != Entry, and so precessors must be in the loop, and so in the region.
assert(inRegion(Pred)); // dupe of above
        if (!Loopers.count(Pred)) {
          LoopEntries.insert(Looper);
          LoopEnterers[Looper].insert(Pred);
        }
      }
    }
  }
};

// Finds the blocks in a single-entry loop, given the loop entry and the
// list of blocks that enter the loop.
class LoopBlocks {
public:
  LoopBlocks(MachineBasicBlock *Entry, const BlockSet &Enterers) :
    Entry(Entry), Enterers(Enterers) {
    calculate();
  }

  const BlockSet &getBlocks() { return Blocks; }

private:
  MachineBasicBlock *Entry;
  const BlockSet &Enterers;

  BlockSet Blocks;

  void calculate() {
    // Going backwards from the loop entry, if we ignore the blocks entering
    // from outside, we will traverse all the blocks in the loop.
    BlockSet WorkList;
    Blocks.insert(Entry);
    for (auto *Pred : Entry->predecessors()) {
      if (!Enterers.count(Pred)) {
        WorkList.insert(Pred);
      }
    }

    while (!WorkList.empty()) {
      auto *MBB = *WorkList.begin();
      WorkList.erase(MBB);
      assert(!Enterers.count(MBB));
      if (Blocks.insert(MBB).second) {
        for (auto *Pred : MBB->predecessors()) {
          WorkList.insert(Pred);
        }
      }
    }
  }
};

class LoopFixer {
public:
  LoopFixer(MachineFunction &MF) : MF(MF) {}

  // Run on the given input. Returns whether changes were made.
  bool run() {
    // Start the recursive process on the entire function body.
    BlockSet AllBlocks;
    for (auto &MBB : MF) {
      AllBlocks.insert(&MBB);
    }
    processRegion(&*MF.begin(), AllBlocks);
    return Changed;
  }

private:
  MachineFunction &MF;

  bool Changed = false;

  void processRegion(MachineBasicBlock *Entry, const BlockSet &Blocks) {
    // Remove irreducibility before processing child loops, which may take
    // multiple iterations.
    do {
      ReachabilityGraph Graph(Entry, Blocks);

      bool FoundIrreducibility = false;

      for (auto *LoopEntry : Graph.getLoopEntries()) {
        // Find mutual entries - other entries which can reach this one, and are
        // reached by it. Such mutual entries must be in the same loop, and so indicate
        // irreducible control flow.
        BlockSet MutualLoopEntries;
        for (auto *OtherLoopEntry : Graph.getLoopEntries()) {
          if (Graph.canReach(LoopEntry, OtherLoopEntry) &&
              Graph.canReach(OtherLoopEntry, LoopEntry)) {
            MutualLoopEntries.insert(OtherLoopEntry);
          }
        }

        if (!MutualLoopEntries.empty()) {
          auto AllLoopEntries = std::move(MutualLoopEntries);
          AllLoopEntries.insert(LoopEntry);
          makeSingleEntryLoop(AllLoopEntries);
          FoundIrreducibility = true;
          break;
        }
      }

      // Only go on to actually process the inner loops when we are done removing
      // irreducible control flow and changing the graph.
      if (FoundIrreducibility) {
        continue;
      }

      for (auto *LoopEntry : Graph.getLoopEntries()) {
        LoopBlocks InnerBlocks(LoopEntry, Graph.getLoopEnterers(LoopEntry));
        // Each of these calls to processRegion may change the graph, but are guaranteed not
        // to interfere with each other. The only changes we make to the graph
        // are to add blocks on the way to a loop entry. As the loops are disjoint,
        // that means we may only alter branches exiting another loop,
        // which are ignored when recursing into that other loop anyhow.
        processRegion(LoopEntry, InnerBlocks.getBlocks());
      }
    } while (false);
  }

  // Given a set of entries to a single loop, create a single entry for that
  // loop by creating a dispatch block for them, routing control flow using
  // a helper variable.
  void makeSingleEntryLoop(BlockSet& Entries) {
    assert(Entries.size() >= 2);

    // Sort the entries to ensure a deterministic build.
    BlockVector SortedEntries;
    for (auto *Entry : Entries) {
      SortedEntries.push_back(Entry);
    }
    llvm::sort(SortedEntries,
               [&](const MachineBasicBlock *A, const MachineBasicBlock *B) {
                 auto ANum = A->getNumber();
                 auto BNum = B->getNumber();
                 return ANum < BNum;
               });

#ifndef NDEBUG
    for (auto Block : SortedEntries)
      assert(Block->getNumber() != -1);
    if (SortedEntries.size() > 1) {
      for (auto I = SortedEntries.begin(), E = SortedEntries.end() - 1;
           I != E; ++I) {
        auto ANum = (*I)->getNumber();
        auto BNum = (*(std::next(I)))->getNumber();
        assert(ANum != BNum);
      }
    }
#endif

    // Create a dispatch block which will contain a jump table to the entries.
    MachineBasicBlock *Dispatch = MF.CreateMachineBasicBlock();
    MF.insert(MF.end(), Dispatch);

    // Add the jump table.
    const auto &TII = *MF.getSubtarget<WebAssemblySubtarget>().getInstrInfo();
    MachineInstrBuilder MIB = BuildMI(*Dispatch, Dispatch->end(), DebugLoc(),
                                      TII.get(WebAssembly::BR_TABLE_I32));

    // Add the register which will be used to tell the jump table which block to
    // jump to.
    MachineRegisterInfo &MRI = MF.getRegInfo();
    unsigned Reg = MRI.createVirtualRegister(&WebAssembly::I32RegClass);
    MIB.addReg(Reg);

    // Compute the indices in the superheader, one for each bad block, and
    // add them as successors.
    DenseMap<MachineBasicBlock *, unsigned> Indices;
    for (auto *Entry : SortedEntries) {
      auto Pair = Indices.insert(std::make_pair(Entry, 0));
      if (!Pair.second) {
        continue;
      }

      unsigned Index = MIB.getInstr()->getNumExplicitOperands() - 1;
      Pair.first->second = Index;

      MIB.addMBB(Entry);
      Dispatch->addSuccessor(Entry);
    }

    // Rewrite the problematic successors for every block that wants to reach the
    // bad blocks. For simplicity, we just introduce a new block for every edge
    // we need to rewrite. (Fancier things are possible.)

    BlockVector AllPreds;
    for (auto *Entry : SortedEntries) {
      for (auto *Pred : Entry->predecessors()) {
        if (Pred != Dispatch) {
          AllPreds.push_back(Pred);
        }
      }
    }

    for (MachineBasicBlock *Pred : AllPreds) {
      DenseMap<MachineBasicBlock *, MachineBasicBlock *> Map;
      for (auto *Entry : Pred->successors()) {
        if (!Entries.count(Entry)) {
          continue;
        }

        // This is a successor we need to rewrite.
        MachineBasicBlock *Split = MF.CreateMachineBasicBlock();
        MF.insert(Pred->isLayoutSuccessor(Entry) ? MachineFunction::iterator(Entry)
                                               : MF.end(),
                  Split);

        // Set the jump table's register of the index of the block we wish to
        // jump to, and jump to the jump table.
        BuildMI(*Split, Split->end(), DebugLoc(), TII.get(WebAssembly::CONST_I32),
                Reg)
            .addImm(Indices[Entry]);
        BuildMI(*Split, Split->end(), DebugLoc(), TII.get(WebAssembly::BR))
            .addMBB(Dispatch);
        Split->addSuccessor(Dispatch);
        Map[Entry] = Split;
      }
      // Remap the terminator operands and the successor list.
      for (MachineInstr &Term : Pred->terminators())
        for (auto &Op : Term.explicit_uses())
          if (Op.isMBB() && Indices.count(Op.getMBB()))
            Op.setMBB(Map[Op.getMBB()]);
      for (auto Rewrite : Map)
        Pred->replaceSuccessor(Rewrite.first, Rewrite.second);
    }

    // Create a fake default label, because br_table requires one.
    MIB.addMBB(MIB.getInstr()
                   ->getOperand(MIB.getInstr()->getNumExplicitOperands() - 1)
                   .getMBB());

    Changed = true;
  }
};

class WebAssemblyFixIrreducibleControlFlow final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Fix Irreducible Control Flow";
  }

#if 0 // needed?
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addPreserved<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }
#endif

  bool runOnMachineFunction(MachineFunction &MF) override;

public:
  static char ID; // Pass identification, replacement for typeid
  WebAssemblyFixIrreducibleControlFlow() : MachineFunctionPass(ID) {}
};

} // end anonymous namespace

char WebAssemblyFixIrreducibleControlFlow::ID = 0;
INITIALIZE_PASS(WebAssemblyFixIrreducibleControlFlow, DEBUG_TYPE,
                "Removes irreducible control flow", false, false)

FunctionPass *llvm::createWebAssemblyFixIrreducibleControlFlow() {
  return new WebAssemblyFixIrreducibleControlFlow();
}

bool WebAssemblyFixIrreducibleControlFlow::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Fixing Irreducible Control Flow **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  if (LLVM_UNLIKELY(LoopFixer(MF).run())) {
    // We rewrote part of the function; recompute relevant things.
    MF.getRegInfo().invalidateLiveness();
    MF.RenumberBlocks();
#if 0 // XXX?
    getAnalysis<MachineDominatorTree>().runOnMachineFunction(MF);
#endif
    return true;
  }

  return false;
}

