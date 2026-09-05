; -taint-dit-clone-seeded: every SEEDED, DEFINED function, and every definition
; it reaches by direct call in this module, gets a `<name>.dit` twin carrying
; the seed attributes and "taint-dit-clone", with the original's linkage (so
; another TU can name an external one), address taken or not; a seeded
; DECLARATION gets no twin here (the TU that defines it makes one) and keeps its
; "taint-seeded-elsewhere" stamp; a function nothing seeded reaches gets none;
; the module is flagged so the MIR pass knows this build clones. Default on
; since 2026-09-05; with =0 nothing is cloned.
;
; RUN: rm -f %t.src
; RUN: echo "f,0"         >  %t.src
; RUN: echo "g,0,pointee" >> %t.src
; RUN: echo "h,0"         >> %t.src
; (r and u are not seeded: f reaches r, nothing reaches u.)
; RUN: opt -S -passes=taint-annotate -taint-src=%t.src -taint-dit-clone-seeded %s \
; RUN:   | FileCheck %s
; RUN: opt -S -passes=taint-annotate -taint-src=%t.src -taint-dit-clone-seeded=0 %s \
; RUN:   | FileCheck --check-prefix=OFF %s

; Output order: the originals, the stamped declaration, then the twins.
; CHECK:     @llvm.used = appending global [2 x ptr] [ptr @r.dit, ptr @g.dit]
; CHECK:     define i32 @f(i32 "tainted" %a) #[[SEEDED:[0-9]+]]
; CHECK:     define internal i32 @r(i32 %a) {
; CHECK:     define internal i32 @u(i32 %a) {
; CHECK:     define internal i32 @g(ptr "tainted-pointee" %p) #[[SEEDED]]
; CHECK:     declare i32 @h(i32) #[[ELSEWHERE:[0-9]+]]
; CHECK:     define i32 @f.dit(i32 "tainted" %a) #[[CLONE:[0-9]+]]
; CHECK:     define internal i32 @r.dit(i32 %a) #[[REACHED:[0-9]+]]
; CHECK:     define internal i32 @g.dit(ptr "tainted-pointee" %p) #[[CLONE]]
; CHECK-NOT: @u.dit
; CHECK-NOT: @h.dit
; CHECK:     attributes #[[SEEDED]] = { noinline }
; CHECK:     attributes #[[ELSEWHERE]] = { "taint-seeded-elsewhere"="1,0" }
; CHECK:     attributes #[[CLONE]] = { noinline "taint-dit-clone" }
; CHECK:     attributes #[[REACHED]] = { "taint-dit-clone" }
; CHECK:     !{i32 4, !"taint-dit-clone-seeded", i32 1}
;
; OFF-NOT: .dit
; OFF-NOT: "taint-dit-clone"

@tbl = global ptr @f

; The external seeded function: external twin, seed copied, address taken is
; no bar (the table keeps naming @f; only direct calls are redirected).
define i32 @f(i32 %a) {
  %r = call i32 @g(ptr @tbl)
  %s = add i32 %a, %r
  %t = call i32 @r(i32 %s)
  ret i32 %t
}

define internal i32 @r(i32 %a) {
  %v = mul i32 %a, 3
  ret i32 %v
}

define internal i32 @u(i32 %a) {
  %v = mul i32 %a, 5
  ret i32 %v
}

; The internal seeded function: internal twin, kept alive in llvm.used.
define internal i32 @g(ptr %p) {
  %v = load i32, ptr %p
  ret i32 %v
}

; The seeded declaration: stamped, not cloned.
declare i32 @h(i32)
