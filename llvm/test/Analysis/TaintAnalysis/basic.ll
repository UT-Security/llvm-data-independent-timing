; NOTE: This test requires a CSV file specifying taint sources
; Create a file test_sources.csv with content:
;   simple_taint,0
;   caller,0
;
; RUN: echo "simple_taint,0" > %t.csv
; RUN: echo "caller,0" >> %t.csv
; RUN: opt -passes='print<taint-analysis>' -taint-sources-file=%t.csv -disable-output < %s 2>&1 | FileCheck %s

; Test basic taint propagation from function argument
define i32 @simple_taint(i32 %arg) {
; CHECK: Taint Source Arguments:
; CHECK: simple_taint::arg0
entry:
  %add = add i32 %arg, 10
  %mul = mul i32 %add, 2
  ret i32 %mul
}

; Test interprocedural taint propagation
define i32 @callee(i32 %x) {
entry:
  %result = mul i32 %x, 2
  ret i32 %result
}

define i32 @caller(i32 %tainted) {
; CHECK: caller::arg0
entry:
  %val = call i32 @callee(i32 %tainted)
  ret i32 %val
}

; Test with memory operations
define void @taint_memory(i32 %tainted_arg, i32* %ptr) {
entry:
  %computed = add i32 %tainted_arg, 5
  store i32 %computed, i32* %ptr
  ret void
}
