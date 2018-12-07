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

namespace {

/// A utility for walking the blocks of a loop, handling a nested inner
/// loop as a monolithic conceptual block.
class MetaBlock {
  MachineBasicBlock *Block;
  SmallVector<MachineBasicBlock *, 2> Preds;
  SmallVector<MachineBasicBlock *, 2> Succs;

public:
  explicit MetaBlock(MachineBasicBlock *MBB)
      : Block(MBB), Preds(MBB->pred_begin(), MBB->pred_end()),
        Succs(MBB->succ_begin(), MBB->succ_end()) {}

  explicit MetaBlock(MachineLoop *Loop) : Block(Loop->getHeader()) {
    Loop->getExitBlocks(Succs);
    for (MachineBasicBlock *Pred : Block->predecessors())
      if (!Loop->contains(Pred))
        Preds.push_back(Pred);
  }

  MachineBasicBlock *getBlock() const { return Block; }

  const SmallVectorImpl<MachineBasicBlock *> &predecessors() const {
    return Preds;
  }
  const SmallVectorImpl<MachineBasicBlock *> &successors() const {
    return Succs;
  }

  bool operator==(const MetaBlock &MBB) { return Block == MBB.Block; }
  bool operator!=(const MetaBlock &MBB) { return Block != MBB.Block; }
};

class SuccessorList final : public MetaBlock {
  size_t Index;
  size_t Num;

public:
  explicit SuccessorList(MachineBasicBlock *MBB)
      : MetaBlock(MBB), Index(0), Num(successors().size()) {}

  explicit SuccessorList(MachineLoop *Loop)
      : MetaBlock(Loop), Index(0), Num(successors().size()) {}

  bool HasNext() const { return Index != Num; }

  MachineBasicBlock *Next() {
    assert(HasNext());
    return successors()[Index++];
  }
};

} // end anonymous namespace

bool WebAssemblyFixIrreducibleControlFlow::VisitLoop(MachineFunction &MF,
                                                     MachineLoopInfo &MLI,
                                                     MachineLoop *Loop) {
  MachineBasicBlock *Header = Loop ? Loop->getHeader() : &*MF.begin();

if (getenv("DAN")) {
  SetVector<MachineBasicBlock *> RewriteSuccs;
errs() << "dan is considering " << MF.getFunction().getName() << '\n';

  // DFS through Loop's body, looking for irreducible control flow. Loop is
  // natural, and we stay in its body, and we treat any nested loops
  // monolithically, so any cycles we encounter indicate irreducibility.
  SmallPtrSet<MachineBasicBlock *, 8> OnStack;
  SmallPtrSet<MachineBasicBlock *, 8> Visited;
  SmallVector<SuccessorList, 4> LoopWorklist;
  LoopWorklist.push_back(SuccessorList(Header));
  OnStack.insert(Header);
  Visited.insert(Header);
  while (!LoopWorklist.empty()) {
    SuccessorList &Top = LoopWorklist.back();
    if (Top.HasNext()) {
      MachineBasicBlock *Next = Top.Next();
      if (Next == Header || (Loop && !Loop->contains(Next)))
        continue;
      if (LLVM_LIKELY(OnStack.insert(Next).second)) {
//#if 0
        if (!Visited.insert(Next).second) {
          OnStack.erase(Next);
          continue;
        }
//#endif
        MachineLoop *InnerLoop = MLI.getLoopFor(Next);
        if (InnerLoop != Loop)
          LoopWorklist.push_back(SuccessorList(InnerLoop));
        else
          LoopWorklist.push_back(SuccessorList(Next));
      } else {
        RewriteSuccs.insert(Top.getBlock());
      }
      continue;
    }
    OnStack.erase(Top.getBlock());
    LoopWorklist.pop_back();
  }

  // Most likely, we didn't find any irreducible control flow.
  if (LLVM_LIKELY(RewriteSuccs.empty()))
    return false;

errs() << "irreduyciuble! " << MF.getFunction().getName() << " with " << RewriteSuccs.size() << '\n';

  LLVM_DEBUG(dbgs() << "Irreducible control flow detected!\n");

  // Ok. We have irreducible control flow! Create a dispatch block which will
  // contains a jump table to any block in the problematic set of blocks.
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

  // Collect all the blocks which need to have their successors rewritten,
  // add the successors to the jump table, and remember their index.
  DenseMap<MachineBasicBlock *, unsigned> Indices;
  SmallVector<MachineBasicBlock *, 4> SuccWorklist(RewriteSuccs.begin(),
                                                   RewriteSuccs.end());
  while (!SuccWorklist.empty()) {
    MachineBasicBlock *MBB = SuccWorklist.pop_back_val();
    auto Pair = Indices.insert(std::make_pair(MBB, 0));
    if (!Pair.second)
      continue;

    unsigned Index = MIB.getInstr()->getNumExplicitOperands() - 1;
    LLVM_DEBUG(dbgs() << printMBBReference(*MBB) << " has index " << Index
                      << "\n");

    Pair.first->second = Index;
    for (auto Pred : MBB->predecessors()) // teh evil
      RewriteSuccs.insert(Pred);

    MIB.addMBB(MBB);
    Dispatch->addSuccessor(MBB);

    MetaBlock Meta(MBB);
    for (auto *Succ : Meta.successors())
      if (Succ != Header && (!Loop || Loop->contains(Succ)))
        SuccWorklist.push_back(Succ);
  }
errs() << "   tootal " << RewriteSuccs.size() << '\n';
  // Rewrite the problematic successors for every block in RewriteSuccs.
  // For simplicity, we just introduce a new block for every edge we need to
  // rewrite. Fancier things are possible.
  for (MachineBasicBlock *MBB : RewriteSuccs) {
    DenseMap<MachineBasicBlock *, MachineBasicBlock *> Map;
    for (auto *Succ : MBB->successors()) {
      if (!Indices.count(Succ))
        continue;

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

} else {

// An alternatie approahc

errs() << "we are considering " << MF.getFunction().getName() << " : " << Loop << '\n';

// TODO: iterations?

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

errs() << "  total blocks in this scope: " << LoopBlocks.size() << " with header bb." << Header->getNumber() << "." << Header->getName() << '\n';
for (auto *MBB : LoopBlocks) {
errs() << MBB->getNumber() << " (in loop " << MLI.getLoopFor(MBB) << ")\n";
}
errs() << '\n';
errs() << "  relevant blocks (not in an inner loop scope; note this does not include loops in our scope, the header of which is relevant):\n";
for (auto *MBB : LoopBlocks) {
  if (MLI.getLoopFor(MBB) == Loop) {
    //errs() << MBB->getNumber() << ' ';
  }
}
errs() << '\n';

#if 0
  auto Relevant = [&](MachineBasicBlock *MBB) {
    return MLI.getLoopFor(Succ) == Loop;
  };
#endif

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
      if (InnerLoop->getLoopDepth() != LoopDepth + 1) {
        return nullptr;
      }
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

  auto MaybeInsert = [&](std::unordered_set<MachineBasicBlock *>& Set, MachineBasicBlock *MBB) {
////errs() << "  1mayhb " << MBB << '\n';
    if (!MBB) return false;
    MBB = CanonicalizeSuccessor(MBB);
////errs() << "  2mayhb " << MBB << '\n';
    if (!MBB) return false;
errs() << "    actual addaddition of bb." << MBB->getNumber() << "." << MBB->getName() << '\n';
    return Set.insert(MBB).second;
  };

  // Compute which (canonicalized) blocks each block can reach.
  std::unordered_map<MachineBasicBlock *, std::unordered_set<MachineBasicBlock *>> Reachable;

  // The worklist contains items which have a successor with new things, each pair is
  //  (item to work on, the successor with new things)
  typedef std::pair<MachineBasicBlock *, MachineBasicBlock *> BlockPair;
  std::set<BlockPair> WorkList;

  auto AddPredecessors = [&](MachineBasicBlock *MBB) {
    assert(MBB == Canonicalize(MBB));
    // If we don't care about MBB as a successor, there's nothing to do.
    if (!CanonicalizeSuccessor(MBB)) return;
    // This is correct for both a block and a block representing a loop, as
    // the loop is natural and so the predecessors are all predecessors of
    // the loop header, which is the block we have here.
    for (auto *Pred : MBB->predecessors()) {
      // Canonicalize, make sure it's relevant, and check it's not the
      // same block (an update to the block itself doesn't help compute
      // that same block).
      Pred = Canonicalize(Pred);
      if (Pred && Pred != MBB) {
        WorkList.insert(BlockPair(Pred, MBB));
      }
    }
  };

  for (auto *MBB : LoopBlocks) {
    bool Added = false;
    MachineLoop *InnerLoop = MLI.getLoopFor(MBB);

errs() << "initial addition of bb." << MBB->getNumber() << " in inner " << InnerLoop << " : " << Loop << '\n';

    if (InnerLoop == Loop) {
      for (auto *Succ : MBB->successors()) {
errs() << "  maybe add " << Succ->getNumber() << '\n';
        Added |= MaybeInsert(Reachable[MBB], Succ);
      }
    } else {
      // It can't be in an outer loop - we loop on LoopBlocks - and so it must be
      // an inner loop.
if (!InnerLoop) {
  //errs() << "very bad " << Loop << " : " << InnerLoop << " : bb." << MBB->getNumber() << "." << MBB->getName() << '\n';
}
      assert(InnerLoop);
      if (Canonicalize(MBB) != MBB) {
        continue;
      }
      // We canonicalize it to the header of that loop, so ignore if it isn't that.
// remove this vvv, redundant now FIXME
      if (MBB != InnerLoop->getHeader()) {
errs() << "not header\n";
        continue;
      }
      // The successors are those of the loop.
      SmallVector<MachineBasicBlock *, 2> ExitBlocks;
      InnerLoop->getExitBlocks(ExitBlocks);
errs() << ExitBlocks.size() << " exits\n";
      for (auto *Succ : ExitBlocks) {
errs() << "  maybe inner add " << Succ->getNumber() << '\n';
        Added |= MaybeInsert(Reachable[MBB], Succ);
      }
    }
    // Finally, add the item to the work list, if we added anything.
    if (Added) {
      AddPredecessors(MBB);
    }
  }
errs() << "Relevant blocks for reachability: " << Reachable.size() << '\n';
  while (!WorkList.empty()) {
    BlockPair Pair = *WorkList.begin();
    WorkList.erase(WorkList.begin());
    auto *MBB = Pair.first;
    auto *Succ = Pair.second;
    assert(MBB);
errs() << "at " << MBB->getNumber() << " : " << Succ->getNumber() << '\n';
    assert(Succ != MBB);
//if (!Reachable[MBB].count(Succ)) continue;
assert(Reachable[MBB].count(Succ));
    SmallSet<MachineBasicBlock *, 4> ToAdd;
    assert(Succ);
    for (auto *Succ2 : Reachable[Succ]) {
      assert(Succ2);
      if (!Reachable[MBB].count(Succ2)) {
        ToAdd.insert(Succ2);
errs() << "  add " << MBB->getNumber() << " => " << Succ->getNumber() << " => " << Succ2->getNumber() << '\n';
      }
    }
    if (!ToAdd.empty()) {
      for (auto *Add : ToAdd) {
        Reachable[MBB].insert(Add);
      }
      AddPredecessors(MBB);
    }
  }
errs() << "Computed reachabilities\n";
for (auto& pair : Reachable) {
errs() << "bb." << pair.first->getNumber() << "." << pair.first->getName() << '\n';
  for (auto* S : pair.second) {
errs() << "  => bb." << S->getNumber() << "." << S->getName() << '\n';
  }
}

  SetVector<MachineBasicBlock *> Loopers;
  for (auto MBB : LoopBlocks) {
    if (Reachable[MBB].count(MBB)) {
      Loopers.insert(MBB);
    }
  }

errs() << "loopers: " << Loopers.size() << '\n';

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
errs() << "entries: " << Entries.size() << '\n';

  if (Entries.size() <= 1) return false;

for (auto* MBB : SortedEntries) {
errs() << " bad: bb." << MBB->getNumber() << "." << MBB->getName() << '\n';
}

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
errs() << "total preds to bads: " << AllPreds.size() << '\n';

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

}

bool WebAssemblyFixIrreducibleControlFlow::runOnMachineFunction(
    MachineFunction &MF) {
  LLVM_DEBUG(dbgs() << "********** Fixing Irreducible Control Flow **********\n"
                       "********** Function: "
                    << MF.getName() << '\n');

  bool Changed = false;
  auto &MLI = getAnalysis<MachineLoopInfo>();

if (getenv("DAN")) {
  // Visit the function body, which is identified as a null loop.
  Changed |= VisitLoop(MF, MLI, nullptr);

  // Visit all the loops.
  SmallVector<MachineLoop *, 8> Worklist(MLI.begin(), MLI.end());
  while (!Worklist.empty()) {
    MachineLoop *CurLoop = Worklist.pop_back_val();
    Worklist.append(CurLoop->begin(), CurLoop->end());
    Changed |= VisitLoop(MF, MLI, CurLoop);
  }
} else {

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
errs() << "Loops: " << Worklist.size() << '\n';
    while (!Worklist.empty()) {
      MachineLoop *Loop = Worklist.pop_back_val();
      Worklist.append(Loop->begin(), Loop->end());
      if (DoVisitLoop(MF, MLI, Loop)) return true;
    }

    return false;
  };

  while (Iteration()) {}
}

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
