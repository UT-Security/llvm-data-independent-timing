; RUN: rm -f %t.src
; RUN: echo "f1,0" > %t.src
; RUN: echo "f1,2" >> %t.src
; RUN: echo "f2,1" >> %t.src
; RUN: opt -S -passes=taint-annotate -taint-src=%t.src %s | FileCheck %s

; Test that taint-annotate pass correctly adds "tainted" attribute
; to function arguments based on a taint-sources file.

; f1: arg 0 and arg 2 should be tainted
; CHECK-LABEL: define i32 @f1(
; CHECK-SAME: i32 "tainted" %a
; CHECK-SAME: i32 %b
; CHECK-SAME: i32 "tainted" %c
define i32 @f1(i32 %a, i32 %b, i32 %c) {
  %sum = add i32 %a, %b
  %result = add i32 %sum, %c
  ret i32 %result
}

; f2: arg 1 should be tainted, arg 0 should not
; CHECK-LABEL: define i32 @f2(
; CHECK-SAME: i32 %x
; CHECK-SAME: ptr "tainted" %y
define i32 @f2(i32 %x, ptr %y) {
  %val = load i32, ptr %y
  %sum = add i32 %x, %val
  ret i32 %sum
}

; f3: not in taint-sources file, no arguments should be tainted
; CHECK-LABEL: define i32 @f3(
; CHECK-NOT: "tainted"
; CHECK-SAME: )
define i32 @f3(i32 %a, i32 %b) {
  %sum = add i32 %a, %b
  ret i32 %sum
}
