//= Nios2InstPrinter.h - Convert Nios2 MCInst to assembly syntax -*- C++ -*-==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints a Nios2 MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_NIOS2_INSTPRINTER_NIOS2INSTPRINTER_H
#define LLVM_LIB_TARGET_NIOS2_INSTPRINTER_NIOS2INSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class Nios2InstPrinter : public MCInstPrinter {
public:
  Nios2InstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                   const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  void printRegName(raw_ostream &OS, unsigned RegNo) const override;
  void printInst(const MCInst *MI, raw_ostream &O, StringRef Annot,
                 const MCSubtargetInfo &STI) override;

  // Autogenerated by tblgen.
  void printInstruction(const MCInst *MI, const MCSubtargetInfo &STI,
                        raw_ostream &O);
  static const char *getRegisterName(unsigned RegNo);

  bool printAliasInstr(const MCInst *MI, const MCSubtargetInfo &STI,
                       raw_ostream &O);

  void printCustomAliasOperand(const MCInst *MI, unsigned OpIdx,
                               unsigned PrintMethodIdx,
                               const MCSubtargetInfo &STI, raw_ostream &O);
  void printOperand(const MCInst *MI, int opNum, const MCSubtargetInfo &STI,
                    raw_ostream &OS);
  void printMemOperand(const MCInst *MI, int opNum, const MCSubtargetInfo &STI,
                       raw_ostream &OS, const char *Modifier = nullptr);
};
} // end namespace llvm

#endif
