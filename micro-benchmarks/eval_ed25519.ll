; ModuleID = 'eval_ed25519.c'
source_filename = "eval_ed25519.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [119 x i8] c"Usage: %s <num_benchmark_iterations> <num_warmup_iterations> <message> <cycle_counts_file> [<dynamic_hitcounts_file>]\0A\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [30 x i8] c"Error initializing lib sodium\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [125 x i8] c"(sodium_init_success == sodium_init_result || sodium_already_initd == sodium_init_result) && \22Error initializing lib sodium\22\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [15 x i8] c"eval_ed25519.c\00", align 1, !dbg !17
@__PRETTY_FUNCTION__.main = private unnamed_addr constant [23 x i8] c"int main(int, char **)\00", align 1, !dbg !22
@.str.4 = private unnamed_addr constant [53 x i8] c"Couldn't allocate opened_msg bytes in eval_ed25519.c\00", align 1, !dbg !28
@.str.5 = private unnamed_addr constant [69 x i8] c"opened_msg && \22Couldn't allocate opened_msg bytes in eval_ed25519.c\22\00", align 1, !dbg !33
@.str.6 = private unnamed_addr constant [53 x i8] c"Couldn't allocate signed msg bytes in eval_ed25519.c\00", align 1, !dbg !38
@.str.7 = private unnamed_addr constant [62 x i8] c"msg && \22Couldn't allocate signed msg bytes in eval_ed25519.c\22\00", align 1, !dbg !40
@.str.8 = private unnamed_addr constant [54 x i8] c"Couldn't allocate private key bytes in eval_ed25519.c\00", align 1, !dbg !45
@.str.9 = private unnamed_addr constant [65 x i8] c"privk && \22Couldn't allocate private key bytes in eval_ed25519.c\22\00", align 1, !dbg !50
@.str.10 = private unnamed_addr constant [50 x i8] c"Couldn't allocate pub key bytes in eval_ed25519.c\00", align 1, !dbg !55
@.str.11 = private unnamed_addr constant [60 x i8] c"pubk && \22Couldn't allocate pub key bytes in eval_ed25519.c\22\00", align 1, !dbg !60
@.str.12 = private unnamed_addr constant [62 x i8] c"Couldn't allocate array for benchmark times in eval_ed25519.c\00", align 1, !dbg !65
@.str.13 = private unnamed_addr constant [73 x i8] c"times && \22Couldn't allocate array for benchmark times in eval_ed25519.c\22\00", align 1, !dbg !67
@.str.14 = private unnamed_addr constant [44 x i8] c"FAILURE: eval_ed25519 failed at crypto_sign\00", align 1, !dbg !72
@.str.15 = private unnamed_addr constant [70 x i8] c"FAILURE: eval_ed25519 failed sanity check, sign of msg did not verify\00", align 1, !dbg !77
@.str.16 = private unnamed_addr constant [2 x i8] c"w\00", align 1, !dbg !82
@.str.17 = private unnamed_addr constant [44 x i8] c"Couldn't open cycle counts file for writing\00", align 1, !dbg !87
@.str.18 = private unnamed_addr constant [69 x i8] c"ccounts_out != NULL && \22Couldn't open cycle counts file for writing\22\00", align 1, !dbg !89
@.str.19 = private unnamed_addr constant [49 x i8] c"ed25519 cycle counts (%d iterations, %d warmup)\0A\00", align 1, !dbg !91
@.str.20 = private unnamed_addr constant [5 x i8] c"%lu\0A\00", align 1, !dbg !96
@.str.21 = private unnamed_addr constant [33 x i8] c"Couldn't close cycle counts file\00", align 1, !dbg !101
@.str.22 = private unnamed_addr constant [65 x i8] c"fclose(ccounts_out) != EOF && \22Couldn't close cycle counts file\22\00", align 1, !dbg !106

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %argc, ptr noundef %argv) #0 !dbg !128 {
entry:
  %retval = alloca i32, align 4
  %argc.addr = alloca i32, align 4
  %argv.addr = alloca ptr, align 8
  %sodium_init_success = alloca i32, align 4
  %sodium_already_initd = alloca i32, align 4
  %sodium_init_result = alloca i32, align 4
  %num_iter = alloca i32, align 4
  %num_warmup = alloca i32, align 4
  %msg = alloca ptr, align 8
  %msg_sz = alloca i64, align 8
  %opened_msg = alloca ptr, align 8
  %signed_msg_sz = alloca i64, align 8
  %signed_msg = alloca ptr, align 8
  %privk = alloca ptr, align 8
  %pubk = alloca ptr, align 8
  %times = alloca ptr, align 8
  %start_time = alloca i64, align 8
  %end_time = alloca i64, align 8
  %cur_iter = alloca i32, align 4
  %_eval_unused = alloca i32, align 4
  %_eval_cycles_low = alloca i32, align 4
  %_eval_cycles_high = alloca i32, align 4
  %tmp = alloca i64, align 8
  %sign_result = alloca i32, align 4
  %_eval_cycles_low53 = alloca i32, align 4
  %_eval_cycles_high54 = alloca i32, align 4
  %tmp57 = alloca i64, align 8
  %open_result = alloca i32, align 4
  %ccounts_out = alloca ptr, align 8
  %ii = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 %argc, ptr %argc.addr, align 4
    #dbg_declare(ptr %argc.addr, !133, !DIExpression(), !134)
  store ptr %argv, ptr %argv.addr, align 8
    #dbg_declare(ptr %argv.addr, !135, !DIExpression(), !136)
  %0 = load i32, ptr %argc.addr, align 4, !dbg !137
  %cmp = icmp slt i32 %0, 5, !dbg !139
  br i1 %cmp, label %if.then, label %if.end, !dbg !139

if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %argv.addr, align 8, !dbg !140
  %arrayidx = getelementptr inbounds ptr, ptr %1, i64 0, !dbg !140
  %2 = load ptr, ptr %arrayidx, align 8, !dbg !140
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str, ptr noundef %2), !dbg !142
  br label %if.end, !dbg !143

if.end:                                           ; preds = %if.then, %entry
    #dbg_declare(ptr %sodium_init_success, !144, !DIExpression(), !146)
  store i32 0, ptr %sodium_init_success, align 4, !dbg !146
    #dbg_declare(ptr %sodium_already_initd, !147, !DIExpression(), !148)
  store i32 1, ptr %sodium_already_initd, align 4, !dbg !148
    #dbg_declare(ptr %sodium_init_result, !149, !DIExpression(), !150)
  %call1 = call i32 @sodium_init(), !dbg !151
  store i32 %call1, ptr %sodium_init_result, align 4, !dbg !150
  %3 = load i32, ptr %sodium_init_result, align 4, !dbg !152
  %cmp2 = icmp eq i32 0, %3, !dbg !153
  br i1 %cmp2, label %land.lhs.true, label %lor.lhs.false, !dbg !154

lor.lhs.false:                                    ; preds = %if.end
  %4 = load i32, ptr %sodium_init_result, align 4, !dbg !155
  %cmp3 = icmp eq i32 1, %4, !dbg !156
  br i1 %cmp3, label %land.lhs.true, label %cond.false, !dbg !157

land.lhs.true:                                    ; preds = %lor.lhs.false, %if.end
  br i1 true, label %cond.true, label %cond.false, !dbg !158

cond.true:                                        ; preds = %land.lhs.true
  br label %cond.end, !dbg !158

cond.false:                                       ; preds = %land.lhs.true, %lor.lhs.false
  call void @__assert_fail(ptr noundef @.str.2, ptr noundef @.str.3, i32 noundef 71, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !158
  unreachable, !dbg !158

5:                                                ; No predecessors!
  br label %cond.end, !dbg !158

cond.end:                                         ; preds = %5, %cond.true
    #dbg_declare(ptr %num_iter, !159, !DIExpression(), !160)
  %6 = load ptr, ptr %argv.addr, align 8, !dbg !161
  %arrayidx4 = getelementptr inbounds ptr, ptr %6, i64 1, !dbg !161
  %7 = load ptr, ptr %arrayidx4, align 8, !dbg !161
  %call5 = call i64 @strtol(ptr noundef %7, ptr noundef null, i32 noundef 10) #8, !dbg !162
  %conv = trunc i64 %call5 to i32, !dbg !162
  store i32 %conv, ptr %num_iter, align 4, !dbg !160
    #dbg_declare(ptr %num_warmup, !163, !DIExpression(), !164)
  %8 = load ptr, ptr %argv.addr, align 8, !dbg !165
  %arrayidx6 = getelementptr inbounds ptr, ptr %8, i64 2, !dbg !165
  %9 = load ptr, ptr %arrayidx6, align 8, !dbg !165
  %call7 = call i64 @strtol(ptr noundef %9, ptr noundef null, i32 noundef 10) #8, !dbg !166
  %conv8 = trunc i64 %call7 to i32, !dbg !166
  store i32 %conv8, ptr %num_warmup, align 4, !dbg !164
    #dbg_declare(ptr %msg, !167, !DIExpression(), !168)
  %10 = load ptr, ptr %argv.addr, align 8, !dbg !169
  %arrayidx9 = getelementptr inbounds ptr, ptr %10, i64 3, !dbg !169
  %11 = load ptr, ptr %arrayidx9, align 8, !dbg !169
  store ptr %11, ptr %msg, align 8, !dbg !168
    #dbg_declare(ptr %msg_sz, !170, !DIExpression(), !172)
  %12 = load ptr, ptr %argv.addr, align 8, !dbg !173
  %arrayidx10 = getelementptr inbounds ptr, ptr %12, i64 3, !dbg !173
  %13 = load ptr, ptr %arrayidx10, align 8, !dbg !173
  %call11 = call i64 @strlen(ptr noundef %13) #9, !dbg !174
  store i64 %call11, ptr %msg_sz, align 8, !dbg !172
    #dbg_declare(ptr %opened_msg, !175, !DIExpression(), !176)
  %14 = load i64, ptr %msg_sz, align 8, !dbg !177
  %call12 = call noalias ptr @malloc(i64 noundef %14) #10, !dbg !178
  store ptr %call12, ptr %opened_msg, align 8, !dbg !176
  %15 = load ptr, ptr %opened_msg, align 8, !dbg !179
  %tobool = icmp ne ptr %15, null, !dbg !179
  br i1 %tobool, label %land.lhs.true13, label %cond.false15, !dbg !180

land.lhs.true13:                                  ; preds = %cond.end
  br i1 true, label %cond.true14, label %cond.false15, !dbg !181

cond.true14:                                      ; preds = %land.lhs.true13
  br label %cond.end16, !dbg !181

cond.false15:                                     ; preds = %land.lhs.true13, %cond.end
  call void @__assert_fail(ptr noundef @.str.5, ptr noundef @.str.3, i32 noundef 87, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !181
  unreachable, !dbg !181

16:                                               ; No predecessors!
  br label %cond.end16, !dbg !181

cond.end16:                                       ; preds = %16, %cond.true14
    #dbg_declare(ptr %signed_msg_sz, !182, !DIExpression(), !183)
  %17 = load i64, ptr %msg_sz, align 8, !dbg !184
  %call17 = call i64 @crypto_sign_bytes(), !dbg !185
  %add = add i64 %17, %call17, !dbg !186
  store i64 %add, ptr %signed_msg_sz, align 8, !dbg !183
    #dbg_declare(ptr %signed_msg, !187, !DIExpression(), !188)
  %18 = load i64, ptr %signed_msg_sz, align 8, !dbg !189
  %call18 = call noalias ptr @malloc(i64 noundef %18) #10, !dbg !190
  store ptr %call18, ptr %signed_msg, align 8, !dbg !188
  %19 = load ptr, ptr %msg, align 8, !dbg !191
  %tobool19 = icmp ne ptr %19, null, !dbg !191
  br i1 %tobool19, label %land.lhs.true20, label %cond.false22, !dbg !192

land.lhs.true20:                                  ; preds = %cond.end16
  br i1 true, label %cond.true21, label %cond.false22, !dbg !193

cond.true21:                                      ; preds = %land.lhs.true20
  br label %cond.end23, !dbg !193

cond.false22:                                     ; preds = %land.lhs.true20, %cond.end16
  call void @__assert_fail(ptr noundef @.str.7, ptr noundef @.str.3, i32 noundef 92, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !193
  unreachable, !dbg !193

20:                                               ; No predecessors!
  br label %cond.end23, !dbg !193

cond.end23:                                       ; preds = %20, %cond.true21
    #dbg_declare(ptr %privk, !194, !DIExpression(), !195)
  %call24 = call i64 @crypto_sign_secretkeybytes(), !dbg !196
  %call25 = call noalias ptr @malloc(i64 noundef %call24) #10, !dbg !197
  store ptr %call25, ptr %privk, align 8, !dbg !195
    #dbg_declare(ptr %pubk, !198, !DIExpression(), !199)
  %call26 = call i64 @crypto_sign_publickeybytes(), !dbg !200
  %call27 = call noalias ptr @malloc(i64 noundef %call26) #10, !dbg !201
  store ptr %call27, ptr %pubk, align 8, !dbg !199
  %21 = load ptr, ptr %privk, align 8, !dbg !202
  %tobool28 = icmp ne ptr %21, null, !dbg !202
  br i1 %tobool28, label %land.lhs.true29, label %cond.false31, !dbg !203

land.lhs.true29:                                  ; preds = %cond.end23
  br i1 true, label %cond.true30, label %cond.false31, !dbg !204

cond.true30:                                      ; preds = %land.lhs.true29
  br label %cond.end32, !dbg !204

cond.false31:                                     ; preds = %land.lhs.true29, %cond.end23
  call void @__assert_fail(ptr noundef @.str.9, ptr noundef @.str.3, i32 noundef 97, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !204
  unreachable, !dbg !204

22:                                               ; No predecessors!
  br label %cond.end32, !dbg !204

cond.end32:                                       ; preds = %22, %cond.true30
  %23 = load ptr, ptr %pubk, align 8, !dbg !205
  %tobool33 = icmp ne ptr %23, null, !dbg !205
  br i1 %tobool33, label %land.lhs.true34, label %cond.false36, !dbg !206

land.lhs.true34:                                  ; preds = %cond.end32
  br i1 true, label %cond.true35, label %cond.false36, !dbg !207

cond.true35:                                      ; preds = %land.lhs.true34
  br label %cond.end37, !dbg !207

cond.false36:                                     ; preds = %land.lhs.true34, %cond.end32
  call void @__assert_fail(ptr noundef @.str.11, ptr noundef @.str.3, i32 noundef 98, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !207
  unreachable, !dbg !207

24:                                               ; No predecessors!
  br label %cond.end37, !dbg !207

cond.end37:                                       ; preds = %24, %cond.true35
    #dbg_declare(ptr %times, !208, !DIExpression(), !210)
  %25 = load i32, ptr %num_iter, align 4, !dbg !211
  %conv38 = sext i32 %25 to i64, !dbg !211
  %call39 = call noalias ptr @calloc(i64 noundef %conv38, i64 noundef 8) #11, !dbg !212
  store ptr %call39, ptr %times, align 8, !dbg !210
  %26 = load ptr, ptr %times, align 8, !dbg !213
  %tobool40 = icmp ne ptr %26, null, !dbg !213
  br i1 %tobool40, label %land.lhs.true41, label %cond.false43, !dbg !214

land.lhs.true41:                                  ; preds = %cond.end37
  br i1 true, label %cond.true42, label %cond.false43, !dbg !215

cond.true42:                                      ; preds = %land.lhs.true41
  br label %cond.end44, !dbg !215

cond.false43:                                     ; preds = %land.lhs.true41, %cond.end37
  call void @__assert_fail(ptr noundef @.str.13, ptr noundef @.str.3, i32 noundef 103, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !215
  unreachable, !dbg !215

27:                                               ; No predecessors!
  br label %cond.end44, !dbg !215

cond.end44:                                       ; preds = %27, %cond.true42
    #dbg_declare(ptr %start_time, !216, !DIExpression(), !218)
  store volatile i64 0, ptr %start_time, align 8, !dbg !218
    #dbg_declare(ptr %end_time, !219, !DIExpression(), !220)
  store volatile i64 0, ptr %end_time, align 8, !dbg !220
    #dbg_declare(ptr %cur_iter, !221, !DIExpression(), !223)
  store i32 0, ptr %cur_iter, align 4, !dbg !223
  br label %for.cond, !dbg !224

for.cond:                                         ; preds = %for.inc, %cond.end44
  %28 = load i32, ptr %cur_iter, align 4, !dbg !225
  %29 = load i32, ptr %num_iter, align 4, !dbg !227
  %30 = load i32, ptr %num_warmup, align 4, !dbg !228
  %add45 = add nsw i32 %29, %30, !dbg !229
  %cmp46 = icmp slt i32 %28, %add45, !dbg !230
  br i1 %cmp46, label %for.body, label %for.end, !dbg !231

for.body:                                         ; preds = %for.cond
    #dbg_declare(ptr %_eval_unused, !232, !DIExpression(), !234)
  %31 = load ptr, ptr %pubk, align 8, !dbg !235
  %32 = load ptr, ptr %privk, align 8, !dbg !236
  %call48 = call i32 @crypto_sign_keypair(ptr noundef %31, ptr noundef %32), !dbg !237
  store i32 %call48, ptr %_eval_unused, align 4, !dbg !234
    #dbg_declare(ptr %_eval_cycles_low, !238, !DIExpression(), !243)
  store i32 0, ptr %_eval_cycles_low, align 4, !dbg !243
    #dbg_declare(ptr %_eval_cycles_high, !244, !DIExpression(), !243)
  store i32 0, ptr %_eval_cycles_high, align 4, !dbg !243
  %33 = call { i32, i32 } asm sideeffect "cpuid\0A\09rdtsc\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !243, !srcloc !245
  %asmresult = extractvalue { i32, i32 } %33, 0, !dbg !243
  %asmresult49 = extractvalue { i32, i32 } %33, 1, !dbg !243
  store i32 %asmresult, ptr %_eval_cycles_low, align 4, !dbg !243
  store i32 %asmresult49, ptr %_eval_cycles_high, align 4, !dbg !243
  %34 = load i32, ptr %_eval_cycles_high, align 4, !dbg !243
  %conv50 = zext i32 %34 to i64, !dbg !243
  %shl = shl i64 %conv50, 32, !dbg !243
  %35 = load i32, ptr %_eval_cycles_low, align 4, !dbg !243
  %conv51 = zext i32 %35 to i64, !dbg !243
  %or = or i64 %shl, %conv51, !dbg !243
  store i64 %or, ptr %tmp, align 8, !dbg !243
  %36 = load i64, ptr %tmp, align 8, !dbg !243
  store volatile i64 %36, ptr %start_time, align 8, !dbg !246
    #dbg_declare(ptr %sign_result, !247, !DIExpression(), !248)
  %37 = load ptr, ptr %signed_msg, align 8, !dbg !249
  %38 = load ptr, ptr %msg, align 8, !dbg !250
  %39 = load i64, ptr %msg_sz, align 8, !dbg !251
  %40 = load ptr, ptr %privk, align 8, !dbg !252
  %call52 = call i32 @crypto_sign(ptr noundef %37, ptr noundef %signed_msg_sz, ptr noundef %38, i64 noundef %39, ptr noundef %40), !dbg !253
  store i32 %call52, ptr %sign_result, align 4, !dbg !248
    #dbg_declare(ptr %_eval_cycles_low53, !254, !DIExpression(), !256)
  store i32 0, ptr %_eval_cycles_low53, align 4, !dbg !256
    #dbg_declare(ptr %_eval_cycles_high54, !257, !DIExpression(), !256)
  store i32 0, ptr %_eval_cycles_high54, align 4, !dbg !256
  %41 = call { i32, i32 } asm sideeffect "rdtscp\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09cpuid\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !256, !srcloc !258
  %asmresult55 = extractvalue { i32, i32 } %41, 0, !dbg !256
  %asmresult56 = extractvalue { i32, i32 } %41, 1, !dbg !256
  store i32 %asmresult55, ptr %_eval_cycles_low53, align 4, !dbg !256
  store i32 %asmresult56, ptr %_eval_cycles_high54, align 4, !dbg !256
  %42 = load i32, ptr %_eval_cycles_high54, align 4, !dbg !256
  %conv58 = zext i32 %42 to i64, !dbg !256
  %shl59 = shl i64 %conv58, 32, !dbg !256
  %43 = load i32, ptr %_eval_cycles_low53, align 4, !dbg !256
  %conv60 = zext i32 %43 to i64, !dbg !256
  %or61 = or i64 %shl59, %conv60, !dbg !256
  store i64 %or61, ptr %tmp57, align 8, !dbg !256
  %44 = load i64, ptr %tmp57, align 8, !dbg !256
  store volatile i64 %44, ptr %end_time, align 8, !dbg !259
  %45 = load i32, ptr %sign_result, align 4, !dbg !260
  %cmp62 = icmp eq i32 -1, %45, !dbg !262
  br i1 %cmp62, label %if.then64, label %if.end66, !dbg !262

if.then64:                                        ; preds = %for.body
  %call65 = call i32 (ptr, ...) @printf(ptr noundef @.str.14), !dbg !263
  call void @exit(i32 noundef 0) #7, !dbg !265
  unreachable, !dbg !265

if.end66:                                         ; preds = %for.body
  %46 = load i32, ptr %cur_iter, align 4, !dbg !266
  %47 = load i32, ptr %num_warmup, align 4, !dbg !268
  %cmp67 = icmp sge i32 %46, %47, !dbg !269
  br i1 %cmp67, label %if.then69, label %if.end72, !dbg !269

if.then69:                                        ; preds = %if.end66
  %48 = load volatile i64, ptr %end_time, align 8, !dbg !270
  %49 = load volatile i64, ptr %start_time, align 8, !dbg !272
  %sub = sub i64 %48, %49, !dbg !273
  %50 = load ptr, ptr %times, align 8, !dbg !274
  %51 = load i32, ptr %cur_iter, align 4, !dbg !275
  %52 = load i32, ptr %num_warmup, align 4, !dbg !276
  %sub70 = sub nsw i32 %51, %52, !dbg !277
  %idxprom = sext i32 %sub70 to i64, !dbg !274
  %arrayidx71 = getelementptr inbounds i64, ptr %50, i64 %idxprom, !dbg !274
  store i64 %sub, ptr %arrayidx71, align 8, !dbg !278
  br label %if.end72, !dbg !279

if.end72:                                         ; preds = %if.then69, %if.end66
    #dbg_declare(ptr %open_result, !280, !DIExpression(), !281)
  %53 = load ptr, ptr %opened_msg, align 8, !dbg !282
  %54 = load ptr, ptr %signed_msg, align 8, !dbg !283
  %55 = load i64, ptr %signed_msg_sz, align 8, !dbg !284
  %56 = load ptr, ptr %pubk, align 8, !dbg !285
  %call73 = call i32 @crypto_sign_open(ptr noundef %53, ptr noundef %msg_sz, ptr noundef %54, i64 noundef %55, ptr noundef %56), !dbg !286
  store i32 %call73, ptr %open_result, align 4, !dbg !281
  %57 = load i32, ptr %open_result, align 4, !dbg !287
  %cmp74 = icmp ne i32 0, %57, !dbg !289
  br i1 %cmp74, label %if.then76, label %if.end78, !dbg !289

if.then76:                                        ; preds = %if.end72
  %call77 = call i32 (ptr, ...) @printf(ptr noundef @.str.15), !dbg !290
  call void @exit(i32 noundef 0) #7, !dbg !292
  unreachable, !dbg !292

if.end78:                                         ; preds = %if.end72
  br label %for.inc, !dbg !293

for.inc:                                          ; preds = %if.end78
  %58 = load i32, ptr %cur_iter, align 4, !dbg !294
  %inc = add nsw i32 %58, 1, !dbg !294
  store i32 %inc, ptr %cur_iter, align 4, !dbg !294
  br label %for.cond, !dbg !295, !llvm.loop !296

for.end:                                          ; preds = %for.cond
    #dbg_declare(ptr %ccounts_out, !299, !DIExpression(), !357)
  %59 = load ptr, ptr %argv.addr, align 8, !dbg !358
  %arrayidx79 = getelementptr inbounds ptr, ptr %59, i64 4, !dbg !358
  %60 = load ptr, ptr %arrayidx79, align 8, !dbg !358
  %call80 = call noalias ptr @fopen(ptr noundef %60, ptr noundef @.str.16), !dbg !359
  store ptr %call80, ptr %ccounts_out, align 8, !dbg !357
  %61 = load ptr, ptr %ccounts_out, align 8, !dbg !360
  %cmp81 = icmp ne ptr %61, null, !dbg !361
  br i1 %cmp81, label %land.lhs.true83, label %cond.false85, !dbg !362

land.lhs.true83:                                  ; preds = %for.end
  br i1 true, label %cond.true84, label %cond.false85, !dbg !363

cond.true84:                                      ; preds = %land.lhs.true83
  br label %cond.end86, !dbg !363

cond.false85:                                     ; preds = %land.lhs.true83, %for.end
  call void @__assert_fail(ptr noundef @.str.18, ptr noundef @.str.3, i32 noundef 147, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !363
  unreachable, !dbg !363

62:                                               ; No predecessors!
  br label %cond.end86, !dbg !363

cond.end86:                                       ; preds = %62, %cond.true84
  %63 = load ptr, ptr %ccounts_out, align 8, !dbg !364
  %64 = load i32, ptr %num_iter, align 4, !dbg !365
  %65 = load i32, ptr %num_warmup, align 4, !dbg !366
  %call87 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %63, ptr noundef @.str.19, i32 noundef %64, i32 noundef %65) #8, !dbg !367
    #dbg_declare(ptr %ii, !368, !DIExpression(), !370)
  store i32 0, ptr %ii, align 4, !dbg !370
  br label %for.cond88, !dbg !371

for.cond88:                                       ; preds = %for.inc95, %cond.end86
  %66 = load i32, ptr %ii, align 4, !dbg !372
  %67 = load i32, ptr %num_iter, align 4, !dbg !374
  %cmp89 = icmp slt i32 %66, %67, !dbg !375
  br i1 %cmp89, label %for.body91, label %for.end97, !dbg !376

for.body91:                                       ; preds = %for.cond88
  %68 = load ptr, ptr %ccounts_out, align 8, !dbg !377
  %69 = load ptr, ptr %times, align 8, !dbg !379
  %70 = load i32, ptr %ii, align 4, !dbg !380
  %idxprom92 = sext i32 %70 to i64, !dbg !379
  %arrayidx93 = getelementptr inbounds i64, ptr %69, i64 %idxprom92, !dbg !379
  %71 = load i64, ptr %arrayidx93, align 8, !dbg !379
  %call94 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %68, ptr noundef @.str.20, i64 noundef %71) #8, !dbg !381
  br label %for.inc95, !dbg !382

for.inc95:                                        ; preds = %for.body91
  %72 = load i32, ptr %ii, align 4, !dbg !383
  %inc96 = add nsw i32 %72, 1, !dbg !383
  store i32 %inc96, ptr %ii, align 4, !dbg !383
  br label %for.cond88, !dbg !384, !llvm.loop !385

for.end97:                                        ; preds = %for.cond88
  %73 = load ptr, ptr %ccounts_out, align 8, !dbg !387
  %call98 = call i32 @fclose(ptr noundef %73), !dbg !388
  %cmp99 = icmp ne i32 %call98, -1, !dbg !389
  br i1 %cmp99, label %land.lhs.true101, label %cond.false103, !dbg !390

land.lhs.true101:                                 ; preds = %for.end97
  br i1 true, label %cond.true102, label %cond.false103, !dbg !391

cond.true102:                                     ; preds = %land.lhs.true101
  br label %cond.end104, !dbg !391

cond.false103:                                    ; preds = %land.lhs.true101, %for.end97
  call void @__assert_fail(ptr noundef @.str.22, ptr noundef @.str.3, i32 noundef 155, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !391
  unreachable, !dbg !391

74:                                               ; No predecessors!
  br label %cond.end104, !dbg !391

cond.end104:                                      ; preds = %74, %cond.true102
  ret i32 0, !dbg !392
}

declare i32 @printf(ptr noundef, ...) #1

declare i32 @sodium_init() #1

; Function Attrs: noreturn nounwind
declare void @__assert_fail(ptr noundef, ptr noundef, i32 noundef, ptr noundef) #2

; Function Attrs: nounwind
declare i64 @strtol(ptr noundef, ptr noundef, i32 noundef) #3

; Function Attrs: nounwind willreturn memory(read)
declare i64 @strlen(ptr noundef) #4

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #5

declare i64 @crypto_sign_bytes() #1

declare i64 @crypto_sign_secretkeybytes() #1

declare i64 @crypto_sign_publickeybytes() #1

; Function Attrs: nounwind allocsize(0,1)
declare noalias ptr @calloc(i64 noundef, i64 noundef) #6

declare i32 @crypto_sign_keypair(ptr noundef, ptr noundef) #1

declare i32 @crypto_sign(ptr noundef, ptr noundef, ptr noundef, i64 noundef, ptr noundef) #1

; Function Attrs: noreturn nounwind
declare void @exit(i32 noundef) #2

declare i32 @crypto_sign_open(ptr noundef, ptr noundef, ptr noundef, i64 noundef, ptr noundef) #1

declare noalias ptr @fopen(ptr noundef, ptr noundef) #1

; Function Attrs: nounwind
declare i32 @fprintf(ptr noundef, ptr noundef, ...) #3

declare i32 @fclose(ptr noundef) #1

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { noreturn nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind willreturn memory(read) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind allocsize(0) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { nounwind allocsize(0,1) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { noreturn nounwind }
attributes #8 = { nounwind }
attributes #9 = { nounwind willreturn memory(read) }
attributes #10 = { nounwind allocsize(0) }
attributes #11 = { nounwind allocsize(0,1) }

!llvm.dbg.cu = !{!108}
!llvm.module.flags = !{!121, !122, !123, !124, !125, !126}
!llvm.ident = !{!127}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 61, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "eval_ed25519.c", directory: "/home/rgangar/Documents/llvm-data-independent-timing/micro-benchmarks", checksumkind: CSK_MD5, checksum: "c9430a790fc37a004cf513dc828be2fb")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 952, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 119)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 71, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 240, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 30)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 69, type: !14, isLocal: true, isDefinition: true)
!14 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1000, elements: !15)
!15 = !{!16}
!16 = !DISubrange(count: 125)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(scope: null, file: !2, line: 69, type: !19, isLocal: true, isDefinition: true)
!19 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 120, elements: !20)
!20 = !{!21}
!21 = !DISubrange(count: 15)
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(scope: null, file: !2, line: 69, type: !24, isLocal: true, isDefinition: true)
!24 = !DICompositeType(tag: DW_TAG_array_type, baseType: !25, size: 184, elements: !26)
!25 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!26 = !{!27}
!27 = !DISubrange(count: 23)
!28 = !DIGlobalVariableExpression(var: !29, expr: !DIExpression())
!29 = distinct !DIGlobalVariable(scope: null, file: !2, line: 87, type: !30, isLocal: true, isDefinition: true)
!30 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 424, elements: !31)
!31 = !{!32}
!32 = !DISubrange(count: 53)
!33 = !DIGlobalVariableExpression(var: !34, expr: !DIExpression())
!34 = distinct !DIGlobalVariable(scope: null, file: !2, line: 87, type: !35, isLocal: true, isDefinition: true)
!35 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 552, elements: !36)
!36 = !{!37}
!37 = !DISubrange(count: 69)
!38 = !DIGlobalVariableExpression(var: !39, expr: !DIExpression())
!39 = distinct !DIGlobalVariable(scope: null, file: !2, line: 92, type: !30, isLocal: true, isDefinition: true)
!40 = !DIGlobalVariableExpression(var: !41, expr: !DIExpression())
!41 = distinct !DIGlobalVariable(scope: null, file: !2, line: 92, type: !42, isLocal: true, isDefinition: true)
!42 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 496, elements: !43)
!43 = !{!44}
!44 = !DISubrange(count: 62)
!45 = !DIGlobalVariableExpression(var: !46, expr: !DIExpression())
!46 = distinct !DIGlobalVariable(scope: null, file: !2, line: 97, type: !47, isLocal: true, isDefinition: true)
!47 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 432, elements: !48)
!48 = !{!49}
!49 = !DISubrange(count: 54)
!50 = !DIGlobalVariableExpression(var: !51, expr: !DIExpression())
!51 = distinct !DIGlobalVariable(scope: null, file: !2, line: 97, type: !52, isLocal: true, isDefinition: true)
!52 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 520, elements: !53)
!53 = !{!54}
!54 = !DISubrange(count: 65)
!55 = !DIGlobalVariableExpression(var: !56, expr: !DIExpression())
!56 = distinct !DIGlobalVariable(scope: null, file: !2, line: 98, type: !57, isLocal: true, isDefinition: true)
!57 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 400, elements: !58)
!58 = !{!59}
!59 = !DISubrange(count: 50)
!60 = !DIGlobalVariableExpression(var: !61, expr: !DIExpression())
!61 = distinct !DIGlobalVariable(scope: null, file: !2, line: 98, type: !62, isLocal: true, isDefinition: true)
!62 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 480, elements: !63)
!63 = !{!64}
!64 = !DISubrange(count: 60)
!65 = !DIGlobalVariableExpression(var: !66, expr: !DIExpression())
!66 = distinct !DIGlobalVariable(scope: null, file: !2, line: 103, type: !42, isLocal: true, isDefinition: true)
!67 = !DIGlobalVariableExpression(var: !68, expr: !DIExpression())
!68 = distinct !DIGlobalVariable(scope: null, file: !2, line: 102, type: !69, isLocal: true, isDefinition: true)
!69 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 584, elements: !70)
!70 = !{!71}
!71 = !DISubrange(count: 73)
!72 = !DIGlobalVariableExpression(var: !73, expr: !DIExpression())
!73 = distinct !DIGlobalVariable(scope: null, file: !2, line: 128, type: !74, isLocal: true, isDefinition: true)
!74 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 352, elements: !75)
!75 = !{!76}
!76 = !DISubrange(count: 44)
!77 = !DIGlobalVariableExpression(var: !78, expr: !DIExpression())
!78 = distinct !DIGlobalVariable(scope: null, file: !2, line: 140, type: !79, isLocal: true, isDefinition: true)
!79 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 560, elements: !80)
!80 = !{!81}
!81 = !DISubrange(count: 70)
!82 = !DIGlobalVariableExpression(var: !83, expr: !DIExpression())
!83 = distinct !DIGlobalVariable(scope: null, file: !2, line: 146, type: !84, isLocal: true, isDefinition: true)
!84 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 16, elements: !85)
!85 = !{!86}
!86 = !DISubrange(count: 2)
!87 = !DIGlobalVariableExpression(var: !88, expr: !DIExpression())
!88 = distinct !DIGlobalVariable(scope: null, file: !2, line: 147, type: !74, isLocal: true, isDefinition: true)
!89 = !DIGlobalVariableExpression(var: !90, expr: !DIExpression())
!90 = distinct !DIGlobalVariable(scope: null, file: !2, line: 147, type: !35, isLocal: true, isDefinition: true)
!91 = !DIGlobalVariableExpression(var: !92, expr: !DIExpression())
!92 = distinct !DIGlobalVariable(scope: null, file: !2, line: 150, type: !93, isLocal: true, isDefinition: true)
!93 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 392, elements: !94)
!94 = !{!95}
!95 = !DISubrange(count: 49)
!96 = !DIGlobalVariableExpression(var: !97, expr: !DIExpression())
!97 = distinct !DIGlobalVariable(scope: null, file: !2, line: 153, type: !98, isLocal: true, isDefinition: true)
!98 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !99)
!99 = !{!100}
!100 = !DISubrange(count: 5)
!101 = !DIGlobalVariableExpression(var: !102, expr: !DIExpression())
!102 = distinct !DIGlobalVariable(scope: null, file: !2, line: 155, type: !103, isLocal: true, isDefinition: true)
!103 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 264, elements: !104)
!104 = !{!105}
!105 = !DISubrange(count: 33)
!106 = !DIGlobalVariableExpression(var: !107, expr: !DIExpression())
!107 = distinct !DIGlobalVariable(scope: null, file: !2, line: 155, type: !52, isLocal: true, isDefinition: true)
!108 = distinct !DICompileUnit(language: DW_LANG_C11, file: !2, producer: "clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, retainedTypes: !109, globals: !120, splitDebugInlining: false, nameTableKind: None)
!109 = !{!110, !112, !113, !115}
!110 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !111, size: 64)
!111 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!112 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!113 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !114, size: 64)
!114 = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
!115 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint64_t", file: !116, line: 27, baseType: !117)
!116 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/stdint-uintn.h", directory: "", checksumkind: CSK_MD5, checksum: "2bf2ae53c58c01b1a1b9383b5195125c")
!117 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint64_t", file: !118, line: 45, baseType: !119)
!118 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types.h", directory: "", checksumkind: CSK_MD5, checksum: "d108b5f93a74c50510d7d9bc0ab36df9")
!119 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!120 = !{!0, !7, !12, !17, !22, !28, !33, !38, !40, !45, !50, !55, !60, !65, !67, !72, !77, !82, !87, !89, !91, !96, !101, !106}
!121 = !{i32 7, !"Dwarf Version", i32 5}
!122 = !{i32 2, !"Debug Info Version", i32 3}
!123 = !{i32 1, !"wchar_size", i32 4}
!124 = !{i32 8, !"PIC Level", i32 2}
!125 = !{i32 7, !"PIE Level", i32 2}
!126 = !{i32 7, !"uwtable", i32 2}
!127 = !{!"clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)"}
!128 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 58, type: !129, scopeLine: 59, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !108, retainedNodes: !132)
!129 = !DISubroutineType(types: !130)
!130 = !{!131, !131, !110}
!131 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!132 = !{}
!133 = !DILocalVariable(name: "argc", arg: 1, scope: !128, file: !2, line: 58, type: !131)
!134 = !DILocation(line: 58, column: 10, scope: !128)
!135 = !DILocalVariable(name: "argv", arg: 2, scope: !128, file: !2, line: 58, type: !110)
!136 = !DILocation(line: 58, column: 23, scope: !128)
!137 = !DILocation(line: 60, column: 7, scope: !138)
!138 = distinct !DILexicalBlock(scope: !128, file: !2, line: 60, column: 7)
!139 = !DILocation(line: 60, column: 12, scope: !138)
!140 = !DILocation(line: 62, column: 74, scope: !141)
!141 = distinct !DILexicalBlock(scope: !138, file: !2, line: 60, column: 29)
!142 = !DILocation(line: 61, column: 5, scope: !141)
!143 = !DILocation(line: 63, column: 3, scope: !141)
!144 = !DILocalVariable(name: "sodium_init_success", scope: !128, file: !2, line: 66, type: !145)
!145 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !131)
!146 = !DILocation(line: 66, column: 13, scope: !128)
!147 = !DILocalVariable(name: "sodium_already_initd", scope: !128, file: !2, line: 67, type: !145)
!148 = !DILocation(line: 67, column: 13, scope: !128)
!149 = !DILocalVariable(name: "sodium_init_result", scope: !128, file: !2, line: 68, type: !131)
!150 = !DILocation(line: 68, column: 7, scope: !128)
!151 = !DILocation(line: 68, column: 28, scope: !128)
!152 = !DILocation(line: 69, column: 34, scope: !128)
!153 = !DILocation(line: 69, column: 31, scope: !128)
!154 = !DILocation(line: 69, column: 53, scope: !128)
!155 = !DILocation(line: 70, column: 28, scope: !128)
!156 = !DILocation(line: 70, column: 25, scope: !128)
!157 = !DILocation(line: 70, column: 48, scope: !128)
!158 = !DILocation(line: 69, column: 3, scope: !128)
!159 = !DILocalVariable(name: "num_iter", scope: !128, file: !2, line: 74, type: !131)
!160 = !DILocation(line: 74, column: 7, scope: !128)
!161 = !DILocation(line: 74, column: 34, scope: !128)
!162 = !DILocation(line: 74, column: 18, scope: !128)
!163 = !DILocalVariable(name: "num_warmup", scope: !128, file: !2, line: 78, type: !131)
!164 = !DILocation(line: 78, column: 7, scope: !128)
!165 = !DILocation(line: 78, column: 36, scope: !128)
!166 = !DILocation(line: 78, column: 20, scope: !128)
!167 = !DILocalVariable(name: "msg", scope: !128, file: !2, line: 82, type: !113)
!168 = !DILocation(line: 82, column: 18, scope: !128)
!169 = !DILocation(line: 82, column: 40, scope: !128)
!170 = !DILocalVariable(name: "msg_sz", scope: !128, file: !2, line: 83, type: !171)
!171 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!172 = !DILocation(line: 83, column: 22, scope: !128)
!173 = !DILocation(line: 83, column: 38, scope: !128)
!174 = !DILocation(line: 83, column: 31, scope: !128)
!175 = !DILocalVariable(name: "opened_msg", scope: !128, file: !2, line: 86, type: !113)
!176 = !DILocation(line: 86, column: 18, scope: !128)
!177 = !DILocation(line: 86, column: 38, scope: !128)
!178 = !DILocation(line: 86, column: 31, scope: !128)
!179 = !DILocation(line: 87, column: 10, scope: !128)
!180 = !DILocation(line: 87, column: 21, scope: !128)
!181 = !DILocation(line: 87, column: 3, scope: !128)
!182 = !DILocalVariable(name: "signed_msg_sz", scope: !128, file: !2, line: 90, type: !171)
!183 = !DILocation(line: 90, column: 22, scope: !128)
!184 = !DILocation(line: 90, column: 38, scope: !128)
!185 = !DILocation(line: 90, column: 47, scope: !128)
!186 = !DILocation(line: 90, column: 45, scope: !128)
!187 = !DILocalVariable(name: "signed_msg", scope: !128, file: !2, line: 91, type: !113)
!188 = !DILocation(line: 91, column: 18, scope: !128)
!189 = !DILocation(line: 91, column: 38, scope: !128)
!190 = !DILocation(line: 91, column: 31, scope: !128)
!191 = !DILocation(line: 92, column: 10, scope: !128)
!192 = !DILocation(line: 92, column: 14, scope: !128)
!193 = !DILocation(line: 92, column: 3, scope: !128)
!194 = !DILocalVariable(name: "privk", scope: !128, file: !2, line: 95, type: !113)
!195 = !DILocation(line: 95, column: 18, scope: !128)
!196 = !DILocation(line: 95, column: 33, scope: !128)
!197 = !DILocation(line: 95, column: 26, scope: !128)
!198 = !DILocalVariable(name: "pubk", scope: !128, file: !2, line: 96, type: !113)
!199 = !DILocation(line: 96, column: 18, scope: !128)
!200 = !DILocation(line: 96, column: 32, scope: !128)
!201 = !DILocation(line: 96, column: 25, scope: !128)
!202 = !DILocation(line: 97, column: 10, scope: !128)
!203 = !DILocation(line: 97, column: 16, scope: !128)
!204 = !DILocation(line: 97, column: 3, scope: !128)
!205 = !DILocation(line: 98, column: 10, scope: !128)
!206 = !DILocation(line: 98, column: 15, scope: !128)
!207 = !DILocation(line: 98, column: 3, scope: !128)
!208 = !DILocalVariable(name: "times", scope: !128, file: !2, line: 101, type: !209)
!209 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !115, size: 64)
!210 = !DILocation(line: 101, column: 13, scope: !128)
!211 = !DILocation(line: 101, column: 28, scope: !128)
!212 = !DILocation(line: 101, column: 21, scope: !128)
!213 = !DILocation(line: 102, column: 10, scope: !128)
!214 = !DILocation(line: 102, column: 16, scope: !128)
!215 = !DILocation(line: 102, column: 3, scope: !128)
!216 = !DILocalVariable(name: "start_time", scope: !128, file: !2, line: 105, type: !217)
!217 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !115)
!218 = !DILocation(line: 105, column: 21, scope: !128)
!219 = !DILocalVariable(name: "end_time", scope: !128, file: !2, line: 106, type: !217)
!220 = !DILocation(line: 106, column: 21, scope: !128)
!221 = !DILocalVariable(name: "cur_iter", scope: !222, file: !2, line: 109, type: !131)
!222 = distinct !DILexicalBlock(scope: !128, file: !2, line: 109, column: 3)
!223 = !DILocation(line: 109, column: 12, scope: !222)
!224 = !DILocation(line: 109, column: 8, scope: !222)
!225 = !DILocation(line: 109, column: 26, scope: !226)
!226 = distinct !DILexicalBlock(scope: !222, file: !2, line: 109, column: 3)
!227 = !DILocation(line: 109, column: 37, scope: !226)
!228 = !DILocation(line: 109, column: 48, scope: !226)
!229 = !DILocation(line: 109, column: 46, scope: !226)
!230 = !DILocation(line: 109, column: 35, scope: !226)
!231 = !DILocation(line: 109, column: 3, scope: !222)
!232 = !DILocalVariable(name: "_eval_unused", scope: !233, file: !2, line: 112, type: !131)
!233 = distinct !DILexicalBlock(scope: !226, file: !2, line: 109, column: 72)
!234 = !DILocation(line: 112, column: 9, scope: !233)
!235 = !DILocation(line: 112, column: 56, scope: !233)
!236 = !DILocation(line: 112, column: 74, scope: !233)
!237 = !DILocation(line: 112, column: 24, scope: !233)
!238 = !DILocalVariable(name: "_eval_cycles_low", scope: !239, file: !2, line: 115, type: !240)
!239 = distinct !DILexicalBlock(scope: !233, file: !2, line: 115, column: 18)
!240 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !116, line: 26, baseType: !241)
!241 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !118, line: 42, baseType: !242)
!242 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!243 = !DILocation(line: 115, column: 18, scope: !239)
!244 = !DILocalVariable(name: "_eval_cycles_high", scope: !239, file: !2, line: 115, type: !240)
!245 = !{i64 2147803302, i64 2147803310, i64 2147803335, i64 2147803380, i64 2147803421}
!246 = !DILocation(line: 115, column: 16, scope: !233)
!247 = !DILocalVariable(name: "sign_result", scope: !233, file: !2, line: 118, type: !131)
!248 = !DILocation(line: 118, column: 9, scope: !233)
!249 = !DILocation(line: 118, column: 55, scope: !233)
!250 = !DILocation(line: 120, column: 20, scope: !233)
!251 = !DILocation(line: 121, column: 19, scope: !233)
!252 = !DILocation(line: 122, column: 23, scope: !233)
!253 = !DILocation(line: 118, column: 23, scope: !233)
!254 = !DILocalVariable(name: "_eval_cycles_low", scope: !255, file: !2, line: 125, type: !240)
!255 = distinct !DILexicalBlock(scope: !233, file: !2, line: 125, column: 16)
!256 = !DILocation(line: 125, column: 16, scope: !255)
!257 = !DILocalVariable(name: "_eval_cycles_high", scope: !255, file: !2, line: 125, type: !240)
!258 = !{i64 2147803800, i64 2147803809, i64 2147803909, i64 2147803989, i64 2147804051}
!259 = !DILocation(line: 125, column: 14, scope: !233)
!260 = !DILocation(line: 127, column: 15, scope: !261)
!261 = distinct !DILexicalBlock(scope: !233, file: !2, line: 127, column: 9)
!262 = !DILocation(line: 127, column: 12, scope: !261)
!263 = !DILocation(line: 128, column: 7, scope: !264)
!264 = distinct !DILexicalBlock(scope: !261, file: !2, line: 127, column: 28)
!265 = !DILocation(line: 129, column: 7, scope: !264)
!266 = !DILocation(line: 132, column: 9, scope: !267)
!267 = distinct !DILexicalBlock(scope: !233, file: !2, line: 132, column: 9)
!268 = !DILocation(line: 132, column: 21, scope: !267)
!269 = !DILocation(line: 132, column: 18, scope: !267)
!270 = !DILocation(line: 133, column: 38, scope: !271)
!271 = distinct !DILexicalBlock(scope: !267, file: !2, line: 132, column: 33)
!272 = !DILocation(line: 133, column: 49, scope: !271)
!273 = !DILocation(line: 133, column: 47, scope: !271)
!274 = !DILocation(line: 133, column: 7, scope: !271)
!275 = !DILocation(line: 133, column: 13, scope: !271)
!276 = !DILocation(line: 133, column: 24, scope: !271)
!277 = !DILocation(line: 133, column: 22, scope: !271)
!278 = !DILocation(line: 133, column: 36, scope: !271)
!279 = !DILocation(line: 134, column: 5, scope: !271)
!280 = !DILocalVariable(name: "open_result", scope: !233, file: !2, line: 137, type: !131)
!281 = !DILocation(line: 137, column: 9, scope: !233)
!282 = !DILocation(line: 137, column: 40, scope: !233)
!283 = !DILocation(line: 138, column: 12, scope: !233)
!284 = !DILocation(line: 138, column: 24, scope: !233)
!285 = !DILocation(line: 138, column: 39, scope: !233)
!286 = !DILocation(line: 137, column: 23, scope: !233)
!287 = !DILocation(line: 139, column: 14, scope: !288)
!288 = distinct !DILexicalBlock(scope: !233, file: !2, line: 139, column: 9)
!289 = !DILocation(line: 139, column: 11, scope: !288)
!290 = !DILocation(line: 140, column: 7, scope: !291)
!291 = distinct !DILexicalBlock(scope: !288, file: !2, line: 139, column: 27)
!292 = !DILocation(line: 141, column: 7, scope: !291)
!293 = !DILocation(line: 143, column: 3, scope: !233)
!294 = !DILocation(line: 109, column: 60, scope: !226)
!295 = !DILocation(line: 109, column: 3, scope: !226)
!296 = distinct !{!296, !231, !297, !298}
!297 = !DILocation(line: 143, column: 3, scope: !222)
!298 = !{!"llvm.loop.mustprogress"}
!299 = !DILocalVariable(name: "ccounts_out", scope: !128, file: !2, line: 146, type: !300)
!300 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !301, size: 64)
!301 = !DIDerivedType(tag: DW_TAG_typedef, name: "FILE", file: !302, line: 7, baseType: !303)
!302 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "571f9fb6223c42439075fdde11a0de5d")
!303 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_FILE", file: !304, line: 49, size: 1728, elements: !305)
!304 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "1bad07471b7974df4ecc1d1c2ca207e6")
!305 = !{!306, !307, !308, !309, !310, !311, !312, !313, !314, !315, !316, !317, !318, !321, !323, !324, !325, !328, !330, !332, !336, !339, !341, !344, !347, !348, !349, !352, !353}
!306 = !DIDerivedType(tag: DW_TAG_member, name: "_flags", scope: !303, file: !304, line: 51, baseType: !131, size: 32)
!307 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_ptr", scope: !303, file: !304, line: 54, baseType: !111, size: 64, offset: 64)
!308 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_end", scope: !303, file: !304, line: 55, baseType: !111, size: 64, offset: 128)
!309 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_base", scope: !303, file: !304, line: 56, baseType: !111, size: 64, offset: 192)
!310 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_base", scope: !303, file: !304, line: 57, baseType: !111, size: 64, offset: 256)
!311 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_ptr", scope: !303, file: !304, line: 58, baseType: !111, size: 64, offset: 320)
!312 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_end", scope: !303, file: !304, line: 59, baseType: !111, size: 64, offset: 384)
!313 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_base", scope: !303, file: !304, line: 60, baseType: !111, size: 64, offset: 448)
!314 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_end", scope: !303, file: !304, line: 61, baseType: !111, size: 64, offset: 512)
!315 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_base", scope: !303, file: !304, line: 64, baseType: !111, size: 64, offset: 576)
!316 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_backup_base", scope: !303, file: !304, line: 65, baseType: !111, size: 64, offset: 640)
!317 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_end", scope: !303, file: !304, line: 66, baseType: !111, size: 64, offset: 704)
!318 = !DIDerivedType(tag: DW_TAG_member, name: "_markers", scope: !303, file: !304, line: 68, baseType: !319, size: 64, offset: 768)
!319 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !320, size: 64)
!320 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_marker", file: !304, line: 36, flags: DIFlagFwdDecl)
!321 = !DIDerivedType(tag: DW_TAG_member, name: "_chain", scope: !303, file: !304, line: 70, baseType: !322, size: 64, offset: 832)
!322 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !303, size: 64)
!323 = !DIDerivedType(tag: DW_TAG_member, name: "_fileno", scope: !303, file: !304, line: 72, baseType: !131, size: 32, offset: 896)
!324 = !DIDerivedType(tag: DW_TAG_member, name: "_flags2", scope: !303, file: !304, line: 73, baseType: !131, size: 32, offset: 928)
!325 = !DIDerivedType(tag: DW_TAG_member, name: "_old_offset", scope: !303, file: !304, line: 74, baseType: !326, size: 64, offset: 960)
!326 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off_t", file: !118, line: 152, baseType: !327)
!327 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!328 = !DIDerivedType(tag: DW_TAG_member, name: "_cur_column", scope: !303, file: !304, line: 77, baseType: !329, size: 16, offset: 1024)
!329 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!330 = !DIDerivedType(tag: DW_TAG_member, name: "_vtable_offset", scope: !303, file: !304, line: 78, baseType: !331, size: 8, offset: 1040)
!331 = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
!332 = !DIDerivedType(tag: DW_TAG_member, name: "_shortbuf", scope: !303, file: !304, line: 79, baseType: !333, size: 8, offset: 1048)
!333 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 8, elements: !334)
!334 = !{!335}
!335 = !DISubrange(count: 1)
!336 = !DIDerivedType(tag: DW_TAG_member, name: "_lock", scope: !303, file: !304, line: 81, baseType: !337, size: 64, offset: 1088)
!337 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !338, size: 64)
!338 = !DIDerivedType(tag: DW_TAG_typedef, name: "_IO_lock_t", file: !304, line: 43, baseType: null)
!339 = !DIDerivedType(tag: DW_TAG_member, name: "_offset", scope: !303, file: !304, line: 89, baseType: !340, size: 64, offset: 1152)
!340 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off64_t", file: !118, line: 153, baseType: !327)
!341 = !DIDerivedType(tag: DW_TAG_member, name: "_codecvt", scope: !303, file: !304, line: 91, baseType: !342, size: 64, offset: 1216)
!342 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !343, size: 64)
!343 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_codecvt", file: !304, line: 37, flags: DIFlagFwdDecl)
!344 = !DIDerivedType(tag: DW_TAG_member, name: "_wide_data", scope: !303, file: !304, line: 92, baseType: !345, size: 64, offset: 1280)
!345 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !346, size: 64)
!346 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_wide_data", file: !304, line: 38, flags: DIFlagFwdDecl)
!347 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_list", scope: !303, file: !304, line: 93, baseType: !322, size: 64, offset: 1344)
!348 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_buf", scope: !303, file: !304, line: 94, baseType: !112, size: 64, offset: 1408)
!349 = !DIDerivedType(tag: DW_TAG_member, name: "__pad5", scope: !303, file: !304, line: 95, baseType: !350, size: 64, offset: 1472)
!350 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !351, line: 18, baseType: !119)
!351 = !DIFile(filename: "build/lib/clang/22/include/__stddef_size_t.h", directory: "/home/rgangar/Documents/llvm-data-independent-timing", checksumkind: CSK_MD5, checksum: "2c44e821a2b1951cde2eb0fb2e656867")
!352 = !DIDerivedType(tag: DW_TAG_member, name: "_mode", scope: !303, file: !304, line: 96, baseType: !131, size: 32, offset: 1536)
!353 = !DIDerivedType(tag: DW_TAG_member, name: "_unused2", scope: !303, file: !304, line: 98, baseType: !354, size: 160, offset: 1568)
!354 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !355)
!355 = !{!356}
!356 = !DISubrange(count: 20)
!357 = !DILocation(line: 146, column: 9, scope: !128)
!358 = !DILocation(line: 146, column: 29, scope: !128)
!359 = !DILocation(line: 146, column: 23, scope: !128)
!360 = !DILocation(line: 147, column: 10, scope: !128)
!361 = !DILocation(line: 147, column: 22, scope: !128)
!362 = !DILocation(line: 147, column: 30, scope: !128)
!363 = !DILocation(line: 147, column: 3, scope: !128)
!364 = !DILocation(line: 149, column: 11, scope: !128)
!365 = !DILocation(line: 151, column: 4, scope: !128)
!366 = !DILocation(line: 151, column: 14, scope: !128)
!367 = !DILocation(line: 149, column: 3, scope: !128)
!368 = !DILocalVariable(name: "ii", scope: !369, file: !2, line: 152, type: !131)
!369 = distinct !DILexicalBlock(scope: !128, file: !2, line: 152, column: 3)
!370 = !DILocation(line: 152, column: 12, scope: !369)
!371 = !DILocation(line: 152, column: 8, scope: !369)
!372 = !DILocation(line: 152, column: 20, scope: !373)
!373 = distinct !DILexicalBlock(scope: !369, file: !2, line: 152, column: 3)
!374 = !DILocation(line: 152, column: 25, scope: !373)
!375 = !DILocation(line: 152, column: 23, scope: !373)
!376 = !DILocation(line: 152, column: 3, scope: !369)
!377 = !DILocation(line: 153, column: 12, scope: !378)
!378 = distinct !DILexicalBlock(scope: !373, file: !2, line: 152, column: 41)
!379 = !DILocation(line: 153, column: 42, scope: !378)
!380 = !DILocation(line: 153, column: 48, scope: !378)
!381 = !DILocation(line: 153, column: 4, scope: !378)
!382 = !DILocation(line: 154, column: 3, scope: !378)
!383 = !DILocation(line: 152, column: 35, scope: !373)
!384 = !DILocation(line: 152, column: 3, scope: !373)
!385 = distinct !{!385, !376, !386, !298}
!386 = !DILocation(line: 154, column: 3, scope: !369)
!387 = !DILocation(line: 155, column: 17, scope: !128)
!388 = !DILocation(line: 155, column: 10, scope: !128)
!389 = !DILocation(line: 155, column: 30, scope: !128)
!390 = !DILocation(line: 155, column: 37, scope: !128)
!391 = !DILocation(line: 155, column: 3, scope: !128)
!392 = !DILocation(line: 164, column: 3, scope: !128)
