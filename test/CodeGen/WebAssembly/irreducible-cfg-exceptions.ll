; RUN: llc < %s -asm-verbose=false -verify-machineinstrs -disable-block-placement -wasm-disable-explicit-locals -wasm-keep-registers -enable-emscripten-cxx-exceptions | FileCheck %s

target datalayout = "e-m:e-p:32:32-i64:64-n32:64-S128"
target triple = "wasm32-unknown-unknown-wasm"

$crashy = comdat any

declare i32 @__gxx_personality_v0(...)

; Check an interesting case of complex control flow due to exceptions CFG rewriting.
; There should *not* be any irreducible control flow here.

; CHECK-LABEL: crashy:
; CHECK-NOT: br_table

; Function Attrs: minsize noinline optsize
define hidden void @crashy() unnamed_addr #0 comdat personality i8* bitcast (i32 (...)* @__gxx_personality_v0 to i8*) {
entry:
  invoke void undef() #1
          to label %invoke.cont unwind label %lpad

invoke.cont:                                      ; preds = %entry
  invoke void undef() #1
          to label %invoke.cont4 unwind label %lpad3

invoke.cont4:                                     ; preds = %invoke.cont
  %call.i82 = invoke i8* undef() #1
          to label %invoke.cont6 unwind label %lpad3

invoke.cont6:                                     ; preds = %invoke.cont4
  invoke void undef() #1
          to label %invoke.cont13 unwind label %lpad12

invoke.cont13:                                    ; preds = %invoke.cont6
  br label %for.cond

for.cond:                                         ; preds = %for.cond.backedge, %invoke.cont13
  br i1 undef, label %_ZNKSt3__219istreambuf_iteratorIcNS_11char_traitsIcEEE14__test_for_eofEv.exit.i.i, label %land.lhs.true.i.i.i

land.lhs.true.i.i.i:                              ; preds = %for.cond
  %call.i.i.i.i92 = invoke i32 undef() #1
          to label %_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i.i.i unwind label %lpad16.loopexit

_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i.i.i: ; preds = %land.lhs.true.i.i.i
  br label %_ZNKSt3__219istreambuf_iteratorIcNS_11char_traitsIcEEE14__test_for_eofEv.exit.i.i

_ZNKSt3__219istreambuf_iteratorIcNS_11char_traitsIcEEE14__test_for_eofEv.exit.i.i: ; preds = %_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i.i.i, %for.cond
  %call.i.i12.i.i93 = invoke i32 undef() #1
          to label %_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i19.i.i unwind label %lpad16.loopexit

_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i19.i.i: ; preds = %_ZNKSt3__219istreambuf_iteratorIcNS_11char_traitsIcEEE14__test_for_eofEv.exit.i.i
  invoke void undef() #1
          to label %invoke.cont23 unwind label %lpad22

invoke.cont23:                                    ; preds = %_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i19.i.i
  invoke void undef() #1
          to label %invoke.cont25 unwind label %lpad22

invoke.cont25:                                    ; preds = %invoke.cont23
  %call.i.i137 = invoke i32 undef() #1
          to label %invoke.cont29 unwind label %lpad16.loopexit

lpad:                                             ; preds = %entry
  %0 = landingpad { i8*, i32 }
          cleanup
  unreachable

lpad3:                                            ; preds = %invoke.cont4, %invoke.cont
  %1 = landingpad { i8*, i32 }
          cleanup
  unreachable

lpad12:                                           ; preds = %invoke.cont6
  %2 = landingpad { i8*, i32 }
          cleanup
  resume { i8*, i32 } undef

lpad16.loopexit:                                  ; preds = %if.then.i.i144, %invoke.cont29, %invoke.cont25, %_ZNKSt3__219istreambuf_iteratorIcNS_11char_traitsIcEEE14__test_for_eofEv.exit.i.i, %land.lhs.true.i.i.i
  %lpad.loopexit = landingpad { i8*, i32 }
          cleanup
  unreachable

lpad22:                                           ; preds = %invoke.cont23, %_ZNSt3__215basic_streambufIcNS_11char_traitsIcEEE5sgetcEv.exit.i19.i.i
  %3 = landingpad { i8*, i32 }
          cleanup
  unreachable

invoke.cont29:                                    ; preds = %invoke.cont25
  invoke void undef() #1
          to label %invoke.cont33 unwind label %lpad16.loopexit

invoke.cont33:                                    ; preds = %invoke.cont29
  br label %for.inc

for.inc:                                          ; preds = %invoke.cont33
  %cmp.i.i141 = icmp eq i8* undef, undef
  br i1 %cmp.i.i141, label %if.then.i.i144, label %if.end.i.i146

if.then.i.i144:                                   ; preds = %for.inc
  %call.i.i148 = invoke i32 undef() #1
          to label %for.cond.backedge unwind label %lpad16.loopexit

for.cond.backedge:                                ; preds = %if.end.i.i146, %if.then.i.i144
  br label %for.cond

if.end.i.i146:                                    ; preds = %for.inc
  call void undef() #2
  br label %for.cond.backedge
}

attributes #0 = { minsize noinline optsize "correctly-rounded-divide-sqrt-fp-math"="false" "disable-tail-calls"="false" "less-precise-fpmad"="false" "min-legal-vector-width"="0" "no-frame-pointer-elim"="false" "no-infs-fp-math"="false" "no-jump-tables"="false" "no-nans-fp-math"="false" "no-signed-zeros-fp-math"="false" "no-trapping-math"="false" "stack-protector-buffer-size"="8" "target-cpu"="generic" "unsafe-fp-math"="false" "use-soft-float"="false" }
attributes #1 = { minsize optsize }
attributes #2 = { minsize nounwind optsize }

!llvm.ident = !{!0}

!0 = !{!"clang version 8.0.0 (git@github.com:llvm-mirror/clang.git f8ec7c38feebd5cccae31acc7a50182b5474bfa9) (git@github.com:llvm-mirror/llvm.git eb54373b783a878c7397bc4503e93563c322e19a)"}
