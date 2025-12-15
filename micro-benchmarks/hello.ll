; ModuleID = 'hello.c'
source_filename = "hello.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@__const.main.input = private unnamed_addr constant [10 x i8] c"user_data\00", align 1

; Function Attrs: nofree noinline norecurse nosync nounwind memory(argmem: readwrite) uwtable
define dso_local noundef ptr @my_strcpy(ptr noundef returned writeonly captures(ret: address, provenance) %dest, ptr noundef readonly captures(none) %src) local_unnamed_addr #0 !dbg !14 {
entry:
    #dbg_value(ptr %dest, !22, !DIExpression(), !25)
    #dbg_value(ptr %src, !23, !DIExpression(), !25)
    #dbg_value(ptr %dest, !24, !DIExpression(), !25)
  %0 = load i8, ptr %src, align 1, !dbg !26, !tbaa !27
  %cmp.not8 = icmp eq i8 %0, 0, !dbg !28
  br i1 %cmp.not8, label %while.end, label %while.body, !dbg !29

while.body:                                       ; preds = %entry, %while.body
  %1 = phi i8 [ %2, %while.body ], [ %0, %entry ]
  %dest.addr.010 = phi ptr [ %incdec.ptr, %while.body ], [ %dest, %entry ]
  %src.addr.09 = phi ptr [ %incdec.ptr2, %while.body ], [ %src, %entry ]
    #dbg_value(ptr %dest.addr.010, !22, !DIExpression(), !25)
    #dbg_value(ptr %src.addr.09, !23, !DIExpression(), !25)
  store i8 %1, ptr %dest.addr.010, align 1, !dbg !30, !tbaa !27
  %incdec.ptr = getelementptr inbounds nuw i8, ptr %dest.addr.010, i64 1, !dbg !32
    #dbg_value(ptr %incdec.ptr, !22, !DIExpression(), !25)
  %incdec.ptr2 = getelementptr inbounds nuw i8, ptr %src.addr.09, i64 1, !dbg !33
    #dbg_value(ptr %incdec.ptr2, !23, !DIExpression(), !25)
  %2 = load i8, ptr %incdec.ptr2, align 1, !dbg !26, !tbaa !27
  %cmp.not = icmp eq i8 %2, 0, !dbg !34
  br i1 %cmp.not, label %while.end, label %while.body, !dbg !35, !llvm.loop !36

while.end:                                        ; preds = %while.body, %entry
  %dest.addr.0.lcssa = phi ptr [ %dest, %entry ], [ %incdec.ptr, %while.body ]
  store i8 0, ptr %dest.addr.0.lcssa, align 1, !dbg !40, !tbaa !27
  ret ptr %dest, !dbg !41
}

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.start.p0(ptr captures(none)) #1

; Function Attrs: mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite)
declare void @llvm.lifetime.end.p0(ptr captures(none)) #1

; Function Attrs: mustprogress nofree noinline norecurse nounwind willreturn memory(argmem: read) uwtable
define dso_local i32 @my_strlen(ptr noundef readonly captures(none) %str) local_unnamed_addr #2 !dbg !42 {
entry:
    #dbg_value(ptr %str, !47, !DIExpression(), !49)
    #dbg_value(i32 0, !48, !DIExpression(), !49)
  %0 = load i8, ptr %str, align 1, !dbg !50, !tbaa !27
  %cmp.not4 = icmp eq i8 %0, 0, !dbg !51
  br i1 %cmp.not4, label %while.end, label %while.body.preheader, !dbg !52

while.body.preheader:                             ; preds = %entry
  %scevgep = getelementptr i8, ptr %str, i64 1, !dbg !53
  %strlen = tail call i64 @strlen(ptr noundef nonnull dereferenceable(1) %scevgep), !dbg !53
  %1 = trunc i64 %strlen to i32, !dbg !53
  %2 = add i32 %1, 1, !dbg !53
    #dbg_value(i32 0, !48, !DIExpression(), !49)
    #dbg_value(ptr %str, !47, !DIExpression(), !49)
  br label %while.end, !dbg !54

while.end:                                        ; preds = %while.body.preheader, %entry
  %len.0.lcssa = phi i32 [ 0, %entry ], [ %2, %while.body.preheader ], !dbg !49
  ret i32 %len.0.lcssa, !dbg !54
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483638, -2147483648) i32 @process_input(i32 noundef %user_input) local_unnamed_addr #3 !dbg !55 {
entry:
    #dbg_value(i32 %user_input, !59, !DIExpression(), !61)
  %mul = shl nsw i32 %user_input, 1, !dbg !62
    #dbg_value(i32 %mul, !60, !DIExpression(), !61)
  %add = add nsw i32 %mul, 10, !dbg !63
  ret i32 %add, !dbg !64
}

; Function Attrs: nofree norecurse nosync nounwind memory(argmem: readwrite) uwtable
define dso_local void @process_string(ptr noundef readonly captures(none) %user_input) local_unnamed_addr #4 !dbg !65 {
entry:
  %buffer = alloca [100 x i8], align 16, !DIAssignID !75
    #dbg_assign(i1 poison, !70, !DIExpression(), !75, ptr %buffer, !DIExpression(), !76)
    #dbg_value(ptr %user_input, !69, !DIExpression(), !76)
  call void @llvm.lifetime.start.p0(ptr nonnull %buffer) #8, !dbg !77
  %call = call ptr @my_strcpy(ptr noundef nonnull %buffer, ptr noundef %user_input), !dbg !78
    #dbg_value(i32 poison, !74, !DIExpression(), !76)
    #dbg_assign(i8 88, !70, !DIExpression(DW_OP_LLVM_fragment, 0, 8), !79, ptr %buffer, !DIExpression(), !76)
  call void @llvm.lifetime.end.p0(ptr nonnull %buffer) #8, !dbg !80
  ret void, !dbg !81
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local i32 @helper(i32 noundef %x) local_unnamed_addr #3 !dbg !82 {
entry:
    #dbg_value(i32 %x, !84, !DIExpression(), !85)
  %mul = mul nsw i32 %x, 3, !dbg !86
  ret i32 %mul, !dbg !87
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483643, -2147483648) i32 @compute(i32 noundef %tainted_arg) local_unnamed_addr #3 !dbg !88 {
entry:
    #dbg_value(i32 %tainted_arg, !90, !DIExpression(), !92)
    #dbg_value(i32 %tainted_arg, !84, !DIExpression(), !93)
  %mul.i = mul nsw i32 %tainted_arg, 3, !dbg !95
    #dbg_value(i32 %mul.i, !91, !DIExpression(), !92)
  %add = add nsw i32 %mul.i, 5, !dbg !96
  ret i32 %add, !dbg !97
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483647, -2147483648) i32 @level3(i32 noundef %val) local_unnamed_addr #3 !dbg !98 {
entry:
    #dbg_value(i32 %val, !100, !DIExpression(), !101)
  %add = add nsw i32 %val, 1, !dbg !102
  ret i32 %add, !dbg !103
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483648, 2147483647) i32 @level2(i32 noundef %val) local_unnamed_addr #3 !dbg !104 {
entry:
    #dbg_value(i32 %val, !106, !DIExpression(), !107)
  %add.i = shl i32 %val, 1, !dbg !108
  %mul = add i32 %add.i, 2, !dbg !108
  ret i32 %mul, !dbg !109
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483638, -2147483648) i32 @level1(i32 noundef %user_val) local_unnamed_addr #3 !dbg !110 {
entry:
    #dbg_value(i32 %user_val, !112, !DIExpression(), !113)
    #dbg_value(i32 %user_val, !106, !DIExpression(), !114)
  %add.i.i = shl i32 %user_val, 1, !dbg !116
  %add = add i32 %add.i.i, 12, !dbg !117
  ret i32 %add, !dbg !118
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable
define dso_local range(i32 -2147483648, 2147483647) i32 @conditional_process(i32 noundef %secret, i32 noundef %flag) local_unnamed_addr #3 !dbg !119 {
entry:
    #dbg_value(i32 %secret, !123, !DIExpression(), !126)
    #dbg_value(i32 %flag, !124, !DIExpression(), !126)
  %cmp = icmp sgt i32 %flag, 0, !dbg !127
  %mul = shl nsw i32 %secret, 1, !dbg !129
  %result.0 = select i1 %cmp, i32 %mul, i32 100, !dbg !129
    #dbg_value(i32 %result.0, !125, !DIExpression(), !126)
  ret i32 %result.0, !dbg !130
}

; Function Attrs: mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable
define dso_local void @array_process(ptr noundef captures(none) initializes((4, 8)) %arr, i32 noundef %tainted_index) local_unnamed_addr #5 !dbg !131 {
entry:
    #dbg_value(ptr %arr, !136, !DIExpression(), !139)
    #dbg_value(i32 %tainted_index, !137, !DIExpression(), !139)
  %idxprom = sext i32 %tainted_index to i64, !dbg !140
  %arrayidx = getelementptr inbounds i32, ptr %arr, i64 %idxprom, !dbg !140
  store i32 42, ptr %arrayidx, align 4, !dbg !141, !tbaa !10
  %0 = load i32, ptr %arr, align 4, !dbg !142, !tbaa !10
    #dbg_value(i32 %0, !138, !DIExpression(), !139)
  %add = add nsw i32 %0, 10, !dbg !143
  %arrayidx2 = getelementptr inbounds nuw i8, ptr %arr, i64 4, !dbg !144
  store i32 %add, ptr %arrayidx2, align 4, !dbg !145, !tbaa !10
  ret void, !dbg !146
}

; Function Attrs: nofree norecurse nosync nounwind memory(none) uwtable
define dso_local noundef i32 @main() local_unnamed_addr #6 !dbg !147 {
entry:
  %buffer.i = alloca [100 x i8], align 16, !DIAssignID !161
    #dbg_assign(i1 poison, !153, !DIExpression(), !162, ptr @__const.main.input, !DIExpression(), !163)
    #dbg_value(i32 10, !151, !DIExpression(), !163)
    #dbg_value(i32 30, !152, !DIExpression(), !163)
    #dbg_assign(i1 poison, !153, !DIExpression(), !164, ptr @__const.main.input, !DIExpression(), !163)
    #dbg_assign(i1 poison, !70, !DIExpression(), !161, ptr %buffer.i, !DIExpression(), !165)
    #dbg_value(ptr @__const.main.input, !69, !DIExpression(), !165)
  call void @llvm.lifetime.start.p0(ptr nonnull %buffer.i) #8, !dbg !167
  %call.i = call ptr @my_strcpy(ptr noundef nonnull %buffer.i, ptr noundef nonnull readonly @__const.main.input), !dbg !168
    #dbg_value(i32 poison, !74, !DIExpression(), !165)
    #dbg_assign(i8 88, !70, !DIExpression(DW_OP_LLVM_fragment, 0, 8), !169, ptr %buffer.i, !DIExpression(), !165)
  call void @llvm.lifetime.end.p0(ptr nonnull %buffer.i) #8, !dbg !170
    #dbg_value(i32 32, !157, !DIExpression(), !163)
    #dbg_value(i32 20, !158, !DIExpression(), !163)
    #dbg_value(i32 undef, !159, !DIExpression(DW_OP_plus_uconst, 10, DW_OP_stack_value, DW_OP_LLVM_fragment, 32, 32), !163)
  ret i32 82, !dbg !171
}

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: read)
declare i64 @strlen(ptr captures(none)) local_unnamed_addr #7

attributes #0 = { nofree noinline norecurse nosync nounwind memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { mustprogress nocallback nofree nosync nounwind willreturn memory(argmem: readwrite) }
attributes #2 = { mustprogress nofree noinline norecurse nounwind willreturn memory(argmem: read) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { mustprogress nofree norecurse nosync nounwind willreturn memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nofree norecurse nosync nounwind memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { mustprogress nofree norecurse nosync nounwind willreturn memory(argmem: readwrite) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { nofree norecurse nosync nounwind memory(none) uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { nocallback nofree nounwind willreturn memory(argmem: read) }
attributes #8 = { nounwind }

!llvm.dbg.cu = !{!0}
!llvm.module.flags = !{!2, !3, !4, !5, !6, !7, !8}
!llvm.ident = !{!9}
!llvm.errno.tbaa = !{!10}

!0 = distinct !DICompileUnit(language: DW_LANG_C11, file: !1, producer: "clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, splitDebugInlining: false, nameTableKind: None)
!1 = !DIFile(filename: "hello.c", directory: "/home/rgangar/Documents/llvm-data-independent-timing/micro-benchmarks", checksumkind: CSK_MD5, checksum: "19c42ecfd78acd11b0f0f1fe1b9bad84")
!2 = !{i32 7, !"Dwarf Version", i32 5}
!3 = !{i32 2, !"Debug Info Version", i32 3}
!4 = !{i32 1, !"wchar_size", i32 4}
!5 = !{i32 8, !"PIC Level", i32 2}
!6 = !{i32 7, !"PIE Level", i32 2}
!7 = !{i32 7, !"uwtable", i32 2}
!8 = !{i32 7, !"debug-info-assignment-tracking", i1 true}
!9 = !{!"clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)"}
!10 = !{!11, !11, i64 0}
!11 = !{!"int", !12, i64 0}
!12 = !{!"omnipotent char", !13, i64 0}
!13 = !{!"Simple C/C++ TBAA"}
!14 = distinct !DISubprogram(name: "my_strcpy", scope: !1, file: !1, line: 6, type: !15, scopeLine: 6, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !21, keyInstructions: true)
!15 = !DISubroutineType(types: !16)
!16 = !{!17, !17, !19}
!17 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !18, size: 64)
!18 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!19 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !20, size: 64)
!20 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !18)
!21 = !{!22, !23, !24}
!22 = !DILocalVariable(name: "dest", arg: 1, scope: !14, file: !1, line: 6, type: !17)
!23 = !DILocalVariable(name: "src", arg: 2, scope: !14, file: !1, line: 6, type: !19)
!24 = !DILocalVariable(name: "original_dest", scope: !14, file: !1, line: 7, type: !17)
!25 = !DILocation(line: 0, scope: !14)
!26 = !DILocation(line: 8, column: 12, scope: !14)
!27 = !{!12, !12, i64 0}
!28 = !DILocation(line: 8, column: 17, scope: !14, atomGroup: 10, atomRank: 1)
!29 = !DILocation(line: 8, column: 5, scope: !14, atomGroup: 11, atomRank: 1)
!30 = !DILocation(line: 9, column: 15, scope: !31, atomGroup: 4, atomRank: 1)
!31 = distinct !DILexicalBlock(scope: !14, file: !1, line: 8, column: 26)
!32 = !DILocation(line: 10, column: 13, scope: !31, atomGroup: 5, atomRank: 2)
!33 = !DILocation(line: 11, column: 12, scope: !31, atomGroup: 6, atomRank: 2)
!34 = !DILocation(line: 8, column: 17, scope: !14, atomGroup: 2, atomRank: 1)
!35 = !DILocation(line: 8, column: 5, scope: !14, atomGroup: 3, atomRank: 1)
!36 = distinct !{!36, !37, !38, !39}
!37 = !DILocation(line: 8, column: 5, scope: !14)
!38 = !DILocation(line: 12, column: 5, scope: !14)
!39 = !{!"llvm.loop.mustprogress"}
!40 = !DILocation(line: 13, column: 11, scope: !14, atomGroup: 7, atomRank: 1)
!41 = !DILocation(line: 14, column: 5, scope: !14, atomGroup: 9, atomRank: 1)
!42 = distinct !DISubprogram(name: "my_strlen", scope: !1, file: !1, line: 19, type: !43, scopeLine: 19, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !46, keyInstructions: true)
!43 = !DISubroutineType(types: !44)
!44 = !{!45, !19}
!45 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!46 = !{!47, !48}
!47 = !DILocalVariable(name: "str", arg: 1, scope: !42, file: !1, line: 19, type: !19)
!48 = !DILocalVariable(name: "len", scope: !42, file: !1, line: 20, type: !45)
!49 = !DILocation(line: 0, scope: !42)
!50 = !DILocation(line: 21, column: 12, scope: !42)
!51 = !DILocation(line: 21, column: 17, scope: !42, atomGroup: 12, atomRank: 1)
!52 = !DILocation(line: 21, column: 5, scope: !42, atomGroup: 13, atomRank: 1)
!53 = !DILocation(line: 21, column: 5, scope: !42)
!54 = !DILocation(line: 25, column: 5, scope: !42, atomGroup: 7, atomRank: 1)
!55 = distinct !DISubprogram(name: "process_input", scope: !1, file: !1, line: 29, type: !56, scopeLine: 29, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !58, keyInstructions: true)
!56 = !DISubroutineType(types: !57)
!57 = !{!45, !45}
!58 = !{!59, !60}
!59 = !DILocalVariable(name: "user_input", arg: 1, scope: !55, file: !1, line: 29, type: !45)
!60 = !DILocalVariable(name: "result", scope: !55, file: !1, line: 30, type: !45)
!61 = !DILocation(line: 0, scope: !55)
!62 = !DILocation(line: 30, column: 29, scope: !55, atomGroup: 1, atomRank: 2)
!63 = !DILocation(line: 31, column: 19, scope: !55, atomGroup: 3, atomRank: 2)
!64 = !DILocation(line: 31, column: 5, scope: !55, atomGroup: 3, atomRank: 1)
!65 = distinct !DISubprogram(name: "process_string", scope: !1, file: !1, line: 35, type: !66, scopeLine: 35, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !68, keyInstructions: true)
!66 = !DISubroutineType(types: !67)
!67 = !{null, !17}
!68 = !{!69, !70, !74}
!69 = !DILocalVariable(name: "user_input", arg: 1, scope: !65, file: !1, line: 35, type: !17)
!70 = !DILocalVariable(name: "buffer", scope: !65, file: !1, line: 36, type: !71)
!71 = !DICompositeType(tag: DW_TAG_array_type, baseType: !18, size: 800, elements: !72)
!72 = !{!73}
!73 = !DISubrange(count: 100)
!74 = !DILocalVariable(name: "len", scope: !65, file: !1, line: 38, type: !45)
!75 = distinct !DIAssignID()
!76 = !DILocation(line: 0, scope: !65)
!77 = !DILocation(line: 36, column: 5, scope: !65)
!78 = !DILocation(line: 37, column: 5, scope: !65)
!79 = distinct !DIAssignID()
!80 = !DILocation(line: 40, column: 1, scope: !65)
!81 = !DILocation(line: 40, column: 1, scope: !65, atomGroup: 3, atomRank: 1)
!82 = distinct !DISubprogram(name: "helper", scope: !1, file: !1, line: 43, type: !56, scopeLine: 43, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !83, keyInstructions: true)
!83 = !{!84}
!84 = !DILocalVariable(name: "x", arg: 1, scope: !82, file: !1, line: 43, type: !45)
!85 = !DILocation(line: 0, scope: !82)
!86 = !DILocation(line: 44, column: 14, scope: !82, atomGroup: 1, atomRank: 2)
!87 = !DILocation(line: 44, column: 5, scope: !82, atomGroup: 1, atomRank: 1)
!88 = distinct !DISubprogram(name: "compute", scope: !1, file: !1, line: 47, type: !56, scopeLine: 47, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !89, keyInstructions: true)
!89 = !{!90, !91}
!90 = !DILocalVariable(name: "tainted_arg", arg: 1, scope: !88, file: !1, line: 47, type: !45)
!91 = !DILocalVariable(name: "result", scope: !88, file: !1, line: 48, type: !45)
!92 = !DILocation(line: 0, scope: !88)
!93 = !DILocation(line: 0, scope: !82, inlinedAt: !94)
!94 = distinct !DILocation(line: 48, column: 18, scope: !88)
!95 = !DILocation(line: 44, column: 14, scope: !82, inlinedAt: !94, atomGroup: 1, atomRank: 2)
!96 = !DILocation(line: 49, column: 19, scope: !88, atomGroup: 3, atomRank: 2)
!97 = !DILocation(line: 49, column: 5, scope: !88, atomGroup: 3, atomRank: 1)
!98 = distinct !DISubprogram(name: "level3", scope: !1, file: !1, line: 53, type: !56, scopeLine: 53, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !99, keyInstructions: true)
!99 = !{!100}
!100 = !DILocalVariable(name: "val", arg: 1, scope: !98, file: !1, line: 53, type: !45)
!101 = !DILocation(line: 0, scope: !98)
!102 = !DILocation(line: 54, column: 16, scope: !98, atomGroup: 1, atomRank: 2)
!103 = !DILocation(line: 54, column: 5, scope: !98, atomGroup: 1, atomRank: 1)
!104 = distinct !DISubprogram(name: "level2", scope: !1, file: !1, line: 57, type: !56, scopeLine: 57, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !105, keyInstructions: true)
!105 = !{!106}
!106 = !DILocalVariable(name: "val", arg: 1, scope: !104, file: !1, line: 57, type: !45)
!107 = !DILocation(line: 0, scope: !104)
!108 = !DILocation(line: 58, column: 24, scope: !104, atomGroup: 1, atomRank: 2)
!109 = !DILocation(line: 58, column: 5, scope: !104, atomGroup: 1, atomRank: 1)
!110 = distinct !DISubprogram(name: "level1", scope: !1, file: !1, line: 61, type: !56, scopeLine: 61, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !111, keyInstructions: true)
!111 = !{!112}
!112 = !DILocalVariable(name: "user_val", arg: 1, scope: !110, file: !1, line: 61, type: !45)
!113 = !DILocation(line: 0, scope: !110)
!114 = !DILocation(line: 0, scope: !104, inlinedAt: !115)
!115 = distinct !DILocation(line: 62, column: 12, scope: !110)
!116 = !DILocation(line: 58, column: 24, scope: !104, inlinedAt: !115, atomGroup: 1, atomRank: 2)
!117 = !DILocation(line: 62, column: 29, scope: !110, atomGroup: 1, atomRank: 2)
!118 = !DILocation(line: 62, column: 5, scope: !110, atomGroup: 1, atomRank: 1)
!119 = distinct !DISubprogram(name: "conditional_process", scope: !1, file: !1, line: 66, type: !120, scopeLine: 66, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !122, keyInstructions: true)
!120 = !DISubroutineType(types: !121)
!121 = !{!45, !45, !45}
!122 = !{!123, !124, !125}
!123 = !DILocalVariable(name: "secret", arg: 1, scope: !119, file: !1, line: 66, type: !45)
!124 = !DILocalVariable(name: "flag", arg: 2, scope: !119, file: !1, line: 66, type: !45)
!125 = !DILocalVariable(name: "result", scope: !119, file: !1, line: 67, type: !45)
!126 = !DILocation(line: 0, scope: !119)
!127 = !DILocation(line: 68, column: 14, scope: !128, atomGroup: 1, atomRank: 2)
!128 = distinct !DILexicalBlock(scope: !119, file: !1, line: 68, column: 9)
!129 = !DILocation(line: 68, column: 14, scope: !128, atomGroup: 1, atomRank: 1)
!130 = !DILocation(line: 73, column: 5, scope: !119, atomGroup: 5, atomRank: 1)
!131 = distinct !DISubprogram(name: "array_process", scope: !1, file: !1, line: 77, type: !132, scopeLine: 77, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !135, keyInstructions: true)
!132 = !DISubroutineType(types: !133)
!133 = !{null, !134, !45}
!134 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !45, size: 64)
!135 = !{!136, !137, !138}
!136 = !DILocalVariable(name: "arr", arg: 1, scope: !131, file: !1, line: 77, type: !134)
!137 = !DILocalVariable(name: "tainted_index", arg: 2, scope: !131, file: !1, line: 77, type: !45)
!138 = !DILocalVariable(name: "val", scope: !131, file: !1, line: 79, type: !45)
!139 = !DILocation(line: 0, scope: !131)
!140 = !DILocation(line: 78, column: 5, scope: !131)
!141 = !DILocation(line: 78, column: 24, scope: !131, atomGroup: 1, atomRank: 1)
!142 = !DILocation(line: 79, column: 15, scope: !131, atomGroup: 2, atomRank: 2)
!143 = !DILocation(line: 80, column: 18, scope: !131, atomGroup: 3, atomRank: 2)
!144 = !DILocation(line: 80, column: 5, scope: !131)
!145 = !DILocation(line: 80, column: 12, scope: !131, atomGroup: 3, atomRank: 1)
!146 = !DILocation(line: 81, column: 1, scope: !131, atomGroup: 4, atomRank: 1)
!147 = distinct !DISubprogram(name: "main", scope: !1, file: !1, line: 84, type: !148, scopeLine: 84, flags: DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !0, retainedNodes: !150, keyInstructions: true)
!148 = !DISubroutineType(types: !149)
!149 = !{!45}
!150 = !{!151, !152, !153, !157, !158, !159}
!151 = !DILocalVariable(name: "user_val", scope: !147, file: !1, line: 86, type: !45)
!152 = !DILocalVariable(name: "processed", scope: !147, file: !1, line: 87, type: !45)
!153 = !DILocalVariable(name: "input", scope: !147, file: !1, line: 90, type: !154)
!154 = !DICompositeType(tag: DW_TAG_array_type, baseType: !18, size: 80, elements: !155)
!155 = !{!156}
!156 = !DISubrange(count: 10)
!157 = !DILocalVariable(name: "nested_result", scope: !147, file: !1, line: 94, type: !45)
!158 = !DILocalVariable(name: "cond_result", scope: !147, file: !1, line: 97, type: !45)
!159 = !DILocalVariable(name: "arr", scope: !147, file: !1, line: 100, type: !160)
!160 = !DICompositeType(tag: DW_TAG_array_type, baseType: !45, size: 320, elements: !155)
!161 = distinct !DIAssignID()
!162 = distinct !DIAssignID()
!163 = !DILocation(line: 0, scope: !147)
!164 = distinct !DIAssignID()
!165 = !DILocation(line: 0, scope: !65, inlinedAt: !166)
!166 = distinct !DILocation(line: 91, column: 5, scope: !147)
!167 = !DILocation(line: 36, column: 5, scope: !65, inlinedAt: !166)
!168 = !DILocation(line: 37, column: 5, scope: !65, inlinedAt: !166)
!169 = distinct !DIAssignID()
!170 = !DILocation(line: 40, column: 1, scope: !65, inlinedAt: !166)
!171 = !DILocation(line: 103, column: 5, scope: !147, atomGroup: 7, atomRank: 1)
