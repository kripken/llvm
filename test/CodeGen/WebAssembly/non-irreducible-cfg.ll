; RUN: llc < %s -O0 -asm-verbose=false -verify-machineinstrs -disable-block-placement -wasm-disable-explicit-locals -wasm-keep-registers | FileCheck %s

; Test irreducible CFG handling.

target datalayout = "e-m:e-p:32:32-i64:64-n32:64-S128"
target triple = "wasm32-unknown-unknown"

; A simple sequence of loops with blocks in between, that should not be
; misinterpreted as irreducible control flow.

; CHECK-NOT: br_table
define hidden i32 @_Z15fannkuch_workerPv(i8* %_arg) #0 {
for.cond:                                         ; preds = %entry
  br label %do.body

do.body:                                          ; preds = %do.cond, %for.cond
  br label %for.cond1

for.cond1:                                        ; preds = %for.body, %do.body
  br i1 1, label %for.cond1, label %for.end

for.end:                                          ; preds = %for.cond1
  br label %do.cond

do.cond:                                          ; preds = %for.end
  br i1 1, label %do.body, label %do.end

do.end:                                           ; preds = %do.cond
  br label %for.cond2

for.cond2:                                        ; preds = %for.end6, %do.end
  br label %for.cond3

for.cond3:                                        ; preds = %for.body5, %for.cond2
  br i1 1, label %for.cond3, label %for.end6

for.end6:                                         ; preds = %for.cond3
  br label %for.cond2

return:                                           ; No predecessors!
  ret i32 1
}

