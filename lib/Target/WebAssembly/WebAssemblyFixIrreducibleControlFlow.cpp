//=- WebAssemblyFixIrreducibleControlFlow.cpp - Fix irreducible control flow -//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
///
/// \file
/// This file implements a pass that transforms irreducible control flow
/// into reducible control flow. Irreducible control flow means multiple-entry
/// loops; they appear as CFG cycles that are not recorded in MachineLoopInfo
/// due to being unnatural.
///
/// Note that LLVM has a generic pass that lowers irreducible control flow, but
/// it linearizes control flow, turning diamonds into two triangles, which is
/// both unnecessary and undesirable for WebAssembly.
///
/// TODO: The transformation implemented here handles all irreducible control
/// flow, without exponential code-size expansion, though it does so by creating
/// inefficient code in many cases. Ideally, we should add other
/// transformations, including code-duplicating cases, which can be more
/// efficient in common cases, and they can fall back to this conservative
/// implementation as needed.
///
//===----------------------------------------------------------------------===//

#include <unordered_map>
#include <unordered_set>

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
class WebAssemblyFixIrreducibleControlFlow final : public MachineFunctionPass {
  StringRef getPassName() const override {
    return "WebAssembly Fix Irreducible Control Flow";
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesCFG();
    AU.addRequired<MachineDominatorTree>();
    AU.addPreserved<MachineDominatorTree>();
    AU.addRequired<MachineLoopInfo>();
    AU.addPreserved<MachineLoopInfo>();
    MachineFunctionPass::getAnalysisUsage(AU);
  }

  bool runOnMachineFunction(MachineFunction &MF) override;

  bool VisitLoop(MachineFunction &MF, MachineLoopInfo &MLI, MachineLoop *Loop);

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

bool WebAssemblyFixIrreducibleControlFlow::VisitLoop(MachineFunction &MF,
                                                     MachineLoopInfo &MLI,
                                                     MachineLoop *Loop) {
  MachineBasicBlock *Header = Loop ? Loop->getHeader() : &*MF.begin();

  // Ignoring natural loops (seeing them monolithically), and in a loop ignoring the header,
  // we find all the blocks which can return to themselves, and the blocks which
  // cannot. Loopers reachable from the non-loopers are loop entries: if there is
  // 1, it is a natural loop; otherwise, we have irreducible control flow.

  std::set<MachineBasicBlock *> LoopBlocks;
  if (Loop) {
    for (auto *MBB : Loop->getBlocks()) {
      LoopBlocks.insert(MBB);
    }
  } else {
    for (auto &MBB : MF) {
      LoopBlocks.insert(&MBB);
    }
  }

  auto LoopDepth = Loop ? Loop->getLoopDepth() : 0;

  // Get a canonical block to represent a block or a loop: the block,
  // or if in a loop, the loop header.
  // XXX stop looking past loop exits..?
  auto Canonicalize = [&](MachineBasicBlock *MBB) -> MachineBasicBlock * {
    MachineLoop *InnerLoop = MLI.getLoopFor(MBB);
    if (InnerLoop == Loop) {
      return MBB;
    } else {
      // This is either in an outer or an inner loop, and not in ours.
      if (!LoopBlocks.count(MBB)) {
        // It's in outer code, ignore it.
        return nullptr;
      }
      assert(InnerLoop);
// We just care about immediate inner loops, not children of them.
//if (InnerLoop->getLoopDepth() != LoopDepth + 1) {
//  return nullptr;
//}
      // It's in an inner loop, canonicalize it to the header of that loop.
      return InnerLoop->getHeader();
    }
  };

  auto CanonicalizeSuccessor = [&](MachineBasicBlock *MBB) -> MachineBasicBlock * {
    if (Loop && MBB == Loop->getHeader()) {
      // Ignore branches going to the loop's natural header.
      return nullptr;
    }
    return Canonicalize(MBB);
  };

  // Compute which (canonicalized) blocks each block can reach.
  std::unordered_map<MachineBasicBlock *, std::unordered_set<MachineBasicBlock *>> Reachable;

  // The worklist contains pairs of recent additions, (a, b), where we just added
  // a link a => b.
  typedef std::pair<MachineBasicBlock *, MachineBasicBlock *> BlockPair;
  std::vector<BlockPair> WorkList;

  auto MaybeInsert = [&](MachineBasicBlock *MBB, MachineBasicBlock *Succ) {
    assert(MBB == Canonicalize(MBB));
    if (!Succ) return;
    Succ = CanonicalizeSuccessor(Succ);
    if (!Succ) return;
    if (Reachable[MBB].insert(Succ).second) {
      WorkList.push_back(BlockPair(MBB, Succ));
    }
  };

  for (auto *MBB : LoopBlocks) {
    MachineLoop *InnerLoop = MLI.getLoopFor(MBB);

    if (InnerLoop == Loop) {
      for (auto *Succ : MBB->successors()) {
        MaybeInsert(MBB, Succ);
      }
    } else {
      // It can't be in an outer loop - we loop on LoopBlocks - and so it must be
      // an inner loop.
      assert(InnerLoop);
      if (Canonicalize(MBB) != MBB) {
        continue;
      }
      // We canonicalize it to the header of that loop, so ignore if it isn't that.
// remove this vvv, redundant now FIXME
      if (MBB != InnerLoop->getHeader()) {
        continue;
      }
      // The successors are those of the loop.
      SmallVector<MachineBasicBlock *, 2> ExitBlocks;
      InnerLoop->getExitBlocks(ExitBlocks);
      for (auto *Succ : ExitBlocks) {
        MaybeInsert(MBB, Succ);
      }
    }
  }

  while (!WorkList.empty()) {
    BlockPair Pair = WorkList.back();
    WorkList.pop_back();
    auto *MBB = Pair.first;
    auto *Succ = Pair.second;
    assert(MBB);
    assert(Succ);
    // We recently added MBB => Succ, and that means we may have enabled
    // Pred => MBB => Succ.
    assert(MBB == Canonicalize(MBB));
    // If we don't care about MBB as a successor, there's nothing to do.
    if (!CanonicalizeSuccessor(MBB)) continue;
    // This is correct for both a block and a block representing a loop, as
    // the loop is natural and so the predecessors are all predecessors of
    // the loop header, which is the block we have here.
    for (auto *Pred : MBB->predecessors()) {
      // Canonicalize, make sure it's relevant, and check it's not the
      // same block (an update to the block itself doesn't help compute
      // that same block).
      Pred = Canonicalize(Pred);
      if (Pred && Pred != MBB) {
        MaybeInsert(Pred, Succ);
      }
    }
  }

  SetVector<MachineBasicBlock *> Loopers;
  for (auto MBB : LoopBlocks) {
    if (Reachable[MBB].count(MBB)) {
      Loopers.insert(MBB);
    }
  }

  // The header cannot be a looper. At the toplevel, LLVM does not allow the entry to be
  // in a loop, and in a natural loop we should ignore the header.
  assert(Loopers.count(Header) == 0);

  SmallPtrSet<MachineBasicBlock *, 4> Entries;
  SmallVector<MachineBasicBlock *, 4> SortedEntries;
  for (auto *Looper : Loopers) {
    for (auto *Pred : Looper->predecessors()) {
      Pred = Canonicalize(Pred);
      if (Pred && !Loopers.count(Pred)) {
        Entries.insert(Looper);
        SortedEntries.push_back(Looper);
        break;
      }
    }
  }

  std::sort(SortedEntries.begin(), SortedEntries.end(), [&](const MachineBasicBlock *A, const MachineBasicBlock *B) {
    auto ANum = A->getNumber();
    auto BNum = B->getNumber();
    assert(ANum != -1 && BNum != -1);
    assert(ANum != BNum);
    return ANum < BNum;
  });

  if (Entries.size() <= 1) return false;

  // Create a dispatch block which will
  // contain a jump table to any block in the problematic set of blocks.
  MachineBasicBlock *Dispatch = MF.CreateMachineBasicBlock();
  MF.insert(MF.end(), Dispatch);
  MLI.changeLoopFor(Dispatch, Loop);

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
  for (auto *MBB : SortedEntries) {
    auto Pair = Indices.insert(std::make_pair(MBB, 0));
    if (!Pair.second)
      continue;

    unsigned Index = MIB.getInstr()->getNumExplicitOperands() - 1;
    Pair.first->second = Index;

    MIB.addMBB(MBB);
    Dispatch->addSuccessor(MBB);
  }

  // Rewrite the problematic successors for every that wants to reach the
  // bad blocks. For simplicity, we just introduce a new block for every
  // edge we need to rewrite. (Fancier things are possible.)

  SetVector<MachineBasicBlock *> AllPreds;
  for (auto *MBB : SortedEntries) {
    for (auto *Pred : MBB->predecessors()) {
      if (Pred != Dispatch) {
        AllPreds.insert(Pred);
      }
    }
  }

  for (MachineBasicBlock *MBB : AllPreds) {
    DenseMap<MachineBasicBlock *, MachineBasicBlock *> Map;
    for (auto *Succ : MBB->successors()) {
      if (!Entries.count(Succ)) {
        continue;
      }

      // This is a successor we need to rewrite.
      MachineBasicBlock *Split = MF.CreateMachineBasicBlock();
      MF.insert(MBB->isLayoutSuccessor(Succ) ? MachineFunction::iterator(Succ)
                                             : MF.end(),
                Split);
      MLI.changeLoopFor(Split, Loop);

      // Set the jump table's register of the index of the block we wish to
      // jump to, and jump to the jump table.
      BuildMI(*Split, Split->end(), DebugLoc(), TII.get(WebAssembly::CONST_I32),
              Reg)
          .addImm(Indices[Succ]);
      BuildMI(*Split, Split->end(), DebugLoc(), TII.get(WebAssembly::BR))
          .addMBB(Dispatch);
      Split->addSuccessor(Dispatch);
      Map[Succ] = Split;
    }
    // Remap the terminator operands and the successor list.
    for (MachineInstr &Term : MBB->terminators())
      for (auto &Op : Term.explicit_uses())
        if (Op.isMBB() && Indices.count(Op.getMBB()))
          Op.setMBB(Map[Op.getMBB()]);
    for (auto Rewrite : Map)
      MBB->replaceSuccessor(Rewrite.first, Rewrite.second);
  }

  // Create a fake default label, because br_table requires one.
  MIB.addMBB(MIB.getInstr()
                 ->getOperand(MIB.getInstr()->getNumExplicitOperands() - 1)
                 .getMBB());

  return true;
}

bool WebAssemblyFixIrreducibleControlFlow::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Fixing Irreducible Control Flow **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  auto &MLI = getAnalysis<MachineLoopInfo>();

  // When we modify something, bail out and recompute MLI, then start again. Multiple
  // iterations may be necessary for particularly pesky irreducible control flow.

  auto Iteration = [&]() {

    auto DoVisitLoop = [&](MachineFunction&MF, MachineLoopInfo& MLI, MachineLoop *Loop) {
      if (VisitLoop(MF, MLI, Loop)) {
        // We rewrote part of the function; recompute MLI and start again.
    MF.RenumberBlocks();
    getAnalysis<MachineDominatorTree>().runOnMachineFunction(MF);
    MLI.runOnMachineFunction(MF);
  //MF.dump();
        return Changed = true;
      }
      return false;
    };
    // Visit the function body, which is identified as a null loop.
    if (DoVisitLoop(MF, MLI, nullptr)) return true;

    // Visit all the loops.
    SmallVector<MachineLoop *, 8> Worklist(MLI.begin(), MLI.end());
    while (!Worklist.empty()) {
      MachineLoop *Loop = Worklist.pop_back_val();
      Worklist.append(Loop->begin(), Loop->end());
      if (DoVisitLoop(MF, MLI, Loop)) return true;
    }

    return false;
  };

  while (Iteration()) {}

  // If we made any changes, completely recompute everything.
  if (LLVM_UNLIKELY(Changed)) {
    LLVM_DEBUG(dbgs() << "Recomputing dominators and loops.\n");
    MF.getRegInfo().invalidateLiveness();
    MF.RenumberBlocks();
    getAnalysis<MachineDominatorTree>().runOnMachineFunction(MF);
    MLI.runOnMachineFunction(MF);
  }

  return Changed;
}
