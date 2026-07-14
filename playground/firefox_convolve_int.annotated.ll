; ModuleID = '/Users/rgangar/Documents/llvm-project-yichi/playground/firefox_convolve_int.ll'
source_filename = "playground/firefox_convolve_int.c"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-n32:64-S128-Fn32"
target triple = "arm64-apple-macosx15.0.0"

@.str = private unnamed_addr constant [8 x i8] c"--width\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [9 x i8] c"--height\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [9 x i8] c"--kernel\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [7 x i8] c"--iter\00", align 1, !dbg !14
@.str.4 = private unnamed_addr constant [9 x i8] c"--warmup\00", align 1, !dbg !19
@.str.5 = private unnamed_addr constant [69 x i8] c"firefox_convolve_int width=%d height=%d kernel=%d iter=%d warmup=%d\0A\00", align 1, !dbg !21
@__stderrp = external local_unnamed_addr global ptr, align 8
@.str.6 = private unnamed_addr constant [19 x i8] c"allocation failed\0A\00", align 1, !dbg !26
@.str.7 = private unnamed_addr constant [15 x i8] c"checksum=%llu\0A\00", align 1, !dbg !31

; Function Attrs: nounwind ssp
define range(i32 0, 2) i32 @main(i32 noundef %argc, ptr noundef readonly captures(none) %argv) local_unnamed_addr #0 !dbg !72 {
entry:
    #dbg_value(i32 %argc, !78, !DIExpression(), !100)
    #dbg_value(ptr %argv, !79, !DIExpression(), !100)
    #dbg_value(i32 256, !80, !DIExpression(), !100)
    #dbg_value(i32 256, !81, !DIExpression(), !100)
    #dbg_value(i32 3, !82, !DIExpression(), !100)
    #dbg_value(i32 50, !83, !DIExpression(), !100)
    #dbg_value(i32 5, !84, !DIExpression(), !100)
    #dbg_value(i32 1, !85, !DIExpression(), !101)
  %cmp242 = icmp sgt i32 %argc, 1, !dbg !102
  br i1 %cmp242, label %for.body, label %for.cond.cleanup, !dbg !104

for.cond.cleanup.loopexit:                        ; preds = %for.inc
  %0 = tail call i32 @llvm.smax.i32(i32 %kernel_size.1, i32 1), !dbg !105
  br label %for.cond.cleanup, !dbg !105

for.cond.cleanup:                                 ; preds = %for.cond.cleanup.loopexit, %entry
  %warmup_iterations.0.lcssa = phi i32 [ 5, %entry ], [ %warmup_iterations.1, %for.cond.cleanup.loopexit ], !dbg !107
  %iterations.0.lcssa = phi i32 [ 50, %entry ], [ %iterations.1, %for.cond.cleanup.loopexit ], !dbg !108
  %kernel_size.0.lcssa = phi i32 [ 3, %entry ], [ %0, %for.cond.cleanup.loopexit ], !dbg !109
  %height.0.lcssa = phi i32 [ 256, %entry ], [ %height.1, %for.cond.cleanup.loopexit ], !dbg !110
  %width.0.lcssa = phi i32 [ 256, %entry ], [ %width.1, %for.cond.cleanup.loopexit ], !dbg !100
    #dbg_value(i32 %kernel_size.0.lcssa, !82, !DIExpression(), !100)
  %spec.select = or i32 %kernel_size.0.lcssa, 1, !dbg !111
    #dbg_value(i32 %kernel_size.0.lcssa, !82, !DIExpression(DW_OP_constu, 1, DW_OP_or, DW_OP_stack_value), !100)
  %div240241 = and i32 %kernel_size.0.lcssa, 2147483646, !dbg !113
    #dbg_value(i32 %kernel_size.0.lcssa, !87, !DIExpression(DW_OP_constu, 1, DW_OP_shr, DW_OP_plus_uconst, 1, DW_OP_stack_value), !100)
  %mul = add nuw i32 %div240241, 2, !dbg !113
  %cmp70.not = icmp sgt i32 %width.0.lcssa, %mul, !dbg !115
  %add73 = add nuw i32 %div240241, 3, !dbg !116
  %width.2 = select i1 %cmp70.not, i32 %width.0.lcssa, i32 %add73, !dbg !116
    #dbg_value(i32 %width.2, !80, !DIExpression(), !100)
  %cmp76.not = icmp sgt i32 %height.0.lcssa, %mul, !dbg !117
  %height.2 = select i1 %cmp76.not, i32 %height.0.lcssa, i32 %add73, !dbg !119
    #dbg_value(i32 %height.2, !81, !DIExpression(), !100)
  %call81 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.5, i32 noundef %width.2, i32 noundef %height.2, i32 noundef %spec.select, i32 noundef %iterations.0.lcssa, i32 noundef %warmup_iterations.0.lcssa), !dbg !120
  %mul82 = shl nsw i32 %width.2, 2, !dbg !121
    #dbg_value(i32 %mul82, !88, !DIExpression(), !100)
  %conv = sext i32 %mul82 to i64, !dbg !122
  %conv83 = sext i32 %height.2 to i64, !dbg !123
  %mul84 = mul nsw i64 %conv, %conv83, !dbg !124
    #dbg_value(i64 %mul84, !89, !DIExpression(), !100)
  %call85 = tail call ptr @malloc(i64 noundef %mul84) #12, !dbg !125
    #dbg_value(ptr %call85, !90, !DIExpression(), !100)
  %call86 = tail call ptr @calloc(i64 noundef %mul84, i64 noundef 1) #13, !dbg !126
    #dbg_value(ptr %call86, !91, !DIExpression(), !100)
  %conv87 = zext nneg i32 %spec.select to i64, !dbg !127
  %mul89 = shl nuw nsw i64 %conv87, 2, !dbg !128
  %mul90 = mul nuw i64 %mul89, %conv87, !dbg !129
  %call91 = tail call ptr @malloc(i64 noundef %mul90) #12, !dbg !130
    #dbg_value(ptr %call91, !92, !DIExpression(), !100)
  %tobool = icmp ne ptr %call85, null, !dbg !131
  %tobool92 = icmp ne ptr %call86, null
  %or.cond = and i1 %tobool, %tobool92, !dbg !133
  %tobool94 = icmp ne ptr %call91, null
  %or.cond140 = and i1 %or.cond, %tobool94, !dbg !133
  br i1 %or.cond140, label %for.cond99.preheader, label %if.then95, !dbg !133

for.cond99.preheader:                             ; preds = %for.cond.cleanup
    #dbg_value(i64 0, !93, !DIExpression(), !134)
  %cmp100253.not = icmp eq i64 %mul84, 0, !dbg !135
  br i1 %cmp100253.not, label %for.cond112.preheader, label %iter.check, !dbg !137

iter.check:                                       ; preds = %for.cond99.preheader
  %min.iters.check = icmp ult i64 %mul84, 8, !dbg !138
  br i1 %min.iters.check, label %for.body103.preheader, label %vector.main.loop.iter.check, !dbg !138

vector.main.loop.iter.check:                      ; preds = %iter.check
  %min.iters.check257 = icmp ult i64 %mul84, 64, !dbg !138
  br i1 %min.iters.check257, label %vec.epilog.ph, label %vector.ph, !dbg !138

vector.ph:                                        ; preds = %vector.main.loop.iter.check
  %n.mod.vf = and i64 %mul84, 56
  %n.vec = and i64 %mul84, -64
  br label %vector.body, !dbg !138

vector.body:                                      ; preds = %vector.body, %vector.ph
  %index = phi i64 [ 0, %vector.ph ], [ %index.next, %vector.body ], !dbg !139
  %vec.ind = phi <16 x i8> [ <i8 0, i8 1, i8 2, i8 3, i8 4, i8 5, i8 6, i8 7, i8 8, i8 9, i8 10, i8 11, i8 12, i8 13, i8 14, i8 15>, %vector.ph ], [ %vec.ind.next, %vector.body ], !dbg !140
  %1 = mul <16 x i8> %vec.ind, splat (i8 17), !dbg !140
  %2 = mul <16 x i8> %vec.ind, splat (i8 17), !dbg !140
  %3 = mul <16 x i8> %vec.ind, splat (i8 17), !dbg !140
  %4 = mul <16 x i8> %vec.ind, splat (i8 17), !dbg !140
  %5 = add <16 x i8> %1, splat (i8 31), !dbg !140
  %6 = add <16 x i8> %2, splat (i8 47), !dbg !140
  %7 = add <16 x i8> %3, splat (i8 63), !dbg !140
  %8 = add <16 x i8> %4, splat (i8 79), !dbg !140
  %9 = getelementptr inbounds nuw i8, ptr %call85, i64 %index, !dbg !141
  %10 = getelementptr inbounds nuw i8, ptr %9, i64 16, !dbg !142
  %11 = getelementptr inbounds nuw i8, ptr %9, i64 32, !dbg !142
  %12 = getelementptr inbounds nuw i8, ptr %9, i64 48, !dbg !142
  store <16 x i8> %5, ptr %9, align 1, !dbg !142, !tbaa !143
  store <16 x i8> %6, ptr %10, align 1, !dbg !142, !tbaa !143
  store <16 x i8> %7, ptr %11, align 1, !dbg !142, !tbaa !143
  store <16 x i8> %8, ptr %12, align 1, !dbg !142, !tbaa !143
  %index.next = add nuw i64 %index, 64, !dbg !139
  %vec.ind.next = add <16 x i8> %vec.ind, splat (i8 64), !dbg !140
  %13 = icmp eq i64 %index.next, %n.vec, !dbg !144
  br i1 %13, label %middle.block, label %vector.body, !dbg !144, !llvm.loop !145

middle.block:                                     ; preds = %vector.body
  %cmp.n = icmp eq i64 %mul84, %n.vec, !dbg !144
  br i1 %cmp.n, label %for.cond112.preheader, label %vec.epilog.iter.check, !dbg !144

vec.epilog.iter.check:                            ; preds = %middle.block
  %min.epilog.iters.check = icmp eq i64 %n.mod.vf, 0
  br i1 %min.epilog.iters.check, label %for.body103.preheader, label %vec.epilog.ph, !prof !149

vec.epilog.ph:                                    ; preds = %vec.epilog.iter.check, %vector.main.loop.iter.check
  %bc.resume.val = phi i64 [ %n.vec, %vec.epilog.iter.check ], [ 0, %vector.main.loop.iter.check ]
  %n.vec259 = and i64 %mul84, -8
  %14 = trunc i64 %bc.resume.val to i8, !dbg !140
  %broadcast.splatinsert = insertelement <8 x i8> poison, i8 %14, i64 0
  %broadcast.splat = shufflevector <8 x i8> %broadcast.splatinsert, <8 x i8> poison, <8 x i32> zeroinitializer
  %induction = or disjoint <8 x i8> %broadcast.splat, <i8 0, i8 1, i8 2, i8 3, i8 4, i8 5, i8 6, i8 7>
  br label %vec.epilog.vector.body

vec.epilog.vector.body:                           ; preds = %vec.epilog.vector.body, %vec.epilog.ph
  %index260 = phi i64 [ %bc.resume.val, %vec.epilog.ph ], [ %index.next262, %vec.epilog.vector.body ], !dbg !139
  %vec.ind261 = phi <8 x i8> [ %induction, %vec.epilog.ph ], [ %vec.ind.next263, %vec.epilog.vector.body ], !dbg !140
  %15 = mul <8 x i8> %vec.ind261, splat (i8 17), !dbg !140
  %16 = add <8 x i8> %15, splat (i8 31), !dbg !140
  %17 = getelementptr inbounds nuw i8, ptr %call85, i64 %index260, !dbg !141
  store <8 x i8> %16, ptr %17, align 1, !dbg !142, !tbaa !143
  %index.next262 = add nuw i64 %index260, 8, !dbg !139
  %vec.ind.next263 = add <8 x i8> %vec.ind261, splat (i8 8), !dbg !140
  %18 = icmp eq i64 %index.next262, %n.vec259, !dbg !144
  br i1 %18, label %vec.epilog.middle.block, label %vec.epilog.vector.body, !dbg !144, !llvm.loop !150

vec.epilog.middle.block:                          ; preds = %vec.epilog.vector.body
  %cmp.n264 = icmp eq i64 %mul84, %n.vec259, !dbg !144
  br i1 %cmp.n264, label %for.cond112.preheader, label %for.body103.preheader, !dbg !144

for.body103.preheader:                            ; preds = %vec.epilog.middle.block, %vec.epilog.iter.check, %iter.check
  %i98.0254.ph = phi i64 [ 0, %iter.check ], [ %n.vec, %vec.epilog.iter.check ], [ %n.vec259, %vec.epilog.middle.block ]
  br label %for.body103, !dbg !144

for.body:                                         ; preds = %for.inc, %entry
  %width.0248 = phi i32 [ %width.1, %for.inc ], [ 256, %entry ]
  %height.0247 = phi i32 [ %height.1, %for.inc ], [ 256, %entry ]
  %kernel_size.0246 = phi i32 [ %kernel_size.1, %for.inc ], [ 3, %entry ]
  %iterations.0245 = phi i32 [ %iterations.1, %for.inc ], [ 50, %entry ]
  %warmup_iterations.0244 = phi i32 [ %warmup_iterations.1, %for.inc ], [ 5, %entry ]
  %i.0243 = phi i32 [ %inc61, %for.inc ], [ 1, %entry ]
    #dbg_value(i32 %width.0248, !80, !DIExpression(), !100)
    #dbg_value(i32 %height.0247, !81, !DIExpression(), !100)
    #dbg_value(i32 %kernel_size.0246, !82, !DIExpression(), !100)
    #dbg_value(i32 %iterations.0245, !83, !DIExpression(), !100)
    #dbg_value(i32 %warmup_iterations.0244, !84, !DIExpression(), !100)
    #dbg_value(i32 %i.0243, !85, !DIExpression(), !101)
  %idxprom = sext i32 %i.0243 to i64, !dbg !151
  %arrayidx = getelementptr inbounds ptr, ptr %argv, i64 %idxprom, !dbg !151
  %19 = load ptr, ptr %arrayidx, align 8, !dbg !151, !tbaa !154
  %call = tail call i32 @strcmp(ptr noundef nonnull dereferenceable(1) %19, ptr noundef nonnull dereferenceable(8) @.str) #14, !dbg !157
  %cmp1 = icmp eq i32 %call, 0, !dbg !158
  br i1 %cmp1, label %land.lhs.true, label %if.else, !dbg !159

land.lhs.true:                                    ; preds = %for.body
  %add = add nsw i32 %i.0243, 1, !dbg !160
  %cmp2 = icmp slt i32 %add, %argc, !dbg !161
  br i1 %cmp2, label %if.then, label %if.else, !dbg !162

if.then:                                          ; preds = %land.lhs.true
    #dbg_value(i32 %add, !85, !DIExpression(), !101)
  %idxprom3 = sext i32 %add to i64, !dbg !163
  %arrayidx4 = getelementptr inbounds ptr, ptr %argv, i64 %idxprom3, !dbg !163
  %20 = load ptr, ptr %arrayidx4, align 8, !dbg !163, !tbaa !154
    #dbg_value(ptr %20, !165, !DIExpression(), !174)
    #dbg_value(i32 %width.0248, !172, !DIExpression(), !174)
  %call.i = tail call i32 @atoi(ptr noundef readonly %20), !dbg !176
    #dbg_value(i32 %call.i, !173, !DIExpression(), !174)
  %cmp.i = icmp sgt i32 %call.i, 0, !dbg !177
  %cond.i = select i1 %cmp.i, i32 %call.i, i32 %width.0248, !dbg !178
    #dbg_value(i32 %cond.i, !80, !DIExpression(), !100)
  br label %for.inc, !dbg !179

if.else:                                          ; preds = %land.lhs.true, %for.body
  %call8 = tail call i32 @strcmp(ptr noundef nonnull dereferenceable(1) %19, ptr noundef nonnull dereferenceable(9) @.str.1) #14, !dbg !180
  %cmp9 = icmp eq i32 %call8, 0, !dbg !182
  br i1 %cmp9, label %land.lhs.true10, label %if.else18, !dbg !183

land.lhs.true10:                                  ; preds = %if.else
  %add11 = add nsw i32 %i.0243, 1, !dbg !184
  %cmp12 = icmp slt i32 %add11, %argc, !dbg !185
  br i1 %cmp12, label %if.then13, label %if.else18, !dbg !186

if.then13:                                        ; preds = %land.lhs.true10
    #dbg_value(i32 %add11, !85, !DIExpression(), !101)
  %idxprom15 = sext i32 %add11 to i64, !dbg !187
  %arrayidx16 = getelementptr inbounds ptr, ptr %argv, i64 %idxprom15, !dbg !187
  %21 = load ptr, ptr %arrayidx16, align 8, !dbg !187, !tbaa !154
    #dbg_value(ptr %21, !165, !DIExpression(), !189)
    #dbg_value(i32 %height.0247, !172, !DIExpression(), !189)
  %call.i228 = tail call i32 @atoi(ptr noundef readonly %21), !dbg !191
    #dbg_value(i32 %call.i228, !173, !DIExpression(), !189)
  %cmp.i229 = icmp sgt i32 %call.i228, 0, !dbg !192
  %cond.i230 = select i1 %cmp.i229, i32 %call.i228, i32 %height.0247, !dbg !193
    #dbg_value(i32 %cond.i230, !81, !DIExpression(), !100)
  br label %for.inc, !dbg !194

if.else18:                                        ; preds = %land.lhs.true10, %if.else
  %call21 = tail call i32 @strcmp(ptr noundef nonnull dereferenceable(1) %19, ptr noundef nonnull dereferenceable(9) @.str.2) #14, !dbg !195
  %cmp22 = icmp eq i32 %call21, 0, !dbg !197
  br i1 %cmp22, label %land.lhs.true23, label %if.else31, !dbg !198

land.lhs.true23:                                  ; preds = %if.else18
  %add24 = add nsw i32 %i.0243, 1, !dbg !199
  %cmp25 = icmp slt i32 %add24, %argc, !dbg !200
  br i1 %cmp25, label %if.then26, label %if.else31, !dbg !201

if.then26:                                        ; preds = %land.lhs.true23
    #dbg_value(i32 %add24, !85, !DIExpression(), !101)
  %idxprom28 = sext i32 %add24 to i64, !dbg !202
  %arrayidx29 = getelementptr inbounds ptr, ptr %argv, i64 %idxprom28, !dbg !202
  %22 = load ptr, ptr %arrayidx29, align 8, !dbg !202, !tbaa !154
    #dbg_value(ptr %22, !165, !DIExpression(), !204)
    #dbg_value(i32 %kernel_size.0246, !172, !DIExpression(), !204)
  %call.i231 = tail call i32 @atoi(ptr noundef readonly %22), !dbg !206
    #dbg_value(i32 %call.i231, !173, !DIExpression(), !204)
  %cmp.i232 = icmp sgt i32 %call.i231, 0, !dbg !207
  %cond.i233 = select i1 %cmp.i232, i32 %call.i231, i32 %kernel_size.0246, !dbg !208
    #dbg_value(i32 %cond.i233, !82, !DIExpression(), !100)
  br label %for.inc, !dbg !209

if.else31:                                        ; preds = %land.lhs.true23, %if.else18
  %call34 = tail call i32 @strcmp(ptr noundef nonnull dereferenceable(1) %19, ptr noundef nonnull dereferenceable(7) @.str.3) #14, !dbg !210
  %cmp35 = icmp eq i32 %call34, 0, !dbg !212
  br i1 %cmp35, label %land.lhs.true36, label %if.else44, !dbg !213

land.lhs.true36:                                  ; preds = %if.else31
  %add37 = add nsw i32 %i.0243, 1, !dbg !214
  %cmp38 = icmp slt i32 %add37, %argc, !dbg !215
  br i1 %cmp38, label %if.then39, label %if.else44, !dbg !216

if.then39:                                        ; preds = %land.lhs.true36
    #dbg_value(i32 %add37, !85, !DIExpression(), !101)
  %idxprom41 = sext i32 %add37 to i64, !dbg !217
  %arrayidx42 = getelementptr inbounds ptr, ptr %argv, i64 %idxprom41, !dbg !217
  %23 = load ptr, ptr %arrayidx42, align 8, !dbg !217, !tbaa !154
    #dbg_value(ptr %23, !165, !DIExpression(), !219)
    #dbg_value(i32 %iterations.0245, !172, !DIExpression(), !219)
  %call.i234 = tail call i32 @atoi(ptr noundef readonly %23), !dbg !221
    #dbg_value(i32 %call.i234, !173, !DIExpression(), !219)
  %cmp.i235 = icmp sgt i32 %call.i234, 0, !dbg !222
  %cond.i236 = select i1 %cmp.i235, i32 %call.i234, i32 %iterations.0245, !dbg !223
    #dbg_value(i32 %cond.i236, !83, !DIExpression(), !100)
  br label %for.inc, !dbg !224

if.else44:                                        ; preds = %land.lhs.true36, %if.else31
  %call47 = tail call i32 @strcmp(ptr noundef nonnull dereferenceable(1) %19, ptr noundef nonnull dereferenceable(9) @.str.4) #14, !dbg !225
  %cmp48 = icmp eq i32 %call47, 0, !dbg !227
  br i1 %cmp48, label %land.lhs.true49, label %for.inc, !dbg !228

land.lhs.true49:                                  ; preds = %if.else44
  %add50 = add nsw i32 %i.0243, 1, !dbg !229
  %cmp51 = icmp slt i32 %add50, %argc, !dbg !230
  br i1 %cmp51, label %if.then52, label %for.inc, !dbg !231

if.then52:                                        ; preds = %land.lhs.true49
    #dbg_value(i32 %add50, !85, !DIExpression(), !101)
  %idxprom54 = sext i32 %add50 to i64, !dbg !232
  %arrayidx55 = getelementptr inbounds ptr, ptr %argv, i64 %idxprom54, !dbg !232
  %24 = load ptr, ptr %arrayidx55, align 8, !dbg !232, !tbaa !154
    #dbg_value(ptr %24, !165, !DIExpression(), !234)
    #dbg_value(i32 %warmup_iterations.0244, !172, !DIExpression(), !234)
  %call.i237 = tail call i32 @atoi(ptr noundef readonly %24), !dbg !236
    #dbg_value(i32 %call.i237, !173, !DIExpression(), !234)
  %cmp.i238 = icmp sgt i32 %call.i237, 0, !dbg !237
  %cond.i239 = select i1 %cmp.i238, i32 %call.i237, i32 %warmup_iterations.0244, !dbg !238
    #dbg_value(i32 %cond.i239, !84, !DIExpression(), !100)
  br label %for.inc, !dbg !239

for.inc:                                          ; preds = %if.then52, %land.lhs.true49, %if.else44, %if.then39, %if.then26, %if.then13, %if.then
  %i.1 = phi i32 [ %add, %if.then ], [ %add11, %if.then13 ], [ %add24, %if.then26 ], [ %add37, %if.then39 ], [ %add50, %if.then52 ], [ %i.0243, %land.lhs.true49 ], [ %i.0243, %if.else44 ], !dbg !101
  %warmup_iterations.1 = phi i32 [ %warmup_iterations.0244, %if.then ], [ %warmup_iterations.0244, %if.then13 ], [ %warmup_iterations.0244, %if.then26 ], [ %warmup_iterations.0244, %if.then39 ], [ %cond.i239, %if.then52 ], [ %warmup_iterations.0244, %land.lhs.true49 ], [ %warmup_iterations.0244, %if.else44 ], !dbg !100
  %iterations.1 = phi i32 [ %iterations.0245, %if.then ], [ %iterations.0245, %if.then13 ], [ %iterations.0245, %if.then26 ], [ %cond.i236, %if.then39 ], [ %iterations.0245, %if.then52 ], [ %iterations.0245, %land.lhs.true49 ], [ %iterations.0245, %if.else44 ], !dbg !100
  %kernel_size.1 = phi i32 [ %kernel_size.0246, %if.then ], [ %kernel_size.0246, %if.then13 ], [ %cond.i233, %if.then26 ], [ %kernel_size.0246, %if.then39 ], [ %kernel_size.0246, %if.then52 ], [ %kernel_size.0246, %land.lhs.true49 ], [ %kernel_size.0246, %if.else44 ], !dbg !100
  %height.1 = phi i32 [ %height.0247, %if.then ], [ %cond.i230, %if.then13 ], [ %height.0247, %if.then26 ], [ %height.0247, %if.then39 ], [ %height.0247, %if.then52 ], [ %height.0247, %land.lhs.true49 ], [ %height.0247, %if.else44 ], !dbg !100
  %width.1 = phi i32 [ %cond.i, %if.then ], [ %width.0248, %if.then13 ], [ %width.0248, %if.then26 ], [ %width.0248, %if.then39 ], [ %width.0248, %if.then52 ], [ %width.0248, %land.lhs.true49 ], [ %width.0248, %if.else44 ], !dbg !100
    #dbg_value(i32 %width.1, !80, !DIExpression(), !100)
    #dbg_value(i32 %height.1, !81, !DIExpression(), !100)
    #dbg_value(i32 %kernel_size.1, !82, !DIExpression(), !100)
    #dbg_value(i32 %iterations.1, !83, !DIExpression(), !100)
    #dbg_value(i32 %warmup_iterations.1, !84, !DIExpression(), !100)
    #dbg_value(i32 %i.1, !85, !DIExpression(), !101)
  %inc61 = add nsw i32 %i.1, 1, !dbg !240
    #dbg_value(i32 %inc61, !85, !DIExpression(), !101)
  %cmp = icmp slt i32 %inc61, %argc, !dbg !241
  br i1 %cmp, label %for.body, label %for.cond.cleanup.loopexit, !dbg !242, !llvm.loop !243

if.then95:                                        ; preds = %for.cond.cleanup
  tail call void @free(ptr noundef %call85), !dbg !246
  tail call void @free(ptr noundef %call86), !dbg !248
  tail call void @free(ptr noundef %call91), !dbg !249
  %25 = load ptr, ptr @__stderrp, align 8, !dbg !250, !tbaa !251
  %26 = tail call i64 @fwrite(ptr nonnull @.str.6, i64 18, i64 1, ptr %25), !dbg !253
  br label %cleanup, !dbg !254

for.cond112.preheader:                            ; preds = %for.body103, %vec.epilog.middle.block, %middle.block, %for.cond99.preheader
  %mul113 = mul nuw nsw i32 %spec.select, %spec.select
    #dbg_value(i32 0, !95, !DIExpression(), !255)
  %27 = zext nneg i32 %mul113 to i64, !dbg !256
  tail call void @llvm.experimental.memset.pattern.p0.i32.i64(ptr nonnull align 4 %call91, i32 1, i64 %27, i1 false), !dbg !257, !tbaa !68
    #dbg_value(i64 poison, !95, !DIExpression(), !255)
  br label %while.cond, !dbg !259

for.body103:                                      ; preds = %for.body103, %for.body103.preheader
  %i98.0254 = phi i64 [ %inc109, %for.body103 ], [ %i98.0254.ph, %for.body103.preheader ]
    #dbg_value(i64 %i98.0254, !93, !DIExpression(), !134)
  %28 = trunc i64 %i98.0254 to i8, !dbg !140
  %29 = mul i8 %28, 17, !dbg !140
  %conv106 = add i8 %29, 31, !dbg !140
  %arrayidx107 = getelementptr inbounds nuw i8, ptr %call85, i64 %i98.0254, !dbg !141
  store i8 %conv106, ptr %arrayidx107, align 1, !dbg !142, !tbaa !143
  %inc109 = add nuw i64 %i98.0254, 1, !dbg !139
    #dbg_value(i64 %inc109, !93, !DIExpression(), !134)
  %exitcond.not = icmp eq i64 %inc109, %mul84, !dbg !260
  br i1 %exitcond.not, label %for.cond112.preheader, label %for.body103, !dbg !144, !llvm.loop !261

while.cond:                                       ; preds = %while.cond, %for.cond112.preheader
  %shift_r.0 = phi i32 [ %add124, %while.cond ], [ 0, %for.cond112.preheader ], !dbg !100
    #dbg_value(i32 %shift_r.0, !98, !DIExpression(), !100)
  %shl = shl nuw i32 2, %shift_r.0, !dbg !262
  %cmp125 = icmp slt i32 %shl, %mul113, !dbg !263
  %add124 = add nuw nsw i32 %shift_r.0, 1, !dbg !264
    #dbg_value(i32 %add124, !98, !DIExpression(), !100)
  br i1 %cmp125, label %while.cond, label %while.end, !dbg !265, !llvm.loop !266

while.end:                                        ; preds = %while.cond
  tail call fastcc void @run_kernel_int(i32 noundef %width.2, i32 noundef %height.2, i32 noundef %spec.select, i32 noundef %warmup_iterations.0.lcssa, ptr noundef %call85, ptr noundef %call86, ptr noundef %call91, i32 noundef %shift_r.0), !dbg !268
  tail call fastcc void @run_kernel_int(i32 noundef %width.2, i32 noundef %height.2, i32 noundef %spec.select, i32 noundef %iterations.0.lcssa, ptr noundef %call85, ptr noundef %call86, ptr noundef %call91, i32 noundef %shift_r.0), !dbg !269
    #dbg_value(ptr %call86, !270, !DIExpression(), !283)
    #dbg_value(i64 %mul84, !279, !DIExpression(), !283)
    #dbg_value(i64 0, !280, !DIExpression(), !283)
    #dbg_value(i64 0, !281, !DIExpression(), !285)
  br i1 %cmp100253.not, label %checksum.exit, label %for.body.i, !dbg !286

for.body.i:                                       ; preds = %for.body.i, %while.end
  %i.06.i = phi i64 [ %inc.i, %for.body.i ], [ 0, %while.end ]
  %sum.05.i = phi i64 [ %add.i, %for.body.i ], [ 0, %while.end ]
    #dbg_value(i64 %i.06.i, !281, !DIExpression(), !285)
    #dbg_value(i64 %sum.05.i, !280, !DIExpression(), !283)
  %mul.i = mul i64 %sum.05.i, 131, !dbg !287
  %arrayidx.i = getelementptr inbounds nuw i8, ptr %call86, i64 %i.06.i, !dbg !289
  %30 = load i8, ptr %arrayidx.i, align 1, !dbg !289, !tbaa !143
  %conv.i = zext i8 %30 to i64, !dbg !289
  %add.i = add i64 %mul.i, %conv.i, !dbg !290
    #dbg_value(i64 %add.i, !280, !DIExpression(), !283)
  %inc.i = add nuw i64 %i.06.i, 1, !dbg !291
    #dbg_value(i64 %inc.i, !281, !DIExpression(), !285)
  %exitcond.not.i = icmp eq i64 %inc.i, %mul84, !dbg !292
  br i1 %exitcond.not.i, label %checksum.exit, label %for.body.i, !dbg !293, !llvm.loop !294

checksum.exit:                                    ; preds = %for.body.i, %while.end
  %sum.0.lcssa.i = phi i64 [ 0, %while.end ], [ %add.i, %for.body.i ], !dbg !283
  %call129 = tail call i32 (ptr, ...) @printf(ptr noundef nonnull dereferenceable(1) @.str.7, i64 noundef %sum.0.lcssa.i), !dbg !297
  tail call void @free(ptr noundef nonnull %call85), !dbg !298
  tail call void @free(ptr noundef nonnull %call86), !dbg !299
  tail call void @free(ptr noundef nonnull %call91), !dbg !300
  br label %cleanup

cleanup:                                          ; preds = %checksum.exit, %if.then95
  %retval.0 = phi i32 [ 0, %checksum.exit ], [ 1, %if.then95 ], !dbg !100
  ret i32 %retval.0, !dbg !301
}

; Function Attrs: mustprogress nocallback nofree nounwind willreturn memory(argmem: read)
declare !dbg !302 i32 @strcmp(ptr noundef captures(none), ptr noundef captures(none)) local_unnamed_addr #1

; Function Attrs: nofree nounwind
declare !dbg !306 noundef i32 @printf(ptr noundef readonly captures(none), ...) local_unnamed_addr #2

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite)
declare !dbg !311 noalias noundef ptr @malloc(i64 noundef) local_unnamed_addr #3

; Function Attrs: mustprogress nofree nounwind willreturn allockind("alloc,zeroed") allocsize(0,1) memory(inaccessiblemem: readwrite)
declare !dbg !316 noalias noundef ptr @calloc(i64 noundef, i64 noundef) local_unnamed_addr #4

; Function Attrs: mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite)
declare !dbg !319 void @free(ptr allocptr noundef captures(none)) local_unnamed_addr #5

; Function Attrs: nofree noinline norecurse nosync nounwind ssp memory(argmem: readwrite)
define internal fastcc void @run_kernel_int(i32 noundef %width, i32 noundef %height, i32 noundef %kernel_size, i32 noundef %iterations, ptr noundef nonnull readonly captures(none) "tainted-pointee" %source, ptr noundef nonnull writeonly captures(none) %target, ptr noundef nonnull readonly captures(none) %kernel, i32 noundef %shift_r) unnamed_addr #6 !dbg !322 {
entry:
    #dbg_value(i32 %width, !328, !DIExpression(), !351)
    #dbg_value(i32 %height, !329, !DIExpression(), !351)
    #dbg_value(i32 %kernel_size, !330, !DIExpression(), !351)
    #dbg_value(i32 %iterations, !331, !DIExpression(), !351)
    #dbg_value(ptr %source, !332, !DIExpression(), !351)
    #dbg_value(ptr %target, !333, !DIExpression(), !351)
    #dbg_value(ptr %kernel, !334, !DIExpression(), !351)
    #dbg_value(i32 0, !335, !DIExpression(), !351)
    #dbg_value(i32 %shift_r, !336, !DIExpression(), !351)
  %mul = shl nsw i32 %width, 2, !dbg !352
    #dbg_value(i32 %mul, !337, !DIExpression(), !351)
  %div = sdiv i32 %kernel_size, 2, !dbg !353
    #dbg_value(i32 %div, !338, !DIExpression(), !351)
    #dbg_value(i32 %div, !339, !DIExpression(), !351)
  %add = add nsw i32 %div, 1, !dbg !354
    #dbg_value(i32 %add, !340, !DIExpression(), !351)
    #dbg_value(i32 0, !341, !DIExpression(), !355)
  %cmp38 = icmp sgt i32 %iterations, 0, !dbg !356
  br i1 %cmp38, label %for.cond3.preheader.lr.ph, label %for.cond.cleanup, !dbg !357

for.cond3.preheader.lr.ph:                        ; preds = %entry
  %sub = sub nsw i32 %height, %add
  %cmp436 = icmp slt i32 %add, %sub
  %sub8 = sub i32 %width, %add
  %cmp934 = icmp slt i32 %add, %sub8
  br label %for.cond3.preheader, !dbg !357

for.cond3.preheader:                              ; preds = %for.cond.cleanup5, %for.cond3.preheader.lr.ph
  %iter.039 = phi i32 [ 0, %for.cond3.preheader.lr.ph ], [ %inc16, %for.cond.cleanup5 ]
    #dbg_value(i32 %iter.039, !341, !DIExpression(), !355)
    #dbg_value(i32 %add, !343, !DIExpression(), !358)
  br i1 %cmp436, label %for.cond7.preheader, label %for.cond.cleanup5, !dbg !359

for.cond.cleanup:                                 ; preds = %for.cond.cleanup5, %entry
  ret void, !dbg !360

for.cond7.preheader:                              ; preds = %for.cond.cleanup10, %for.cond3.preheader
  %y.037 = phi i32 [ %inc13, %for.cond.cleanup10 ], [ %add, %for.cond3.preheader ]
    #dbg_value(i32 %y.037, !343, !DIExpression(), !358)
    #dbg_value(i32 %add, !347, !DIExpression(), !361)
  br i1 %cmp934, label %for.body11, label %for.cond.cleanup10, !dbg !362

for.cond.cleanup5:                                ; preds = %for.cond.cleanup10, %for.cond3.preheader
  %inc16 = add nuw nsw i32 %iter.039, 1, !dbg !363
    #dbg_value(i32 %inc16, !341, !DIExpression(), !355)
  %exitcond41.not = icmp eq i32 %inc16, %iterations, !dbg !364
  br i1 %exitcond41.not, label %for.cond.cleanup, label %for.cond3.preheader, !dbg !365, !llvm.loop !366

for.cond.cleanup10:                               ; preds = %for.body11, %for.cond7.preheader
  %inc13 = add i32 %y.037, 1, !dbg !369
    #dbg_value(i32 %inc13, !343, !DIExpression(), !358)
  %exitcond40.not = icmp eq i32 %inc13, %sub, !dbg !370
  br i1 %exitcond40.not, label %for.cond.cleanup5, label %for.cond7.preheader, !dbg !371, !llvm.loop !372

for.body11:                                       ; preds = %for.body11, %for.cond7.preheader
  %x.035 = phi i32 [ %inc, %for.body11 ], [ %add, %for.cond7.preheader ]
    #dbg_value(i32 %x.035, !347, !DIExpression(), !361)
  tail call fastcc void @convolve_pixel_int(ptr noundef %source, ptr noundef %target, i32 noundef %mul, i32 noundef %mul, i32 noundef %x.035, i32 noundef %y.037, ptr noundef %kernel, i32 noundef %shift_r, i32 noundef %kernel_size, i32 noundef %kernel_size, i32 noundef %div, i32 noundef %div), !dbg !375
  %inc = add i32 %x.035, 1, !dbg !378
    #dbg_value(i32 %inc, !347, !DIExpression(), !361)
  %exitcond.not = icmp eq i32 %inc, %sub8, !dbg !379
  br i1 %exitcond.not, label %for.cond.cleanup10, label %for.body11, !dbg !380, !llvm.loop !381
}

; Function Attrs: mustprogress nocallback nofree nounwind willreturn memory(read)
declare !dbg !384 i32 @atoi(ptr noundef captures(none)) local_unnamed_addr #7

; Function Attrs: nofree noinline norecurse nosync nounwind ssp memory(argmem: readwrite)
define internal fastcc void @convolve_pixel_int(ptr noundef nonnull readonly captures(none) "tainted-pointee" %source_data, ptr noundef nonnull writeonly captures(none) %target_data, i32 noundef %source_stride, i32 noundef %target_stride, i32 noundef %x, i32 noundef %y, ptr noundef nonnull readonly captures(none) %kernel, i32 noundef %shift_r, i32 noundef %order_x, i32 noundef %order_y, i32 noundef range(i32 -1073741824, 1073741824) %target_x, i32 noundef range(i32 -1073741824, 1073741824) %target_y) unnamed_addr #6 !dbg !388 {
entry:
    #dbg_value(ptr %source_data, !392, !DIExpression(), !436)
    #dbg_value(ptr %target_data, !393, !DIExpression(), !436)
    #dbg_value(i32 poison, !394, !DIExpression(), !436)
    #dbg_value(i32 poison, !395, !DIExpression(), !436)
    #dbg_value(i32 %source_stride, !396, !DIExpression(), !436)
    #dbg_value(i32 %target_stride, !397, !DIExpression(), !436)
    #dbg_value(i32 %x, !398, !DIExpression(), !436)
    #dbg_value(i32 %y, !399, !DIExpression(), !436)
    #dbg_value(ptr %kernel, !400, !DIExpression(), !436)
    #dbg_value(i32 0, !401, !DIExpression(), !436)
    #dbg_value(i32 0, !402, !DIExpression(), !436)
    #dbg_value(i32 %shift_r, !403, !DIExpression(), !436)
    #dbg_value(i32 0, !404, !DIExpression(), !436)
    #dbg_value(i32 %order_x, !405, !DIExpression(), !436)
    #dbg_value(i32 %order_y, !406, !DIExpression(), !436)
    #dbg_value(i32 %target_x, !407, !DIExpression(), !436)
    #dbg_value(i32 %target_y, !408, !DIExpression(), !436)
    #dbg_value(i32 1, !409, !DIExpression(), !436)
    #dbg_value(i32 1, !410, !DIExpression(), !436)
    #dbg_value(i32 0, !411, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !436)
    #dbg_value(i32 0, !411, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !436)
    #dbg_value(i32 0, !411, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !436)
    #dbg_value(i32 0, !411, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !436)
    #dbg_declare(ptr poison, !415, !DIExpression(), !437)
    #dbg_value(i32 4, !416, !DIExpression(), !436)
    #dbg_value(i32 0, !417, !DIExpression(), !436)
    #dbg_value(i32 0, !418, !DIExpression(), !438)
  %cmp2102 = icmp sgt i32 %order_y, 0, !dbg !439
  br i1 %cmp2102, label %for.body.lr.ph, label %for.cond30.preheader, !dbg !440

for.body.lr.ph:                                   ; preds = %entry
  %sub3 = sub i32 %y, %target_y
  %cmp5100 = icmp sgt i32 %order_x, 0
  %sub8 = sub i32 %x, %target_x
  %0 = sext i32 %order_x to i64, !dbg !440
  %wide.trip.count112 = zext nneg i32 %order_y to i64, !dbg !441
  %wide.trip.count = zext i32 %order_x to i64
  %1 = add nsw i64 %wide.trip.count, -1, !dbg !440
  %2 = sub i32 %y, %target_y, !dbg !440
  %3 = mul i32 %source_stride, %2, !dbg !440
  %4 = shl i32 %x, 2, !dbg !440
  %5 = add i32 %3, %4, !dbg !440
  %6 = shl i32 %target_x, 2, !dbg !440
  %7 = sub i32 %5, %6, !dbg !440
  %min.iters.check = icmp ult i32 %order_x, 8
  %8 = trunc i64 %1 to i32
  %mul.result = shl i32 %8, 2
  %9 = icmp ugt i64 %1, 1073741823
  %min.iters.check1 = icmp ult i32 %order_x, 16
  %n.mod.vf = and i64 %wide.trip.count, 8
  %n.vec = and i64 %wide.trip.count, 2147483632
  %cmp.n = icmp eq i64 %n.vec, %wide.trip.count
  %min.epilog.iters.check.not.not = icmp eq i64 %n.mod.vf, 0
  %n.vec12 = and i64 %wide.trip.count, 2147483640
  %cmp.n25 = icmp eq i64 %n.vec12, %wide.trip.count
  br label %for.body, !dbg !440

for.cond30.preheader.loopexit:                    ; preds = %for.cond.cleanup6
  %10 = tail call range(i32 0, -2147483648) i32 @llvm.smax.i32(i32 %sum.sroa.0.1, i32 0), !dbg !442
  %11 = tail call range(i32 0, -2147483648) i32 @llvm.smax.i32(i32 %sum.sroa.6.1, i32 0), !dbg !449
  %12 = tail call range(i32 0, -2147483648) i32 @llvm.smax.i32(i32 %sum.sroa.9.1, i32 0), !dbg !450
  %13 = tail call range(i32 0, -2147483648) i32 @llvm.smax.i32(i32 %sum.sroa.12.1, i32 0), !dbg !451
  %14 = insertelement <4 x i32> poison, i32 %12, i64 0, !dbg !436
  %15 = insertelement <4 x i32> %14, i32 %11, i64 1, !dbg !436
  %16 = insertelement <4 x i32> %15, i32 %10, i64 2, !dbg !436
  %17 = insertelement <4 x i32> %16, i32 %13, i64 3, !dbg !436
  br label %for.cond30.preheader

for.cond30.preheader:                             ; preds = %for.cond30.preheader.loopexit, %entry
  %18 = phi <4 x i32> [ %17, %for.cond30.preheader.loopexit ], [ zeroinitializer, %entry ], !dbg !436
    #dbg_value(i32 poison, !411, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !436)
    #dbg_value(i32 poison, !411, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !436)
    #dbg_value(i32 poison, !411, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !436)
    #dbg_value(i32 poison, !411, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !436)
  %shr = lshr i32 255, %shift_r
  %mul45 = mul nsw i32 %y, %target_stride
  %mul46 = shl nsw i32 %x, 2
  %add47 = add nsw i32 %mul45, %mul46
    #dbg_value(i64 0, !431, !DIExpression(), !452)
    #dbg_value(i32 poison, !447, !DIExpression(), !453)
    #dbg_value(i32 poison, !454, !DIExpression(), !460)
    #dbg_value(i32 %shr, !459, !DIExpression(), !460)
    #dbg_value(i32 poison, !433, !DIExpression(), !462)
    #dbg_value(i64 1, !431, !DIExpression(), !452)
    #dbg_value(i32 poison, !447, !DIExpression(), !453)
    #dbg_value(i32 poison, !454, !DIExpression(), !460)
    #dbg_value(i32 poison, !433, !DIExpression(), !462)
    #dbg_value(i64 2, !431, !DIExpression(), !452)
    #dbg_value(i32 poison, !447, !DIExpression(), !453)
    #dbg_value(i32 poison, !454, !DIExpression(), !460)
    #dbg_value(i32 poison, !433, !DIExpression(), !462)
  %idxprom51.2 = sext i32 %add47 to i64, !dbg !463
  %arrayidx52.2 = getelementptr inbounds i8, ptr %target_data, i64 %idxprom51.2, !dbg !463
    #dbg_value(i64 3, !431, !DIExpression(), !452)
    #dbg_value(i32 poison, !447, !DIExpression(), !453)
    #dbg_value(i32 poison, !454, !DIExpression(), !460)
    #dbg_value(i32 poison, !433, !DIExpression(), !462)
  %19 = insertelement <4 x i32> poison, i32 %shr, i64 0, !dbg !464
  %20 = shufflevector <4 x i32> %19, <4 x i32> poison, <4 x i32> zeroinitializer, !dbg !464
  %21 = tail call <4 x i32> @llvm.umin.v4i32(<4 x i32> %18, <4 x i32> %20), !dbg !464
  %22 = insertelement <4 x i32> poison, i32 %shift_r, i64 0, !dbg !465
  %23 = shufflevector <4 x i32> %22, <4 x i32> poison, <4 x i32> zeroinitializer, !dbg !465
  %24 = shl <4 x i32> %21, %23, !dbg !465
  %25 = trunc <4 x i32> %24 to <4 x i8>, !dbg !466
  store <4 x i8> %25, ptr %arrayidx52.2, align 1, !dbg !467, !tbaa !143
    #dbg_value(i64 4, !431, !DIExpression(), !452)
  ret void, !dbg !468

for.body:                                         ; preds = %for.cond.cleanup6, %for.body.lr.ph
  %sum.sroa.0.0 = phi i32 [ 0, %for.body.lr.ph ], [ %sum.sroa.0.1, %for.cond.cleanup6 ], !dbg !436
  %sum.sroa.6.0 = phi i32 [ 0, %for.body.lr.ph ], [ %sum.sroa.6.1, %for.cond.cleanup6 ], !dbg !436
  %sum.sroa.9.0 = phi i32 [ 0, %for.body.lr.ph ], [ %sum.sroa.9.1, %for.cond.cleanup6 ], !dbg !436
  %sum.sroa.12.0 = phi i32 [ 0, %for.body.lr.ph ], [ %sum.sroa.12.1, %for.cond.cleanup6 ], !dbg !436
  %indvars.iv108 = phi i64 [ 0, %for.body.lr.ph ], [ %indvars.iv.next109, %for.cond.cleanup6 ]
    #dbg_value(i32 %sum.sroa.12.0, !411, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !436)
    #dbg_value(i32 %sum.sroa.9.0, !411, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !436)
    #dbg_value(i32 %sum.sroa.6.0, !411, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !436)
    #dbg_value(i32 %sum.sroa.0.0, !411, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !436)
    #dbg_value(i64 %indvars.iv108, !418, !DIExpression(), !438)
    #dbg_value(!DIArgList(i32 %sub3, i64 %indvars.iv108), !420, !DIExpression(DW_OP_LLVM_arg, 0, DW_OP_LLVM_arg, 1, DW_OP_plus, DW_OP_stack_value), !469)
    #dbg_value(i32 0, !423, !DIExpression(), !470)
  %26 = trunc i64 %indvars.iv108 to i32, !dbg !471
  %27 = mul i32 %source_stride, %26, !dbg !471
  %28 = add i32 %27, %7, !dbg !471
  br i1 %cmp5100, label %iter.check, label %for.cond.cleanup6, !dbg !471

iter.check:                                       ; preds = %for.body
  %29 = trunc nuw nsw i64 %indvars.iv108 to i32, !dbg !472
  %add = add i32 %sub3, %29, !dbg !472
    #dbg_value(i32 %add, !420, !DIExpression(), !469)
  %30 = mul nuw nsw i64 %indvars.iv108, %0
  %mul.i = mul nsw i32 %add, %source_stride
  %invariant.gep = getelementptr i32, ptr %kernel, i64 %30, !dbg !471
  br i1 %min.iters.check, label %for.body7.preheader, label %vector.scevcheck, !dbg !473

vector.scevcheck:                                 ; preds = %iter.check
  %31 = add i32 %28, %mul.result, !dbg !471
  %32 = icmp slt i32 %31, %28, !dbg !471
  %33 = or i1 %32, %9, !dbg !471
  br i1 %33, label %for.body7.preheader, label %vector.main.loop.iter.check, !dbg !474

vector.main.loop.iter.check:                      ; preds = %vector.scevcheck
  br i1 %min.iters.check1, label %vec.epilog.ph, label %vector.ph, !dbg !473

vector.ph:                                        ; preds = %vector.main.loop.iter.check
  %34 = insertelement <16 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %sum.sroa.0.0, i64 0
  %35 = insertelement <16 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %sum.sroa.6.0, i64 0
  %36 = insertelement <16 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %sum.sroa.9.0, i64 0
  %37 = insertelement <16 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %sum.sroa.12.0, i64 0
  br label %vector.body, !dbg !473

vector.body:                                      ; preds = %vector.body, %vector.ph
  %index = phi i64 [ 0, %vector.ph ], [ %index.next, %vector.body ], !dbg !474
  %vec.phi = phi <16 x i32> [ %34, %vector.ph ], [ %47, %vector.body ]
  %vec.phi2 = phi <16 x i32> [ %35, %vector.ph ], [ %50, %vector.body ]
  %vec.phi3 = phi <16 x i32> [ %36, %vector.ph ], [ %53, %vector.body ]
  %vec.phi4 = phi <16 x i32> [ %37, %vector.ph ], [ %56, %vector.body ]
  %38 = trunc i64 %index to i32, !dbg !475
  %39 = add i32 %sub8, %38, !dbg !475
  %40 = getelementptr i32, ptr %invariant.gep, i64 %index, !dbg !476
  %wide.load = load <16 x i32>, ptr %40, align 4, !dbg !477, !tbaa !68
  %41 = shl nsw i32 %39, 2
  %42 = add nsw i32 %41, %mul.i
  %43 = sext i32 %42 to i64, !dbg !478
  %44 = getelementptr i8, ptr %source_data, i64 %43, !dbg !478
  %wide.vec = load <64 x i8>, ptr %44, align 1, !dbg !492, !tbaa !143
  %strided.vec = shufflevector <64 x i8> %wide.vec, <64 x i8> poison, <16 x i32> <i32 0, i32 4, i32 8, i32 12, i32 16, i32 20, i32 24, i32 28, i32 32, i32 36, i32 40, i32 44, i32 48, i32 52, i32 56, i32 60>, !dbg !492
  %strided.vec5 = shufflevector <64 x i8> %wide.vec, <64 x i8> poison, <16 x i32> <i32 1, i32 5, i32 9, i32 13, i32 17, i32 21, i32 25, i32 29, i32 33, i32 37, i32 41, i32 45, i32 49, i32 53, i32 57, i32 61>, !dbg !492
  %strided.vec6 = shufflevector <64 x i8> %wide.vec, <64 x i8> poison, <16 x i32> <i32 2, i32 6, i32 10, i32 14, i32 18, i32 22, i32 26, i32 30, i32 34, i32 38, i32 42, i32 46, i32 50, i32 54, i32 58, i32 62>, !dbg !492
  %strided.vec7 = shufflevector <64 x i8> %wide.vec, <64 x i8> poison, <16 x i32> <i32 3, i32 7, i32 11, i32 15, i32 19, i32 23, i32 27, i32 31, i32 35, i32 39, i32 43, i32 47, i32 51, i32 55, i32 59, i32 63>, !dbg !492
  %45 = zext <16 x i8> %strided.vec6 to <16 x i32>, !dbg !493
  %46 = mul nsw <16 x i32> %wide.load, %45, !dbg !494
  %47 = add <16 x i32> %46, %vec.phi, !dbg !495
  %48 = zext <16 x i8> %strided.vec5 to <16 x i32>, !dbg !493
  %49 = mul nsw <16 x i32> %wide.load, %48, !dbg !494
  %50 = add <16 x i32> %49, %vec.phi2, !dbg !496
  %51 = zext <16 x i8> %strided.vec to <16 x i32>, !dbg !493
  %52 = mul nsw <16 x i32> %wide.load, %51, !dbg !494
  %53 = add <16 x i32> %52, %vec.phi3, !dbg !497
  %54 = zext <16 x i8> %strided.vec7 to <16 x i32>, !dbg !493
  %55 = mul nsw <16 x i32> %wide.load, %54, !dbg !494
  %56 = add <16 x i32> %55, %vec.phi4, !dbg !498
  %index.next = add nuw i64 %index, 16, !dbg !474
  %57 = icmp eq i64 %index.next, %n.vec, !dbg !499
  br i1 %57, label %middle.block, label %vector.body, !dbg !499, !llvm.loop !500

middle.block:                                     ; preds = %vector.body
  %58 = tail call i32 @llvm.vector.reduce.add.v16i32(<16 x i32> %47), !dbg !499
  %59 = tail call i32 @llvm.vector.reduce.add.v16i32(<16 x i32> %50), !dbg !499
  %60 = tail call i32 @llvm.vector.reduce.add.v16i32(<16 x i32> %53), !dbg !499
  %61 = tail call i32 @llvm.vector.reduce.add.v16i32(<16 x i32> %56), !dbg !499
  br i1 %cmp.n, label %for.cond.cleanup6, label %vec.epilog.iter.check, !dbg !499

vec.epilog.iter.check:                            ; preds = %middle.block
  br i1 %min.epilog.iters.check.not.not, label %for.body7.preheader, label %vec.epilog.ph, !prof !501

vec.epilog.ph:                                    ; preds = %vec.epilog.iter.check, %vector.main.loop.iter.check
  %vec.epilog.resume.val = phi i64 [ %n.vec, %vec.epilog.iter.check ], [ 0, %vector.main.loop.iter.check ]
  %bc.merge.rdx = phi i32 [ %58, %vec.epilog.iter.check ], [ %sum.sroa.0.0, %vector.main.loop.iter.check ], !dbg !436
  %bc.merge.rdx8 = phi i32 [ %59, %vec.epilog.iter.check ], [ %sum.sroa.6.0, %vector.main.loop.iter.check ], !dbg !436
  %bc.merge.rdx9 = phi i32 [ %60, %vec.epilog.iter.check ], [ %sum.sroa.9.0, %vector.main.loop.iter.check ], !dbg !436
  %bc.merge.rdx10 = phi i32 [ %61, %vec.epilog.iter.check ], [ %sum.sroa.12.0, %vector.main.loop.iter.check ], !dbg !436
  %62 = insertelement <8 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %bc.merge.rdx, i64 0
  %63 = insertelement <8 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %bc.merge.rdx8, i64 0
  %64 = insertelement <8 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %bc.merge.rdx9, i64 0
  %65 = insertelement <8 x i32> <i32 poison, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0, i32 0>, i32 %bc.merge.rdx10, i64 0
  br label %vec.epilog.vector.body

vec.epilog.vector.body:                           ; preds = %vec.epilog.vector.body, %vec.epilog.ph
  %index13 = phi i64 [ %vec.epilog.resume.val, %vec.epilog.ph ], [ %index.next24, %vec.epilog.vector.body ], !dbg !474
  %vec.phi14 = phi <8 x i32> [ %62, %vec.epilog.ph ], [ %75, %vec.epilog.vector.body ]
  %vec.phi15 = phi <8 x i32> [ %63, %vec.epilog.ph ], [ %78, %vec.epilog.vector.body ]
  %vec.phi16 = phi <8 x i32> [ %64, %vec.epilog.ph ], [ %81, %vec.epilog.vector.body ]
  %vec.phi17 = phi <8 x i32> [ %65, %vec.epilog.ph ], [ %84, %vec.epilog.vector.body ]
  %66 = trunc i64 %index13 to i32, !dbg !475
  %67 = add i32 %sub8, %66, !dbg !475
  %68 = getelementptr i32, ptr %invariant.gep, i64 %index13, !dbg !476
  %wide.load18 = load <8 x i32>, ptr %68, align 4, !dbg !477, !tbaa !68
  %69 = shl nsw i32 %67, 2
  %70 = add nsw i32 %69, %mul.i
  %71 = sext i32 %70 to i64, !dbg !478
  %72 = getelementptr i8, ptr %source_data, i64 %71, !dbg !478
  %wide.vec19 = load <32 x i8>, ptr %72, align 1, !dbg !492, !tbaa !143
  %strided.vec20 = shufflevector <32 x i8> %wide.vec19, <32 x i8> poison, <8 x i32> <i32 0, i32 4, i32 8, i32 12, i32 16, i32 20, i32 24, i32 28>, !dbg !492
  %strided.vec21 = shufflevector <32 x i8> %wide.vec19, <32 x i8> poison, <8 x i32> <i32 1, i32 5, i32 9, i32 13, i32 17, i32 21, i32 25, i32 29>, !dbg !492
  %strided.vec22 = shufflevector <32 x i8> %wide.vec19, <32 x i8> poison, <8 x i32> <i32 2, i32 6, i32 10, i32 14, i32 18, i32 22, i32 26, i32 30>, !dbg !492
  %strided.vec23 = shufflevector <32 x i8> %wide.vec19, <32 x i8> poison, <8 x i32> <i32 3, i32 7, i32 11, i32 15, i32 19, i32 23, i32 27, i32 31>, !dbg !492
  %73 = zext <8 x i8> %strided.vec22 to <8 x i32>, !dbg !493
  %74 = mul nsw <8 x i32> %wide.load18, %73, !dbg !494
  %75 = add <8 x i32> %74, %vec.phi14, !dbg !495
  %76 = zext <8 x i8> %strided.vec21 to <8 x i32>, !dbg !493
  %77 = mul nsw <8 x i32> %wide.load18, %76, !dbg !494
  %78 = add <8 x i32> %77, %vec.phi15, !dbg !496
  %79 = zext <8 x i8> %strided.vec20 to <8 x i32>, !dbg !493
  %80 = mul nsw <8 x i32> %wide.load18, %79, !dbg !494
  %81 = add <8 x i32> %80, %vec.phi16, !dbg !497
  %82 = zext <8 x i8> %strided.vec23 to <8 x i32>, !dbg !493
  %83 = mul nsw <8 x i32> %wide.load18, %82, !dbg !494
  %84 = add <8 x i32> %83, %vec.phi17, !dbg !498
  %index.next24 = add nuw i64 %index13, 8, !dbg !474
  %85 = icmp eq i64 %index.next24, %n.vec12, !dbg !499
  br i1 %85, label %vec.epilog.middle.block, label %vec.epilog.vector.body, !dbg !499, !llvm.loop !502

vec.epilog.middle.block:                          ; preds = %vec.epilog.vector.body
  %86 = tail call i32 @llvm.vector.reduce.add.v8i32(<8 x i32> %75), !dbg !499
  %87 = tail call i32 @llvm.vector.reduce.add.v8i32(<8 x i32> %78), !dbg !499
  %88 = tail call i32 @llvm.vector.reduce.add.v8i32(<8 x i32> %81), !dbg !499
  %89 = tail call i32 @llvm.vector.reduce.add.v8i32(<8 x i32> %84), !dbg !499
  br i1 %cmp.n25, label %for.cond.cleanup6, label %for.body7.preheader, !dbg !499

for.body7.preheader:                              ; preds = %vec.epilog.middle.block, %vec.epilog.iter.check, %vector.scevcheck, %iter.check
  %sum.sroa.0.2.ph = phi i32 [ %sum.sroa.0.0, %iter.check ], [ %sum.sroa.0.0, %vector.scevcheck ], [ %58, %vec.epilog.iter.check ], [ %86, %vec.epilog.middle.block ]
  %sum.sroa.6.2.ph = phi i32 [ %sum.sroa.6.0, %iter.check ], [ %sum.sroa.6.0, %vector.scevcheck ], [ %59, %vec.epilog.iter.check ], [ %87, %vec.epilog.middle.block ]
  %sum.sroa.9.2.ph = phi i32 [ %sum.sroa.9.0, %iter.check ], [ %sum.sroa.9.0, %vector.scevcheck ], [ %60, %vec.epilog.iter.check ], [ %88, %vec.epilog.middle.block ]
  %sum.sroa.12.2.ph = phi i32 [ %sum.sroa.12.0, %iter.check ], [ %sum.sroa.12.0, %vector.scevcheck ], [ %61, %vec.epilog.iter.check ], [ %89, %vec.epilog.middle.block ]
  %indvars.iv.ph = phi i64 [ 0, %iter.check ], [ 0, %vector.scevcheck ], [ %n.vec, %vec.epilog.iter.check ], [ %n.vec12, %vec.epilog.middle.block ]
  br label %for.body7, !dbg !473

for.cond.cleanup6:                                ; preds = %for.body7, %vec.epilog.middle.block, %middle.block, %for.body
  %sum.sroa.0.1 = phi i32 [ %sum.sroa.0.0, %for.body ], [ %86, %vec.epilog.middle.block ], [ %58, %middle.block ], [ %add22, %for.body7 ], !dbg !436
  %sum.sroa.6.1 = phi i32 [ %sum.sroa.6.0, %for.body ], [ %87, %vec.epilog.middle.block ], [ %59, %middle.block ], [ %add22.1, %for.body7 ], !dbg !436
  %sum.sroa.9.1 = phi i32 [ %sum.sroa.9.0, %for.body ], [ %88, %vec.epilog.middle.block ], [ %60, %middle.block ], [ %add22.2, %for.body7 ], !dbg !436
  %sum.sroa.12.1 = phi i32 [ %sum.sroa.12.0, %for.body ], [ %89, %vec.epilog.middle.block ], [ %61, %middle.block ], [ %add22.3, %for.body7 ], !dbg !436
    #dbg_value(i32 %sum.sroa.12.1, !411, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !436)
    #dbg_value(i32 %sum.sroa.9.1, !411, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !436)
    #dbg_value(i32 %sum.sroa.6.1, !411, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !436)
    #dbg_value(i32 %sum.sroa.0.1, !411, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !436)
  %indvars.iv.next109 = add nuw nsw i64 %indvars.iv108, 1, !dbg !503
    #dbg_value(i64 %indvars.iv.next109, !418, !DIExpression(), !438)
  %exitcond113.not = icmp eq i64 %indvars.iv.next109, %wide.trip.count112, !dbg !441
  br i1 %exitcond113.not, label %for.cond30.preheader.loopexit, label %for.body, !dbg !504, !llvm.loop !505

for.body7:                                        ; preds = %for.body7, %for.body7.preheader
  %sum.sroa.0.2 = phi i32 [ %add22, %for.body7 ], [ %sum.sroa.0.2.ph, %for.body7.preheader ], !dbg !436
  %sum.sroa.6.2 = phi i32 [ %add22.1, %for.body7 ], [ %sum.sroa.6.2.ph, %for.body7.preheader ], !dbg !436
  %sum.sroa.9.2 = phi i32 [ %add22.2, %for.body7 ], [ %sum.sroa.9.2.ph, %for.body7.preheader ], !dbg !436
  %sum.sroa.12.2 = phi i32 [ %add22.3, %for.body7 ], [ %sum.sroa.12.2.ph, %for.body7.preheader ], !dbg !436
  %indvars.iv = phi i64 [ %indvars.iv.next, %for.body7 ], [ %indvars.iv.ph, %for.body7.preheader ]
    #dbg_value(i32 %sum.sroa.12.2, !411, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !436)
    #dbg_value(i32 %sum.sroa.9.2, !411, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !436)
    #dbg_value(i32 %sum.sroa.6.2, !411, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !436)
    #dbg_value(i32 %sum.sroa.0.2, !411, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !436)
    #dbg_value(i64 %indvars.iv, !423, !DIExpression(), !470)
  %90 = trunc nuw nsw i64 %indvars.iv to i32, !dbg !475
  %add10 = add i32 %sub8, %90, !dbg !475
    #dbg_value(i32 %add10, !425, !DIExpression(), !508)
  %gep = getelementptr i32, ptr %invariant.gep, i64 %indvars.iv, !dbg !476
  %91 = load i32, ptr %gep, align 4, !dbg !477, !tbaa !68
    #dbg_value(i32 %91, !428, !DIExpression(), !508)
    #dbg_value(i32 0, !429, !DIExpression(), !509)
  %mul1.i = shl nsw i32 %add10, 2
  %add.i = add nsw i32 %mul1.i, %mul.i
    #dbg_value(i64 0, !429, !DIExpression(), !509)
    #dbg_value(ptr %source_data, !483, !DIExpression(), !510)
    #dbg_value(i32 %source_stride, !484, !DIExpression(), !510)
    #dbg_value(i32 %add10, !485, !DIExpression(), !510)
    #dbg_value(!DIArgList(i32 %sub3, i64 %indvars.iv108), !486, !DIExpression(DW_OP_LLVM_arg, 0, DW_OP_LLVM_arg, 1, DW_OP_plus, DW_OP_stack_value), !510)
    #dbg_value(i32 4, !487, !DIExpression(), !510)
    #dbg_value(i32 2, !488, !DIExpression(), !510)
  %92 = sext i32 %add.i to i64, !dbg !478
  %93 = getelementptr i8, ptr %source_data, i64 %92, !dbg !478
  %arrayidx.i = getelementptr i8, ptr %93, i64 2, !dbg !478
    #dbg_value(i32 %add22, !411, !DIExpression(DW_OP_LLVM_fragment, 0, 32), !436)
    #dbg_value(i64 1, !429, !DIExpression(), !509)
    #dbg_value(i32 1, !488, !DIExpression(), !510)
  %94 = sext i32 %add.i to i64, !dbg !478
  %95 = getelementptr i8, ptr %source_data, i64 %94, !dbg !478
  %arrayidx.i.1 = getelementptr i8, ptr %95, i64 1, !dbg !478
    #dbg_value(i32 %add22.1, !411, !DIExpression(DW_OP_LLVM_fragment, 32, 32), !436)
    #dbg_value(i64 2, !429, !DIExpression(), !509)
    #dbg_value(i32 0, !488, !DIExpression(), !510)
  %idxprom.i.2 = sext i32 %add.i to i64, !dbg !478
  %arrayidx.i.2 = getelementptr inbounds i8, ptr %source_data, i64 %idxprom.i.2, !dbg !478
    #dbg_value(i32 %add22.2, !411, !DIExpression(DW_OP_LLVM_fragment, 64, 32), !436)
    #dbg_value(i64 3, !429, !DIExpression(), !509)
    #dbg_value(i32 3, !488, !DIExpression(), !510)
  %96 = sext i32 %add.i to i64, !dbg !478
  %97 = getelementptr i8, ptr %source_data, i64 %96, !dbg !478
  %arrayidx.i.3 = getelementptr i8, ptr %97, i64 3, !dbg !478
  %98 = load i8, ptr %arrayidx.i.3, align 1, !dbg !511, !tbaa !143
  %99 = load i8, ptr %arrayidx.i, align 1, !dbg !492, !tbaa !143
  %100 = load i8, ptr %arrayidx.i.1, align 1, !dbg !512, !tbaa !143
  %101 = load i8, ptr %arrayidx.i.2, align 1, !dbg !513, !tbaa !143
  %conv.3 = zext i8 %98 to i32, !dbg !493
  %conv = zext i8 %99 to i32, !dbg !493
  %conv.1 = zext i8 %100 to i32, !dbg !493
  %conv.2 = zext i8 %101 to i32, !dbg !493
  %mul19.3 = mul nsw i32 %91, %conv.3, !dbg !494
  %mul19 = mul nsw i32 %91, %conv, !dbg !494
  %mul19.1 = mul nsw i32 %91, %conv.1, !dbg !494
  %mul19.2 = mul nsw i32 %91, %conv.2, !dbg !494
  %add22 = add nsw i32 %mul19, %sum.sroa.0.2, !dbg !495
  %add22.1 = add nsw i32 %mul19.1, %sum.sroa.6.2, !dbg !496
  %add22.2 = add nsw i32 %mul19.2, %sum.sroa.9.2, !dbg !497
  %add22.3 = add nsw i32 %mul19.3, %sum.sroa.12.2, !dbg !498
    #dbg_value(i32 %add22.3, !411, !DIExpression(DW_OP_LLVM_fragment, 96, 32), !436)
    #dbg_value(i64 4, !429, !DIExpression(), !509)
  %indvars.iv.next = add nuw nsw i64 %indvars.iv, 1, !dbg !474
    #dbg_value(i64 %indvars.iv.next, !423, !DIExpression(), !470)
  %exitcond.not = icmp eq i64 %indvars.iv.next, %wide.trip.count, !dbg !514
  br i1 %exitcond.not, label %for.cond.cleanup6, label %for.body7, !dbg !499, !llvm.loop !515
}

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.smax.i32(i32, i32) #8

; Function Attrs: nofree nounwind
declare noundef i64 @fwrite(ptr noundef readonly captures(none), i64 noundef, i64 noundef, ptr noundef captures(none)) local_unnamed_addr #9

; Function Attrs: nocallback nofree nounwind willreturn memory(argmem: write)
declare void @llvm.experimental.memset.pattern.p0.i32.i64(ptr writeonly captures(none), i32, i64, i1 immarg) #10

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.vector.reduce.add.v16i32(<16 x i32>) #11

; Function Attrs: nocallback nofree nosync nounwind speculatable willreturn memory(none)
declare i32 @llvm.vector.reduce.add.v8i32(<8 x i32>) #11

; Function Attrs: nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none)
declare <4 x i32> @llvm.umin.v4i32(<4 x i32>, <4 x i32>) #8

attributes #0 = { nounwind ssp "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #1 = { mustprogress nocallback nofree nounwind willreturn memory(argmem: read) "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #2 = { nofree nounwind "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #3 = { mustprogress nofree nounwind willreturn allockind("alloc,uninitialized") allocsize(0) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #4 = { mustprogress nofree nounwind willreturn allockind("alloc,zeroed") allocsize(0,1) memory(inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #5 = { mustprogress nounwind willreturn allockind("free") memory(argmem: readwrite, inaccessiblemem: readwrite) "alloc-family"="malloc" "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #6 = { nofree noinline norecurse nosync nounwind ssp memory(argmem: readwrite) "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #7 = { mustprogress nocallback nofree nounwind willreturn memory(read) "frame-pointer"="non-leaf-no-reserve" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="apple-m1" "target-features"="+aes,+altnzcv,+ccdp,+ccidx,+ccpp,+complxnum,+crc,+dit,+dotprod,+flagm,+fp-armv8,+fp16fml,+fptoint,+fullfp16,+jsconv,+lse,+neon,+pauth,+perfmon,+predres,+ras,+rcpc,+rdm,+sb,+sha2,+sha3,+specrestrict,+ssbs,+v8.1a,+v8.2a,+v8.3a,+v8.4a,+v8a" }
attributes #8 = { nocallback nocreateundeforpoison nofree nosync nounwind speculatable willreturn memory(none) }
attributes #9 = { nofree nounwind }
attributes #10 = { nocallback nofree nounwind willreturn memory(argmem: write) }
attributes #11 = { nocallback nofree nosync nounwind speculatable willreturn memory(none) }
attributes #12 = { allocsize(0) }
attributes #13 = { allocsize(0,1) }
attributes #14 = { nounwind }

!llvm.module.flags = !{!36, !37, !38, !39, !40, !41}
!llvm.dbg.cu = !{!42}
!llvm.ident = !{!67}
!llvm.errno.tbaa = !{!68}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 122, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "playground/firefox_convolve_int.c", directory: "/Users/rgangar/Documents/llvm-project-yichi", checksumkind: CSK_MD5, checksum: "36e37fbcb0f5cafc8c7486ab1c30b50e")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 64, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 8)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 124, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 72, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 9)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 126, type: !9, isLocal: true, isDefinition: true)
!14 = !DIGlobalVariableExpression(var: !15, expr: !DIExpression())
!15 = distinct !DIGlobalVariable(scope: null, file: !2, line: 128, type: !16, isLocal: true, isDefinition: true)
!16 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 56, elements: !17)
!17 = !{!18}
!18 = !DISubrange(count: 7)
!19 = !DIGlobalVariableExpression(var: !20, expr: !DIExpression())
!20 = distinct !DIGlobalVariable(scope: null, file: !2, line: 130, type: !9, isLocal: true, isDefinition: true)
!21 = !DIGlobalVariableExpression(var: !22, expr: !DIExpression())
!22 = distinct !DIGlobalVariable(scope: null, file: !2, line: 146, type: !23, isLocal: true, isDefinition: true)
!23 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 552, elements: !24)
!24 = !{!25}
!25 = !DISubrange(count: 69)
!26 = !DIGlobalVariableExpression(var: !27, expr: !DIExpression())
!27 = distinct !DIGlobalVariable(scope: null, file: !2, line: 161, type: !28, isLocal: true, isDefinition: true)
!28 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 152, elements: !29)
!29 = !{!30}
!30 = !DISubrange(count: 19)
!31 = !DIGlobalVariableExpression(var: !32, expr: !DIExpression())
!32 = distinct !DIGlobalVariable(scope: null, file: !2, line: 181, type: !33, isLocal: true, isDefinition: true)
!33 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 120, elements: !34)
!34 = !{!35}
!35 = !DISubrange(count: 15)
!36 = !{i32 2, !"SDK Version", [2 x i32] [i32 26, i32 1]}
!37 = !{i32 7, !"Dwarf Version", i32 5}
!38 = !{i32 2, !"Debug Info Version", i32 3}
!39 = !{i32 1, !"wchar_size", i32 4}
!40 = !{i32 8, !"PIC Level", i32 2}
!41 = !{i32 7, !"frame-pointer", i32 4}
!42 = distinct !DICompileUnit(language: DW_LANG_C11, file: !2, producer: "clang version 23.0.0git (git@github.com:yichi170/llvm-project.git 99803c937dab41e4dc2e475a572fcf1cde1db9b2)", isOptimized: true, runtimeVersion: 0, emissionKind: FullDebug, enums: !43, retainedTypes: !51, globals: !66, splitDebugInlining: false, nameTableKind: Apple, sysroot: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk", sdk: "MacOSX.sdk")
!43 = !{!44}
!44 = !DICompositeType(tag: DW_TAG_enumeration_type, file: !2, line: 17, baseType: !45, size: 32, elements: !46)
!45 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!46 = !{!47, !48, !49, !50}
!47 = !DIEnumerator(name: "B8G8R8A8_COMPONENT_BYTEOFFSET_B", value: 0)
!48 = !DIEnumerator(name: "B8G8R8A8_COMPONENT_BYTEOFFSET_G", value: 1)
!49 = !DIEnumerator(name: "B8G8R8A8_COMPONENT_BYTEOFFSET_R", value: 2)
!50 = !DIEnumerator(name: "B8G8R8A8_COMPONENT_BYTEOFFSET_A", value: 3)
!51 = !{!52, !57, !61, !58, !65}
!52 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !53, line: 50, baseType: !54)
!53 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/sys/_types/_size_t.h", directory: "", checksumkind: CSK_MD5, checksum: "f7981334d28e0c246f35cd24042aa2a4")
!54 = !DIDerivedType(tag: DW_TAG_typedef, name: "__darwin_size_t", file: !55, line: 87, baseType: !56)
!55 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/arm/_types.h", directory: "", checksumkind: CSK_MD5, checksum: "b270144f57ae258d0ce80b8f87be068c")
!56 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!57 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !58, size: 64)
!58 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint8_t", file: !59, line: 31, baseType: !60)
!59 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/_types/_uint8_t.h", directory: "", checksumkind: CSK_MD5, checksum: "8b64ccf8c67b8c006b07b8daf1b49be5")
!60 = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
!61 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !62, size: 64)
!62 = !DIDerivedType(tag: DW_TAG_typedef, name: "int32_t", file: !63, line: 30, baseType: !64)
!63 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/sys/_types/_int32_t.h", directory: "", checksumkind: CSK_MD5, checksum: "d23e8406e80ee79983f28509c741fa17")
!64 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!65 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!66 = !{!0, !7, !12, !14, !19, !21, !26, !31}
!67 = !{!"clang version 23.0.0git (git@github.com:yichi170/llvm-project.git 99803c937dab41e4dc2e475a572fcf1cde1db9b2)"}
!68 = !{!69, !69, i64 0}
!69 = !{!"int", !70, i64 0}
!70 = !{!"omnipotent char", !71, i64 0}
!71 = !{!"Simple C/C++ TBAA"}
!72 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 114, type: !73, scopeLine: 114, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !77, keyInstructions: true)
!73 = !DISubroutineType(types: !74)
!74 = !{!64, !64, !75}
!75 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !76, size: 64)
!76 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!77 = !{!78, !79, !80, !81, !82, !83, !84, !85, !87, !88, !89, !90, !91, !92, !93, !95, !97, !98, !99}
!78 = !DILocalVariable(name: "argc", arg: 1, scope: !72, file: !2, line: 114, type: !64)
!79 = !DILocalVariable(name: "argv", arg: 2, scope: !72, file: !2, line: 114, type: !75)
!80 = !DILocalVariable(name: "width", scope: !72, file: !2, line: 115, type: !64)
!81 = !DILocalVariable(name: "height", scope: !72, file: !2, line: 116, type: !64)
!82 = !DILocalVariable(name: "kernel_size", scope: !72, file: !2, line: 117, type: !64)
!83 = !DILocalVariable(name: "iterations", scope: !72, file: !2, line: 118, type: !64)
!84 = !DILocalVariable(name: "warmup_iterations", scope: !72, file: !2, line: 119, type: !64)
!85 = !DILocalVariable(name: "i", scope: !86, file: !2, line: 121, type: !64)
!86 = distinct !DILexicalBlock(scope: !72, file: !2, line: 121, column: 3)
!87 = !DILocalVariable(name: "margin", scope: !72, file: !2, line: 140, type: !64)
!88 = !DILocalVariable(name: "stride", scope: !72, file: !2, line: 149, type: !62)
!89 = !DILocalVariable(name: "image_size", scope: !72, file: !2, line: 150, type: !52)
!90 = !DILocalVariable(name: "source", scope: !72, file: !2, line: 151, type: !57)
!91 = !DILocalVariable(name: "target", scope: !72, file: !2, line: 152, type: !57)
!92 = !DILocalVariable(name: "kernel", scope: !72, file: !2, line: 153, type: !61)
!93 = !DILocalVariable(name: "i", scope: !94, file: !2, line: 165, type: !52)
!94 = distinct !DILexicalBlock(scope: !72, file: !2, line: 165, column: 3)
!95 = !DILocalVariable(name: "i", scope: !96, file: !2, line: 167, type: !64)
!96 = distinct !DILexicalBlock(scope: !72, file: !2, line: 167, column: 3)
!97 = !DILocalVariable(name: "shift_l", scope: !72, file: !2, line: 170, type: !62)
!98 = !DILocalVariable(name: "shift_r", scope: !72, file: !2, line: 171, type: !62)
!99 = !DILocalVariable(name: "divisor", scope: !72, file: !2, line: 172, type: !64)
!100 = !DILocation(line: 0, scope: !72)
!101 = !DILocation(line: 0, scope: !86)
!102 = !DILocation(line: 121, column: 21, scope: !103, atomGroup: 118, atomRank: 1)
!103 = distinct !DILexicalBlock(scope: !86, file: !2, line: 121, column: 3)
!104 = !DILocation(line: 121, column: 3, scope: !86, atomGroup: 119, atomRank: 1)
!105 = !DILocation(line: 135, column: 19, scope: !106, atomGroup: 31, atomRank: 1)
!106 = distinct !DILexicalBlock(scope: !72, file: !2, line: 135, column: 7)
!107 = !DILocation(line: 119, column: 7, scope: !72, atomGroup: 5, atomRank: 1)
!108 = !DILocation(line: 118, column: 7, scope: !72, atomGroup: 4, atomRank: 1)
!109 = !DILocation(line: 117, column: 7, scope: !72, atomGroup: 3, atomRank: 1)
!110 = !DILocation(line: 116, column: 7, scope: !72, atomGroup: 2, atomRank: 1)
!111 = !DILocation(line: 137, column: 25, scope: !112, atomGroup: 33, atomRank: 1)
!112 = distinct !DILexicalBlock(scope: !72, file: !2, line: 137, column: 7)
!113 = !DILocation(line: 141, column: 23, scope: !114)
!114 = distinct !DILexicalBlock(scope: !72, file: !2, line: 141, column: 7)
!115 = !DILocation(line: 141, column: 13, scope: !114, atomGroup: 36, atomRank: 2)
!116 = !DILocation(line: 141, column: 13, scope: !114, atomGroup: 36, atomRank: 1)
!117 = !DILocation(line: 143, column: 14, scope: !118, atomGroup: 38, atomRank: 2)
!118 = distinct !DILexicalBlock(scope: !72, file: !2, line: 143, column: 7)
!119 = !DILocation(line: 143, column: 14, scope: !118, atomGroup: 38, atomRank: 1)
!120 = !DILocation(line: 146, column: 3, scope: !72)
!121 = !DILocation(line: 149, column: 26, scope: !72, atomGroup: 40, atomRank: 2)
!122 = !DILocation(line: 150, column: 23, scope: !72)
!123 = !DILocation(line: 150, column: 40, scope: !72)
!124 = !DILocation(line: 150, column: 38, scope: !72, atomGroup: 41, atomRank: 2)
!125 = !DILocation(line: 151, column: 32, scope: !72, atomGroup: 42, atomRank: 2)
!126 = !DILocation(line: 152, column: 32, scope: !72, atomGroup: 43, atomRank: 2)
!127 = !DILocation(line: 154, column: 25, scope: !72)
!128 = !DILocation(line: 154, column: 45, scope: !72)
!129 = !DILocation(line: 154, column: 67, scope: !72)
!130 = !DILocation(line: 154, column: 18, scope: !72, atomGroup: 44, atomRank: 2)
!131 = !DILocation(line: 157, column: 8, scope: !132, atomGroup: 45, atomRank: 2)
!132 = distinct !DILexicalBlock(scope: !72, file: !2, line: 157, column: 7)
!133 = !DILocation(line: 157, column: 15, scope: !132, atomGroup: 45, atomRank: 1)
!134 = !DILocation(line: 0, scope: !94)
!135 = !DILocation(line: 165, column: 24, scope: !136, atomGroup: 120, atomRank: 1)
!136 = distinct !DILexicalBlock(scope: !94, file: !2, line: 165, column: 3)
!137 = !DILocation(line: 165, column: 3, scope: !94, atomGroup: 121, atomRank: 1)
!138 = !DILocation(line: 165, column: 3, scope: !94)
!139 = !DILocation(line: 165, column: 39, scope: !136, atomGroup: 53, atomRank: 2)
!140 = !DILocation(line: 166, column: 17, scope: !136, atomGroup: 52, atomRank: 2)
!141 = !DILocation(line: 166, column: 5, scope: !136)
!142 = !DILocation(line: 166, column: 15, scope: !136, atomGroup: 52, atomRank: 1)
!143 = !{!70, !70, i64 0}
!144 = !DILocation(line: 165, column: 3, scope: !94, atomGroup: 51, atomRank: 1)
!145 = distinct !{!145, !146, !147, !148}
!146 = !{!"llvm.loop.mustprogress"}
!147 = !{!"llvm.loop.isvectorized", i32 1}
!148 = !{!"llvm.loop.unroll.runtime.disable"}
!149 = !{!"branch_weights", i32 8, i32 56}
!150 = distinct !{!150, !146, !147, !148}
!151 = !DILocation(line: 122, column: 16, scope: !152)
!152 = distinct !DILexicalBlock(scope: !153, file: !2, line: 122, column: 9)
!153 = distinct !DILexicalBlock(scope: !103, file: !2, line: 121, column: 34)
!154 = !{!155, !155, i64 0}
!155 = !{!"p1 omnipotent char", !156, i64 0}
!156 = !{!"any pointer", !70, i64 0}
!157 = !DILocation(line: 122, column: 9, scope: !152)
!158 = !DILocation(line: 122, column: 36, scope: !152, atomGroup: 9, atomRank: 2)
!159 = !DILocation(line: 122, column: 41, scope: !152, atomGroup: 9, atomRank: 1)
!160 = !DILocation(line: 122, column: 46, scope: !152)
!161 = !DILocation(line: 122, column: 50, scope: !152, atomGroup: 10, atomRank: 2)
!162 = !DILocation(line: 122, column: 41, scope: !152, atomGroup: 10, atomRank: 1)
!163 = !DILocation(line: 123, column: 34, scope: !164)
!164 = distinct !DILexicalBlock(scope: !152, file: !2, line: 122, column: 58)
!165 = !DILocalVariable(name: "value", arg: 1, scope: !166, file: !2, line: 109, type: !169)
!166 = distinct !DISubprogram(name: "parse_positive_int", scope: !2, file: !2, line: 109, type: !167, scopeLine: 109, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !171, keyInstructions: true)
!167 = !DISubroutineType(types: !168)
!168 = !{!64, !169, !64}
!169 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !170, size: 64)
!170 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!171 = !{!165, !172, !173}
!172 = !DILocalVariable(name: "fallback", arg: 2, scope: !166, file: !2, line: 109, type: !64)
!173 = !DILocalVariable(name: "parsed", scope: !166, file: !2, line: 110, type: !64)
!174 = !DILocation(line: 0, scope: !166, inlinedAt: !175)
!175 = distinct !DILocation(line: 123, column: 15, scope: !164)
!176 = !DILocation(line: 110, column: 16, scope: !166, inlinedAt: !175, atomGroup: 1, atomRank: 2)
!177 = !DILocation(line: 111, column: 17, scope: !166, inlinedAt: !175, atomGroup: 3, atomRank: 2)
!178 = !DILocation(line: 111, column: 10, scope: !166, inlinedAt: !175, atomGroup: 3, atomRank: 1)
!179 = !DILocation(line: 124, column: 5, scope: !164)
!180 = !DILocation(line: 124, column: 16, scope: !181)
!181 = distinct !DILexicalBlock(scope: !152, file: !2, line: 124, column: 16)
!182 = !DILocation(line: 124, column: 44, scope: !181, atomGroup: 13, atomRank: 2)
!183 = !DILocation(line: 124, column: 49, scope: !181, atomGroup: 13, atomRank: 1)
!184 = !DILocation(line: 124, column: 54, scope: !181)
!185 = !DILocation(line: 124, column: 58, scope: !181, atomGroup: 14, atomRank: 2)
!186 = !DILocation(line: 124, column: 49, scope: !181, atomGroup: 14, atomRank: 1)
!187 = !DILocation(line: 125, column: 35, scope: !188)
!188 = distinct !DILexicalBlock(scope: !181, file: !2, line: 124, column: 66)
!189 = !DILocation(line: 0, scope: !166, inlinedAt: !190)
!190 = distinct !DILocation(line: 125, column: 16, scope: !188)
!191 = !DILocation(line: 110, column: 16, scope: !166, inlinedAt: !190, atomGroup: 1, atomRank: 2)
!192 = !DILocation(line: 111, column: 17, scope: !166, inlinedAt: !190, atomGroup: 3, atomRank: 2)
!193 = !DILocation(line: 111, column: 10, scope: !166, inlinedAt: !190, atomGroup: 3, atomRank: 1)
!194 = !DILocation(line: 126, column: 5, scope: !188)
!195 = !DILocation(line: 126, column: 16, scope: !196)
!196 = distinct !DILexicalBlock(scope: !181, file: !2, line: 126, column: 16)
!197 = !DILocation(line: 126, column: 44, scope: !196, atomGroup: 17, atomRank: 2)
!198 = !DILocation(line: 126, column: 49, scope: !196, atomGroup: 17, atomRank: 1)
!199 = !DILocation(line: 126, column: 54, scope: !196)
!200 = !DILocation(line: 126, column: 58, scope: !196, atomGroup: 18, atomRank: 2)
!201 = !DILocation(line: 126, column: 49, scope: !196, atomGroup: 18, atomRank: 1)
!202 = !DILocation(line: 127, column: 40, scope: !203)
!203 = distinct !DILexicalBlock(scope: !196, file: !2, line: 126, column: 66)
!204 = !DILocation(line: 0, scope: !166, inlinedAt: !205)
!205 = distinct !DILocation(line: 127, column: 21, scope: !203)
!206 = !DILocation(line: 110, column: 16, scope: !166, inlinedAt: !205, atomGroup: 1, atomRank: 2)
!207 = !DILocation(line: 111, column: 17, scope: !166, inlinedAt: !205, atomGroup: 3, atomRank: 2)
!208 = !DILocation(line: 111, column: 10, scope: !166, inlinedAt: !205, atomGroup: 3, atomRank: 1)
!209 = !DILocation(line: 128, column: 5, scope: !203)
!210 = !DILocation(line: 128, column: 16, scope: !211)
!211 = distinct !DILexicalBlock(scope: !196, file: !2, line: 128, column: 16)
!212 = !DILocation(line: 128, column: 42, scope: !211, atomGroup: 21, atomRank: 2)
!213 = !DILocation(line: 128, column: 47, scope: !211, atomGroup: 21, atomRank: 1)
!214 = !DILocation(line: 128, column: 52, scope: !211)
!215 = !DILocation(line: 128, column: 56, scope: !211, atomGroup: 22, atomRank: 2)
!216 = !DILocation(line: 128, column: 47, scope: !211, atomGroup: 22, atomRank: 1)
!217 = !DILocation(line: 129, column: 39, scope: !218)
!218 = distinct !DILexicalBlock(scope: !211, file: !2, line: 128, column: 64)
!219 = !DILocation(line: 0, scope: !166, inlinedAt: !220)
!220 = distinct !DILocation(line: 129, column: 20, scope: !218)
!221 = !DILocation(line: 110, column: 16, scope: !166, inlinedAt: !220, atomGroup: 1, atomRank: 2)
!222 = !DILocation(line: 111, column: 17, scope: !166, inlinedAt: !220, atomGroup: 3, atomRank: 2)
!223 = !DILocation(line: 111, column: 10, scope: !166, inlinedAt: !220, atomGroup: 3, atomRank: 1)
!224 = !DILocation(line: 130, column: 5, scope: !218)
!225 = !DILocation(line: 130, column: 16, scope: !226)
!226 = distinct !DILexicalBlock(scope: !211, file: !2, line: 130, column: 16)
!227 = !DILocation(line: 130, column: 44, scope: !226, atomGroup: 25, atomRank: 2)
!228 = !DILocation(line: 130, column: 49, scope: !226, atomGroup: 25, atomRank: 1)
!229 = !DILocation(line: 130, column: 54, scope: !226)
!230 = !DILocation(line: 130, column: 58, scope: !226, atomGroup: 26, atomRank: 2)
!231 = !DILocation(line: 130, column: 49, scope: !226, atomGroup: 26, atomRank: 1)
!232 = !DILocation(line: 131, column: 46, scope: !233)
!233 = distinct !DILexicalBlock(scope: !226, file: !2, line: 130, column: 66)
!234 = !DILocation(line: 0, scope: !166, inlinedAt: !235)
!235 = distinct !DILocation(line: 131, column: 27, scope: !233)
!236 = !DILocation(line: 110, column: 16, scope: !166, inlinedAt: !235, atomGroup: 1, atomRank: 2)
!237 = !DILocation(line: 111, column: 17, scope: !166, inlinedAt: !235, atomGroup: 3, atomRank: 2)
!238 = !DILocation(line: 111, column: 10, scope: !166, inlinedAt: !235, atomGroup: 3, atomRank: 1)
!239 = !DILocation(line: 132, column: 5, scope: !233)
!240 = !DILocation(line: 121, column: 30, scope: !103, atomGroup: 29, atomRank: 2)
!241 = !DILocation(line: 121, column: 21, scope: !103, atomGroup: 7, atomRank: 1)
!242 = !DILocation(line: 121, column: 3, scope: !86, atomGroup: 8, atomRank: 1)
!243 = distinct !{!243, !244, !245, !146}
!244 = !DILocation(line: 121, column: 3, scope: !86)
!245 = !DILocation(line: 133, column: 3, scope: !86)
!246 = !DILocation(line: 158, column: 5, scope: !247)
!247 = distinct !DILexicalBlock(scope: !132, file: !2, line: 157, column: 38)
!248 = !DILocation(line: 159, column: 5, scope: !247)
!249 = !DILocation(line: 160, column: 5, scope: !247)
!250 = !DILocation(line: 161, column: 13, scope: !247)
!251 = !{!252, !252, i64 0}
!252 = !{!"p1 _ZTS7__sFILE", !156, i64 0}
!253 = !DILocation(line: 161, column: 5, scope: !247)
!254 = !DILocation(line: 162, column: 5, scope: !247, atomGroup: 48, atomRank: 1)
!255 = !DILocation(line: 0, scope: !96)
!256 = !DILocation(line: 167, column: 3, scope: !96, atomGroup: 123, atomRank: 1)
!257 = !DILocation(line: 168, column: 15, scope: !258, atomGroup: 58, atomRank: 1)
!258 = distinct !DILexicalBlock(scope: !96, file: !2, line: 167, column: 3)
!259 = !DILocation(line: 173, column: 3, scope: !72)
!260 = !DILocation(line: 165, column: 24, scope: !136, atomGroup: 50, atomRank: 1)
!261 = distinct !{!261, !146, !148, !147}
!262 = !DILocation(line: 173, column: 13, scope: !72)
!263 = !DILocation(line: 173, column: 31, scope: !72, atomGroup: 64, atomRank: 1)
!264 = !DILocation(line: 173, column: 25, scope: !72)
!265 = !DILocation(line: 173, column: 3, scope: !72, atomGroup: 65, atomRank: 1)
!266 = distinct !{!266, !259, !267, !146}
!267 = !DILocation(line: 174, column: 12, scope: !72)
!268 = !DILocation(line: 176, column: 3, scope: !72)
!269 = !DILocation(line: 178, column: 3, scope: !72)
!270 = !DILocalVariable(name: "buf", arg: 1, scope: !271, file: !2, line: 102, type: !276)
!271 = distinct !DISubprogram(name: "checksum", scope: !2, file: !2, line: 102, type: !272, scopeLine: 102, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !278, keyInstructions: true)
!272 = !DISubroutineType(types: !273)
!273 = !{!274, !276, !52}
!274 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint64_t", file: !275, line: 31, baseType: !65)
!275 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/_types/_uint64_t.h", directory: "", checksumkind: CSK_MD5, checksum: "77fc5e91653260959605f129691cf9b1")
!276 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !277, size: 64)
!277 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !58)
!278 = !{!270, !279, !280, !281}
!279 = !DILocalVariable(name: "size", arg: 2, scope: !271, file: !2, line: 102, type: !52)
!280 = !DILocalVariable(name: "sum", scope: !271, file: !2, line: 103, type: !274)
!281 = !DILocalVariable(name: "i", scope: !282, file: !2, line: 104, type: !52)
!282 = distinct !DILexicalBlock(scope: !271, file: !2, line: 104, column: 3)
!283 = !DILocation(line: 0, scope: !271, inlinedAt: !284)
!284 = distinct !DILocation(line: 181, column: 49, scope: !72)
!285 = !DILocation(line: 0, scope: !282, inlinedAt: !284)
!286 = !DILocation(line: 104, column: 3, scope: !282, inlinedAt: !284, atomGroup: 117, atomRank: 1)
!287 = !DILocation(line: 105, column: 16, scope: !288, inlinedAt: !284)
!288 = distinct !DILexicalBlock(scope: !282, file: !2, line: 104, column: 3)
!289 = !DILocation(line: 105, column: 25, scope: !288, inlinedAt: !284)
!290 = !DILocation(line: 105, column: 23, scope: !288, inlinedAt: !284, atomGroup: 5, atomRank: 2)
!291 = !DILocation(line: 104, column: 33, scope: !288, inlinedAt: !284, atomGroup: 6, atomRank: 2)
!292 = !DILocation(line: 104, column: 24, scope: !288, inlinedAt: !284, atomGroup: 3, atomRank: 1)
!293 = !DILocation(line: 104, column: 3, scope: !282, inlinedAt: !284, atomGroup: 4, atomRank: 1)
!294 = distinct !{!294, !295, !296, !146}
!295 = !DILocation(line: 104, column: 3, scope: !282, inlinedAt: !284)
!296 = !DILocation(line: 105, column: 30, scope: !282, inlinedAt: !284)
!297 = !DILocation(line: 181, column: 3, scope: !72)
!298 = !DILocation(line: 183, column: 3, scope: !72)
!299 = !DILocation(line: 184, column: 3, scope: !72)
!300 = !DILocation(line: 185, column: 3, scope: !72)
!301 = !DILocation(line: 187, column: 1, scope: !72, atomGroup: 68, atomRank: 1)
!302 = !DISubprogram(name: "strcmp", scope: !303, file: !303, line: 89, type: !304, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!303 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/_string.h", directory: "", checksumkind: CSK_MD5, checksum: "f3896c7cfc45aebfb770e493ae07cb17")
!304 = !DISubroutineType(types: !305)
!305 = !{!64, !169, !169}
!306 = !DISubprogram(name: "printf", scope: !307, file: !307, line: 34, type: !308, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!307 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/_printf.h", directory: "", checksumkind: CSK_MD5, checksum: "2d37517bd0342aa326aa1d3660ad4ab4")
!308 = !DISubroutineType(types: !309)
!309 = !{!64, !310, null}
!310 = !DIDerivedType(tag: DW_TAG_restrict_type, baseType: !169)
!311 = !DISubprogram(name: "malloc", scope: !312, file: !312, line: 54, type: !313, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!312 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/malloc/_malloc.h", directory: "", checksumkind: CSK_MD5, checksum: "1ff1e04bc418b1c4bb5edfe9e395b8c0")
!313 = !DISubroutineType(types: !314)
!314 = !{!315, !52}
!315 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!316 = !DISubprogram(name: "calloc", scope: !312, file: !312, line: 55, type: !317, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!317 = !DISubroutineType(types: !318)
!318 = !{!315, !52, !52}
!319 = !DISubprogram(name: "free", scope: !312, file: !312, line: 56, type: !320, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!320 = !DISubroutineType(types: !321)
!321 = !{null, !315}
!322 = distinct !DISubprogram(name: "run_kernel_int", scope: !2, file: !2, line: 82, type: !323, scopeLine: 85, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !327, keyInstructions: true)
!323 = !DISubroutineType(cc: DW_CC_nocall, types: !324)
!324 = !{null, !64, !64, !64, !64, !276, !57, !325, !62, !62}
!325 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !326, size: 64)
!326 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !62)
!327 = !{!328, !329, !330, !331, !332, !333, !334, !335, !336, !337, !338, !339, !340, !341, !343, !347}
!328 = !DILocalVariable(name: "width", arg: 1, scope: !322, file: !2, line: 82, type: !64)
!329 = !DILocalVariable(name: "height", arg: 2, scope: !322, file: !2, line: 82, type: !64)
!330 = !DILocalVariable(name: "kernel_size", arg: 3, scope: !322, file: !2, line: 82, type: !64)
!331 = !DILocalVariable(name: "iterations", arg: 4, scope: !322, file: !2, line: 83, type: !64)
!332 = !DILocalVariable(name: "source", arg: 5, scope: !322, file: !2, line: 83, type: !276)
!333 = !DILocalVariable(name: "target", arg: 6, scope: !322, file: !2, line: 84, type: !57)
!334 = !DILocalVariable(name: "kernel", arg: 7, scope: !322, file: !2, line: 84, type: !325)
!335 = !DILocalVariable(name: "shift_l", arg: 8, scope: !322, file: !2, line: 85, type: !62)
!336 = !DILocalVariable(name: "shift_r", arg: 9, scope: !322, file: !2, line: 85, type: !62)
!337 = !DILocalVariable(name: "stride", scope: !322, file: !2, line: 86, type: !62)
!338 = !DILocalVariable(name: "target_x", scope: !322, file: !2, line: 87, type: !62)
!339 = !DILocalVariable(name: "target_y", scope: !322, file: !2, line: 88, type: !62)
!340 = !DILocalVariable(name: "margin", scope: !322, file: !2, line: 89, type: !64)
!341 = !DILocalVariable(name: "iter", scope: !342, file: !2, line: 91, type: !64)
!342 = distinct !DILexicalBlock(scope: !322, file: !2, line: 91, column: 3)
!343 = !DILocalVariable(name: "y", scope: !344, file: !2, line: 92, type: !62)
!344 = distinct !DILexicalBlock(scope: !345, file: !2, line: 92, column: 5)
!345 = distinct !DILexicalBlock(scope: !346, file: !2, line: 91, column: 49)
!346 = distinct !DILexicalBlock(scope: !342, file: !2, line: 91, column: 3)
!347 = !DILocalVariable(name: "x", scope: !348, file: !2, line: 93, type: !62)
!348 = distinct !DILexicalBlock(scope: !349, file: !2, line: 93, column: 7)
!349 = distinct !DILexicalBlock(scope: !350, file: !2, line: 92, column: 56)
!350 = distinct !DILexicalBlock(scope: !344, file: !2, line: 92, column: 5)
!351 = !DILocation(line: 0, scope: !322)
!352 = !DILocation(line: 86, column: 26, scope: !322, atomGroup: 1, atomRank: 2)
!353 = !DILocation(line: 87, column: 34, scope: !322, atomGroup: 2, atomRank: 2)
!354 = !DILocation(line: 89, column: 32, scope: !322, atomGroup: 4, atomRank: 2)
!355 = !DILocation(line: 0, scope: !342)
!356 = !DILocation(line: 91, column: 27, scope: !346, atomGroup: 114, atomRank: 1)
!357 = !DILocation(line: 91, column: 3, scope: !342, atomGroup: 115, atomRank: 1)
!358 = !DILocation(line: 0, scope: !344)
!359 = !DILocation(line: 92, column: 5, scope: !344, atomGroup: 113, atomRank: 1)
!360 = !DILocation(line: 100, column: 1, scope: !322, atomGroup: 20, atomRank: 1)
!361 = !DILocation(line: 0, scope: !348)
!362 = !DILocation(line: 93, column: 7, scope: !348, atomGroup: 111, atomRank: 1)
!363 = !DILocation(line: 91, column: 45, scope: !346, atomGroup: 18, atomRank: 2)
!364 = !DILocation(line: 91, column: 27, scope: !346, atomGroup: 6, atomRank: 1)
!365 = !DILocation(line: 91, column: 3, scope: !342, atomGroup: 7, atomRank: 1)
!366 = distinct !{!366, !367, !368, !146}
!367 = !DILocation(line: 91, column: 3, scope: !342)
!368 = !DILocation(line: 99, column: 3, scope: !342)
!369 = !DILocation(line: 92, column: 52, scope: !350, atomGroup: 16, atomRank: 2)
!370 = !DILocation(line: 92, column: 32, scope: !350, atomGroup: 9, atomRank: 1)
!371 = !DILocation(line: 92, column: 5, scope: !344, atomGroup: 10, atomRank: 1)
!372 = distinct !{!372, !373, !374, !146}
!373 = !DILocation(line: 92, column: 5, scope: !344)
!374 = !DILocation(line: 98, column: 5, scope: !344)
!375 = !DILocation(line: 94, column: 9, scope: !376)
!376 = distinct !DILexicalBlock(scope: !377, file: !2, line: 93, column: 57)
!377 = distinct !DILexicalBlock(scope: !348, file: !2, line: 93, column: 7)
!378 = !DILocation(line: 93, column: 53, scope: !377, atomGroup: 14, atomRank: 2)
!379 = !DILocation(line: 93, column: 34, scope: !377, atomGroup: 12, atomRank: 1)
!380 = !DILocation(line: 93, column: 7, scope: !348, atomGroup: 13, atomRank: 1)
!381 = distinct !{!381, !382, !383, !146}
!382 = !DILocation(line: 93, column: 7, scope: !348)
!383 = !DILocation(line: 97, column: 7, scope: !348)
!384 = !DISubprogram(name: "atoi", scope: !385, file: !385, line: 155, type: !386, flags: DIFlagPrototyped, spFlags: DISPFlagOptimized)
!385 = !DIFile(filename: "/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk/usr/include/_stdlib.h", directory: "", checksumkind: CSK_MD5, checksum: "3d0c06785d9f6367bf8af617fa81283d")
!386 = !DISubroutineType(types: !387)
!387 = !{!64, !169}
!388 = distinct !DISubprogram(name: "convolve_pixel_int", scope: !2, file: !2, line: 35, type: !389, scopeLine: 41, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !391, keyInstructions: true)
!389 = !DISubroutineType(cc: DW_CC_nocall, types: !390)
!390 = !{null, !276, !57, !62, !62, !62, !62, !62, !62, !325, !62, !62, !62, !64, !62, !62, !62, !62, !62, !62}
!391 = !{!392, !393, !394, !395, !396, !397, !398, !399, !400, !401, !402, !403, !404, !405, !406, !407, !408, !409, !410, !411, !415, !416, !417, !418, !420, !423, !425, !428, !429, !431, !433}
!392 = !DILocalVariable(name: "source_data", arg: 1, scope: !388, file: !2, line: 36, type: !276)
!393 = !DILocalVariable(name: "target_data", arg: 2, scope: !388, file: !2, line: 36, type: !57)
!394 = !DILocalVariable(name: "width", arg: 3, scope: !388, file: !2, line: 36, type: !62)
!395 = !DILocalVariable(name: "height", arg: 4, scope: !388, file: !2, line: 37, type: !62)
!396 = !DILocalVariable(name: "source_stride", arg: 5, scope: !388, file: !2, line: 37, type: !62)
!397 = !DILocalVariable(name: "target_stride", arg: 6, scope: !388, file: !2, line: 37, type: !62)
!398 = !DILocalVariable(name: "x", arg: 7, scope: !388, file: !2, line: 37, type: !62)
!399 = !DILocalVariable(name: "y", arg: 8, scope: !388, file: !2, line: 38, type: !62)
!400 = !DILocalVariable(name: "kernel", arg: 9, scope: !388, file: !2, line: 38, type: !325)
!401 = !DILocalVariable(name: "bias", arg: 10, scope: !388, file: !2, line: 38, type: !62)
!402 = !DILocalVariable(name: "shift_l", arg: 11, scope: !388, file: !2, line: 38, type: !62)
!403 = !DILocalVariable(name: "shift_r", arg: 12, scope: !388, file: !2, line: 39, type: !62)
!404 = !DILocalVariable(name: "preserve_alpha", arg: 13, scope: !388, file: !2, line: 39, type: !64)
!405 = !DILocalVariable(name: "order_x", arg: 14, scope: !388, file: !2, line: 39, type: !62)
!406 = !DILocalVariable(name: "order_y", arg: 15, scope: !388, file: !2, line: 39, type: !62)
!407 = !DILocalVariable(name: "target_x", arg: 16, scope: !388, file: !2, line: 40, type: !62)
!408 = !DILocalVariable(name: "target_y", arg: 17, scope: !388, file: !2, line: 40, type: !62)
!409 = !DILocalVariable(name: "kernel_unit_length_x", arg: 18, scope: !388, file: !2, line: 40, type: !62)
!410 = !DILocalVariable(name: "kernel_unit_length_y", arg: 19, scope: !388, file: !2, line: 41, type: !62)
!411 = !DILocalVariable(name: "sum", scope: !388, file: !2, line: 45, type: !412)
!412 = !DICompositeType(tag: DW_TAG_array_type, baseType: !62, size: 128, elements: !413)
!413 = !{!414}
!414 = !DISubrange(count: 4)
!415 = !DILocalVariable(name: "offsets", scope: !388, file: !2, line: 46, type: !412)
!416 = !DILocalVariable(name: "channels", scope: !388, file: !2, line: 52, type: !62)
!417 = !DILocalVariable(name: "rounding_addition", scope: !388, file: !2, line: 53, type: !62)
!418 = !DILocalVariable(name: "ky", scope: !419, file: !2, line: 55, type: !62)
!419 = distinct !DILexicalBlock(scope: !388, file: !2, line: 55, column: 3)
!420 = !DILocalVariable(name: "sample_y", scope: !421, file: !2, line: 56, type: !62)
!421 = distinct !DILexicalBlock(scope: !422, file: !2, line: 55, column: 44)
!422 = distinct !DILexicalBlock(scope: !419, file: !2, line: 55, column: 3)
!423 = !DILocalVariable(name: "kx", scope: !424, file: !2, line: 57, type: !62)
!424 = distinct !DILexicalBlock(scope: !421, file: !2, line: 57, column: 5)
!425 = !DILocalVariable(name: "sample_x", scope: !426, file: !2, line: 58, type: !62)
!426 = distinct !DILexicalBlock(scope: !427, file: !2, line: 57, column: 46)
!427 = distinct !DILexicalBlock(scope: !424, file: !2, line: 57, column: 5)
!428 = !DILocalVariable(name: "coeff", scope: !426, file: !2, line: 59, type: !62)
!429 = !DILocalVariable(name: "i", scope: !430, file: !2, line: 60, type: !62)
!430 = distinct !DILexicalBlock(scope: !426, file: !2, line: 60, column: 7)
!431 = !DILocalVariable(name: "i", scope: !432, file: !2, line: 68, type: !62)
!432 = distinct !DILexicalBlock(scope: !388, file: !2, line: 68, column: 3)
!433 = !DILocalVariable(name: "clamped", scope: !434, file: !2, line: 69, type: !62)
!434 = distinct !DILexicalBlock(scope: !435, file: !2, line: 68, column: 42)
!435 = distinct !DILexicalBlock(scope: !432, file: !2, line: 68, column: 3)
!436 = !DILocation(line: 0, scope: !388)
!437 = !DILocation(line: 46, column: 11, scope: !388)
!438 = !DILocation(line: 0, scope: !419)
!439 = !DILocation(line: 55, column: 27, scope: !422, atomGroup: 73, atomRank: 1)
!440 = !DILocation(line: 55, column: 3, scope: !419, atomGroup: 74, atomRank: 1)
!441 = !DILocation(line: 55, column: 27, scope: !422, atomGroup: 7, atomRank: 1)
!442 = !DILocation(line: 32, column: 12, scope: !443, inlinedAt: !448, atomGroup: 1, atomRank: 2)
!443 = distinct !DISubprogram(name: "clamp_to_nonzero", scope: !2, file: !2, line: 31, type: !444, scopeLine: 31, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !446, keyInstructions: true)
!444 = !DISubroutineType(types: !445)
!445 = !{!62, !62}
!446 = !{!447}
!447 = !DILocalVariable(name: "a", arg: 1, scope: !443, file: !2, line: 31, type: !62)
!448 = distinct !DILocation(line: 70, column: 14, scope: !434)
!449 = !DILocation(line: 32, column: 12, scope: !443, inlinedAt: !448, atomGroup: 92, atomRank: 2)
!450 = !DILocation(line: 32, column: 12, scope: !443, inlinedAt: !448, atomGroup: 98, atomRank: 2)
!451 = !DILocation(line: 32, column: 12, scope: !443, inlinedAt: !448, atomGroup: 104, atomRank: 2)
!452 = !DILocation(line: 0, scope: !432)
!453 = !DILocation(line: 0, scope: !443, inlinedAt: !448)
!454 = !DILocalVariable(name: "a", arg: 1, scope: !455, file: !2, line: 13, type: !45)
!455 = distinct !DISubprogram(name: "umin", scope: !2, file: !2, line: 13, type: !456, scopeLine: 13, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !458, keyInstructions: true)
!456 = !DISubroutineType(types: !457)
!457 = !{!45, !45, !45}
!458 = !{!454, !459}
!459 = !DILocalVariable(name: "b", arg: 2, scope: !455, file: !2, line: 13, type: !45)
!460 = !DILocation(line: 0, scope: !455, inlinedAt: !461)
!461 = distinct !DILocation(line: 70, column: 9, scope: !434)
!462 = !DILocation(line: 0, scope: !434)
!463 = !DILocation(line: 71, column: 5, scope: !434)
!464 = !DILocation(line: 14, column: 12, scope: !455, inlinedAt: !461, atomGroup: 99, atomRank: 2)
!465 = !DILocation(line: 72, column: 39, scope: !434)
!466 = !DILocation(line: 72, column: 9, scope: !434, atomGroup: 100, atomRank: 2)
!467 = !DILocation(line: 71, column: 57, scope: !434, atomGroup: 100, atomRank: 1)
!468 = !DILocation(line: 80, column: 1, scope: !388, atomGroup: 34, atomRank: 1)
!469 = !DILocation(line: 0, scope: !421)
!470 = !DILocation(line: 0, scope: !424)
!471 = !DILocation(line: 57, column: 5, scope: !424, atomGroup: 72, atomRank: 1)
!472 = !DILocation(line: 56, column: 26, scope: !421, atomGroup: 9, atomRank: 2)
!473 = !DILocation(line: 57, column: 5, scope: !424)
!474 = !DILocation(line: 57, column: 42, scope: !427, atomGroup: 21, atomRank: 2)
!475 = !DILocation(line: 58, column: 28, scope: !426, atomGroup: 13, atomRank: 2)
!476 = !DILocation(line: 59, column: 23, scope: !426)
!477 = !DILocation(line: 59, column: 23, scope: !426, atomGroup: 14, atomRank: 2)
!478 = !DILocation(line: 28, column: 10, scope: !479, inlinedAt: !489)
!479 = distinct !DISubprogram(name: "color_component_int", scope: !2, file: !2, line: 24, type: !480, scopeLine: 27, flags: DIFlagPrototyped | DIFlagAllCallsDescribed, spFlags: DISPFlagLocalToUnit | DISPFlagDefinition | DISPFlagOptimized, unit: !42, retainedNodes: !482, keyInstructions: true)
!480 = !DISubroutineType(types: !481)
!481 = !{!58, !276, !62, !62, !62, !62, !62}
!482 = !{!483, !484, !485, !486, !487, !488}
!483 = !DILocalVariable(name: "data", arg: 1, scope: !479, file: !2, line: 24, type: !276)
!484 = !DILocalVariable(name: "stride", arg: 2, scope: !479, file: !2, line: 25, type: !62)
!485 = !DILocalVariable(name: "x", arg: 3, scope: !479, file: !2, line: 25, type: !62)
!486 = !DILocalVariable(name: "y", arg: 4, scope: !479, file: !2, line: 26, type: !62)
!487 = !DILocalVariable(name: "bpp", arg: 5, scope: !479, file: !2, line: 26, type: !62)
!488 = !DILocalVariable(name: "c", arg: 6, scope: !479, file: !2, line: 27, type: !62)
!489 = distinct !DILocation(line: 61, column: 27, scope: !490)
!490 = distinct !DILexicalBlock(scope: !491, file: !2, line: 60, column: 46)
!491 = distinct !DILexicalBlock(scope: !430, file: !2, line: 60, column: 7)
!492 = !DILocation(line: 28, column: 10, scope: !479, inlinedAt: !489, atomGroup: 1, atomRank: 2)
!493 = !DILocation(line: 61, column: 27, scope: !490)
!494 = !DILocation(line: 61, column: 25, scope: !490)
!495 = !DILocation(line: 61, column: 16, scope: !490, atomGroup: 18, atomRank: 2)
!496 = !DILocation(line: 61, column: 16, scope: !490, atomGroup: 78, atomRank: 2)
!497 = !DILocation(line: 61, column: 16, scope: !490, atomGroup: 83, atomRank: 2)
!498 = !DILocation(line: 61, column: 16, scope: !490, atomGroup: 88, atomRank: 2)
!499 = !DILocation(line: 57, column: 5, scope: !424, atomGroup: 12, atomRank: 1)
!500 = distinct !{!500, !146, !147, !148}
!501 = !{!"branch_weights", i32 8, i32 8}
!502 = distinct !{!502, !146, !147, !148}
!503 = !DILocation(line: 55, column: 40, scope: !422, atomGroup: 23, atomRank: 2)
!504 = !DILocation(line: 55, column: 3, scope: !419, atomGroup: 8, atomRank: 1)
!505 = distinct !{!505, !506, !507, !146}
!506 = !DILocation(line: 55, column: 3, scope: !419)
!507 = !DILocation(line: 66, column: 3, scope: !419)
!508 = !DILocation(line: 0, scope: !426)
!509 = !DILocation(line: 0, scope: !430)
!510 = !DILocation(line: 0, scope: !479, inlinedAt: !489)
!511 = !DILocation(line: 28, column: 10, scope: !479, inlinedAt: !489, atomGroup: 87, atomRank: 2)
!512 = !DILocation(line: 28, column: 10, scope: !479, inlinedAt: !489, atomGroup: 77, atomRank: 2)
!513 = !DILocation(line: 28, column: 10, scope: !479, inlinedAt: !489, atomGroup: 82, atomRank: 2)
!514 = !DILocation(line: 57, column: 29, scope: !427, atomGroup: 11, atomRank: 1)
!515 = distinct !{!515, !473, !516, !146, !147}
!516 = !DILocation(line: 65, column: 5, scope: !424)
