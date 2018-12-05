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
#if 0
        if (!Visited.insert(Next).second) {
          OnStack.erase(Next);
          continue;
        }
#endif
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

  SetVector<MachineBasicBlock *> BadBlocks;
errs() << "we are considering " << MF.getFunction().getName() << '\n';
if (Loop) errs() << "  a loop of size " << Loop->getBlocks().size() << '\n';

  // DFS through Loop's body, looking for irreducible control flow. Loop is
  // natural, and we stay in its body, and we treat any nested loops
  // monolithically, so any cycles we encounter indicate irreducibility.
  // Note all the blocks that are in such an irreducible position.
  SmallPtrSet<MachineBasicBlock *, 8> OnStack;
  SmallPtrSet<MachineBasicBlock *, 8> Visited;
  SmallVector<SuccessorList, 4> LoopWorklist;
  LoopWorklist.push_back(SuccessorList(Header));
  OnStack.insert(Header);
  Visited.insert(Header);
  while (!LoopWorklist.empty()) {
    SuccessorList &Top = LoopWorklist.back();
errs() << "top: " << Top.getBlock()->getName() << '\n';
    if (Top.HasNext()) {
      MachineBasicBlock *Next = Top.Next();
errs() << "next: " << Next->getName() << '\n';
      if (Next == Header || (Loop && !Loop->contains(Next)))
        continue;
      if (LLVM_LIKELY(OnStack.insert(Next).second)) {
errs() << "add to stack, and add to queue\n";
#if 0
        if (!Visited.insert(Next).second) {
          OnStack.erase(Next);
          continue;
        }
#endif
        MachineLoop *InnerLoop = MLI.getLoopFor(Next);
        if (InnerLoop != Loop)
          LoopWorklist.push_back(SuccessorList(InnerLoop));
        else
          LoopWorklist.push_back(SuccessorList(Next));
      } else {
errs() << "baaad " << Next->getName() << '\n';
        BadBlocks.insert(Next);
      }
      continue;
    }
errs() << "drop from stack to stack\n";
    OnStack.erase(Top.getBlock());
    LoopWorklist.pop_back();
  }

  // Most likely, we didn't find any irreducible control flow.
  if (LLVM_LIKELY(BadBlocks.empty()))
    return false;

errs() << "we say irreduyciuble! " << MF.getFunction().getName() << " with " << BadBlocks.size() << '\n';

// Create a new "superheader", which can direct control flow to any of the
// bad blocks we noted as being super-loop entries (that is, we really need
// a non-natural loop here that has multiple entries; in Relooper terms,
// a Loop with a Multiple). Any block which used to go to them
// must now go through the new superheader.





// Create a dispatch block which will
// contain a jump table to any block in the problematic set of blocks.
MachineBasicBlock *Dispatch = MF.CreateMachineBasicBlock();
MF.insert(MF.end(), Dispatch);
MLI.changeLoopFor(Dispatch, Loop); // XXX we may want to throw out MLI after every change, and recompute - not fast, but fast if no irreducible. or we should not use MLI entirely and be efficient.

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
for (auto *MBB : BadBlocks) {
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

DenseSet<MachineBasicBlock *> AllPreds;
for (auto *MBB : BadBlocks) {
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
    if (!BadBlocks.count(Succ))
      continue;

    // This is a successor we need to rewrite.
    MachineBasicBlock *Split = MF.CreateMachineBasicBlock();
    MF.insert(MBB->isLayoutSuccessor(Succ) ? MachineFunction::iterator(Succ)
                                           : MF.end(),
              Split);
    MLI.changeLoopFor(Split, Loop); // XXX

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

  // Visit the function body, which is identified as a null loop.
  Changed |= VisitLoop(MF, MLI, nullptr);

  // Visit all the loops.
  SmallVector<MachineLoop *, 8> Worklist(MLI.begin(), MLI.end());
  while (!Worklist.empty()) {
    MachineLoop *CurLoop = Worklist.pop_back_val();
    Worklist.append(CurLoop->begin(), CurLoop->end());
    Changed |= VisitLoop(MF, MLI, CurLoop);
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
