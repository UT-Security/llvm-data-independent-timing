; ModuleID = 'eval_argon2id.c'
source_filename = "eval_argon2id.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [154 x i8] c"Usage: %s <num_benchmark_iterations> <num_warmup_iterations> <password> <size_of_output> <file_to_write_cycle_counts_to> [<file_to_write_hit_counts_to>]\0A\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [30 x i8] c"Error initializing lib sodium\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [125 x i8] c"(sodium_init_success == sodium_init_result || sodium_already_initd == sodium_init_result) && \22Error initializing lib sodium\22\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [16 x i8] c"eval_argon2id.c\00", align 1, !dbg !17
@__PRETTY_FUNCTION__.main = private unnamed_addr constant [23 x i8] c"int main(int, char **)\00", align 1, !dbg !22
@.str.4 = private unnamed_addr constant [50 x i8] c"Password size is less than min in eval_argon2id.c\00", align 1, !dbg !28
@.str.5 = private unnamed_addr constant [95 x i8] c"passwd_sz >= crypto_pwhash_passwd_min() && \22Password size is less than min in eval_argon2id.c\22\00", align 1, !dbg !33
@.str.6 = private unnamed_addr constant [53 x i8] c"Password size is greater than max in eval_argon2id.c\00", align 1, !dbg !38
@.str.7 = private unnamed_addr constant [98 x i8] c"passwd_sz <= crypto_pwhash_passwd_max() && \22Password size is greater than max in eval_argon2id.c\22\00", align 1, !dbg !43
@.str.8 = private unnamed_addr constant [48 x i8] c"Output size is less than min in eval_argon2id.c\00", align 1, !dbg !48
@.str.9 = private unnamed_addr constant [89 x i8] c"out_sz >= crypto_pwhash_bytes_min() && \22Output size is less than min in eval_argon2id.c\22\00", align 1, !dbg !53
@.str.10 = private unnamed_addr constant [51 x i8] c"Output size is greater than max in eval_argon2id.c\00", align 1, !dbg !58
@.str.11 = private unnamed_addr constant [92 x i8] c"out_sz <= crypto_pwhash_bytes_max() && \22Output size is greater than max in eval_argon2id.c\22\00", align 1, !dbg !63
@.str.12 = private unnamed_addr constant [50 x i8] c"Couldn't allocate output bytes in eval_argon2id.c\00", align 1, !dbg !68
@.str.13 = private unnamed_addr constant [59 x i8] c"out && \22Couldn't allocate output bytes in eval_argon2id.c\22\00", align 1, !dbg !70
@.str.14 = private unnamed_addr constant [48 x i8] c"Couldn't allocate salt bytes in eval_argon2id.c\00", align 1, !dbg !75
@.str.15 = private unnamed_addr constant [58 x i8] c"salt && \22Couldn't allocate salt bytes in eval_argon2id.c\22\00", align 1, !dbg !77
@.str.16 = private unnamed_addr constant [63 x i8] c"Couldn't allocate array for benchmark times in eval_argon2id.c\00", align 1, !dbg !82
@.str.17 = private unnamed_addr constant [74 x i8] c"times && \22Couldn't allocate array for benchmark times in eval_argon2id.c\22\00", align 1, !dbg !87
@.str.18 = private unnamed_addr constant [20 x i8] c"-1 != pwhash_result\00", align 1, !dbg !92
@.str.19 = private unnamed_addr constant [2 x i8] c"w\00", align 1, !dbg !97
@.str.20 = private unnamed_addr constant [44 x i8] c"Couldn't open cycle counts file for writing\00", align 1, !dbg !102
@.str.21 = private unnamed_addr constant [69 x i8] c"ccounts_out != NULL && \22Couldn't open cycle counts file for writing\22\00", align 1, !dbg !107
@.str.22 = private unnamed_addr constant [50 x i8] c"argon2id cycle counts (%d iterations, %d warmup)\0A\00", align 1, !dbg !112
@.str.23 = private unnamed_addr constant [5 x i8] c"%lu\0A\00", align 1, !dbg !114
@.str.24 = private unnamed_addr constant [33 x i8] c"Couldn't close cycle counts file\00", align 1, !dbg !119
@.str.25 = private unnamed_addr constant [65 x i8] c"fclose(ccounts_out) != EOF && \22Couldn't close cycle counts file\22\00", align 1, !dbg !124

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %argc, ptr noundef %argv) #0 !dbg !150 {
entry:
  %retval = alloca i32, align 4
  %argc.addr = alloca i32, align 4
  %argv.addr = alloca ptr, align 8
  %sodium_init_success = alloca i32, align 4
  %sodium_already_initd = alloca i32, align 4
  %sodium_init_result = alloca i32, align 4
  %num_iter = alloca i32, align 4
  %num_warmup = alloca i32, align 4
  %passwd = alloca ptr, align 8
  %passwd_sz = alloca i64, align 8
  %out_sz = alloca i64, align 8
  %out = alloca ptr, align 8
  %salt = alloca ptr, align 8
  %opslimit = alloca i64, align 8
  %memlimit = alloca i64, align 8
  %alg = alloca i32, align 4
  %times = alloca ptr, align 8
  %start_time = alloca i64, align 8
  %end_time = alloca i64, align 8
  %cur_iter = alloca i32, align 4
  %_eval_cycles_low = alloca i32, align 4
  %_eval_cycles_high = alloca i32, align 4
  %tmp = alloca i64, align 8
  %pwhash_result = alloca i32, align 4
  %_eval_cycles_low70 = alloca i32, align 4
  %_eval_cycles_high71 = alloca i32, align 4
  %tmp74 = alloca i64, align 8
  %ccounts_out = alloca ptr, align 8
  %ii = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 %argc, ptr %argc.addr, align 4
    #dbg_declare(ptr %argc.addr, !155, !DIExpression(), !156)
  store ptr %argv, ptr %argv.addr, align 8
    #dbg_declare(ptr %argv.addr, !157, !DIExpression(), !158)
  %0 = load i32, ptr %argc.addr, align 4, !dbg !159
  %cmp = icmp slt i32 %0, 6, !dbg !161
  br i1 %cmp, label %if.then, label %if.end, !dbg !161

if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %argv.addr, align 8, !dbg !162
  %arrayidx = getelementptr inbounds ptr, ptr %1, i64 0, !dbg !162
  %2 = load ptr, ptr %arrayidx, align 8, !dbg !162
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str, ptr noundef %2), !dbg !164
  call void @exit(i32 noundef -1) #7, !dbg !165
  unreachable, !dbg !165

if.end:                                           ; preds = %entry
    #dbg_declare(ptr %sodium_init_success, !166, !DIExpression(), !168)
  store i32 0, ptr %sodium_init_success, align 4, !dbg !168
    #dbg_declare(ptr %sodium_already_initd, !169, !DIExpression(), !170)
  store i32 1, ptr %sodium_already_initd, align 4, !dbg !170
    #dbg_declare(ptr %sodium_init_result, !171, !DIExpression(), !172)
  %call1 = call i32 @sodium_init(), !dbg !173
  store i32 %call1, ptr %sodium_init_result, align 4, !dbg !172
  %3 = load i32, ptr %sodium_init_result, align 4, !dbg !174
  %cmp2 = icmp eq i32 0, %3, !dbg !175
  br i1 %cmp2, label %land.lhs.true, label %lor.lhs.false, !dbg !176

lor.lhs.false:                                    ; preds = %if.end
  %4 = load i32, ptr %sodium_init_result, align 4, !dbg !177
  %cmp3 = icmp eq i32 1, %4, !dbg !178
  br i1 %cmp3, label %land.lhs.true, label %cond.false, !dbg !179

land.lhs.true:                                    ; preds = %lor.lhs.false, %if.end
  br i1 true, label %cond.true, label %cond.false, !dbg !180

cond.true:                                        ; preds = %land.lhs.true
  br label %cond.end, !dbg !180

cond.false:                                       ; preds = %land.lhs.true, %lor.lhs.false
  call void @__assert_fail(ptr noundef @.str.2, ptr noundef @.str.3, i32 noundef 79, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !180
  unreachable, !dbg !180

5:                                                ; No predecessors!
  br label %cond.end, !dbg !180

cond.end:                                         ; preds = %5, %cond.true
    #dbg_declare(ptr %num_iter, !181, !DIExpression(), !182)
  %6 = load ptr, ptr %argv.addr, align 8, !dbg !183
  %arrayidx4 = getelementptr inbounds ptr, ptr %6, i64 1, !dbg !183
  %7 = load ptr, ptr %arrayidx4, align 8, !dbg !183
  %call5 = call i64 @strtol(ptr noundef %7, ptr noundef null, i32 noundef 10) #8, !dbg !184
  %conv = trunc i64 %call5 to i32, !dbg !184
  store i32 %conv, ptr %num_iter, align 4, !dbg !182
    #dbg_declare(ptr %num_warmup, !185, !DIExpression(), !186)
  %8 = load ptr, ptr %argv.addr, align 8, !dbg !187
  %arrayidx6 = getelementptr inbounds ptr, ptr %8, i64 2, !dbg !187
  %9 = load ptr, ptr %arrayidx6, align 8, !dbg !187
  %call7 = call i64 @strtol(ptr noundef %9, ptr noundef null, i32 noundef 10) #8, !dbg !188
  %conv8 = trunc i64 %call7 to i32, !dbg !188
  store i32 %conv8, ptr %num_warmup, align 4, !dbg !186
    #dbg_declare(ptr %passwd, !189, !DIExpression(), !190)
  %10 = load ptr, ptr %argv.addr, align 8, !dbg !191
  %arrayidx9 = getelementptr inbounds ptr, ptr %10, i64 3, !dbg !191
  %11 = load ptr, ptr %arrayidx9, align 8, !dbg !191
  store ptr %11, ptr %passwd, align 8, !dbg !190
    #dbg_declare(ptr %passwd_sz, !192, !DIExpression(), !194)
  %12 = load ptr, ptr %argv.addr, align 8, !dbg !195
  %arrayidx10 = getelementptr inbounds ptr, ptr %12, i64 3, !dbg !195
  %13 = load ptr, ptr %arrayidx10, align 8, !dbg !195
  %call11 = call i64 @strlen(ptr noundef %13) #9, !dbg !196
  store i64 %call11, ptr %passwd_sz, align 8, !dbg !194
  %14 = load i64, ptr %passwd_sz, align 8, !dbg !197
  %call12 = call i64 @crypto_pwhash_passwd_min(), !dbg !198
  %cmp13 = icmp uge i64 %14, %call12, !dbg !199
  br i1 %cmp13, label %land.lhs.true15, label %cond.false17, !dbg !200

land.lhs.true15:                                  ; preds = %cond.end
  br i1 true, label %cond.true16, label %cond.false17, !dbg !201

cond.true16:                                      ; preds = %land.lhs.true15
  br label %cond.end18, !dbg !201

cond.false17:                                     ; preds = %land.lhs.true15, %cond.end
  call void @__assert_fail(ptr noundef @.str.5, ptr noundef @.str.3, i32 noundef 94, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !201
  unreachable, !dbg !201

15:                                               ; No predecessors!
  br label %cond.end18, !dbg !201

cond.end18:                                       ; preds = %15, %cond.true16
  %16 = load i64, ptr %passwd_sz, align 8, !dbg !202
  %call19 = call i64 @crypto_pwhash_passwd_max(), !dbg !203
  %cmp20 = icmp ule i64 %16, %call19, !dbg !204
  br i1 %cmp20, label %land.lhs.true22, label %cond.false24, !dbg !205

land.lhs.true22:                                  ; preds = %cond.end18
  br i1 true, label %cond.true23, label %cond.false24, !dbg !206

cond.true23:                                      ; preds = %land.lhs.true22
  br label %cond.end25, !dbg !206

cond.false24:                                     ; preds = %land.lhs.true22, %cond.end18
  call void @__assert_fail(ptr noundef @.str.7, ptr noundef @.str.3, i32 noundef 96, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !206
  unreachable, !dbg !206

17:                                               ; No predecessors!
  br label %cond.end25, !dbg !206

cond.end25:                                       ; preds = %17, %cond.true23
    #dbg_declare(ptr %out_sz, !207, !DIExpression(), !208)
  %18 = load ptr, ptr %argv.addr, align 8, !dbg !209
  %arrayidx26 = getelementptr inbounds ptr, ptr %18, i64 4, !dbg !209
  %19 = load ptr, ptr %arrayidx26, align 8, !dbg !209
  %call27 = call i64 @strtol(ptr noundef %19, ptr noundef null, i32 noundef 10) #8, !dbg !210
  store i64 %call27, ptr %out_sz, align 8, !dbg !208
  %20 = load i64, ptr %out_sz, align 8, !dbg !211
  %call28 = call i64 @crypto_pwhash_bytes_min(), !dbg !212
  %cmp29 = icmp uge i64 %20, %call28, !dbg !213
  br i1 %cmp29, label %land.lhs.true31, label %cond.false33, !dbg !214

land.lhs.true31:                                  ; preds = %cond.end25
  br i1 true, label %cond.true32, label %cond.false33, !dbg !215

cond.true32:                                      ; preds = %land.lhs.true31
  br label %cond.end34, !dbg !215

cond.false33:                                     ; preds = %land.lhs.true31, %cond.end25
  call void @__assert_fail(ptr noundef @.str.9, ptr noundef @.str.3, i32 noundef 101, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !215
  unreachable, !dbg !215

21:                                               ; No predecessors!
  br label %cond.end34, !dbg !215

cond.end34:                                       ; preds = %21, %cond.true32
  %22 = load i64, ptr %out_sz, align 8, !dbg !216
  %call35 = call i64 @crypto_pwhash_bytes_max(), !dbg !217
  %cmp36 = icmp ule i64 %22, %call35, !dbg !218
  br i1 %cmp36, label %land.lhs.true38, label %cond.false40, !dbg !219

land.lhs.true38:                                  ; preds = %cond.end34
  br i1 true, label %cond.true39, label %cond.false40, !dbg !220

cond.true39:                                      ; preds = %land.lhs.true38
  br label %cond.end41, !dbg !220

cond.false40:                                     ; preds = %land.lhs.true38, %cond.end34
  call void @__assert_fail(ptr noundef @.str.11, ptr noundef @.str.3, i32 noundef 103, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !220
  unreachable, !dbg !220

23:                                               ; No predecessors!
  br label %cond.end41, !dbg !220

cond.end41:                                       ; preds = %23, %cond.true39
    #dbg_declare(ptr %out, !221, !DIExpression(), !222)
  %24 = load i64, ptr %out_sz, align 8, !dbg !223
  %call42 = call noalias ptr @malloc(i64 noundef %24) #10, !dbg !224
  store ptr %call42, ptr %out, align 8, !dbg !222
  %25 = load ptr, ptr %out, align 8, !dbg !225
  %tobool = icmp ne ptr %25, null, !dbg !225
  br i1 %tobool, label %land.lhs.true43, label %cond.false45, !dbg !226

land.lhs.true43:                                  ; preds = %cond.end41
  br i1 true, label %cond.true44, label %cond.false45, !dbg !227

cond.true44:                                      ; preds = %land.lhs.true43
  br label %cond.end46, !dbg !227

cond.false45:                                     ; preds = %land.lhs.true43, %cond.end41
  call void @__assert_fail(ptr noundef @.str.13, ptr noundef @.str.3, i32 noundef 107, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !227
  unreachable, !dbg !227

26:                                               ; No predecessors!
  br label %cond.end46, !dbg !227

cond.end46:                                       ; preds = %26, %cond.true44
    #dbg_declare(ptr %salt, !228, !DIExpression(), !229)
  %call47 = call i64 @crypto_pwhash_saltbytes(), !dbg !230
  %call48 = call noalias ptr @malloc(i64 noundef %call47) #10, !dbg !231
  store ptr %call48, ptr %salt, align 8, !dbg !229
  %27 = load ptr, ptr %salt, align 8, !dbg !232
  %tobool49 = icmp ne ptr %27, null, !dbg !232
  br i1 %tobool49, label %land.lhs.true50, label %cond.false52, !dbg !233

land.lhs.true50:                                  ; preds = %cond.end46
  br i1 true, label %cond.true51, label %cond.false52, !dbg !234

cond.true51:                                      ; preds = %land.lhs.true50
  br label %cond.end53, !dbg !234

cond.false52:                                     ; preds = %land.lhs.true50, %cond.end46
  call void @__assert_fail(ptr noundef @.str.15, ptr noundef @.str.3, i32 noundef 111, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !234
  unreachable, !dbg !234

28:                                               ; No predecessors!
  br label %cond.end53, !dbg !234

cond.end53:                                       ; preds = %28, %cond.true51
    #dbg_declare(ptr %opslimit, !235, !DIExpression(), !236)
  %call54 = call i64 @crypto_pwhash_opslimit_interactive(), !dbg !237
  store i64 %call54, ptr %opslimit, align 8, !dbg !236
    #dbg_declare(ptr %memlimit, !238, !DIExpression(), !239)
  %call55 = call i64 @crypto_pwhash_memlimit_interactive(), !dbg !240
  store i64 %call55, ptr %memlimit, align 8, !dbg !239
    #dbg_declare(ptr %alg, !241, !DIExpression(), !242)
  %call56 = call i32 @crypto_pwhash_alg_argon2id13(), !dbg !243
  store i32 %call56, ptr %alg, align 4, !dbg !242
    #dbg_declare(ptr %times, !244, !DIExpression(), !246)
  %29 = load i32, ptr %num_iter, align 4, !dbg !247
  %conv57 = sext i32 %29 to i64, !dbg !247
  %call58 = call noalias ptr @calloc(i64 noundef %conv57, i64 noundef 8) #11, !dbg !248
  store ptr %call58, ptr %times, align 8, !dbg !246
  %30 = load ptr, ptr %times, align 8, !dbg !249
  %tobool59 = icmp ne ptr %30, null, !dbg !249
  br i1 %tobool59, label %land.lhs.true60, label %cond.false62, !dbg !250

land.lhs.true60:                                  ; preds = %cond.end53
  br i1 true, label %cond.true61, label %cond.false62, !dbg !251

cond.true61:                                      ; preds = %land.lhs.true60
  br label %cond.end63, !dbg !251

cond.false62:                                     ; preds = %land.lhs.true60, %cond.end53
  call void @__assert_fail(ptr noundef @.str.17, ptr noundef @.str.3, i32 noundef 125, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !251
  unreachable, !dbg !251

31:                                               ; No predecessors!
  br label %cond.end63, !dbg !251

cond.end63:                                       ; preds = %31, %cond.true61
    #dbg_declare(ptr %start_time, !252, !DIExpression(), !254)
  store volatile i64 0, ptr %start_time, align 8, !dbg !254
    #dbg_declare(ptr %end_time, !255, !DIExpression(), !256)
  store volatile i64 0, ptr %end_time, align 8, !dbg !256
    #dbg_declare(ptr %cur_iter, !257, !DIExpression(), !259)
  store i32 0, ptr %cur_iter, align 4, !dbg !259
  br label %for.cond, !dbg !260

for.cond:                                         ; preds = %for.inc, %cond.end63
  %32 = load i32, ptr %cur_iter, align 4, !dbg !261
  %33 = load i32, ptr %num_iter, align 4, !dbg !263
  %34 = load i32, ptr %num_warmup, align 4, !dbg !264
  %add = add nsw i32 %33, %34, !dbg !265
  %cmp64 = icmp slt i32 %32, %add, !dbg !266
  br i1 %cmp64, label %for.body, label %for.end, !dbg !267

for.body:                                         ; preds = %for.cond
  %35 = load ptr, ptr %salt, align 8, !dbg !268
  call void @randombytes_buf(ptr noundef %35, i64 noundef 8), !dbg !270
    #dbg_declare(ptr %_eval_cycles_low, !271, !DIExpression(), !276)
  store i32 0, ptr %_eval_cycles_low, align 4, !dbg !276
    #dbg_declare(ptr %_eval_cycles_high, !277, !DIExpression(), !276)
  store i32 0, ptr %_eval_cycles_high, align 4, !dbg !276
  %36 = call { i32, i32 } asm sideeffect "cpuid\0A\09rdtsc\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !276, !srcloc !278
  %asmresult = extractvalue { i32, i32 } %36, 0, !dbg !276
  %asmresult66 = extractvalue { i32, i32 } %36, 1, !dbg !276
  store i32 %asmresult, ptr %_eval_cycles_low, align 4, !dbg !276
  store i32 %asmresult66, ptr %_eval_cycles_high, align 4, !dbg !276
  %37 = load i32, ptr %_eval_cycles_high, align 4, !dbg !276
  %conv67 = zext i32 %37 to i64, !dbg !276
  %shl = shl i64 %conv67, 32, !dbg !276
  %38 = load i32, ptr %_eval_cycles_low, align 4, !dbg !276
  %conv68 = zext i32 %38 to i64, !dbg !276
  %or = or i64 %shl, %conv68, !dbg !276
  store i64 %or, ptr %tmp, align 8, !dbg !276
  %39 = load i64, ptr %tmp, align 8, !dbg !276
  store volatile i64 %39, ptr %start_time, align 8, !dbg !279
    #dbg_declare(ptr %pwhash_result, !280, !DIExpression(), !281)
  %40 = load ptr, ptr %out, align 8, !dbg !282
  %41 = load i64, ptr %out_sz, align 8, !dbg !283
  %42 = load ptr, ptr %passwd, align 8, !dbg !284
  %43 = load i64, ptr %passwd_sz, align 8, !dbg !285
  %44 = load ptr, ptr %salt, align 8, !dbg !286
  %45 = load i64, ptr %opslimit, align 8, !dbg !287
  %46 = load i64, ptr %memlimit, align 8, !dbg !288
  %47 = load i32, ptr %alg, align 4, !dbg !289
  %call69 = call i32 @crypto_pwhash(ptr noundef %40, i64 noundef %41, ptr noundef %42, i64 noundef %43, ptr noundef %44, i64 noundef %45, i64 noundef %46, i32 noundef %47), !dbg !290
  store i32 %call69, ptr %pwhash_result, align 4, !dbg !281
    #dbg_declare(ptr %_eval_cycles_low70, !291, !DIExpression(), !293)
  store i32 0, ptr %_eval_cycles_low70, align 4, !dbg !293
    #dbg_declare(ptr %_eval_cycles_high71, !294, !DIExpression(), !293)
  store i32 0, ptr %_eval_cycles_high71, align 4, !dbg !293
  %48 = call { i32, i32 } asm sideeffect "rdtscp\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09cpuid\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !293, !srcloc !295
  %asmresult72 = extractvalue { i32, i32 } %48, 0, !dbg !293
  %asmresult73 = extractvalue { i32, i32 } %48, 1, !dbg !293
  store i32 %asmresult72, ptr %_eval_cycles_low70, align 4, !dbg !293
  store i32 %asmresult73, ptr %_eval_cycles_high71, align 4, !dbg !293
  %49 = load i32, ptr %_eval_cycles_high71, align 4, !dbg !293
  %conv75 = zext i32 %49 to i64, !dbg !293
  %shl76 = shl i64 %conv75, 32, !dbg !293
  %50 = load i32, ptr %_eval_cycles_low70, align 4, !dbg !293
  %conv77 = zext i32 %50 to i64, !dbg !293
  %or78 = or i64 %shl76, %conv77, !dbg !293
  store i64 %or78, ptr %tmp74, align 8, !dbg !293
  %51 = load i64, ptr %tmp74, align 8, !dbg !293
  store volatile i64 %51, ptr %end_time, align 8, !dbg !296
  %52 = load i32, ptr %pwhash_result, align 4, !dbg !297
  %cmp79 = icmp ne i32 -1, %52, !dbg !298
  br i1 %cmp79, label %cond.true81, label %cond.false82, !dbg !299

cond.true81:                                      ; preds = %for.body
  br label %cond.end83, !dbg !299

cond.false82:                                     ; preds = %for.body
  call void @__assert_fail(ptr noundef @.str.18, ptr noundef @.str.3, i32 noundef 145, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !299
  unreachable, !dbg !299

53:                                               ; No predecessors!
  br label %cond.end83, !dbg !299

cond.end83:                                       ; preds = %53, %cond.true81
  %54 = load i32, ptr %cur_iter, align 4, !dbg !300
  %55 = load i32, ptr %num_warmup, align 4, !dbg !302
  %cmp84 = icmp sge i32 %54, %55, !dbg !303
  br i1 %cmp84, label %if.then86, label %if.end89, !dbg !303

if.then86:                                        ; preds = %cond.end83
  %56 = load volatile i64, ptr %end_time, align 8, !dbg !304
  %57 = load volatile i64, ptr %start_time, align 8, !dbg !306
  %sub = sub i64 %56, %57, !dbg !307
  %58 = load ptr, ptr %times, align 8, !dbg !308
  %59 = load i32, ptr %cur_iter, align 4, !dbg !309
  %60 = load i32, ptr %num_warmup, align 4, !dbg !310
  %sub87 = sub nsw i32 %59, %60, !dbg !311
  %idxprom = sext i32 %sub87 to i64, !dbg !308
  %arrayidx88 = getelementptr inbounds i64, ptr %58, i64 %idxprom, !dbg !308
  store i64 %sub, ptr %arrayidx88, align 8, !dbg !312
  br label %if.end89, !dbg !313

if.end89:                                         ; preds = %if.then86, %cond.end83
  br label %for.inc, !dbg !314

for.inc:                                          ; preds = %if.end89
  %61 = load i32, ptr %cur_iter, align 4, !dbg !315
  %inc = add nsw i32 %61, 1, !dbg !315
  store i32 %inc, ptr %cur_iter, align 4, !dbg !315
  br label %for.cond, !dbg !316, !llvm.loop !317

for.end:                                          ; preds = %for.cond
    #dbg_declare(ptr %ccounts_out, !320, !DIExpression(), !375)
  %62 = load ptr, ptr %argv.addr, align 8, !dbg !376
  %arrayidx90 = getelementptr inbounds ptr, ptr %62, i64 5, !dbg !376
  %63 = load ptr, ptr %arrayidx90, align 8, !dbg !376
  %call91 = call noalias ptr @fopen(ptr noundef %63, ptr noundef @.str.19), !dbg !377
  store ptr %call91, ptr %ccounts_out, align 8, !dbg !375
  %64 = load ptr, ptr %ccounts_out, align 8, !dbg !378
  %cmp92 = icmp ne ptr %64, null, !dbg !379
  br i1 %cmp92, label %land.lhs.true94, label %cond.false96, !dbg !380

land.lhs.true94:                                  ; preds = %for.end
  br i1 true, label %cond.true95, label %cond.false96, !dbg !381

cond.true95:                                      ; preds = %land.lhs.true94
  br label %cond.end97, !dbg !381

cond.false96:                                     ; preds = %land.lhs.true94, %for.end
  call void @__assert_fail(ptr noundef @.str.21, ptr noundef @.str.3, i32 noundef 156, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !381
  unreachable, !dbg !381

65:                                               ; No predecessors!
  br label %cond.end97, !dbg !381

cond.end97:                                       ; preds = %65, %cond.true95
  %66 = load ptr, ptr %ccounts_out, align 8, !dbg !382
  %67 = load i32, ptr %num_iter, align 4, !dbg !383
  %68 = load i32, ptr %num_warmup, align 4, !dbg !384
  %call98 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %66, ptr noundef @.str.22, i32 noundef %67, i32 noundef %68) #8, !dbg !385
    #dbg_declare(ptr %ii, !386, !DIExpression(), !388)
  store i32 0, ptr %ii, align 4, !dbg !388
  br label %for.cond99, !dbg !389

for.cond99:                                       ; preds = %for.inc106, %cond.end97
  %69 = load i32, ptr %ii, align 4, !dbg !390
  %70 = load i32, ptr %num_iter, align 4, !dbg !392
  %cmp100 = icmp slt i32 %69, %70, !dbg !393
  br i1 %cmp100, label %for.body102, label %for.end108, !dbg !394

for.body102:                                      ; preds = %for.cond99
  %71 = load ptr, ptr %ccounts_out, align 8, !dbg !395
  %72 = load ptr, ptr %times, align 8, !dbg !397
  %73 = load i32, ptr %ii, align 4, !dbg !398
  %idxprom103 = sext i32 %73 to i64, !dbg !397
  %arrayidx104 = getelementptr inbounds i64, ptr %72, i64 %idxprom103, !dbg !397
  %74 = load i64, ptr %arrayidx104, align 8, !dbg !397
  %call105 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %71, ptr noundef @.str.23, i64 noundef %74) #8, !dbg !399
  br label %for.inc106, !dbg !400

for.inc106:                                       ; preds = %for.body102
  %75 = load i32, ptr %ii, align 4, !dbg !401
  %inc107 = add nsw i32 %75, 1, !dbg !401
  store i32 %inc107, ptr %ii, align 4, !dbg !401
  br label %for.cond99, !dbg !402, !llvm.loop !403

for.end108:                                       ; preds = %for.cond99
  %76 = load ptr, ptr %ccounts_out, align 8, !dbg !405
  %call109 = call i32 @fclose(ptr noundef %76), !dbg !406
  %cmp110 = icmp ne i32 %call109, -1, !dbg !407
  br i1 %cmp110, label %land.lhs.true112, label %cond.false114, !dbg !408

land.lhs.true112:                                 ; preds = %for.end108
  br i1 true, label %cond.true113, label %cond.false114, !dbg !409

cond.true113:                                     ; preds = %land.lhs.true112
  br label %cond.end115, !dbg !409

cond.false114:                                    ; preds = %land.lhs.true112, %for.end108
  call void @__assert_fail(ptr noundef @.str.25, ptr noundef @.str.3, i32 noundef 164, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !409
  unreachable, !dbg !409

77:                                               ; No predecessors!
  br label %cond.end115, !dbg !409

cond.end115:                                      ; preds = %77, %cond.true113
  ret i32 0, !dbg !410
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noreturn nounwind
declare void @exit(i32 noundef) #2

declare i32 @sodium_init() #1

; Function Attrs: noreturn nounwind
declare void @__assert_fail(ptr noundef, ptr noundef, i32 noundef, ptr noundef) #2

; Function Attrs: nounwind
declare i64 @strtol(ptr noundef, ptr noundef, i32 noundef) #3

; Function Attrs: nounwind willreturn memory(read)
declare i64 @strlen(ptr noundef) #4

declare i64 @crypto_pwhash_passwd_min() #1

declare i64 @crypto_pwhash_passwd_max() #1

declare i64 @crypto_pwhash_bytes_min() #1

declare i64 @crypto_pwhash_bytes_max() #1

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #5

declare i64 @crypto_pwhash_saltbytes() #1

declare i64 @crypto_pwhash_opslimit_interactive() #1

declare i64 @crypto_pwhash_memlimit_interactive() #1

declare i32 @crypto_pwhash_alg_argon2id13() #1

; Function Attrs: nounwind allocsize(0,1)
declare noalias ptr @calloc(i64 noundef, i64 noundef) #6

declare void @randombytes_buf(ptr noundef, i64 noundef) #1

declare i32 @crypto_pwhash(ptr noundef, i64 noundef, ptr noundef, i64 noundef, ptr noundef, i64 noundef, i64 noundef, i32 noundef) #1

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

!llvm.dbg.cu = !{!129}
!llvm.module.flags = !{!143, !144, !145, !146, !147, !148}
!llvm.ident = !{!149}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 65, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "eval_argon2id.c", directory: "/home/rgangar/Documents/llvm-data-independent-timing/micro-benchmarks", checksumkind: CSK_MD5, checksum: "d25ec36710e80008836bb91a574b27c1")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1232, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 154)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 79, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 240, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 30)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 77, type: !14, isLocal: true, isDefinition: true)
!14 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1000, elements: !15)
!15 = !{!16}
!16 = !DISubrange(count: 125)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(scope: null, file: !2, line: 77, type: !19, isLocal: true, isDefinition: true)
!19 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 128, elements: !20)
!20 = !{!21}
!21 = !DISubrange(count: 16)
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(scope: null, file: !2, line: 77, type: !24, isLocal: true, isDefinition: true)
!24 = !DICompositeType(tag: DW_TAG_array_type, baseType: !25, size: 184, elements: !26)
!25 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!26 = !{!27}
!27 = !DISubrange(count: 23)
!28 = !DIGlobalVariableExpression(var: !29, expr: !DIExpression())
!29 = distinct !DIGlobalVariable(scope: null, file: !2, line: 94, type: !30, isLocal: true, isDefinition: true)
!30 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 400, elements: !31)
!31 = !{!32}
!32 = !DISubrange(count: 50)
!33 = !DIGlobalVariableExpression(var: !34, expr: !DIExpression())
!34 = distinct !DIGlobalVariable(scope: null, file: !2, line: 93, type: !35, isLocal: true, isDefinition: true)
!35 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 760, elements: !36)
!36 = !{!37}
!37 = !DISubrange(count: 95)
!38 = !DIGlobalVariableExpression(var: !39, expr: !DIExpression())
!39 = distinct !DIGlobalVariable(scope: null, file: !2, line: 96, type: !40, isLocal: true, isDefinition: true)
!40 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 424, elements: !41)
!41 = !{!42}
!42 = !DISubrange(count: 53)
!43 = !DIGlobalVariableExpression(var: !44, expr: !DIExpression())
!44 = distinct !DIGlobalVariable(scope: null, file: !2, line: 95, type: !45, isLocal: true, isDefinition: true)
!45 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 784, elements: !46)
!46 = !{!47}
!47 = !DISubrange(count: 98)
!48 = !DIGlobalVariableExpression(var: !49, expr: !DIExpression())
!49 = distinct !DIGlobalVariable(scope: null, file: !2, line: 101, type: !50, isLocal: true, isDefinition: true)
!50 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 384, elements: !51)
!51 = !{!52}
!52 = !DISubrange(count: 48)
!53 = !DIGlobalVariableExpression(var: !54, expr: !DIExpression())
!54 = distinct !DIGlobalVariable(scope: null, file: !2, line: 100, type: !55, isLocal: true, isDefinition: true)
!55 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 712, elements: !56)
!56 = !{!57}
!57 = !DISubrange(count: 89)
!58 = !DIGlobalVariableExpression(var: !59, expr: !DIExpression())
!59 = distinct !DIGlobalVariable(scope: null, file: !2, line: 103, type: !60, isLocal: true, isDefinition: true)
!60 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 408, elements: !61)
!61 = !{!62}
!62 = !DISubrange(count: 51)
!63 = !DIGlobalVariableExpression(var: !64, expr: !DIExpression())
!64 = distinct !DIGlobalVariable(scope: null, file: !2, line: 102, type: !65, isLocal: true, isDefinition: true)
!65 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 736, elements: !66)
!66 = !{!67}
!67 = !DISubrange(count: 92)
!68 = !DIGlobalVariableExpression(var: !69, expr: !DIExpression())
!69 = distinct !DIGlobalVariable(scope: null, file: !2, line: 107, type: !30, isLocal: true, isDefinition: true)
!70 = !DIGlobalVariableExpression(var: !71, expr: !DIExpression())
!71 = distinct !DIGlobalVariable(scope: null, file: !2, line: 107, type: !72, isLocal: true, isDefinition: true)
!72 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 472, elements: !73)
!73 = !{!74}
!74 = !DISubrange(count: 59)
!75 = !DIGlobalVariableExpression(var: !76, expr: !DIExpression())
!76 = distinct !DIGlobalVariable(scope: null, file: !2, line: 111, type: !50, isLocal: true, isDefinition: true)
!77 = !DIGlobalVariableExpression(var: !78, expr: !DIExpression())
!78 = distinct !DIGlobalVariable(scope: null, file: !2, line: 111, type: !79, isLocal: true, isDefinition: true)
!79 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 464, elements: !80)
!80 = !{!81}
!81 = !DISubrange(count: 58)
!82 = !DIGlobalVariableExpression(var: !83, expr: !DIExpression())
!83 = distinct !DIGlobalVariable(scope: null, file: !2, line: 125, type: !84, isLocal: true, isDefinition: true)
!84 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 504, elements: !85)
!85 = !{!86}
!86 = !DISubrange(count: 63)
!87 = !DIGlobalVariableExpression(var: !88, expr: !DIExpression())
!88 = distinct !DIGlobalVariable(scope: null, file: !2, line: 124, type: !89, isLocal: true, isDefinition: true)
!89 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 592, elements: !90)
!90 = !{!91}
!91 = !DISubrange(count: 74)
!92 = !DIGlobalVariableExpression(var: !93, expr: !DIExpression())
!93 = distinct !DIGlobalVariable(scope: null, file: !2, line: 145, type: !94, isLocal: true, isDefinition: true)
!94 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !95)
!95 = !{!96}
!96 = !DISubrange(count: 20)
!97 = !DIGlobalVariableExpression(var: !98, expr: !DIExpression())
!98 = distinct !DIGlobalVariable(scope: null, file: !2, line: 155, type: !99, isLocal: true, isDefinition: true)
!99 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 16, elements: !100)
!100 = !{!101}
!101 = !DISubrange(count: 2)
!102 = !DIGlobalVariableExpression(var: !103, expr: !DIExpression())
!103 = distinct !DIGlobalVariable(scope: null, file: !2, line: 156, type: !104, isLocal: true, isDefinition: true)
!104 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 352, elements: !105)
!105 = !{!106}
!106 = !DISubrange(count: 44)
!107 = !DIGlobalVariableExpression(var: !108, expr: !DIExpression())
!108 = distinct !DIGlobalVariable(scope: null, file: !2, line: 156, type: !109, isLocal: true, isDefinition: true)
!109 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 552, elements: !110)
!110 = !{!111}
!111 = !DISubrange(count: 69)
!112 = !DIGlobalVariableExpression(var: !113, expr: !DIExpression())
!113 = distinct !DIGlobalVariable(scope: null, file: !2, line: 159, type: !30, isLocal: true, isDefinition: true)
!114 = !DIGlobalVariableExpression(var: !115, expr: !DIExpression())
!115 = distinct !DIGlobalVariable(scope: null, file: !2, line: 162, type: !116, isLocal: true, isDefinition: true)
!116 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !117)
!117 = !{!118}
!118 = !DISubrange(count: 5)
!119 = !DIGlobalVariableExpression(var: !120, expr: !DIExpression())
!120 = distinct !DIGlobalVariable(scope: null, file: !2, line: 164, type: !121, isLocal: true, isDefinition: true)
!121 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 264, elements: !122)
!122 = !{!123}
!123 = !DISubrange(count: 33)
!124 = !DIGlobalVariableExpression(var: !125, expr: !DIExpression())
!125 = distinct !DIGlobalVariable(scope: null, file: !2, line: 164, type: !126, isLocal: true, isDefinition: true)
!126 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 520, elements: !127)
!127 = !{!128}
!128 = !DISubrange(count: 65)
!129 = distinct !DICompileUnit(language: DW_LANG_C11, file: !2, producer: "clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, retainedTypes: !130, globals: !142, splitDebugInlining: false, nameTableKind: None)
!130 = !{!131, !133, !134, !136, !141}
!131 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !132, size: 64)
!132 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!133 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!134 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !135, size: 64)
!135 = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
!136 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint64_t", file: !137, line: 27, baseType: !138)
!137 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/stdint-uintn.h", directory: "", checksumkind: CSK_MD5, checksum: "2bf2ae53c58c01b1a1b9383b5195125c")
!138 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint64_t", file: !139, line: 45, baseType: !140)
!139 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types.h", directory: "", checksumkind: CSK_MD5, checksum: "d108b5f93a74c50510d7d9bc0ab36df9")
!140 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!141 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !25, size: 64)
!142 = !{!0, !7, !12, !17, !22, !28, !33, !38, !43, !48, !53, !58, !63, !68, !70, !75, !77, !82, !87, !92, !97, !102, !107, !112, !114, !119, !124}
!143 = !{i32 7, !"Dwarf Version", i32 5}
!144 = !{i32 2, !"Debug Info Version", i32 3}
!145 = !{i32 1, !"wchar_size", i32 4}
!146 = !{i32 8, !"PIC Level", i32 2}
!147 = !{i32 7, !"PIE Level", i32 2}
!148 = !{i32 7, !"uwtable", i32 2}
!149 = !{!"clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)"}
!150 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 62, type: !151, scopeLine: 63, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !129, retainedNodes: !154)
!151 = !DISubroutineType(types: !152)
!152 = !{!153, !153, !131}
!153 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!154 = !{}
!155 = !DILocalVariable(name: "argc", arg: 1, scope: !150, file: !2, line: 62, type: !153)
!156 = !DILocation(line: 62, column: 10, scope: !150)
!157 = !DILocalVariable(name: "argv", arg: 2, scope: !150, file: !2, line: 62, type: !131)
!158 = !DILocation(line: 62, column: 23, scope: !150)
!159 = !DILocation(line: 64, column: 7, scope: !160)
!160 = distinct !DILexicalBlock(scope: !150, file: !2, line: 64, column: 7)
!161 = !DILocation(line: 64, column: 12, scope: !160)
!162 = !DILocation(line: 69, column: 50, scope: !163)
!163 = distinct !DILexicalBlock(scope: !160, file: !2, line: 64, column: 29)
!164 = !DILocation(line: 65, column: 5, scope: !163)
!165 = !DILocation(line: 70, column: 5, scope: !163)
!166 = !DILocalVariable(name: "sodium_init_success", scope: !150, file: !2, line: 74, type: !167)
!167 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !153)
!168 = !DILocation(line: 74, column: 13, scope: !150)
!169 = !DILocalVariable(name: "sodium_already_initd", scope: !150, file: !2, line: 75, type: !167)
!170 = !DILocation(line: 75, column: 13, scope: !150)
!171 = !DILocalVariable(name: "sodium_init_result", scope: !150, file: !2, line: 76, type: !153)
!172 = !DILocation(line: 76, column: 7, scope: !150)
!173 = !DILocation(line: 76, column: 28, scope: !150)
!174 = !DILocation(line: 77, column: 34, scope: !150)
!175 = !DILocation(line: 77, column: 31, scope: !150)
!176 = !DILocation(line: 77, column: 53, scope: !150)
!177 = !DILocation(line: 78, column: 28, scope: !150)
!178 = !DILocation(line: 78, column: 25, scope: !150)
!179 = !DILocation(line: 78, column: 48, scope: !150)
!180 = !DILocation(line: 77, column: 3, scope: !150)
!181 = !DILocalVariable(name: "num_iter", scope: !150, file: !2, line: 82, type: !153)
!182 = !DILocation(line: 82, column: 7, scope: !150)
!183 = !DILocation(line: 82, column: 34, scope: !150)
!184 = !DILocation(line: 82, column: 18, scope: !150)
!185 = !DILocalVariable(name: "num_warmup", scope: !150, file: !2, line: 86, type: !153)
!186 = !DILocation(line: 86, column: 7, scope: !150)
!187 = !DILocation(line: 86, column: 36, scope: !150)
!188 = !DILocation(line: 86, column: 20, scope: !150)
!189 = !DILocalVariable(name: "passwd", scope: !150, file: !2, line: 91, type: !134)
!190 = !DILocation(line: 91, column: 18, scope: !150)
!191 = !DILocation(line: 91, column: 43, scope: !150)
!192 = !DILocalVariable(name: "passwd_sz", scope: !150, file: !2, line: 92, type: !193)
!193 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!194 = !DILocation(line: 92, column: 22, scope: !150)
!195 = !DILocation(line: 92, column: 41, scope: !150)
!196 = !DILocation(line: 92, column: 34, scope: !150)
!197 = !DILocation(line: 93, column: 10, scope: !150)
!198 = !DILocation(line: 93, column: 23, scope: !150)
!199 = !DILocation(line: 93, column: 20, scope: !150)
!200 = !DILocation(line: 93, column: 50, scope: !150)
!201 = !DILocation(line: 93, column: 3, scope: !150)
!202 = !DILocation(line: 95, column: 10, scope: !150)
!203 = !DILocation(line: 95, column: 23, scope: !150)
!204 = !DILocation(line: 95, column: 20, scope: !150)
!205 = !DILocation(line: 95, column: 50, scope: !150)
!206 = !DILocation(line: 95, column: 3, scope: !150)
!207 = !DILocalVariable(name: "out_sz", scope: !150, file: !2, line: 99, type: !193)
!208 = !DILocation(line: 99, column: 22, scope: !150)
!209 = !DILocation(line: 99, column: 38, scope: !150)
!210 = !DILocation(line: 99, column: 31, scope: !150)
!211 = !DILocation(line: 100, column: 10, scope: !150)
!212 = !DILocation(line: 100, column: 20, scope: !150)
!213 = !DILocation(line: 100, column: 17, scope: !150)
!214 = !DILocation(line: 100, column: 46, scope: !150)
!215 = !DILocation(line: 100, column: 3, scope: !150)
!216 = !DILocation(line: 102, column: 10, scope: !150)
!217 = !DILocation(line: 102, column: 20, scope: !150)
!218 = !DILocation(line: 102, column: 17, scope: !150)
!219 = !DILocation(line: 102, column: 46, scope: !150)
!220 = !DILocation(line: 102, column: 3, scope: !150)
!221 = !DILocalVariable(name: "out", scope: !150, file: !2, line: 106, type: !134)
!222 = !DILocation(line: 106, column: 18, scope: !150)
!223 = !DILocation(line: 106, column: 31, scope: !150)
!224 = !DILocation(line: 106, column: 24, scope: !150)
!225 = !DILocation(line: 107, column: 10, scope: !150)
!226 = !DILocation(line: 107, column: 14, scope: !150)
!227 = !DILocation(line: 107, column: 3, scope: !150)
!228 = !DILocalVariable(name: "salt", scope: !150, file: !2, line: 110, type: !134)
!229 = !DILocation(line: 110, column: 18, scope: !150)
!230 = !DILocation(line: 110, column: 32, scope: !150)
!231 = !DILocation(line: 110, column: 25, scope: !150)
!232 = !DILocation(line: 111, column: 10, scope: !150)
!233 = !DILocation(line: 111, column: 15, scope: !150)
!234 = !DILocation(line: 111, column: 3, scope: !150)
!235 = !DILocalVariable(name: "opslimit", scope: !150, file: !2, line: 114, type: !193)
!236 = !DILocation(line: 114, column: 22, scope: !150)
!237 = !DILocation(line: 114, column: 33, scope: !150)
!238 = !DILocalVariable(name: "memlimit", scope: !150, file: !2, line: 117, type: !193)
!239 = !DILocation(line: 117, column: 22, scope: !150)
!240 = !DILocation(line: 117, column: 33, scope: !150)
!241 = !DILocalVariable(name: "alg", scope: !150, file: !2, line: 120, type: !153)
!242 = !DILocation(line: 120, column: 7, scope: !150)
!243 = !DILocation(line: 120, column: 13, scope: !150)
!244 = !DILocalVariable(name: "times", scope: !150, file: !2, line: 123, type: !245)
!245 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !136, size: 64)
!246 = !DILocation(line: 123, column: 13, scope: !150)
!247 = !DILocation(line: 123, column: 28, scope: !150)
!248 = !DILocation(line: 123, column: 21, scope: !150)
!249 = !DILocation(line: 124, column: 10, scope: !150)
!250 = !DILocation(line: 124, column: 16, scope: !150)
!251 = !DILocation(line: 124, column: 3, scope: !150)
!252 = !DILocalVariable(name: "start_time", scope: !150, file: !2, line: 127, type: !253)
!253 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !136)
!254 = !DILocation(line: 127, column: 21, scope: !150)
!255 = !DILocalVariable(name: "end_time", scope: !150, file: !2, line: 128, type: !253)
!256 = !DILocation(line: 128, column: 21, scope: !150)
!257 = !DILocalVariable(name: "cur_iter", scope: !258, file: !2, line: 131, type: !153)
!258 = distinct !DILexicalBlock(scope: !150, file: !2, line: 131, column: 3)
!259 = !DILocation(line: 131, column: 12, scope: !258)
!260 = !DILocation(line: 131, column: 8, scope: !258)
!261 = !DILocation(line: 131, column: 26, scope: !262)
!262 = distinct !DILexicalBlock(scope: !258, file: !2, line: 131, column: 3)
!263 = !DILocation(line: 131, column: 37, scope: !262)
!264 = !DILocation(line: 131, column: 48, scope: !262)
!265 = !DILocation(line: 131, column: 46, scope: !262)
!266 = !DILocation(line: 131, column: 35, scope: !262)
!267 = !DILocation(line: 131, column: 3, scope: !258)
!268 = !DILocation(line: 133, column: 21, scope: !269)
!269 = distinct !DILexicalBlock(scope: !262, file: !2, line: 131, column: 72)
!270 = !DILocation(line: 133, column: 5, scope: !269)
!271 = !DILocalVariable(name: "_eval_cycles_low", scope: !272, file: !2, line: 136, type: !273)
!272 = distinct !DILexicalBlock(scope: !269, file: !2, line: 136, column: 18)
!273 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !137, line: 26, baseType: !274)
!274 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !139, line: 42, baseType: !275)
!275 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!276 = !DILocation(line: 136, column: 18, scope: !272)
!277 = !DILocalVariable(name: "_eval_cycles_high", scope: !272, file: !2, line: 136, type: !273)
!278 = !{i64 2147804546, i64 2147804554, i64 2147804579, i64 2147804624, i64 2147804665}
!279 = !DILocation(line: 136, column: 16, scope: !269)
!280 = !DILocalVariable(name: "pwhash_result", scope: !269, file: !2, line: 139, type: !153)
!281 = !DILocation(line: 139, column: 9, scope: !269)
!282 = !DILocation(line: 139, column: 39, scope: !269)
!283 = !DILocation(line: 139, column: 44, scope: !269)
!284 = !DILocation(line: 139, column: 65, scope: !269)
!285 = !DILocation(line: 139, column: 73, scope: !269)
!286 = !DILocation(line: 140, column: 11, scope: !269)
!287 = !DILocation(line: 140, column: 17, scope: !269)
!288 = !DILocation(line: 140, column: 27, scope: !269)
!289 = !DILocation(line: 140, column: 37, scope: !269)
!290 = !DILocation(line: 139, column: 25, scope: !269)
!291 = !DILocalVariable(name: "_eval_cycles_low", scope: !292, file: !2, line: 143, type: !273)
!292 = distinct !DILexicalBlock(scope: !269, file: !2, line: 143, column: 16)
!293 = !DILocation(line: 143, column: 16, scope: !292)
!294 = !DILocalVariable(name: "_eval_cycles_high", scope: !292, file: !2, line: 143, type: !273)
!295 = !{i64 2147805044, i64 2147805053, i64 2147805153, i64 2147805233, i64 2147805295}
!296 = !DILocation(line: 143, column: 14, scope: !269)
!297 = !DILocation(line: 145, column: 18, scope: !269)
!298 = !DILocation(line: 145, column: 15, scope: !269)
!299 = !DILocation(line: 145, column: 5, scope: !269)
!300 = !DILocation(line: 147, column: 9, scope: !301)
!301 = distinct !DILexicalBlock(scope: !269, file: !2, line: 147, column: 9)
!302 = !DILocation(line: 147, column: 21, scope: !301)
!303 = !DILocation(line: 147, column: 18, scope: !301)
!304 = !DILocation(line: 148, column: 38, scope: !305)
!305 = distinct !DILexicalBlock(scope: !301, file: !2, line: 147, column: 33)
!306 = !DILocation(line: 148, column: 49, scope: !305)
!307 = !DILocation(line: 148, column: 47, scope: !305)
!308 = !DILocation(line: 148, column: 7, scope: !305)
!309 = !DILocation(line: 148, column: 13, scope: !305)
!310 = !DILocation(line: 148, column: 24, scope: !305)
!311 = !DILocation(line: 148, column: 22, scope: !305)
!312 = !DILocation(line: 148, column: 36, scope: !305)
!313 = !DILocation(line: 149, column: 5, scope: !305)
!314 = !DILocation(line: 152, column: 3, scope: !269)
!315 = !DILocation(line: 131, column: 60, scope: !262)
!316 = !DILocation(line: 131, column: 3, scope: !262)
!317 = distinct !{!317, !267, !318, !319}
!318 = !DILocation(line: 152, column: 3, scope: !258)
!319 = !{!"llvm.loop.mustprogress"}
!320 = !DILocalVariable(name: "ccounts_out", scope: !150, file: !2, line: 155, type: !321)
!321 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !322, size: 64)
!322 = !DIDerivedType(tag: DW_TAG_typedef, name: "FILE", file: !323, line: 7, baseType: !324)
!323 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "571f9fb6223c42439075fdde11a0de5d")
!324 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_FILE", file: !325, line: 49, size: 1728, elements: !326)
!325 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "1bad07471b7974df4ecc1d1c2ca207e6")
!326 = !{!327, !328, !329, !330, !331, !332, !333, !334, !335, !336, !337, !338, !339, !342, !344, !345, !346, !349, !351, !353, !357, !360, !362, !365, !368, !369, !370, !373, !374}
!327 = !DIDerivedType(tag: DW_TAG_member, name: "_flags", scope: !324, file: !325, line: 51, baseType: !153, size: 32)
!328 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_ptr", scope: !324, file: !325, line: 54, baseType: !132, size: 64, offset: 64)
!329 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_end", scope: !324, file: !325, line: 55, baseType: !132, size: 64, offset: 128)
!330 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_base", scope: !324, file: !325, line: 56, baseType: !132, size: 64, offset: 192)
!331 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_base", scope: !324, file: !325, line: 57, baseType: !132, size: 64, offset: 256)
!332 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_ptr", scope: !324, file: !325, line: 58, baseType: !132, size: 64, offset: 320)
!333 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_end", scope: !324, file: !325, line: 59, baseType: !132, size: 64, offset: 384)
!334 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_base", scope: !324, file: !325, line: 60, baseType: !132, size: 64, offset: 448)
!335 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_end", scope: !324, file: !325, line: 61, baseType: !132, size: 64, offset: 512)
!336 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_base", scope: !324, file: !325, line: 64, baseType: !132, size: 64, offset: 576)
!337 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_backup_base", scope: !324, file: !325, line: 65, baseType: !132, size: 64, offset: 640)
!338 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_end", scope: !324, file: !325, line: 66, baseType: !132, size: 64, offset: 704)
!339 = !DIDerivedType(tag: DW_TAG_member, name: "_markers", scope: !324, file: !325, line: 68, baseType: !340, size: 64, offset: 768)
!340 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !341, size: 64)
!341 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_marker", file: !325, line: 36, flags: DIFlagFwdDecl)
!342 = !DIDerivedType(tag: DW_TAG_member, name: "_chain", scope: !324, file: !325, line: 70, baseType: !343, size: 64, offset: 832)
!343 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !324, size: 64)
!344 = !DIDerivedType(tag: DW_TAG_member, name: "_fileno", scope: !324, file: !325, line: 72, baseType: !153, size: 32, offset: 896)
!345 = !DIDerivedType(tag: DW_TAG_member, name: "_flags2", scope: !324, file: !325, line: 73, baseType: !153, size: 32, offset: 928)
!346 = !DIDerivedType(tag: DW_TAG_member, name: "_old_offset", scope: !324, file: !325, line: 74, baseType: !347, size: 64, offset: 960)
!347 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off_t", file: !139, line: 152, baseType: !348)
!348 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!349 = !DIDerivedType(tag: DW_TAG_member, name: "_cur_column", scope: !324, file: !325, line: 77, baseType: !350, size: 16, offset: 1024)
!350 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!351 = !DIDerivedType(tag: DW_TAG_member, name: "_vtable_offset", scope: !324, file: !325, line: 78, baseType: !352, size: 8, offset: 1040)
!352 = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
!353 = !DIDerivedType(tag: DW_TAG_member, name: "_shortbuf", scope: !324, file: !325, line: 79, baseType: !354, size: 8, offset: 1048)
!354 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 8, elements: !355)
!355 = !{!356}
!356 = !DISubrange(count: 1)
!357 = !DIDerivedType(tag: DW_TAG_member, name: "_lock", scope: !324, file: !325, line: 81, baseType: !358, size: 64, offset: 1088)
!358 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !359, size: 64)
!359 = !DIDerivedType(tag: DW_TAG_typedef, name: "_IO_lock_t", file: !325, line: 43, baseType: null)
!360 = !DIDerivedType(tag: DW_TAG_member, name: "_offset", scope: !324, file: !325, line: 89, baseType: !361, size: 64, offset: 1152)
!361 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off64_t", file: !139, line: 153, baseType: !348)
!362 = !DIDerivedType(tag: DW_TAG_member, name: "_codecvt", scope: !324, file: !325, line: 91, baseType: !363, size: 64, offset: 1216)
!363 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !364, size: 64)
!364 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_codecvt", file: !325, line: 37, flags: DIFlagFwdDecl)
!365 = !DIDerivedType(tag: DW_TAG_member, name: "_wide_data", scope: !324, file: !325, line: 92, baseType: !366, size: 64, offset: 1280)
!366 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !367, size: 64)
!367 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_wide_data", file: !325, line: 38, flags: DIFlagFwdDecl)
!368 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_list", scope: !324, file: !325, line: 93, baseType: !343, size: 64, offset: 1344)
!369 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_buf", scope: !324, file: !325, line: 94, baseType: !133, size: 64, offset: 1408)
!370 = !DIDerivedType(tag: DW_TAG_member, name: "__pad5", scope: !324, file: !325, line: 95, baseType: !371, size: 64, offset: 1472)
!371 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !372, line: 18, baseType: !140)
!372 = !DIFile(filename: "build/lib/clang/22/include/__stddef_size_t.h", directory: "/home/rgangar/Documents/llvm-data-independent-timing", checksumkind: CSK_MD5, checksum: "2c44e821a2b1951cde2eb0fb2e656867")
!373 = !DIDerivedType(tag: DW_TAG_member, name: "_mode", scope: !324, file: !325, line: 96, baseType: !153, size: 32, offset: 1536)
!374 = !DIDerivedType(tag: DW_TAG_member, name: "_unused2", scope: !324, file: !325, line: 98, baseType: !94, size: 160, offset: 1568)
!375 = !DILocation(line: 155, column: 9, scope: !150)
!376 = !DILocation(line: 155, column: 29, scope: !150)
!377 = !DILocation(line: 155, column: 23, scope: !150)
!378 = !DILocation(line: 156, column: 10, scope: !150)
!379 = !DILocation(line: 156, column: 22, scope: !150)
!380 = !DILocation(line: 156, column: 30, scope: !150)
!381 = !DILocation(line: 156, column: 3, scope: !150)
!382 = !DILocation(line: 158, column: 11, scope: !150)
!383 = !DILocation(line: 160, column: 4, scope: !150)
!384 = !DILocation(line: 160, column: 14, scope: !150)
!385 = !DILocation(line: 158, column: 3, scope: !150)
!386 = !DILocalVariable(name: "ii", scope: !387, file: !2, line: 161, type: !153)
!387 = distinct !DILexicalBlock(scope: !150, file: !2, line: 161, column: 3)
!388 = !DILocation(line: 161, column: 12, scope: !387)
!389 = !DILocation(line: 161, column: 8, scope: !387)
!390 = !DILocation(line: 161, column: 20, scope: !391)
!391 = distinct !DILexicalBlock(scope: !387, file: !2, line: 161, column: 3)
!392 = !DILocation(line: 161, column: 25, scope: !391)
!393 = !DILocation(line: 161, column: 23, scope: !391)
!394 = !DILocation(line: 161, column: 3, scope: !387)
!395 = !DILocation(line: 162, column: 12, scope: !396)
!396 = distinct !DILexicalBlock(scope: !391, file: !2, line: 161, column: 41)
!397 = !DILocation(line: 162, column: 42, scope: !396)
!398 = !DILocation(line: 162, column: 48, scope: !396)
!399 = !DILocation(line: 162, column: 4, scope: !396)
!400 = !DILocation(line: 163, column: 3, scope: !396)
!401 = !DILocation(line: 161, column: 35, scope: !391)
!402 = !DILocation(line: 161, column: 3, scope: !391)
!403 = distinct !{!403, !394, !404, !319}
!404 = !DILocation(line: 163, column: 3, scope: !387)
!405 = !DILocation(line: 164, column: 17, scope: !150)
!406 = !DILocation(line: 164, column: 10, scope: !150)
!407 = !DILocation(line: 164, column: 30, scope: !150)
!408 = !DILocation(line: 164, column: 37, scope: !150)
!409 = !DILocation(line: 164, column: 3, scope: !150)
!410 = !DILocation(line: 173, column: 3, scope: !150)
