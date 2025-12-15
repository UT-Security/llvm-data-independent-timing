; ModuleID = 'eval_aesni256gcm_encrypt.c'
source_filename = "eval_aesni256gcm_encrypt.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [145 x i8] c"Usage: %s <num_benchmark_iterations> <num_warmup_iterations> <message> <size_of_associated_data> <cycle_counts_file> [<dynamic_hitcounts_file>]\0A\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [30 x i8] c"Error initializing lib sodium\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [125 x i8] c"(sodium_init_success == sodium_init_result || sodium_already_initd == sodium_init_result) && \22Error initializing lib sodium\22\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [27 x i8] c"eval_aesni256gcm_encrypt.c\00", align 1, !dbg !17
@__PRETTY_FUNCTION__.main = private unnamed_addr constant [23 x i8] c"int main(int, char **)\00", align 1, !dbg !22
@.str.4 = private unnamed_addr constant [68 x i8] c"Couldn't allocate decrypted_msg bytes in eval_aesni256gcm_encrypt.c\00", align 1, !dbg !28
@.str.5 = private unnamed_addr constant [87 x i8] c"decrypted_msg && \22Couldn't allocate decrypted_msg bytes in eval_aesni256gcm_encrypt.c\22\00", align 1, !dbg !33
@.str.6 = private unnamed_addr constant [65 x i8] c"Couldn't allocate ciphertext bytes in eval_aesni256gcm_encrypt.c\00", align 1, !dbg !38
@.str.7 = private unnamed_addr constant [74 x i8] c"msg && \22Couldn't allocate ciphertext bytes in eval_aesni256gcm_encrypt.c\22\00", align 1, !dbg !43
@.str.8 = private unnamed_addr constant [58 x i8] c"Couldn't allocate key bytes in eval_aesni256gcm_encrypt.c\00", align 1, !dbg !48
@.str.9 = private unnamed_addr constant [67 x i8] c"key && \22Couldn't allocate key bytes in eval_aesni256gcm_encrypt.c\22\00", align 1, !dbg !53
@.str.10 = private unnamed_addr constant [69 x i8] c"nonce && \22Couldn't allocate key bytes in eval_aesni256gcm_encrypt.c\22\00", align 1, !dbg !58
@.str.11 = private unnamed_addr constant [82 x i8] c"Couldn't allocate array for encrypt benchmark times in eval_aesni256gcm_encrypt.c\00", align 1, !dbg !63
@.str.12 = private unnamed_addr constant [93 x i8] c"times && \22Couldn't allocate array for encrypt benchmark times in eval_aesni256gcm_encrypt.c\22\00", align 1, !dbg !68
@.str.13 = private unnamed_addr constant [74 x i8] c"FAILURE: eval_aesni256gcm_encrypt failed at crypto_aead_aes256gcm_encrypt\00", align 1, !dbg !73
@.str.14 = private unnamed_addr constant [74 x i8] c"FAILURE: eval_aesni256gcm_encrypt failed at crypto_aead_aes256gcm_decrypt\00", align 1, !dbg !75
@.str.15 = private unnamed_addr constant [76 x i8] c"FAILURE: eval_aesni256gcm_encrypt failed sanity check, decrypted msg != msg\00", align 1, !dbg !77
@.str.16 = private unnamed_addr constant [2 x i8] c"w\00", align 1, !dbg !82
@.str.17 = private unnamed_addr constant [44 x i8] c"Couldn't open cycle counts file for writing\00", align 1, !dbg !87
@.str.18 = private unnamed_addr constant [69 x i8] c"ccounts_out != NULL && \22Couldn't open cycle counts file for writing\22\00", align 1, !dbg !92
@.str.19 = private unnamed_addr constant [61 x i8] c"aesni256gcm_encrypt cycle counts (%d iterations, %d warmup)\0A\00", align 1, !dbg !94
@.str.20 = private unnamed_addr constant [5 x i8] c"%lu\0A\00", align 1, !dbg !99
@.str.21 = private unnamed_addr constant [33 x i8] c"Couldn't close cycle counts file\00", align 1, !dbg !104
@.str.22 = private unnamed_addr constant [65 x i8] c"fclose(ccounts_out) != EOF && \22Couldn't close cycle counts file\22\00", align 1, !dbg !109

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %argc, ptr noundef %argv) #0 !dbg !131 {
entry:
  %retval = alloca i32, align 4
  %argc.addr = alloca i32, align 4
  %argv.addr = alloca ptr, align 8
  %num_iter = alloca i32, align 4
  %num_warmup = alloca i32, align 4
  %msg = alloca ptr, align 8
  %msg_sz = alloca i64, align 8
  %additional_data_sz = alloca i64, align 8
  %sodium_init_success = alloca i32, align 4
  %sodium_already_initd = alloca i32, align 4
  %sodium_init_result = alloca i32, align 4
  %additional_data = alloca ptr, align 8
  %decrypted_msg = alloca ptr, align 8
  %ciphertext_sz = alloca i64, align 8
  %ciphertext = alloca ptr, align 8
  %key = alloca ptr, align 8
  %nonce = alloca ptr, align 8
  %times = alloca ptr, align 8
  %start_time = alloca i64, align 8
  %end_time = alloca i64, align 8
  %cur_iter = alloca i32, align 4
  %_eval_cycles_low = alloca i32, align 4
  %_eval_cycles_high = alloca i32, align 4
  %tmp = alloca i64, align 8
  %encrypt_result = alloca i32, align 4
  %_eval_cycles_low55 = alloca i32, align 4
  %_eval_cycles_high56 = alloca i32, align 4
  %tmp59 = alloca i64, align 8
  %decrypt_result = alloca i32, align 4
  %cmp_result = alloca i32, align 4
  %ccounts_out = alloca ptr, align 8
  %ii = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 %argc, ptr %argc.addr, align 4
    #dbg_declare(ptr %argc.addr, !136, !DIExpression(), !137)
  store ptr %argv, ptr %argv.addr, align 8
    #dbg_declare(ptr %argv.addr, !138, !DIExpression(), !139)
  %0 = load i32, ptr %argc.addr, align 4, !dbg !140
  %cmp = icmp slt i32 %0, 6, !dbg !142
  br i1 %cmp, label %if.then, label %if.end, !dbg !142

if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %argv.addr, align 8, !dbg !143
  %arrayidx = getelementptr inbounds ptr, ptr %1, i64 0, !dbg !143
  %2 = load ptr, ptr %arrayidx, align 8, !dbg !143
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str, ptr noundef %2), !dbg !145
  call void @exit(i32 noundef -1) #7, !dbg !146
  unreachable, !dbg !146

if.end:                                           ; preds = %entry
    #dbg_declare(ptr %num_iter, !147, !DIExpression(), !148)
  %3 = load ptr, ptr %argv.addr, align 8, !dbg !149
  %arrayidx1 = getelementptr inbounds ptr, ptr %3, i64 1, !dbg !149
  %4 = load ptr, ptr %arrayidx1, align 8, !dbg !149
  %call2 = call i64 @strtol(ptr noundef %4, ptr noundef null, i32 noundef 10) #8, !dbg !150
  %conv = trunc i64 %call2 to i32, !dbg !150
  store i32 %conv, ptr %num_iter, align 4, !dbg !148
    #dbg_declare(ptr %num_warmup, !151, !DIExpression(), !152)
  %5 = load ptr, ptr %argv.addr, align 8, !dbg !153
  %arrayidx3 = getelementptr inbounds ptr, ptr %5, i64 2, !dbg !153
  %6 = load ptr, ptr %arrayidx3, align 8, !dbg !153
  %call4 = call i64 @strtol(ptr noundef %6, ptr noundef null, i32 noundef 10) #8, !dbg !154
  %conv5 = trunc i64 %call4 to i32, !dbg !154
  store i32 %conv5, ptr %num_warmup, align 4, !dbg !152
    #dbg_declare(ptr %msg, !155, !DIExpression(), !156)
  %7 = load ptr, ptr %argv.addr, align 8, !dbg !157
  %arrayidx6 = getelementptr inbounds ptr, ptr %7, i64 3, !dbg !157
  %8 = load ptr, ptr %arrayidx6, align 8, !dbg !157
  store ptr %8, ptr %msg, align 8, !dbg !156
    #dbg_declare(ptr %msg_sz, !158, !DIExpression(), !160)
  %9 = load ptr, ptr %argv.addr, align 8, !dbg !161
  %arrayidx7 = getelementptr inbounds ptr, ptr %9, i64 3, !dbg !161
  %10 = load ptr, ptr %arrayidx7, align 8, !dbg !161
  %call8 = call i64 @strlen(ptr noundef %10) #9, !dbg !162
  store i64 %call8, ptr %msg_sz, align 8, !dbg !160
    #dbg_declare(ptr %additional_data_sz, !163, !DIExpression(), !164)
  store i64 0, ptr %additional_data_sz, align 8, !dbg !164
    #dbg_declare(ptr %sodium_init_success, !165, !DIExpression(), !167)
  store i32 0, ptr %sodium_init_success, align 4, !dbg !167
    #dbg_declare(ptr %sodium_already_initd, !168, !DIExpression(), !169)
  store i32 1, ptr %sodium_already_initd, align 4, !dbg !169
    #dbg_declare(ptr %sodium_init_result, !170, !DIExpression(), !171)
  %call9 = call i32 (...) @sodium_init(), !dbg !172
  store i32 %call9, ptr %sodium_init_result, align 4, !dbg !171
  %11 = load i32, ptr %sodium_init_result, align 4, !dbg !173
  %cmp10 = icmp eq i32 0, %11, !dbg !174
  br i1 %cmp10, label %land.lhs.true, label %lor.lhs.false, !dbg !175

lor.lhs.false:                                    ; preds = %if.end
  %12 = load i32, ptr %sodium_init_result, align 4, !dbg !176
  %cmp12 = icmp eq i32 1, %12, !dbg !177
  br i1 %cmp12, label %land.lhs.true, label %cond.false, !dbg !178

land.lhs.true:                                    ; preds = %lor.lhs.false, %if.end
  br i1 true, label %cond.true, label %cond.false, !dbg !179

cond.true:                                        ; preds = %land.lhs.true
  br label %cond.end, !dbg !179

cond.false:                                       ; preds = %land.lhs.true, %lor.lhs.false
  call void @__assert_fail(ptr noundef @.str.2, ptr noundef @.str.3, i32 noundef 94, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !179
  unreachable, !dbg !179

13:                                               ; No predecessors!
  br label %cond.end, !dbg !179

cond.end:                                         ; preds = %13, %cond.true
    #dbg_declare(ptr %additional_data, !180, !DIExpression(), !181)
  store ptr null, ptr %additional_data, align 8, !dbg !181
    #dbg_declare(ptr %decrypted_msg, !182, !DIExpression(), !183)
  %14 = load i64, ptr %msg_sz, align 8, !dbg !184
  %call14 = call noalias ptr @malloc(i64 noundef %14) #10, !dbg !185
  store ptr %call14, ptr %decrypted_msg, align 8, !dbg !183
  %15 = load ptr, ptr %decrypted_msg, align 8, !dbg !186
  %tobool = icmp ne ptr %15, null, !dbg !186
  br i1 %tobool, label %land.lhs.true15, label %cond.false17, !dbg !187

land.lhs.true15:                                  ; preds = %cond.end
  br i1 true, label %cond.true16, label %cond.false17, !dbg !188

cond.true16:                                      ; preds = %land.lhs.true15
  br label %cond.end18, !dbg !188

cond.false17:                                     ; preds = %land.lhs.true15, %cond.end
  call void @__assert_fail(ptr noundef @.str.5, ptr noundef @.str.3, i32 noundef 106, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !188
  unreachable, !dbg !188

16:                                               ; No predecessors!
  br label %cond.end18, !dbg !188

cond.end18:                                       ; preds = %16, %cond.true16
    #dbg_declare(ptr %ciphertext_sz, !189, !DIExpression(), !190)
  %17 = load i64, ptr %msg_sz, align 8, !dbg !191
  %call19 = call i64 @crypto_aead_aes256gcm_abytes(), !dbg !192
  %add = add i64 %17, %call19, !dbg !193
  store i64 %add, ptr %ciphertext_sz, align 8, !dbg !190
    #dbg_declare(ptr %ciphertext, !194, !DIExpression(), !195)
  %18 = load i64, ptr %ciphertext_sz, align 8, !dbg !196
  %call20 = call noalias ptr @malloc(i64 noundef %18) #10, !dbg !197
  store ptr %call20, ptr %ciphertext, align 8, !dbg !195
  %19 = load ptr, ptr %msg, align 8, !dbg !198
  %tobool21 = icmp ne ptr %19, null, !dbg !198
  br i1 %tobool21, label %land.lhs.true22, label %cond.false24, !dbg !199

land.lhs.true22:                                  ; preds = %cond.end18
  br i1 true, label %cond.true23, label %cond.false24, !dbg !200

cond.true23:                                      ; preds = %land.lhs.true22
  br label %cond.end25, !dbg !200

cond.false24:                                     ; preds = %land.lhs.true22, %cond.end18
  call void @__assert_fail(ptr noundef @.str.7, ptr noundef @.str.3, i32 noundef 111, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !200
  unreachable, !dbg !200

20:                                               ; No predecessors!
  br label %cond.end25, !dbg !200

cond.end25:                                       ; preds = %20, %cond.true23
    #dbg_declare(ptr %key, !201, !DIExpression(), !202)
  %call26 = call i64 @crypto_aead_aes256gcm_keybytes(), !dbg !203
  %call27 = call noalias ptr @malloc(i64 noundef %call26) #10, !dbg !204
  store ptr %call27, ptr %key, align 8, !dbg !202
  %21 = load ptr, ptr %key, align 8, !dbg !205
  %tobool28 = icmp ne ptr %21, null, !dbg !205
  br i1 %tobool28, label %land.lhs.true29, label %cond.false31, !dbg !206

land.lhs.true29:                                  ; preds = %cond.end25
  br i1 true, label %cond.true30, label %cond.false31, !dbg !207

cond.true30:                                      ; preds = %land.lhs.true29
  br label %cond.end32, !dbg !207

cond.false31:                                     ; preds = %land.lhs.true29, %cond.end25
  call void @__assert_fail(ptr noundef @.str.9, ptr noundef @.str.3, i32 noundef 115, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !207
  unreachable, !dbg !207

22:                                               ; No predecessors!
  br label %cond.end32, !dbg !207

cond.end32:                                       ; preds = %22, %cond.true30
    #dbg_declare(ptr %nonce, !208, !DIExpression(), !209)
  %call33 = call i64 @crypto_aead_aes256gcm_npubbytes(), !dbg !210
  %call34 = call noalias ptr @malloc(i64 noundef %call33) #10, !dbg !211
  store ptr %call34, ptr %nonce, align 8, !dbg !209
  %23 = load ptr, ptr %nonce, align 8, !dbg !212
  %tobool35 = icmp ne ptr %23, null, !dbg !212
  br i1 %tobool35, label %land.lhs.true36, label %cond.false38, !dbg !213

land.lhs.true36:                                  ; preds = %cond.end32
  br i1 true, label %cond.true37, label %cond.false38, !dbg !214

cond.true37:                                      ; preds = %land.lhs.true36
  br label %cond.end39, !dbg !214

cond.false38:                                     ; preds = %land.lhs.true36, %cond.end32
  call void @__assert_fail(ptr noundef @.str.10, ptr noundef @.str.3, i32 noundef 119, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !214
  unreachable, !dbg !214

24:                                               ; No predecessors!
  br label %cond.end39, !dbg !214

cond.end39:                                       ; preds = %24, %cond.true37
    #dbg_declare(ptr %times, !215, !DIExpression(), !217)
  %25 = load i32, ptr %num_iter, align 4, !dbg !218
  %conv40 = sext i32 %25 to i64, !dbg !218
  %call41 = call noalias ptr @calloc(i64 noundef %conv40, i64 noundef 8) #11, !dbg !219
  store ptr %call41, ptr %times, align 8, !dbg !217
  %26 = load ptr, ptr %times, align 8, !dbg !220
  %tobool42 = icmp ne ptr %26, null, !dbg !220
  br i1 %tobool42, label %land.lhs.true43, label %cond.false45, !dbg !221

land.lhs.true43:                                  ; preds = %cond.end39
  br i1 true, label %cond.true44, label %cond.false45, !dbg !222

cond.true44:                                      ; preds = %land.lhs.true43
  br label %cond.end46, !dbg !222

cond.false45:                                     ; preds = %land.lhs.true43, %cond.end39
  call void @__assert_fail(ptr noundef @.str.12, ptr noundef @.str.3, i32 noundef 124, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !222
  unreachable, !dbg !222

27:                                               ; No predecessors!
  br label %cond.end46, !dbg !222

cond.end46:                                       ; preds = %27, %cond.true44
    #dbg_declare(ptr %start_time, !223, !DIExpression(), !225)
  store volatile i64 0, ptr %start_time, align 8, !dbg !225
    #dbg_declare(ptr %end_time, !226, !DIExpression(), !227)
  store volatile i64 0, ptr %end_time, align 8, !dbg !227
    #dbg_declare(ptr %cur_iter, !228, !DIExpression(), !230)
  store i32 0, ptr %cur_iter, align 4, !dbg !230
  br label %for.cond, !dbg !231

for.cond:                                         ; preds = %for.inc, %cond.end46
  %28 = load i32, ptr %cur_iter, align 4, !dbg !232
  %29 = load i32, ptr %num_iter, align 4, !dbg !234
  %30 = load i32, ptr %num_warmup, align 4, !dbg !235
  %add47 = add nsw i32 %29, %30, !dbg !236
  %cmp48 = icmp slt i32 %28, %add47, !dbg !237
  br i1 %cmp48, label %for.body, label %for.end, !dbg !238

for.body:                                         ; preds = %for.cond
  %31 = load ptr, ptr %key, align 8, !dbg !239
  %call50 = call i32 @crypto_aead_aes256gcm_keygen(ptr noundef %31), !dbg !241
  %32 = load ptr, ptr %nonce, align 8, !dbg !242
  call void @randombytes_buf(ptr noundef %32, i64 noundef 8), !dbg !243
    #dbg_declare(ptr %_eval_cycles_low, !244, !DIExpression(), !249)
  store i32 0, ptr %_eval_cycles_low, align 4, !dbg !249
    #dbg_declare(ptr %_eval_cycles_high, !250, !DIExpression(), !249)
  store i32 0, ptr %_eval_cycles_high, align 4, !dbg !249
  %33 = call { i32, i32 } asm sideeffect "cpuid\0A\09rdtsc\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !249, !srcloc !251
  %asmresult = extractvalue { i32, i32 } %33, 0, !dbg !249
  %asmresult51 = extractvalue { i32, i32 } %33, 1, !dbg !249
  store i32 %asmresult, ptr %_eval_cycles_low, align 4, !dbg !249
  store i32 %asmresult51, ptr %_eval_cycles_high, align 4, !dbg !249
  %34 = load i32, ptr %_eval_cycles_high, align 4, !dbg !249
  %conv52 = zext i32 %34 to i64, !dbg !249
  %shl = shl i64 %conv52, 32, !dbg !249
  %35 = load i32, ptr %_eval_cycles_low, align 4, !dbg !249
  %conv53 = zext i32 %35 to i64, !dbg !249
  %or = or i64 %shl, %conv53, !dbg !249
  store i64 %or, ptr %tmp, align 8, !dbg !249
  %36 = load i64, ptr %tmp, align 8, !dbg !249
  store volatile i64 %36, ptr %start_time, align 8, !dbg !252
    #dbg_declare(ptr %encrypt_result, !253, !DIExpression(), !254)
  %37 = load ptr, ptr %ciphertext, align 8, !dbg !255
  %38 = load ptr, ptr %msg, align 8, !dbg !256
  %39 = load i64, ptr %msg_sz, align 8, !dbg !257
  %40 = load ptr, ptr %additional_data, align 8, !dbg !258
  %41 = load i64, ptr %additional_data_sz, align 8, !dbg !259
  %42 = load ptr, ptr %nonce, align 8, !dbg !260
  %43 = load ptr, ptr %key, align 8, !dbg !261
  %call54 = call i32 @crypto_aead_aes256gcm_encrypt(ptr noundef %37, ptr noundef %ciphertext_sz, ptr noundef %38, i64 noundef %39, ptr noundef %40, i64 noundef %41, ptr noundef null, ptr noundef %42, ptr noundef %43), !dbg !262
  store i32 %call54, ptr %encrypt_result, align 4, !dbg !254
    #dbg_declare(ptr %_eval_cycles_low55, !263, !DIExpression(), !265)
  store i32 0, ptr %_eval_cycles_low55, align 4, !dbg !265
    #dbg_declare(ptr %_eval_cycles_high56, !266, !DIExpression(), !265)
  store i32 0, ptr %_eval_cycles_high56, align 4, !dbg !265
  %44 = call { i32, i32 } asm sideeffect "rdtscp\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09cpuid\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !265, !srcloc !267
  %asmresult57 = extractvalue { i32, i32 } %44, 0, !dbg !265
  %asmresult58 = extractvalue { i32, i32 } %44, 1, !dbg !265
  store i32 %asmresult57, ptr %_eval_cycles_low55, align 4, !dbg !265
  store i32 %asmresult58, ptr %_eval_cycles_high56, align 4, !dbg !265
  %45 = load i32, ptr %_eval_cycles_high56, align 4, !dbg !265
  %conv60 = zext i32 %45 to i64, !dbg !265
  %shl61 = shl i64 %conv60, 32, !dbg !265
  %46 = load i32, ptr %_eval_cycles_low55, align 4, !dbg !265
  %conv62 = zext i32 %46 to i64, !dbg !265
  %or63 = or i64 %shl61, %conv62, !dbg !265
  store i64 %or63, ptr %tmp59, align 8, !dbg !265
  %47 = load i64, ptr %tmp59, align 8, !dbg !265
  store volatile i64 %47, ptr %end_time, align 8, !dbg !268
  %48 = load i32, ptr %encrypt_result, align 4, !dbg !269
  %cmp64 = icmp eq i32 -1, %48, !dbg !271
  br i1 %cmp64, label %if.then66, label %if.end68, !dbg !271

if.then66:                                        ; preds = %for.body
  %call67 = call i32 (ptr, ...) @printf(ptr noundef @.str.13), !dbg !272
  call void @exit(i32 noundef 0) #7, !dbg !274
  unreachable, !dbg !274

if.end68:                                         ; preds = %for.body
  %49 = load i32, ptr %cur_iter, align 4, !dbg !275
  %50 = load i32, ptr %num_warmup, align 4, !dbg !277
  %cmp69 = icmp sge i32 %49, %50, !dbg !278
  br i1 %cmp69, label %if.then71, label %if.end74, !dbg !278

if.then71:                                        ; preds = %if.end68
  %51 = load volatile i64, ptr %end_time, align 8, !dbg !279
  %52 = load volatile i64, ptr %start_time, align 8, !dbg !281
  %sub = sub i64 %51, %52, !dbg !282
  %53 = load ptr, ptr %times, align 8, !dbg !283
  %54 = load i32, ptr %cur_iter, align 4, !dbg !284
  %55 = load i32, ptr %num_warmup, align 4, !dbg !285
  %sub72 = sub nsw i32 %54, %55, !dbg !286
  %idxprom = sext i32 %sub72 to i64, !dbg !283
  %arrayidx73 = getelementptr inbounds i64, ptr %53, i64 %idxprom, !dbg !283
  store i64 %sub, ptr %arrayidx73, align 8, !dbg !287
  br label %if.end74, !dbg !288

if.end74:                                         ; preds = %if.then71, %if.end68
    #dbg_declare(ptr %decrypt_result, !289, !DIExpression(), !290)
  %56 = load ptr, ptr %decrypted_msg, align 8, !dbg !291
  %57 = load ptr, ptr %ciphertext, align 8, !dbg !292
  %58 = load i64, ptr %ciphertext_sz, align 8, !dbg !293
  %59 = load ptr, ptr %additional_data, align 8, !dbg !294
  %60 = load i64, ptr %additional_data_sz, align 8, !dbg !295
  %61 = load ptr, ptr %nonce, align 8, !dbg !296
  %62 = load ptr, ptr %key, align 8, !dbg !297
  %call75 = call i32 @crypto_aead_aes256gcm_decrypt(ptr noundef %56, ptr noundef %msg_sz, ptr noundef null, ptr noundef %57, i64 noundef %58, ptr noundef %59, i64 noundef %60, ptr noundef %61, ptr noundef %62), !dbg !298
  store i32 %call75, ptr %decrypt_result, align 4, !dbg !290
  %63 = load i32, ptr %decrypt_result, align 4, !dbg !299
  %cmp76 = icmp eq i32 -1, %63, !dbg !301
  br i1 %cmp76, label %if.then78, label %if.end80, !dbg !301

if.then78:                                        ; preds = %if.end74
  %call79 = call i32 (ptr, ...) @printf(ptr noundef @.str.14), !dbg !302
  call void @exit(i32 noundef 0) #7, !dbg !304
  unreachable, !dbg !304

if.end80:                                         ; preds = %if.end74
    #dbg_declare(ptr %cmp_result, !305, !DIExpression(), !306)
  %64 = load ptr, ptr %msg, align 8, !dbg !307
  %65 = load ptr, ptr %decrypted_msg, align 8, !dbg !308
  %66 = load i64, ptr %msg_sz, align 8, !dbg !309
  %call81 = call i32 @memcmp(ptr noundef %64, ptr noundef %65, i64 noundef %66) #9, !dbg !310
  store i32 %call81, ptr %cmp_result, align 4, !dbg !306
  %67 = load i32, ptr %cmp_result, align 4, !dbg !311
  %cmp82 = icmp ne i32 0, %67, !dbg !313
  br i1 %cmp82, label %if.then84, label %if.end86, !dbg !313

if.then84:                                        ; preds = %if.end80
  %call85 = call i32 (ptr, ...) @printf(ptr noundef @.str.15), !dbg !314
  call void @exit(i32 noundef 0) #7, !dbg !316
  unreachable, !dbg !316

if.end86:                                         ; preds = %if.end80
  br label %for.inc, !dbg !317

for.inc:                                          ; preds = %if.end86
  %68 = load i32, ptr %cur_iter, align 4, !dbg !318
  %inc = add nsw i32 %68, 1, !dbg !318
  store i32 %inc, ptr %cur_iter, align 4, !dbg !318
  br label %for.cond, !dbg !319, !llvm.loop !320

for.end:                                          ; preds = %for.cond
    #dbg_declare(ptr %ccounts_out, !323, !DIExpression(), !381)
  %69 = load ptr, ptr %argv.addr, align 8, !dbg !382
  %arrayidx87 = getelementptr inbounds ptr, ptr %69, i64 5, !dbg !382
  %70 = load ptr, ptr %arrayidx87, align 8, !dbg !382
  %call88 = call noalias ptr @fopen(ptr noundef %70, ptr noundef @.str.16), !dbg !383
  store ptr %call88, ptr %ccounts_out, align 8, !dbg !381
  %71 = load ptr, ptr %ccounts_out, align 8, !dbg !384
  %cmp89 = icmp ne ptr %71, null, !dbg !385
  br i1 %cmp89, label %land.lhs.true91, label %cond.false93, !dbg !386

land.lhs.true91:                                  ; preds = %for.end
  br i1 true, label %cond.true92, label %cond.false93, !dbg !387

cond.true92:                                      ; preds = %land.lhs.true91
  br label %cond.end94, !dbg !387

cond.false93:                                     ; preds = %land.lhs.true91, %for.end
  call void @__assert_fail(ptr noundef @.str.18, ptr noundef @.str.3, i32 noundef 175, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !387
  unreachable, !dbg !387

72:                                               ; No predecessors!
  br label %cond.end94, !dbg !387

cond.end94:                                       ; preds = %72, %cond.true92
  %73 = load ptr, ptr %ccounts_out, align 8, !dbg !388
  %74 = load i32, ptr %num_iter, align 4, !dbg !389
  %75 = load i32, ptr %num_warmup, align 4, !dbg !390
  %call95 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %73, ptr noundef @.str.19, i32 noundef %74, i32 noundef %75) #8, !dbg !391
    #dbg_declare(ptr %ii, !392, !DIExpression(), !394)
  store i32 0, ptr %ii, align 4, !dbg !394
  br label %for.cond96, !dbg !395

for.cond96:                                       ; preds = %for.inc103, %cond.end94
  %76 = load i32, ptr %ii, align 4, !dbg !396
  %77 = load i32, ptr %num_iter, align 4, !dbg !398
  %cmp97 = icmp slt i32 %76, %77, !dbg !399
  br i1 %cmp97, label %for.body99, label %for.end105, !dbg !400

for.body99:                                       ; preds = %for.cond96
  %78 = load ptr, ptr %ccounts_out, align 8, !dbg !401
  %79 = load ptr, ptr %times, align 8, !dbg !403
  %80 = load i32, ptr %ii, align 4, !dbg !404
  %idxprom100 = sext i32 %80 to i64, !dbg !403
  %arrayidx101 = getelementptr inbounds i64, ptr %79, i64 %idxprom100, !dbg !403
  %81 = load i64, ptr %arrayidx101, align 8, !dbg !403
  %call102 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %78, ptr noundef @.str.20, i64 noundef %81) #8, !dbg !405
  br label %for.inc103, !dbg !406

for.inc103:                                       ; preds = %for.body99
  %82 = load i32, ptr %ii, align 4, !dbg !407
  %inc104 = add nsw i32 %82, 1, !dbg !407
  store i32 %inc104, ptr %ii, align 4, !dbg !407
  br label %for.cond96, !dbg !408, !llvm.loop !409

for.end105:                                       ; preds = %for.cond96
  %83 = load ptr, ptr %ccounts_out, align 8, !dbg !411
  %call106 = call i32 @fclose(ptr noundef %83), !dbg !412
  %cmp107 = icmp ne i32 %call106, -1, !dbg !413
  br i1 %cmp107, label %land.lhs.true109, label %cond.false111, !dbg !414

land.lhs.true109:                                 ; preds = %for.end105
  br i1 true, label %cond.true110, label %cond.false111, !dbg !415

cond.true110:                                     ; preds = %land.lhs.true109
  br label %cond.end112, !dbg !415

cond.false111:                                    ; preds = %land.lhs.true109, %for.end105
  call void @__assert_fail(ptr noundef @.str.22, ptr noundef @.str.3, i32 noundef 184, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !415
  unreachable, !dbg !415

84:                                               ; No predecessors!
  br label %cond.end112, !dbg !415

cond.end112:                                      ; preds = %84, %cond.true110
  ret i32 0, !dbg !416
}

declare i32 @printf(ptr noundef, ...) #1

; Function Attrs: noreturn nounwind
declare void @exit(i32 noundef) #2

; Function Attrs: nounwind
declare i64 @strtol(ptr noundef, ptr noundef, i32 noundef) #3

; Function Attrs: nounwind willreturn memory(read)
declare i64 @strlen(ptr noundef) #4

declare i32 @sodium_init(...) #1

; Function Attrs: noreturn nounwind
declare void @__assert_fail(ptr noundef, ptr noundef, i32 noundef, ptr noundef) #2

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #5

declare i64 @crypto_aead_aes256gcm_abytes() #1

declare i64 @crypto_aead_aes256gcm_keybytes() #1

declare i64 @crypto_aead_aes256gcm_npubbytes() #1

; Function Attrs: nounwind allocsize(0,1)
declare noalias ptr @calloc(i64 noundef, i64 noundef) #6

declare i32 @crypto_aead_aes256gcm_keygen(ptr noundef) #1

declare void @randombytes_buf(ptr noundef, i64 noundef) #1

declare i32 @crypto_aead_aes256gcm_encrypt(ptr noundef, ptr noundef, ptr noundef, i64 noundef, ptr noundef, i64 noundef, ptr noundef, ptr noundef, ptr noundef) #1

declare i32 @crypto_aead_aes256gcm_decrypt(ptr noundef, ptr noundef, ptr noundef, ptr noundef, i64 noundef, ptr noundef, i64 noundef, ptr noundef, ptr noundef) #1

; Function Attrs: nounwind willreturn memory(read)
declare i32 @memcmp(ptr noundef, ptr noundef, i64 noundef) #4

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

!llvm.dbg.cu = !{!111}
!llvm.module.flags = !{!124, !125, !126, !127, !128, !129}
!llvm.ident = !{!130}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 69, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "eval_aesni256gcm_encrypt.c", directory: "/home/rgangar/Documents/llvm-data-independent-timing/micro-benchmarks", checksumkind: CSK_MD5, checksum: "3f05ebbbe3cf5e94421eee754fc39cf1")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1160, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 145)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 94, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 240, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 30)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 92, type: !14, isLocal: true, isDefinition: true)
!14 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1000, elements: !15)
!15 = !{!16}
!16 = !DISubrange(count: 125)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(scope: null, file: !2, line: 92, type: !19, isLocal: true, isDefinition: true)
!19 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 216, elements: !20)
!20 = !{!21}
!21 = !DISubrange(count: 27)
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(scope: null, file: !2, line: 92, type: !24, isLocal: true, isDefinition: true)
!24 = !DICompositeType(tag: DW_TAG_array_type, baseType: !25, size: 184, elements: !26)
!25 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!26 = !{!27}
!27 = !DISubrange(count: 23)
!28 = !DIGlobalVariableExpression(var: !29, expr: !DIExpression())
!29 = distinct !DIGlobalVariable(scope: null, file: !2, line: 106, type: !30, isLocal: true, isDefinition: true)
!30 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 544, elements: !31)
!31 = !{!32}
!32 = !DISubrange(count: 68)
!33 = !DIGlobalVariableExpression(var: !34, expr: !DIExpression())
!34 = distinct !DIGlobalVariable(scope: null, file: !2, line: 105, type: !35, isLocal: true, isDefinition: true)
!35 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 696, elements: !36)
!36 = !{!37}
!37 = !DISubrange(count: 87)
!38 = !DIGlobalVariableExpression(var: !39, expr: !DIExpression())
!39 = distinct !DIGlobalVariable(scope: null, file: !2, line: 111, type: !40, isLocal: true, isDefinition: true)
!40 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 520, elements: !41)
!41 = !{!42}
!42 = !DISubrange(count: 65)
!43 = !DIGlobalVariableExpression(var: !44, expr: !DIExpression())
!44 = distinct !DIGlobalVariable(scope: null, file: !2, line: 111, type: !45, isLocal: true, isDefinition: true)
!45 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 592, elements: !46)
!46 = !{!47}
!47 = !DISubrange(count: 74)
!48 = !DIGlobalVariableExpression(var: !49, expr: !DIExpression())
!49 = distinct !DIGlobalVariable(scope: null, file: !2, line: 115, type: !50, isLocal: true, isDefinition: true)
!50 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 464, elements: !51)
!51 = !{!52}
!52 = !DISubrange(count: 58)
!53 = !DIGlobalVariableExpression(var: !54, expr: !DIExpression())
!54 = distinct !DIGlobalVariable(scope: null, file: !2, line: 115, type: !55, isLocal: true, isDefinition: true)
!55 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 536, elements: !56)
!56 = !{!57}
!57 = !DISubrange(count: 67)
!58 = !DIGlobalVariableExpression(var: !59, expr: !DIExpression())
!59 = distinct !DIGlobalVariable(scope: null, file: !2, line: 119, type: !60, isLocal: true, isDefinition: true)
!60 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 552, elements: !61)
!61 = !{!62}
!62 = !DISubrange(count: 69)
!63 = !DIGlobalVariableExpression(var: !64, expr: !DIExpression())
!64 = distinct !DIGlobalVariable(scope: null, file: !2, line: 124, type: !65, isLocal: true, isDefinition: true)
!65 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 656, elements: !66)
!66 = !{!67}
!67 = !DISubrange(count: 82)
!68 = !DIGlobalVariableExpression(var: !69, expr: !DIExpression())
!69 = distinct !DIGlobalVariable(scope: null, file: !2, line: 123, type: !70, isLocal: true, isDefinition: true)
!70 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 744, elements: !71)
!71 = !{!72}
!72 = !DISubrange(count: 93)
!73 = !DIGlobalVariableExpression(var: !74, expr: !DIExpression())
!74 = distinct !DIGlobalVariable(scope: null, file: !2, line: 147, type: !45, isLocal: true, isDefinition: true)
!75 = !DIGlobalVariableExpression(var: !76, expr: !DIExpression())
!76 = distinct !DIGlobalVariable(scope: null, file: !2, line: 161, type: !45, isLocal: true, isDefinition: true)
!77 = !DIGlobalVariableExpression(var: !78, expr: !DIExpression())
!78 = distinct !DIGlobalVariable(scope: null, file: !2, line: 167, type: !79, isLocal: true, isDefinition: true)
!79 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 608, elements: !80)
!80 = !{!81}
!81 = !DISubrange(count: 76)
!82 = !DIGlobalVariableExpression(var: !83, expr: !DIExpression())
!83 = distinct !DIGlobalVariable(scope: null, file: !2, line: 174, type: !84, isLocal: true, isDefinition: true)
!84 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 16, elements: !85)
!85 = !{!86}
!86 = !DISubrange(count: 2)
!87 = !DIGlobalVariableExpression(var: !88, expr: !DIExpression())
!88 = distinct !DIGlobalVariable(scope: null, file: !2, line: 175, type: !89, isLocal: true, isDefinition: true)
!89 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 352, elements: !90)
!90 = !{!91}
!91 = !DISubrange(count: 44)
!92 = !DIGlobalVariableExpression(var: !93, expr: !DIExpression())
!93 = distinct !DIGlobalVariable(scope: null, file: !2, line: 175, type: !60, isLocal: true, isDefinition: true)
!94 = !DIGlobalVariableExpression(var: !95, expr: !DIExpression())
!95 = distinct !DIGlobalVariable(scope: null, file: !2, line: 178, type: !96, isLocal: true, isDefinition: true)
!96 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 488, elements: !97)
!97 = !{!98}
!98 = !DISubrange(count: 61)
!99 = !DIGlobalVariableExpression(var: !100, expr: !DIExpression())
!100 = distinct !DIGlobalVariable(scope: null, file: !2, line: 182, type: !101, isLocal: true, isDefinition: true)
!101 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !102)
!102 = !{!103}
!103 = !DISubrange(count: 5)
!104 = !DIGlobalVariableExpression(var: !105, expr: !DIExpression())
!105 = distinct !DIGlobalVariable(scope: null, file: !2, line: 184, type: !106, isLocal: true, isDefinition: true)
!106 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 264, elements: !107)
!107 = !{!108}
!108 = !DISubrange(count: 33)
!109 = !DIGlobalVariableExpression(var: !110, expr: !DIExpression())
!110 = distinct !DIGlobalVariable(scope: null, file: !2, line: 184, type: !40, isLocal: true, isDefinition: true)
!111 = distinct !DICompileUnit(language: DW_LANG_C11, file: !2, producer: "clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, retainedTypes: !112, globals: !123, splitDebugInlining: false, nameTableKind: None)
!112 = !{!113, !115, !116, !118}
!113 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !114, size: 64)
!114 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!115 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!116 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !117, size: 64)
!117 = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
!118 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint64_t", file: !119, line: 27, baseType: !120)
!119 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/stdint-uintn.h", directory: "", checksumkind: CSK_MD5, checksum: "2bf2ae53c58c01b1a1b9383b5195125c")
!120 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint64_t", file: !121, line: 45, baseType: !122)
!121 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types.h", directory: "", checksumkind: CSK_MD5, checksum: "d108b5f93a74c50510d7d9bc0ab36df9")
!122 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!123 = !{!0, !7, !12, !17, !22, !28, !33, !38, !43, !48, !53, !58, !63, !68, !73, !75, !77, !82, !87, !92, !94, !99, !104, !109}
!124 = !{i32 7, !"Dwarf Version", i32 5}
!125 = !{i32 2, !"Debug Info Version", i32 3}
!126 = !{i32 1, !"wchar_size", i32 4}
!127 = !{i32 8, !"PIC Level", i32 2}
!128 = !{i32 7, !"PIE Level", i32 2}
!129 = !{i32 7, !"uwtable", i32 2}
!130 = !{!"clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)"}
!131 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 66, type: !132, scopeLine: 67, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !111, retainedNodes: !135)
!132 = !DISubroutineType(types: !133)
!133 = !{!134, !134, !113}
!134 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!135 = !{}
!136 = !DILocalVariable(name: "argc", arg: 1, scope: !131, file: !2, line: 66, type: !134)
!137 = !DILocation(line: 66, column: 10, scope: !131)
!138 = !DILocalVariable(name: "argv", arg: 2, scope: !131, file: !2, line: 66, type: !113)
!139 = !DILocation(line: 66, column: 23, scope: !131)
!140 = !DILocation(line: 68, column: 7, scope: !141)
!141 = distinct !DILexicalBlock(scope: !131, file: !2, line: 68, column: 7)
!142 = !DILocation(line: 68, column: 12, scope: !141)
!143 = !DILocation(line: 71, column: 59, scope: !144)
!144 = distinct !DILexicalBlock(scope: !141, file: !2, line: 68, column: 29)
!145 = !DILocation(line: 69, column: 5, scope: !144)
!146 = !DILocation(line: 72, column: 5, scope: !144)
!147 = !DILocalVariable(name: "num_iter", scope: !131, file: !2, line: 76, type: !134)
!148 = !DILocation(line: 76, column: 7, scope: !131)
!149 = !DILocation(line: 76, column: 34, scope: !131)
!150 = !DILocation(line: 76, column: 18, scope: !131)
!151 = !DILocalVariable(name: "num_warmup", scope: !131, file: !2, line: 80, type: !134)
!152 = !DILocation(line: 80, column: 7, scope: !131)
!153 = !DILocation(line: 80, column: 36, scope: !131)
!154 = !DILocation(line: 80, column: 20, scope: !131)
!155 = !DILocalVariable(name: "msg", scope: !131, file: !2, line: 84, type: !116)
!156 = !DILocation(line: 84, column: 18, scope: !131)
!157 = !DILocation(line: 84, column: 40, scope: !131)
!158 = !DILocalVariable(name: "msg_sz", scope: !131, file: !2, line: 85, type: !159)
!159 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!160 = !DILocation(line: 85, column: 22, scope: !131)
!161 = !DILocation(line: 85, column: 38, scope: !131)
!162 = !DILocation(line: 85, column: 31, scope: !131)
!163 = !DILocalVariable(name: "additional_data_sz", scope: !131, file: !2, line: 86, type: !159)
!164 = !DILocation(line: 86, column: 22, scope: !131)
!165 = !DILocalVariable(name: "sodium_init_success", scope: !131, file: !2, line: 89, type: !166)
!166 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !134)
!167 = !DILocation(line: 89, column: 13, scope: !131)
!168 = !DILocalVariable(name: "sodium_already_initd", scope: !131, file: !2, line: 90, type: !166)
!169 = !DILocation(line: 90, column: 13, scope: !131)
!170 = !DILocalVariable(name: "sodium_init_result", scope: !131, file: !2, line: 91, type: !134)
!171 = !DILocation(line: 91, column: 7, scope: !131)
!172 = !DILocation(line: 91, column: 28, scope: !131)
!173 = !DILocation(line: 92, column: 34, scope: !131)
!174 = !DILocation(line: 92, column: 31, scope: !131)
!175 = !DILocation(line: 92, column: 53, scope: !131)
!176 = !DILocation(line: 93, column: 28, scope: !131)
!177 = !DILocation(line: 93, column: 25, scope: !131)
!178 = !DILocation(line: 93, column: 48, scope: !131)
!179 = !DILocation(line: 92, column: 3, scope: !131)
!180 = !DILocalVariable(name: "additional_data", scope: !131, file: !2, line: 100, type: !116)
!181 = !DILocation(line: 100, column: 18, scope: !131)
!182 = !DILocalVariable(name: "decrypted_msg", scope: !131, file: !2, line: 104, type: !116)
!183 = !DILocation(line: 104, column: 18, scope: !131)
!184 = !DILocation(line: 104, column: 41, scope: !131)
!185 = !DILocation(line: 104, column: 34, scope: !131)
!186 = !DILocation(line: 105, column: 10, scope: !131)
!187 = !DILocation(line: 105, column: 24, scope: !131)
!188 = !DILocation(line: 105, column: 3, scope: !131)
!189 = !DILocalVariable(name: "ciphertext_sz", scope: !131, file: !2, line: 109, type: !159)
!190 = !DILocation(line: 109, column: 22, scope: !131)
!191 = !DILocation(line: 109, column: 38, scope: !131)
!192 = !DILocation(line: 109, column: 47, scope: !131)
!193 = !DILocation(line: 109, column: 45, scope: !131)
!194 = !DILocalVariable(name: "ciphertext", scope: !131, file: !2, line: 110, type: !116)
!195 = !DILocation(line: 110, column: 18, scope: !131)
!196 = !DILocation(line: 110, column: 38, scope: !131)
!197 = !DILocation(line: 110, column: 31, scope: !131)
!198 = !DILocation(line: 111, column: 10, scope: !131)
!199 = !DILocation(line: 111, column: 14, scope: !131)
!200 = !DILocation(line: 111, column: 3, scope: !131)
!201 = !DILocalVariable(name: "key", scope: !131, file: !2, line: 114, type: !116)
!202 = !DILocation(line: 114, column: 18, scope: !131)
!203 = !DILocation(line: 114, column: 31, scope: !131)
!204 = !DILocation(line: 114, column: 24, scope: !131)
!205 = !DILocation(line: 115, column: 10, scope: !131)
!206 = !DILocation(line: 115, column: 14, scope: !131)
!207 = !DILocation(line: 115, column: 3, scope: !131)
!208 = !DILocalVariable(name: "nonce", scope: !131, file: !2, line: 118, type: !116)
!209 = !DILocation(line: 118, column: 18, scope: !131)
!210 = !DILocation(line: 118, column: 33, scope: !131)
!211 = !DILocation(line: 118, column: 26, scope: !131)
!212 = !DILocation(line: 119, column: 10, scope: !131)
!213 = !DILocation(line: 119, column: 16, scope: !131)
!214 = !DILocation(line: 119, column: 3, scope: !131)
!215 = !DILocalVariable(name: "times", scope: !131, file: !2, line: 122, type: !216)
!216 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !118, size: 64)
!217 = !DILocation(line: 122, column: 13, scope: !131)
!218 = !DILocation(line: 122, column: 28, scope: !131)
!219 = !DILocation(line: 122, column: 21, scope: !131)
!220 = !DILocation(line: 123, column: 10, scope: !131)
!221 = !DILocation(line: 123, column: 16, scope: !131)
!222 = !DILocation(line: 123, column: 3, scope: !131)
!223 = !DILocalVariable(name: "start_time", scope: !131, file: !2, line: 126, type: !224)
!224 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !118)
!225 = !DILocation(line: 126, column: 21, scope: !131)
!226 = !DILocalVariable(name: "end_time", scope: !131, file: !2, line: 127, type: !224)
!227 = !DILocation(line: 127, column: 21, scope: !131)
!228 = !DILocalVariable(name: "cur_iter", scope: !229, file: !2, line: 130, type: !134)
!229 = distinct !DILexicalBlock(scope: !131, file: !2, line: 130, column: 3)
!230 = !DILocation(line: 130, column: 12, scope: !229)
!231 = !DILocation(line: 130, column: 8, scope: !229)
!232 = !DILocation(line: 130, column: 26, scope: !233)
!233 = distinct !DILexicalBlock(scope: !229, file: !2, line: 130, column: 3)
!234 = !DILocation(line: 130, column: 37, scope: !233)
!235 = !DILocation(line: 130, column: 48, scope: !233)
!236 = !DILocation(line: 130, column: 46, scope: !233)
!237 = !DILocation(line: 130, column: 35, scope: !233)
!238 = !DILocation(line: 130, column: 3, scope: !229)
!239 = !DILocation(line: 133, column: 34, scope: !240)
!240 = distinct !DILexicalBlock(scope: !233, file: !2, line: 130, column: 72)
!241 = !DILocation(line: 133, column: 5, scope: !240)
!242 = !DILocation(line: 134, column: 21, scope: !240)
!243 = !DILocation(line: 134, column: 5, scope: !240)
!244 = !DILocalVariable(name: "_eval_cycles_low", scope: !245, file: !2, line: 137, type: !246)
!245 = distinct !DILexicalBlock(scope: !240, file: !2, line: 137, column: 18)
!246 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !119, line: 26, baseType: !247)
!247 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !121, line: 42, baseType: !248)
!248 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!249 = !DILocation(line: 137, column: 18, scope: !245)
!250 = !DILocalVariable(name: "_eval_cycles_high", scope: !245, file: !2, line: 137, type: !246)
!251 = !{i64 2147804841, i64 2147804849, i64 2147804874, i64 2147804919, i64 2147804960}
!252 = !DILocation(line: 137, column: 16, scope: !240)
!253 = !DILocalVariable(name: "encrypt_result", scope: !240, file: !2, line: 140, type: !134)
!254 = !DILocation(line: 140, column: 9, scope: !240)
!255 = !DILocation(line: 140, column: 56, scope: !240)
!256 = !DILocation(line: 141, column: 11, scope: !240)
!257 = !DILocation(line: 141, column: 16, scope: !240)
!258 = !DILocation(line: 141, column: 24, scope: !240)
!259 = !DILocation(line: 141, column: 41, scope: !240)
!260 = !DILocation(line: 141, column: 67, scope: !240)
!261 = !DILocation(line: 141, column: 74, scope: !240)
!262 = !DILocation(line: 140, column: 26, scope: !240)
!263 = !DILocalVariable(name: "_eval_cycles_low", scope: !264, file: !2, line: 144, type: !246)
!264 = distinct !DILexicalBlock(scope: !240, file: !2, line: 144, column: 16)
!265 = !DILocation(line: 144, column: 16, scope: !264)
!266 = !DILocalVariable(name: "_eval_cycles_high", scope: !264, file: !2, line: 144, type: !246)
!267 = !{i64 2147805350, i64 2147805359, i64 2147805459, i64 2147805539, i64 2147805601}
!268 = !DILocation(line: 144, column: 14, scope: !240)
!269 = !DILocation(line: 146, column: 15, scope: !270)
!270 = distinct !DILexicalBlock(scope: !240, file: !2, line: 146, column: 9)
!271 = !DILocation(line: 146, column: 12, scope: !270)
!272 = !DILocation(line: 147, column: 7, scope: !273)
!273 = distinct !DILexicalBlock(scope: !270, file: !2, line: 146, column: 31)
!274 = !DILocation(line: 148, column: 7, scope: !273)
!275 = !DILocation(line: 151, column: 9, scope: !276)
!276 = distinct !DILexicalBlock(scope: !240, file: !2, line: 151, column: 9)
!277 = !DILocation(line: 151, column: 21, scope: !276)
!278 = !DILocation(line: 151, column: 18, scope: !276)
!279 = !DILocation(line: 152, column: 38, scope: !280)
!280 = distinct !DILexicalBlock(scope: !276, file: !2, line: 151, column: 33)
!281 = !DILocation(line: 152, column: 49, scope: !280)
!282 = !DILocation(line: 152, column: 47, scope: !280)
!283 = !DILocation(line: 152, column: 7, scope: !280)
!284 = !DILocation(line: 152, column: 13, scope: !280)
!285 = !DILocation(line: 152, column: 24, scope: !280)
!286 = !DILocation(line: 152, column: 22, scope: !280)
!287 = !DILocation(line: 152, column: 36, scope: !280)
!288 = !DILocation(line: 153, column: 5, scope: !280)
!289 = !DILocalVariable(name: "decrypt_result", scope: !240, file: !2, line: 156, type: !134)
!290 = !DILocation(line: 156, column: 9, scope: !240)
!291 = !DILocation(line: 156, column: 56, scope: !240)
!292 = !DILocation(line: 157, column: 17, scope: !240)
!293 = !DILocation(line: 157, column: 29, scope: !240)
!294 = !DILocation(line: 157, column: 44, scope: !240)
!295 = !DILocation(line: 157, column: 61, scope: !240)
!296 = !DILocation(line: 158, column: 11, scope: !240)
!297 = !DILocation(line: 158, column: 18, scope: !240)
!298 = !DILocation(line: 156, column: 26, scope: !240)
!299 = !DILocation(line: 160, column: 15, scope: !300)
!300 = distinct !DILexicalBlock(scope: !240, file: !2, line: 160, column: 9)
!301 = !DILocation(line: 160, column: 12, scope: !300)
!302 = !DILocation(line: 161, column: 7, scope: !303)
!303 = distinct !DILexicalBlock(scope: !300, file: !2, line: 160, column: 31)
!304 = !DILocation(line: 162, column: 7, scope: !303)
!305 = !DILocalVariable(name: "cmp_result", scope: !240, file: !2, line: 165, type: !134)
!306 = !DILocation(line: 165, column: 9, scope: !240)
!307 = !DILocation(line: 165, column: 29, scope: !240)
!308 = !DILocation(line: 165, column: 34, scope: !240)
!309 = !DILocation(line: 165, column: 49, scope: !240)
!310 = !DILocation(line: 165, column: 22, scope: !240)
!311 = !DILocation(line: 166, column: 14, scope: !312)
!312 = distinct !DILexicalBlock(scope: !240, file: !2, line: 166, column: 9)
!313 = !DILocation(line: 166, column: 11, scope: !312)
!314 = !DILocation(line: 167, column: 7, scope: !315)
!315 = distinct !DILexicalBlock(scope: !312, file: !2, line: 166, column: 26)
!316 = !DILocation(line: 168, column: 7, scope: !315)
!317 = !DILocation(line: 170, column: 3, scope: !240)
!318 = !DILocation(line: 130, column: 60, scope: !233)
!319 = !DILocation(line: 130, column: 3, scope: !233)
!320 = distinct !{!320, !238, !321, !322}
!321 = !DILocation(line: 170, column: 3, scope: !229)
!322 = !{!"llvm.loop.mustprogress"}
!323 = !DILocalVariable(name: "ccounts_out", scope: !131, file: !2, line: 174, type: !324)
!324 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !325, size: 64)
!325 = !DIDerivedType(tag: DW_TAG_typedef, name: "FILE", file: !326, line: 7, baseType: !327)
!326 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "571f9fb6223c42439075fdde11a0de5d")
!327 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_FILE", file: !328, line: 49, size: 1728, elements: !329)
!328 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "1bad07471b7974df4ecc1d1c2ca207e6")
!329 = !{!330, !331, !332, !333, !334, !335, !336, !337, !338, !339, !340, !341, !342, !345, !347, !348, !349, !352, !354, !356, !360, !363, !365, !368, !371, !372, !373, !376, !377}
!330 = !DIDerivedType(tag: DW_TAG_member, name: "_flags", scope: !327, file: !328, line: 51, baseType: !134, size: 32)
!331 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_ptr", scope: !327, file: !328, line: 54, baseType: !114, size: 64, offset: 64)
!332 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_end", scope: !327, file: !328, line: 55, baseType: !114, size: 64, offset: 128)
!333 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_base", scope: !327, file: !328, line: 56, baseType: !114, size: 64, offset: 192)
!334 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_base", scope: !327, file: !328, line: 57, baseType: !114, size: 64, offset: 256)
!335 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_ptr", scope: !327, file: !328, line: 58, baseType: !114, size: 64, offset: 320)
!336 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_end", scope: !327, file: !328, line: 59, baseType: !114, size: 64, offset: 384)
!337 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_base", scope: !327, file: !328, line: 60, baseType: !114, size: 64, offset: 448)
!338 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_end", scope: !327, file: !328, line: 61, baseType: !114, size: 64, offset: 512)
!339 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_base", scope: !327, file: !328, line: 64, baseType: !114, size: 64, offset: 576)
!340 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_backup_base", scope: !327, file: !328, line: 65, baseType: !114, size: 64, offset: 640)
!341 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_end", scope: !327, file: !328, line: 66, baseType: !114, size: 64, offset: 704)
!342 = !DIDerivedType(tag: DW_TAG_member, name: "_markers", scope: !327, file: !328, line: 68, baseType: !343, size: 64, offset: 768)
!343 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !344, size: 64)
!344 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_marker", file: !328, line: 36, flags: DIFlagFwdDecl)
!345 = !DIDerivedType(tag: DW_TAG_member, name: "_chain", scope: !327, file: !328, line: 70, baseType: !346, size: 64, offset: 832)
!346 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !327, size: 64)
!347 = !DIDerivedType(tag: DW_TAG_member, name: "_fileno", scope: !327, file: !328, line: 72, baseType: !134, size: 32, offset: 896)
!348 = !DIDerivedType(tag: DW_TAG_member, name: "_flags2", scope: !327, file: !328, line: 73, baseType: !134, size: 32, offset: 928)
!349 = !DIDerivedType(tag: DW_TAG_member, name: "_old_offset", scope: !327, file: !328, line: 74, baseType: !350, size: 64, offset: 960)
!350 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off_t", file: !121, line: 152, baseType: !351)
!351 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!352 = !DIDerivedType(tag: DW_TAG_member, name: "_cur_column", scope: !327, file: !328, line: 77, baseType: !353, size: 16, offset: 1024)
!353 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!354 = !DIDerivedType(tag: DW_TAG_member, name: "_vtable_offset", scope: !327, file: !328, line: 78, baseType: !355, size: 8, offset: 1040)
!355 = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
!356 = !DIDerivedType(tag: DW_TAG_member, name: "_shortbuf", scope: !327, file: !328, line: 79, baseType: !357, size: 8, offset: 1048)
!357 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 8, elements: !358)
!358 = !{!359}
!359 = !DISubrange(count: 1)
!360 = !DIDerivedType(tag: DW_TAG_member, name: "_lock", scope: !327, file: !328, line: 81, baseType: !361, size: 64, offset: 1088)
!361 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !362, size: 64)
!362 = !DIDerivedType(tag: DW_TAG_typedef, name: "_IO_lock_t", file: !328, line: 43, baseType: null)
!363 = !DIDerivedType(tag: DW_TAG_member, name: "_offset", scope: !327, file: !328, line: 89, baseType: !364, size: 64, offset: 1152)
!364 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off64_t", file: !121, line: 153, baseType: !351)
!365 = !DIDerivedType(tag: DW_TAG_member, name: "_codecvt", scope: !327, file: !328, line: 91, baseType: !366, size: 64, offset: 1216)
!366 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !367, size: 64)
!367 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_codecvt", file: !328, line: 37, flags: DIFlagFwdDecl)
!368 = !DIDerivedType(tag: DW_TAG_member, name: "_wide_data", scope: !327, file: !328, line: 92, baseType: !369, size: 64, offset: 1280)
!369 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !370, size: 64)
!370 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_wide_data", file: !328, line: 38, flags: DIFlagFwdDecl)
!371 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_list", scope: !327, file: !328, line: 93, baseType: !346, size: 64, offset: 1344)
!372 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_buf", scope: !327, file: !328, line: 94, baseType: !115, size: 64, offset: 1408)
!373 = !DIDerivedType(tag: DW_TAG_member, name: "__pad5", scope: !327, file: !328, line: 95, baseType: !374, size: 64, offset: 1472)
!374 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !375, line: 18, baseType: !122)
!375 = !DIFile(filename: "build/lib/clang/22/include/__stddef_size_t.h", directory: "/home/rgangar/Documents/llvm-data-independent-timing", checksumkind: CSK_MD5, checksum: "2c44e821a2b1951cde2eb0fb2e656867")
!376 = !DIDerivedType(tag: DW_TAG_member, name: "_mode", scope: !327, file: !328, line: 96, baseType: !134, size: 32, offset: 1536)
!377 = !DIDerivedType(tag: DW_TAG_member, name: "_unused2", scope: !327, file: !328, line: 98, baseType: !378, size: 160, offset: 1568)
!378 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !379)
!379 = !{!380}
!380 = !DISubrange(count: 20)
!381 = !DILocation(line: 174, column: 9, scope: !131)
!382 = !DILocation(line: 174, column: 29, scope: !131)
!383 = !DILocation(line: 174, column: 23, scope: !131)
!384 = !DILocation(line: 175, column: 10, scope: !131)
!385 = !DILocation(line: 175, column: 22, scope: !131)
!386 = !DILocation(line: 175, column: 30, scope: !131)
!387 = !DILocation(line: 175, column: 3, scope: !131)
!388 = !DILocation(line: 177, column: 11, scope: !131)
!389 = !DILocation(line: 179, column: 4, scope: !131)
!390 = !DILocation(line: 179, column: 14, scope: !131)
!391 = !DILocation(line: 177, column: 3, scope: !131)
!392 = !DILocalVariable(name: "ii", scope: !393, file: !2, line: 181, type: !134)
!393 = distinct !DILexicalBlock(scope: !131, file: !2, line: 181, column: 3)
!394 = !DILocation(line: 181, column: 12, scope: !393)
!395 = !DILocation(line: 181, column: 8, scope: !393)
!396 = !DILocation(line: 181, column: 20, scope: !397)
!397 = distinct !DILexicalBlock(scope: !393, file: !2, line: 181, column: 3)
!398 = !DILocation(line: 181, column: 25, scope: !397)
!399 = !DILocation(line: 181, column: 23, scope: !397)
!400 = !DILocation(line: 181, column: 3, scope: !393)
!401 = !DILocation(line: 182, column: 12, scope: !402)
!402 = distinct !DILexicalBlock(scope: !397, file: !2, line: 181, column: 41)
!403 = !DILocation(line: 182, column: 42, scope: !402)
!404 = !DILocation(line: 182, column: 48, scope: !402)
!405 = !DILocation(line: 182, column: 4, scope: !402)
!406 = !DILocation(line: 183, column: 3, scope: !402)
!407 = !DILocation(line: 181, column: 35, scope: !397)
!408 = !DILocation(line: 181, column: 3, scope: !397)
!409 = distinct !{!409, !400, !410, !322}
!410 = !DILocation(line: 183, column: 3, scope: !393)
!411 = !DILocation(line: 184, column: 17, scope: !131)
!412 = !DILocation(line: 184, column: 10, scope: !131)
!413 = !DILocation(line: 184, column: 30, scope: !131)
!414 = !DILocation(line: 184, column: 37, scope: !131)
!415 = !DILocation(line: 184, column: 3, scope: !131)
!416 = !DILocation(line: 193, column: 3, scope: !131)
