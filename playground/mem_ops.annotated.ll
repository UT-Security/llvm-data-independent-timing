; ModuleID = 'mem_ops.ll'
source_filename = "mem_ops.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

%struct.Pair = type { i32, i32 }

@__const.main.arr = private unnamed_addr constant [5 x i32] [i32 10, i32 20, i32 30, i32 40, i32 50], align 4
@__const.main.p = private unnamed_addr constant %struct.Pair { i32 1, i32 2 }, align 4

; Function Attrs: noinline nounwind ssp
define i32 @store_load(i32 noundef "tainted" %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  store i32 %1, ptr %4, align 4
  %6 = load i32, ptr %3, align 4
  store i32 %6, ptr %5, align 4
  %7 = load i32, ptr %5, align 4
  %8 = load i32, ptr %4, align 4
  %9 = add nsw i32 %7, %8
  ret i32 %9
}

; Function Attrs: noinline nounwind ssp
define i32 @array_index(i32 noundef "tainted" %0, ptr noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca ptr, align 8
  store i32 %0, ptr %3, align 4
  store ptr %1, ptr %4, align 8
  %5 = load ptr, ptr %4, align 8
  %6 = load i32, ptr %3, align 4
  %7 = sext i32 %6 to i64
  %8 = getelementptr inbounds i32, ptr %5, i64 %7
  %9 = load i32, ptr %8, align 4
  ret i32 %9
}

; Function Attrs: noinline nounwind ssp
define void @store_via_ptr(ptr noundef "tainted" %0, i32 noundef %1) #0 {
  %3 = alloca ptr, align 8
  %4 = alloca i32, align 4
  store ptr %0, ptr %3, align 8
  store i32 %1, ptr %4, align 4
  %5 = load i32, ptr %4, align 4
  %6 = load ptr, ptr %3, align 8
  store i32 %5, ptr %6, align 4
  ret void
}

; Function Attrs: noinline nounwind ssp
define i32 @load_via_ptr(ptr noundef "tainted" %0) #0 {
  %2 = alloca ptr, align 8
  store ptr %0, ptr %2, align 8
  %3 = load ptr, ptr %2, align 8
  %4 = load i32, ptr %3, align 4
  ret i32 %4
}

; Function Attrs: noinline nounwind ssp
define i32 @struct_access(ptr noundef "tainted" %0) #0 {
  %2 = alloca ptr, align 8
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  store ptr %0, ptr %2, align 8
  %5 = load ptr, ptr %2, align 8
  %6 = getelementptr inbounds nuw %struct.Pair, ptr %5, i32 0, i32 0
  %7 = load i32, ptr %6, align 4
  store i32 %7, ptr %3, align 4
  %8 = load ptr, ptr %2, align 8
  %9 = getelementptr inbounds nuw %struct.Pair, ptr %8, i32 0, i32 1
  %10 = load i32, ptr %9, align 4
  store i32 %10, ptr %4, align 4
  %11 = load i32, ptr %3, align 4
  %12 = load i32, ptr %4, align 4
  %13 = add nsw i32 %11, %12
  ret i32 %13
}

; Function Attrs: noinline nounwind ssp
define i32 @multi_hop(i32 noundef "tainted" %0) #0 {
  %2 = alloca i32, align 4
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %0, ptr %2, align 4
  %6 = load i32, ptr %2, align 4
  %7 = add nsw i32 %6, 1
  store i32 %7, ptr %3, align 4
  %8 = load i32, ptr %3, align 4
  %9 = mul nsw i32 %8, 2
  store i32 %9, ptr %4, align 4
  %10 = load i32, ptr %4, align 4
  %11 = sub nsw i32 %10, 3
  store i32 %11, ptr %5, align 4
  %12 = load i32, ptr %5, align 4
  ret i32 %12
}

; Function Attrs: noinline nounwind ssp
define i32 @cond_store(i32 noundef "tainted" %0, i32 noundef %1) #0 {
  %3 = alloca i32, align 4
  %4 = alloca i32, align 4
  %5 = alloca i32, align 4
  store i32 %0, ptr %3, align 4
  store i32 %1, ptr %4, align 4
  %6 = load i32, ptr %4, align 4
  %7 = icmp ne i32 %6, 0
  br i1 %7, label %8, label %10

8:                                                ; preds = %2
  %9 = load i32, ptr %3, align 4
  store i32 %9, ptr %5, align 4
  br label %11

10:                                               ; preds = %2
  store i32 0, ptr %5, align 4
  br label %11

11:                                               ; preds = %10, %8
  %12 = load i32, ptr %5, align 4
  ret i32 %12
}

; Function Attrs: noinline nounwind ssp
define void @byte_copy(ptr noundef %0, ptr noundef "tainted" %1, i32 noundef %2) #0 {
  %4 = alloca ptr, align 8
  %5 = alloca ptr, align 8
  %6 = alloca i32, align 4
  %7 = alloca i32, align 4
  store ptr %0, ptr %4, align 8
  store ptr %1, ptr %5, align 8
  store i32 %2, ptr %6, align 4
  store i32 0, ptr %7, align 4
  br label %8

8:                                                ; preds = %22, %3
  %9 = load i32, ptr %7, align 4
  %10 = load i32, ptr %6, align 4
  %11 = icmp slt i32 %9, %10
  br i1 %11, label %12, label %25

12:                                               ; preds = %8
  %13 = load ptr, ptr %5, align 8
  %14 = load i32, ptr %7, align 4
  %15 = sext i32 %14 to i64
  %16 = getelementptr inbounds i8, ptr %13, i64 %15
  %17 = load i8, ptr %16, align 1
  %18 = load ptr, ptr %4, align 8
  %19 = load i32, ptr %7, align 4
  %20 = sext i32 %19 to i64
  %21 = getelementptr inbounds i8, ptr %18, i64 %20
  store i8 %17, ptr %21, align 1
  br label %22

22:                                               ; preds = %12
  %23 = load i32, ptr %7, align 4
  %24 = add nsw i32 %23, 1
  store i32 %24, ptr %7, align 4
  br label %8, !llvm.loop !5

25:                                               ; preds = %8
  ret void
}

; Function Attrs: noinline nounwind ssp
define i32 @main() #0 {
  %1 = alloca i32, align 4
  %2 = alloca [5 x i32], align 4
  %3 = alloca %struct.Pair, align 4
  %4 = alloca i32, align 4
  %5 = alloca [8 x i8], align 1
  store i32 0, ptr %1, align 4
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %2, ptr align 4 @__const.main.arr, i64 20, i1 false)
  call void @llvm.memcpy.p0.p0.i64(ptr align 4 %3, ptr align 4 @__const.main.p, i64 8, i1 false)
  store i32 42, ptr %4, align 4
  call void @llvm.memset.p0.i64(ptr align 1 %5, i8 0, i64 8, i1 false)
  %6 = call i32 @store_load(i32 noundef 100, i32 noundef 200)
  %7 = getelementptr inbounds [5 x i32], ptr %2, i64 0, i64 0
  %8 = call i32 @array_index(i32 noundef 2, ptr noundef %7)
  call void @store_via_ptr(ptr noundef %4, i32 noundef 99)
  %9 = call i32 @load_via_ptr(ptr noundef %4)
  %10 = call i32 @struct_access(ptr noundef %3)
  %11 = call i32 @multi_hop(i32 noundef 777)
  %12 = call i32 @cond_store(i32 noundef 999, i32 noundef 1)
  %13 = getelementptr inbounds [8 x i8], ptr %5, i64 0, i64 0
  call void @byte_copy(ptr noundef %13, ptr noundef %4, i32 noundef 4)
  ret i32 0
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: readwrite)
declare void @llvm.memcpy.p0.p0.i64(ptr noalias writeonly captures(none), ptr noalias readonly captures(none), i64, i1 immarg) #1

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.memset.p0.i64(ptr writeonly captures(none), i8, i64, i1 immarg) #2

attributes #0 = { noinline nounwind ssp "frame-pointer"="non-leaf" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a,+zcm,+zcz" }
attributes #1 = { nocallback nofree nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { nocallback nofree nounwind willreturn memory(argmem: write) }

!llvm.module.flags = !{!0, !1, !2, !3}
!llvm.ident = !{!4}

!0 = !{i32 2, !"SDK Version", [2 x i32] [i32 15, i32 5]}
!1 = !{i32 1, !"wchar_size", i32 4}
!2 = !{i32 8, !"PIC Level", i32 2}
!3 = !{i32 7, !"frame-pointer", i32 1}
!4 = !{!"Homebrew clang version 20.1.6"}
!5 = distinct !{!5, !6}
!6 = !{!"llvm.loop.mustprogress"}
