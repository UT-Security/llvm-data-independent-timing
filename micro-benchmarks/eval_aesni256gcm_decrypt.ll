; ModuleID = 'eval_aesni256gcm_decrypt.c'
source_filename = "eval_aesni256gcm_decrypt.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [146 x i8] c"Usage: %s <num_benchmark_iterations> <num_warmup_iterations> <messagee> <size_of_associated_data> <cycle_counts_file> [<dynamic_hitcounts_file>]\0A\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [30 x i8] c"Error initializing lib sodium\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [125 x i8] c"(sodium_init_success == sodium_init_result || sodium_already_initd == sodium_init_result) && \22Error initializing lib sodium\22\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [27 x i8] c"eval_aesni256gcm_decrypt.c\00", align 1, !dbg !17
@__PRETTY_FUNCTION__.main = private unnamed_addr constant [23 x i8] c"int main(int, char **)\00", align 1, !dbg !22
@.str.4 = private unnamed_addr constant [68 x i8] c"Couldn't allocate decrypted_msg bytes in eval_aesni256gcm_decrypt.c\00", align 1, !dbg !28
@.str.5 = private unnamed_addr constant [87 x i8] c"decrypted_msg && \22Couldn't allocate decrypted_msg bytes in eval_aesni256gcm_decrypt.c\22\00", align 1, !dbg !33
@.str.6 = private unnamed_addr constant [65 x i8] c"Couldn't allocate ciphertext bytes in eval_aesni256gcm_decrypt.c\00", align 1, !dbg !38
@.str.7 = private unnamed_addr constant [74 x i8] c"msg && \22Couldn't allocate ciphertext bytes in eval_aesni256gcm_decrypt.c\22\00", align 1, !dbg !43
@.str.8 = private unnamed_addr constant [58 x i8] c"Couldn't allocate key bytes in eval_aesni256gcm_decrypt.c\00", align 1, !dbg !48
@.str.9 = private unnamed_addr constant [67 x i8] c"key && \22Couldn't allocate key bytes in eval_aesni256gcm_decrypt.c\22\00", align 1, !dbg !53
@.str.10 = private unnamed_addr constant [69 x i8] c"nonce && \22Couldn't allocate key bytes in eval_aesni256gcm_decrypt.c\22\00", align 1, !dbg !58
@.str.11 = private unnamed_addr constant [74 x i8] c"Couldn't allocate array for benchmark times in eval_aesni256gcm_decrypt.c\00", align 1, !dbg !63
@.str.12 = private unnamed_addr constant [85 x i8] c"times && \22Couldn't allocate array for benchmark times in eval_aesni256gcm_decrypt.c\22\00", align 1, !dbg !65
@.str.13 = private unnamed_addr constant [74 x i8] c"FAILURE: eval_aesni256gcm_decrypt failed at crypto_aead_aes256gcm_encrypt\00", align 1, !dbg !70
@.str.14 = private unnamed_addr constant [74 x i8] c"FAILURE: eval_aesni256gcm_decrypt failed at crypto_aead_aes256gcm_decrypt\00", align 1, !dbg !72
@.str.15 = private unnamed_addr constant [76 x i8] c"FAILURE: eval_aesni256gcm_decrypt failed sanity check, decrypted msg != msg\00", align 1, !dbg !74
@.str.16 = private unnamed_addr constant [2 x i8] c"w\00", align 1, !dbg !79
@.str.17 = private unnamed_addr constant [44 x i8] c"Couldn't open cycle counts file for writing\00", align 1, !dbg !84
@.str.18 = private unnamed_addr constant [69 x i8] c"ccounts_out != NULL && \22Couldn't open cycle counts file for writing\22\00", align 1, !dbg !89
@.str.19 = private unnamed_addr constant [61 x i8] c"aesni256gcm_decrypt cycle counts (%d iterations, %d warmup)\0A\00", align 1, !dbg !91
@.str.20 = private unnamed_addr constant [5 x i8] c"%lu\0A\00", align 1, !dbg !96
@.str.21 = private unnamed_addr constant [33 x i8] c"Couldn't close cycle counts file\00", align 1, !dbg !101
@.str.22 = private unnamed_addr constant [65 x i8] c"fclose(ccounts_out) != EOF && \22Couldn't close cycle counts file\22\00", align 1, !dbg !106

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %argc, ptr noundef %argv) #0 !dbg !128 {
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
  %encrypt_result = alloca i32, align 4
  %_eval_cycles_low = alloca i32, align 4
  %_eval_cycles_high = alloca i32, align 4
  %tmp = alloca i64, align 8
  %decrypt_result = alloca i32, align 4
  %_eval_cycles_low61 = alloca i32, align 4
  %_eval_cycles_high62 = alloca i32, align 4
  %tmp65 = alloca i64, align 8
  %cmp_result = alloca i32, align 4
  %ccounts_out = alloca ptr, align 8
  %ii = alloca i32, align 4
  store i32 0, ptr %retval, align 4
  store i32 %argc, ptr %argc.addr, align 4
    #dbg_declare(ptr %argc.addr, !133, !DIExpression(), !134)
  store ptr %argv, ptr %argv.addr, align 8
    #dbg_declare(ptr %argv.addr, !135, !DIExpression(), !136)
  %0 = load i32, ptr %argc.addr, align 4, !dbg !137
  %cmp = icmp slt i32 %0, 6, !dbg !139
  br i1 %cmp, label %if.then, label %if.end, !dbg !139

if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %argv.addr, align 8, !dbg !140
  %arrayidx = getelementptr inbounds ptr, ptr %1, i64 0, !dbg !140
  %2 = load ptr, ptr %arrayidx, align 8, !dbg !140
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str, ptr noundef %2), !dbg !142
  call void @exit(i32 noundef -1) #7, !dbg !143
  unreachable, !dbg !143

if.end:                                           ; preds = %entry
    #dbg_declare(ptr %num_iter, !144, !DIExpression(), !145)
  %3 = load ptr, ptr %argv.addr, align 8, !dbg !146
  %arrayidx1 = getelementptr inbounds ptr, ptr %3, i64 1, !dbg !146
  %4 = load ptr, ptr %arrayidx1, align 8, !dbg !146
  %call2 = call i64 @strtol(ptr noundef %4, ptr noundef null, i32 noundef 10) #8, !dbg !147
  %conv = trunc i64 %call2 to i32, !dbg !147
  store i32 %conv, ptr %num_iter, align 4, !dbg !145
    #dbg_declare(ptr %num_warmup, !148, !DIExpression(), !149)
  %5 = load ptr, ptr %argv.addr, align 8, !dbg !150
  %arrayidx3 = getelementptr inbounds ptr, ptr %5, i64 2, !dbg !150
  %6 = load ptr, ptr %arrayidx3, align 8, !dbg !150
  %call4 = call i64 @strtol(ptr noundef %6, ptr noundef null, i32 noundef 10) #8, !dbg !151
  %conv5 = trunc i64 %call4 to i32, !dbg !151
  store i32 %conv5, ptr %num_warmup, align 4, !dbg !149
    #dbg_declare(ptr %msg, !152, !DIExpression(), !153)
  %7 = load ptr, ptr %argv.addr, align 8, !dbg !154
  %arrayidx6 = getelementptr inbounds ptr, ptr %7, i64 3, !dbg !154
  %8 = load ptr, ptr %arrayidx6, align 8, !dbg !154
  store ptr %8, ptr %msg, align 8, !dbg !153
    #dbg_declare(ptr %msg_sz, !155, !DIExpression(), !157)
  %9 = load ptr, ptr %argv.addr, align 8, !dbg !158
  %arrayidx7 = getelementptr inbounds ptr, ptr %9, i64 3, !dbg !158
  %10 = load ptr, ptr %arrayidx7, align 8, !dbg !158
  %call8 = call i64 @strlen(ptr noundef %10) #9, !dbg !159
  store i64 %call8, ptr %msg_sz, align 8, !dbg !157
    #dbg_declare(ptr %additional_data_sz, !160, !DIExpression(), !161)
  store i64 0, ptr %additional_data_sz, align 8, !dbg !161
    #dbg_declare(ptr %sodium_init_success, !162, !DIExpression(), !164)
  store i32 0, ptr %sodium_init_success, align 4, !dbg !164
    #dbg_declare(ptr %sodium_already_initd, !165, !DIExpression(), !166)
  store i32 1, ptr %sodium_already_initd, align 4, !dbg !166
    #dbg_declare(ptr %sodium_init_result, !167, !DIExpression(), !168)
  %call9 = call i32 (...) @sodium_init(), !dbg !169
  store i32 %call9, ptr %sodium_init_result, align 4, !dbg !168
  %11 = load i32, ptr %sodium_init_result, align 4, !dbg !170
  %cmp10 = icmp eq i32 0, %11, !dbg !171
  br i1 %cmp10, label %land.lhs.true, label %lor.lhs.false, !dbg !172

lor.lhs.false:                                    ; preds = %if.end
  %12 = load i32, ptr %sodium_init_result, align 4, !dbg !173
  %cmp12 = icmp eq i32 1, %12, !dbg !174
  br i1 %cmp12, label %land.lhs.true, label %cond.false, !dbg !175

land.lhs.true:                                    ; preds = %lor.lhs.false, %if.end
  br i1 true, label %cond.true, label %cond.false, !dbg !176

cond.true:                                        ; preds = %land.lhs.true
  br label %cond.end, !dbg !176

cond.false:                                       ; preds = %land.lhs.true, %lor.lhs.false
  call void @__assert_fail(ptr noundef @.str.2, ptr noundef @.str.3, i32 noundef 94, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !176
  unreachable, !dbg !176

13:                                               ; No predecessors!
  br label %cond.end, !dbg !176

cond.end:                                         ; preds = %13, %cond.true
    #dbg_declare(ptr %additional_data, !177, !DIExpression(), !178)
  store ptr null, ptr %additional_data, align 8, !dbg !178
    #dbg_declare(ptr %decrypted_msg, !179, !DIExpression(), !180)
  %14 = load i64, ptr %msg_sz, align 8, !dbg !181
  %call14 = call noalias ptr @malloc(i64 noundef %14) #10, !dbg !182
  store ptr %call14, ptr %decrypted_msg, align 8, !dbg !180
  %15 = load ptr, ptr %decrypted_msg, align 8, !dbg !183
  %tobool = icmp ne ptr %15, null, !dbg !183
  br i1 %tobool, label %land.lhs.true15, label %cond.false17, !dbg !184

land.lhs.true15:                                  ; preds = %cond.end
  br i1 true, label %cond.true16, label %cond.false17, !dbg !185

cond.true16:                                      ; preds = %land.lhs.true15
  br label %cond.end18, !dbg !185

cond.false17:                                     ; preds = %land.lhs.true15, %cond.end
  call void @__assert_fail(ptr noundef @.str.5, ptr noundef @.str.3, i32 noundef 105, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !185
  unreachable, !dbg !185

16:                                               ; No predecessors!
  br label %cond.end18, !dbg !185

cond.end18:                                       ; preds = %16, %cond.true16
    #dbg_declare(ptr %ciphertext_sz, !186, !DIExpression(), !187)
  %17 = load i64, ptr %msg_sz, align 8, !dbg !188
  %call19 = call i64 @crypto_aead_aes256gcm_abytes(), !dbg !189
  %add = add i64 %17, %call19, !dbg !190
  store i64 %add, ptr %ciphertext_sz, align 8, !dbg !187
    #dbg_declare(ptr %ciphertext, !191, !DIExpression(), !192)
  %18 = load i64, ptr %ciphertext_sz, align 8, !dbg !193
  %call20 = call noalias ptr @malloc(i64 noundef %18) #10, !dbg !194
  store ptr %call20, ptr %ciphertext, align 8, !dbg !192
  %19 = load ptr, ptr %msg, align 8, !dbg !195
  %tobool21 = icmp ne ptr %19, null, !dbg !195
  br i1 %tobool21, label %land.lhs.true22, label %cond.false24, !dbg !196

land.lhs.true22:                                  ; preds = %cond.end18
  br i1 true, label %cond.true23, label %cond.false24, !dbg !197

cond.true23:                                      ; preds = %land.lhs.true22
  br label %cond.end25, !dbg !197

cond.false24:                                     ; preds = %land.lhs.true22, %cond.end18
  call void @__assert_fail(ptr noundef @.str.7, ptr noundef @.str.3, i32 noundef 110, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !197
  unreachable, !dbg !197

20:                                               ; No predecessors!
  br label %cond.end25, !dbg !197

cond.end25:                                       ; preds = %20, %cond.true23
    #dbg_declare(ptr %key, !198, !DIExpression(), !199)
  %call26 = call i64 @crypto_aead_aes256gcm_keybytes(), !dbg !200
  %call27 = call noalias ptr @malloc(i64 noundef %call26) #10, !dbg !201
  store ptr %call27, ptr %key, align 8, !dbg !199
  %21 = load ptr, ptr %key, align 8, !dbg !202
  %tobool28 = icmp ne ptr %21, null, !dbg !202
  br i1 %tobool28, label %land.lhs.true29, label %cond.false31, !dbg !203

land.lhs.true29:                                  ; preds = %cond.end25
  br i1 true, label %cond.true30, label %cond.false31, !dbg !204

cond.true30:                                      ; preds = %land.lhs.true29
  br label %cond.end32, !dbg !204

cond.false31:                                     ; preds = %land.lhs.true29, %cond.end25
  call void @__assert_fail(ptr noundef @.str.9, ptr noundef @.str.3, i32 noundef 114, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !204
  unreachable, !dbg !204

22:                                               ; No predecessors!
  br label %cond.end32, !dbg !204

cond.end32:                                       ; preds = %22, %cond.true30
    #dbg_declare(ptr %nonce, !205, !DIExpression(), !206)
  %call33 = call i64 @crypto_aead_aes256gcm_npubbytes(), !dbg !207
  %call34 = call noalias ptr @malloc(i64 noundef %call33) #10, !dbg !208
  store ptr %call34, ptr %nonce, align 8, !dbg !206
  %23 = load ptr, ptr %nonce, align 8, !dbg !209
  %tobool35 = icmp ne ptr %23, null, !dbg !209
  br i1 %tobool35, label %land.lhs.true36, label %cond.false38, !dbg !210

land.lhs.true36:                                  ; preds = %cond.end32
  br i1 true, label %cond.true37, label %cond.false38, !dbg !211

cond.true37:                                      ; preds = %land.lhs.true36
  br label %cond.end39, !dbg !211

cond.false38:                                     ; preds = %land.lhs.true36, %cond.end32
  call void @__assert_fail(ptr noundef @.str.10, ptr noundef @.str.3, i32 noundef 118, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !211
  unreachable, !dbg !211

24:                                               ; No predecessors!
  br label %cond.end39, !dbg !211

cond.end39:                                       ; preds = %24, %cond.true37
    #dbg_declare(ptr %times, !212, !DIExpression(), !214)
  %25 = load i32, ptr %num_iter, align 4, !dbg !215
  %conv40 = sext i32 %25 to i64, !dbg !215
  %call41 = call noalias ptr @calloc(i64 noundef %conv40, i64 noundef 8) #11, !dbg !216
  store ptr %call41, ptr %times, align 8, !dbg !214
  %26 = load ptr, ptr %times, align 8, !dbg !217
  %tobool42 = icmp ne ptr %26, null, !dbg !217
  br i1 %tobool42, label %land.lhs.true43, label %cond.false45, !dbg !218

land.lhs.true43:                                  ; preds = %cond.end39
  br i1 true, label %cond.true44, label %cond.false45, !dbg !219

cond.true44:                                      ; preds = %land.lhs.true43
  br label %cond.end46, !dbg !219

cond.false45:                                     ; preds = %land.lhs.true43, %cond.end39
  call void @__assert_fail(ptr noundef @.str.12, ptr noundef @.str.3, i32 noundef 123, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !219
  unreachable, !dbg !219

27:                                               ; No predecessors!
  br label %cond.end46, !dbg !219

cond.end46:                                       ; preds = %27, %cond.true44
    #dbg_declare(ptr %start_time, !220, !DIExpression(), !222)
  store volatile i64 0, ptr %start_time, align 8, !dbg !222
    #dbg_declare(ptr %end_time, !223, !DIExpression(), !224)
  store volatile i64 0, ptr %end_time, align 8, !dbg !224
    #dbg_declare(ptr %cur_iter, !225, !DIExpression(), !227)
  store i32 0, ptr %cur_iter, align 4, !dbg !227
  br label %for.cond, !dbg !228

for.cond:                                         ; preds = %for.inc, %cond.end46
  %28 = load i32, ptr %cur_iter, align 4, !dbg !229
  %29 = load i32, ptr %num_iter, align 4, !dbg !231
  %30 = load i32, ptr %num_warmup, align 4, !dbg !232
  %add47 = add nsw i32 %29, %30, !dbg !233
  %cmp48 = icmp slt i32 %28, %add47, !dbg !234
  br i1 %cmp48, label %for.body, label %for.end, !dbg !235

for.body:                                         ; preds = %for.cond
  %31 = load ptr, ptr %key, align 8, !dbg !236
  %call50 = call i32 @crypto_aead_aes256gcm_keygen(ptr noundef %31), !dbg !238
  %32 = load ptr, ptr %nonce, align 8, !dbg !239
  call void @randombytes_buf(ptr noundef %32, i64 noundef 8), !dbg !240
    #dbg_declare(ptr %encrypt_result, !241, !DIExpression(), !242)
  %33 = load ptr, ptr %ciphertext, align 8, !dbg !243
  %34 = load ptr, ptr %msg, align 8, !dbg !244
  %35 = load i64, ptr %msg_sz, align 8, !dbg !245
  %36 = load ptr, ptr %additional_data, align 8, !dbg !246
  %37 = load i64, ptr %additional_data_sz, align 8, !dbg !247
  %38 = load ptr, ptr %nonce, align 8, !dbg !248
  %39 = load ptr, ptr %key, align 8, !dbg !249
  %call51 = call i32 @crypto_aead_aes256gcm_encrypt(ptr noundef %33, ptr noundef %ciphertext_sz, ptr noundef %34, i64 noundef %35, ptr noundef %36, i64 noundef %37, ptr noundef null, ptr noundef %38, ptr noundef %39), !dbg !250
  store i32 %call51, ptr %encrypt_result, align 4, !dbg !242
  %40 = load i32, ptr %encrypt_result, align 4, !dbg !251
  %cmp52 = icmp eq i32 -1, %40, !dbg !253
  br i1 %cmp52, label %if.then54, label %if.end56, !dbg !253

if.then54:                                        ; preds = %for.body
  %call55 = call i32 (ptr, ...) @printf(ptr noundef @.str.13), !dbg !254
  call void @exit(i32 noundef 0) #7, !dbg !256
  unreachable, !dbg !256

if.end56:                                         ; preds = %for.body
    #dbg_declare(ptr %_eval_cycles_low, !257, !DIExpression(), !262)
  store i32 0, ptr %_eval_cycles_low, align 4, !dbg !262
    #dbg_declare(ptr %_eval_cycles_high, !263, !DIExpression(), !262)
  store i32 0, ptr %_eval_cycles_high, align 4, !dbg !262
  %41 = call { i32, i32 } asm sideeffect "cpuid\0A\09rdtsc\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !262, !srcloc !264
  %asmresult = extractvalue { i32, i32 } %41, 0, !dbg !262
  %asmresult57 = extractvalue { i32, i32 } %41, 1, !dbg !262
  store i32 %asmresult, ptr %_eval_cycles_low, align 4, !dbg !262
  store i32 %asmresult57, ptr %_eval_cycles_high, align 4, !dbg !262
  %42 = load i32, ptr %_eval_cycles_high, align 4, !dbg !262
  %conv58 = zext i32 %42 to i64, !dbg !262
  %shl = shl i64 %conv58, 32, !dbg !262
  %43 = load i32, ptr %_eval_cycles_low, align 4, !dbg !262
  %conv59 = zext i32 %43 to i64, !dbg !262
  %or = or i64 %shl, %conv59, !dbg !262
  store i64 %or, ptr %tmp, align 8, !dbg !262
  %44 = load i64, ptr %tmp, align 8, !dbg !262
  store volatile i64 %44, ptr %start_time, align 8, !dbg !265
    #dbg_declare(ptr %decrypt_result, !266, !DIExpression(), !267)
  %45 = load ptr, ptr %decrypted_msg, align 8, !dbg !268
  %46 = load ptr, ptr %ciphertext, align 8, !dbg !269
  %47 = load i64, ptr %ciphertext_sz, align 8, !dbg !270
  %48 = load ptr, ptr %additional_data, align 8, !dbg !271
  %49 = load i64, ptr %additional_data_sz, align 8, !dbg !272
  %50 = load ptr, ptr %nonce, align 8, !dbg !273
  %51 = load ptr, ptr %key, align 8, !dbg !274
  %call60 = call i32 @crypto_aead_aes256gcm_decrypt(ptr noundef %45, ptr noundef %msg_sz, ptr noundef null, ptr noundef %46, i64 noundef %47, ptr noundef %48, i64 noundef %49, ptr noundef %50, ptr noundef %51), !dbg !275
  store i32 %call60, ptr %decrypt_result, align 4, !dbg !267
    #dbg_declare(ptr %_eval_cycles_low61, !276, !DIExpression(), !278)
  store i32 0, ptr %_eval_cycles_low61, align 4, !dbg !278
    #dbg_declare(ptr %_eval_cycles_high62, !279, !DIExpression(), !278)
  store i32 0, ptr %_eval_cycles_high62, align 4, !dbg !278
  %52 = call { i32, i32 } asm sideeffect "rdtscp\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09cpuid\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #8, !dbg !278, !srcloc !280
  %asmresult63 = extractvalue { i32, i32 } %52, 0, !dbg !278
  %asmresult64 = extractvalue { i32, i32 } %52, 1, !dbg !278
  store i32 %asmresult63, ptr %_eval_cycles_low61, align 4, !dbg !278
  store i32 %asmresult64, ptr %_eval_cycles_high62, align 4, !dbg !278
  %53 = load i32, ptr %_eval_cycles_high62, align 4, !dbg !278
  %conv66 = zext i32 %53 to i64, !dbg !278
  %shl67 = shl i64 %conv66, 32, !dbg !278
  %54 = load i32, ptr %_eval_cycles_low61, align 4, !dbg !278
  %conv68 = zext i32 %54 to i64, !dbg !278
  %or69 = or i64 %shl67, %conv68, !dbg !278
  store i64 %or69, ptr %tmp65, align 8, !dbg !278
  %55 = load i64, ptr %tmp65, align 8, !dbg !278
  store volatile i64 %55, ptr %end_time, align 8, !dbg !281
  %56 = load i32, ptr %decrypt_result, align 4, !dbg !282
  %cmp70 = icmp eq i32 -1, %56, !dbg !284
  br i1 %cmp70, label %if.then72, label %if.end74, !dbg !284

if.then72:                                        ; preds = %if.end56
  %call73 = call i32 (ptr, ...) @printf(ptr noundef @.str.14), !dbg !285
  call void @exit(i32 noundef 0) #7, !dbg !287
  unreachable, !dbg !287

if.end74:                                         ; preds = %if.end56
  %57 = load i32, ptr %cur_iter, align 4, !dbg !288
  %58 = load i32, ptr %num_warmup, align 4, !dbg !290
  %cmp75 = icmp sge i32 %57, %58, !dbg !291
  br i1 %cmp75, label %if.then77, label %if.end80, !dbg !291

if.then77:                                        ; preds = %if.end74
  %59 = load volatile i64, ptr %end_time, align 8, !dbg !292
  %60 = load volatile i64, ptr %start_time, align 8, !dbg !294
  %sub = sub i64 %59, %60, !dbg !295
  %61 = load ptr, ptr %times, align 8, !dbg !296
  %62 = load i32, ptr %cur_iter, align 4, !dbg !297
  %63 = load i32, ptr %num_warmup, align 4, !dbg !298
  %sub78 = sub nsw i32 %62, %63, !dbg !299
  %idxprom = sext i32 %sub78 to i64, !dbg !296
  %arrayidx79 = getelementptr inbounds i64, ptr %61, i64 %idxprom, !dbg !296
  store i64 %sub, ptr %arrayidx79, align 8, !dbg !300
  br label %if.end80, !dbg !301

if.end80:                                         ; preds = %if.then77, %if.end74
    #dbg_declare(ptr %cmp_result, !302, !DIExpression(), !303)
  %64 = load ptr, ptr %msg, align 8, !dbg !304
  %65 = load ptr, ptr %decrypted_msg, align 8, !dbg !305
  %66 = load i64, ptr %msg_sz, align 8, !dbg !306
  %call81 = call i32 @memcmp(ptr noundef %64, ptr noundef %65, i64 noundef %66) #9, !dbg !307
  store i32 %call81, ptr %cmp_result, align 4, !dbg !303
  %67 = load i32, ptr %cmp_result, align 4, !dbg !308
  %cmp82 = icmp ne i32 0, %67, !dbg !310
  br i1 %cmp82, label %if.then84, label %if.end86, !dbg !310

if.then84:                                        ; preds = %if.end80
  %call85 = call i32 (ptr, ...) @printf(ptr noundef @.str.15), !dbg !311
  call void @exit(i32 noundef 0) #7, !dbg !313
  unreachable, !dbg !313

if.end86:                                         ; preds = %if.end80
  br label %for.inc, !dbg !314

for.inc:                                          ; preds = %if.end86
  %68 = load i32, ptr %cur_iter, align 4, !dbg !315
  %inc = add nsw i32 %68, 1, !dbg !315
  store i32 %inc, ptr %cur_iter, align 4, !dbg !315
  br label %for.cond, !dbg !316, !llvm.loop !317

for.end:                                          ; preds = %for.cond
    #dbg_declare(ptr %ccounts_out, !320, !DIExpression(), !378)
  %69 = load ptr, ptr %argv.addr, align 8, !dbg !379
  %arrayidx87 = getelementptr inbounds ptr, ptr %69, i64 5, !dbg !379
  %70 = load ptr, ptr %arrayidx87, align 8, !dbg !379
  %call88 = call noalias ptr @fopen(ptr noundef %70, ptr noundef @.str.16), !dbg !380
  store ptr %call88, ptr %ccounts_out, align 8, !dbg !378
  %71 = load ptr, ptr %ccounts_out, align 8, !dbg !381
  %cmp89 = icmp ne ptr %71, null, !dbg !382
  br i1 %cmp89, label %land.lhs.true91, label %cond.false93, !dbg !383

land.lhs.true91:                                  ; preds = %for.end
  br i1 true, label %cond.true92, label %cond.false93, !dbg !384

cond.true92:                                      ; preds = %land.lhs.true91
  br label %cond.end94, !dbg !384

cond.false93:                                     ; preds = %land.lhs.true91, %for.end
  call void @__assert_fail(ptr noundef @.str.18, ptr noundef @.str.3, i32 noundef 173, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !384
  unreachable, !dbg !384

72:                                               ; No predecessors!
  br label %cond.end94, !dbg !384

cond.end94:                                       ; preds = %72, %cond.true92
  %73 = load ptr, ptr %ccounts_out, align 8, !dbg !385
  %74 = load i32, ptr %num_iter, align 4, !dbg !386
  %75 = load i32, ptr %num_warmup, align 4, !dbg !387
  %call95 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %73, ptr noundef @.str.19, i32 noundef %74, i32 noundef %75) #8, !dbg !388
    #dbg_declare(ptr %ii, !389, !DIExpression(), !391)
  store i32 0, ptr %ii, align 4, !dbg !391
  br label %for.cond96, !dbg !392

for.cond96:                                       ; preds = %for.inc103, %cond.end94
  %76 = load i32, ptr %ii, align 4, !dbg !393
  %77 = load i32, ptr %num_iter, align 4, !dbg !395
  %cmp97 = icmp slt i32 %76, %77, !dbg !396
  br i1 %cmp97, label %for.body99, label %for.end105, !dbg !397

for.body99:                                       ; preds = %for.cond96
  %78 = load ptr, ptr %ccounts_out, align 8, !dbg !398
  %79 = load ptr, ptr %times, align 8, !dbg !400
  %80 = load i32, ptr %ii, align 4, !dbg !401
  %idxprom100 = sext i32 %80 to i64, !dbg !400
  %arrayidx101 = getelementptr inbounds i64, ptr %79, i64 %idxprom100, !dbg !400
  %81 = load i64, ptr %arrayidx101, align 8, !dbg !400
  %call102 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %78, ptr noundef @.str.20, i64 noundef %81) #8, !dbg !402
  br label %for.inc103, !dbg !403

for.inc103:                                       ; preds = %for.body99
  %82 = load i32, ptr %ii, align 4, !dbg !404
  %inc104 = add nsw i32 %82, 1, !dbg !404
  store i32 %inc104, ptr %ii, align 4, !dbg !404
  br label %for.cond96, !dbg !405, !llvm.loop !406

for.end105:                                       ; preds = %for.cond96
  %83 = load ptr, ptr %ccounts_out, align 8, !dbg !408
  %call106 = call i32 @fclose(ptr noundef %83), !dbg !409
  %cmp107 = icmp ne i32 %call106, -1, !dbg !410
  br i1 %cmp107, label %land.lhs.true109, label %cond.false111, !dbg !411

land.lhs.true109:                                 ; preds = %for.end105
  br i1 true, label %cond.true110, label %cond.false111, !dbg !412

cond.true110:                                     ; preds = %land.lhs.true109
  br label %cond.end112, !dbg !412

cond.false111:                                    ; preds = %land.lhs.true109, %for.end105
  call void @__assert_fail(ptr noundef @.str.22, ptr noundef @.str.3, i32 noundef 182, ptr noundef @__PRETTY_FUNCTION__.main) #7, !dbg !412
  unreachable, !dbg !412

84:                                               ; No predecessors!
  br label %cond.end112, !dbg !412

cond.end112:                                      ; preds = %84, %cond.true110
  ret i32 0, !dbg !413
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

!llvm.dbg.cu = !{!108}
!llvm.module.flags = !{!121, !122, !123, !124, !125, !126}
!llvm.ident = !{!127}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 69, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "eval_aesni256gcm_decrypt.c", directory: "/home/rgangar/Documents/llvm-data-independent-timing/micro-benchmarks", checksumkind: CSK_MD5, checksum: "0ecbc86ce6dd5eff645282c10696a23a")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1168, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 146)
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
!29 = distinct !DIGlobalVariable(scope: null, file: !2, line: 105, type: !30, isLocal: true, isDefinition: true)
!30 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 544, elements: !31)
!31 = !{!32}
!32 = !DISubrange(count: 68)
!33 = !DIGlobalVariableExpression(var: !34, expr: !DIExpression())
!34 = distinct !DIGlobalVariable(scope: null, file: !2, line: 105, type: !35, isLocal: true, isDefinition: true)
!35 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 696, elements: !36)
!36 = !{!37}
!37 = !DISubrange(count: 87)
!38 = !DIGlobalVariableExpression(var: !39, expr: !DIExpression())
!39 = distinct !DIGlobalVariable(scope: null, file: !2, line: 110, type: !40, isLocal: true, isDefinition: true)
!40 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 520, elements: !41)
!41 = !{!42}
!42 = !DISubrange(count: 65)
!43 = !DIGlobalVariableExpression(var: !44, expr: !DIExpression())
!44 = distinct !DIGlobalVariable(scope: null, file: !2, line: 110, type: !45, isLocal: true, isDefinition: true)
!45 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 592, elements: !46)
!46 = !{!47}
!47 = !DISubrange(count: 74)
!48 = !DIGlobalVariableExpression(var: !49, expr: !DIExpression())
!49 = distinct !DIGlobalVariable(scope: null, file: !2, line: 114, type: !50, isLocal: true, isDefinition: true)
!50 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 464, elements: !51)
!51 = !{!52}
!52 = !DISubrange(count: 58)
!53 = !DIGlobalVariableExpression(var: !54, expr: !DIExpression())
!54 = distinct !DIGlobalVariable(scope: null, file: !2, line: 114, type: !55, isLocal: true, isDefinition: true)
!55 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 536, elements: !56)
!56 = !{!57}
!57 = !DISubrange(count: 67)
!58 = !DIGlobalVariableExpression(var: !59, expr: !DIExpression())
!59 = distinct !DIGlobalVariable(scope: null, file: !2, line: 118, type: !60, isLocal: true, isDefinition: true)
!60 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 552, elements: !61)
!61 = !{!62}
!62 = !DISubrange(count: 69)
!63 = !DIGlobalVariableExpression(var: !64, expr: !DIExpression())
!64 = distinct !DIGlobalVariable(scope: null, file: !2, line: 123, type: !45, isLocal: true, isDefinition: true)
!65 = !DIGlobalVariableExpression(var: !66, expr: !DIExpression())
!66 = distinct !DIGlobalVariable(scope: null, file: !2, line: 122, type: !67, isLocal: true, isDefinition: true)
!67 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 680, elements: !68)
!68 = !{!69}
!69 = !DISubrange(count: 85)
!70 = !DIGlobalVariableExpression(var: !71, expr: !DIExpression())
!71 = distinct !DIGlobalVariable(scope: null, file: !2, line: 140, type: !45, isLocal: true, isDefinition: true)
!72 = !DIGlobalVariableExpression(var: !73, expr: !DIExpression())
!73 = distinct !DIGlobalVariable(scope: null, file: !2, line: 156, type: !45, isLocal: true, isDefinition: true)
!74 = !DIGlobalVariableExpression(var: !75, expr: !DIExpression())
!75 = distinct !DIGlobalVariable(scope: null, file: !2, line: 167, type: !76, isLocal: true, isDefinition: true)
!76 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 608, elements: !77)
!77 = !{!78}
!78 = !DISubrange(count: 76)
!79 = !DIGlobalVariableExpression(var: !80, expr: !DIExpression())
!80 = distinct !DIGlobalVariable(scope: null, file: !2, line: 172, type: !81, isLocal: true, isDefinition: true)
!81 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 16, elements: !82)
!82 = !{!83}
!83 = !DISubrange(count: 2)
!84 = !DIGlobalVariableExpression(var: !85, expr: !DIExpression())
!85 = distinct !DIGlobalVariable(scope: null, file: !2, line: 173, type: !86, isLocal: true, isDefinition: true)
!86 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 352, elements: !87)
!87 = !{!88}
!88 = !DISubrange(count: 44)
!89 = !DIGlobalVariableExpression(var: !90, expr: !DIExpression())
!90 = distinct !DIGlobalVariable(scope: null, file: !2, line: 173, type: !60, isLocal: true, isDefinition: true)
!91 = !DIGlobalVariableExpression(var: !92, expr: !DIExpression())
!92 = distinct !DIGlobalVariable(scope: null, file: !2, line: 177, type: !93, isLocal: true, isDefinition: true)
!93 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 488, elements: !94)
!94 = !{!95}
!95 = !DISubrange(count: 61)
!96 = !DIGlobalVariableExpression(var: !97, expr: !DIExpression())
!97 = distinct !DIGlobalVariable(scope: null, file: !2, line: 180, type: !98, isLocal: true, isDefinition: true)
!98 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !99)
!99 = !{!100}
!100 = !DISubrange(count: 5)
!101 = !DIGlobalVariableExpression(var: !102, expr: !DIExpression())
!102 = distinct !DIGlobalVariable(scope: null, file: !2, line: 182, type: !103, isLocal: true, isDefinition: true)
!103 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 264, elements: !104)
!104 = !{!105}
!105 = !DISubrange(count: 33)
!106 = !DIGlobalVariableExpression(var: !107, expr: !DIExpression())
!107 = distinct !DIGlobalVariable(scope: null, file: !2, line: 182, type: !40, isLocal: true, isDefinition: true)
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
!120 = !{!0, !7, !12, !17, !22, !28, !33, !38, !43, !48, !53, !58, !63, !65, !70, !72, !74, !79, !84, !89, !91, !96, !101, !106}
!121 = !{i32 7, !"Dwarf Version", i32 5}
!122 = !{i32 2, !"Debug Info Version", i32 3}
!123 = !{i32 1, !"wchar_size", i32 4}
!124 = !{i32 8, !"PIC Level", i32 2}
!125 = !{i32 7, !"PIE Level", i32 2}
!126 = !{i32 7, !"uwtable", i32 2}
!127 = !{!"clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)"}
!128 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 66, type: !129, scopeLine: 67, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !108, retainedNodes: !132)
!129 = !DISubroutineType(types: !130)
!130 = !{!131, !131, !110}
!131 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!132 = !{}
!133 = !DILocalVariable(name: "argc", arg: 1, scope: !128, file: !2, line: 66, type: !131)
!134 = !DILocation(line: 66, column: 10, scope: !128)
!135 = !DILocalVariable(name: "argv", arg: 2, scope: !128, file: !2, line: 66, type: !110)
!136 = !DILocation(line: 66, column: 23, scope: !128)
!137 = !DILocation(line: 68, column: 7, scope: !138)
!138 = distinct !DILexicalBlock(scope: !128, file: !2, line: 68, column: 7)
!139 = !DILocation(line: 68, column: 12, scope: !138)
!140 = !DILocation(line: 71, column: 59, scope: !141)
!141 = distinct !DILexicalBlock(scope: !138, file: !2, line: 68, column: 29)
!142 = !DILocation(line: 69, column: 5, scope: !141)
!143 = !DILocation(line: 72, column: 5, scope: !141)
!144 = !DILocalVariable(name: "num_iter", scope: !128, file: !2, line: 76, type: !131)
!145 = !DILocation(line: 76, column: 7, scope: !128)
!146 = !DILocation(line: 76, column: 34, scope: !128)
!147 = !DILocation(line: 76, column: 18, scope: !128)
!148 = !DILocalVariable(name: "num_warmup", scope: !128, file: !2, line: 80, type: !131)
!149 = !DILocation(line: 80, column: 7, scope: !128)
!150 = !DILocation(line: 80, column: 36, scope: !128)
!151 = !DILocation(line: 80, column: 20, scope: !128)
!152 = !DILocalVariable(name: "msg", scope: !128, file: !2, line: 84, type: !113)
!153 = !DILocation(line: 84, column: 18, scope: !128)
!154 = !DILocation(line: 84, column: 40, scope: !128)
!155 = !DILocalVariable(name: "msg_sz", scope: !128, file: !2, line: 85, type: !156)
!156 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!157 = !DILocation(line: 85, column: 22, scope: !128)
!158 = !DILocation(line: 85, column: 38, scope: !128)
!159 = !DILocation(line: 85, column: 31, scope: !128)
!160 = !DILocalVariable(name: "additional_data_sz", scope: !128, file: !2, line: 86, type: !156)
!161 = !DILocation(line: 86, column: 22, scope: !128)
!162 = !DILocalVariable(name: "sodium_init_success", scope: !128, file: !2, line: 89, type: !163)
!163 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !131)
!164 = !DILocation(line: 89, column: 13, scope: !128)
!165 = !DILocalVariable(name: "sodium_already_initd", scope: !128, file: !2, line: 90, type: !163)
!166 = !DILocation(line: 90, column: 13, scope: !128)
!167 = !DILocalVariable(name: "sodium_init_result", scope: !128, file: !2, line: 91, type: !131)
!168 = !DILocation(line: 91, column: 7, scope: !128)
!169 = !DILocation(line: 91, column: 28, scope: !128)
!170 = !DILocation(line: 92, column: 34, scope: !128)
!171 = !DILocation(line: 92, column: 31, scope: !128)
!172 = !DILocation(line: 92, column: 53, scope: !128)
!173 = !DILocation(line: 93, column: 28, scope: !128)
!174 = !DILocation(line: 93, column: 25, scope: !128)
!175 = !DILocation(line: 93, column: 48, scope: !128)
!176 = !DILocation(line: 92, column: 3, scope: !128)
!177 = !DILocalVariable(name: "additional_data", scope: !128, file: !2, line: 100, type: !113)
!178 = !DILocation(line: 100, column: 18, scope: !128)
!179 = !DILocalVariable(name: "decrypted_msg", scope: !128, file: !2, line: 104, type: !113)
!180 = !DILocation(line: 104, column: 18, scope: !128)
!181 = !DILocation(line: 104, column: 41, scope: !128)
!182 = !DILocation(line: 104, column: 34, scope: !128)
!183 = !DILocation(line: 105, column: 10, scope: !128)
!184 = !DILocation(line: 105, column: 24, scope: !128)
!185 = !DILocation(line: 105, column: 3, scope: !128)
!186 = !DILocalVariable(name: "ciphertext_sz", scope: !128, file: !2, line: 108, type: !156)
!187 = !DILocation(line: 108, column: 22, scope: !128)
!188 = !DILocation(line: 108, column: 38, scope: !128)
!189 = !DILocation(line: 108, column: 47, scope: !128)
!190 = !DILocation(line: 108, column: 45, scope: !128)
!191 = !DILocalVariable(name: "ciphertext", scope: !128, file: !2, line: 109, type: !113)
!192 = !DILocation(line: 109, column: 18, scope: !128)
!193 = !DILocation(line: 109, column: 38, scope: !128)
!194 = !DILocation(line: 109, column: 31, scope: !128)
!195 = !DILocation(line: 110, column: 10, scope: !128)
!196 = !DILocation(line: 110, column: 14, scope: !128)
!197 = !DILocation(line: 110, column: 3, scope: !128)
!198 = !DILocalVariable(name: "key", scope: !128, file: !2, line: 113, type: !113)
!199 = !DILocation(line: 113, column: 18, scope: !128)
!200 = !DILocation(line: 113, column: 31, scope: !128)
!201 = !DILocation(line: 113, column: 24, scope: !128)
!202 = !DILocation(line: 114, column: 10, scope: !128)
!203 = !DILocation(line: 114, column: 14, scope: !128)
!204 = !DILocation(line: 114, column: 3, scope: !128)
!205 = !DILocalVariable(name: "nonce", scope: !128, file: !2, line: 117, type: !113)
!206 = !DILocation(line: 117, column: 18, scope: !128)
!207 = !DILocation(line: 117, column: 33, scope: !128)
!208 = !DILocation(line: 117, column: 26, scope: !128)
!209 = !DILocation(line: 118, column: 10, scope: !128)
!210 = !DILocation(line: 118, column: 16, scope: !128)
!211 = !DILocation(line: 118, column: 3, scope: !128)
!212 = !DILocalVariable(name: "times", scope: !128, file: !2, line: 121, type: !213)
!213 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !115, size: 64)
!214 = !DILocation(line: 121, column: 13, scope: !128)
!215 = !DILocation(line: 121, column: 28, scope: !128)
!216 = !DILocation(line: 121, column: 21, scope: !128)
!217 = !DILocation(line: 122, column: 10, scope: !128)
!218 = !DILocation(line: 122, column: 16, scope: !128)
!219 = !DILocation(line: 122, column: 3, scope: !128)
!220 = !DILocalVariable(name: "start_time", scope: !128, file: !2, line: 125, type: !221)
!221 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !115)
!222 = !DILocation(line: 125, column: 21, scope: !128)
!223 = !DILocalVariable(name: "end_time", scope: !128, file: !2, line: 126, type: !221)
!224 = !DILocation(line: 126, column: 21, scope: !128)
!225 = !DILocalVariable(name: "cur_iter", scope: !226, file: !2, line: 129, type: !131)
!226 = distinct !DILexicalBlock(scope: !128, file: !2, line: 129, column: 3)
!227 = !DILocation(line: 129, column: 12, scope: !226)
!228 = !DILocation(line: 129, column: 8, scope: !226)
!229 = !DILocation(line: 129, column: 26, scope: !230)
!230 = distinct !DILexicalBlock(scope: !226, file: !2, line: 129, column: 3)
!231 = !DILocation(line: 129, column: 37, scope: !230)
!232 = !DILocation(line: 129, column: 48, scope: !230)
!233 = !DILocation(line: 129, column: 46, scope: !230)
!234 = !DILocation(line: 129, column: 35, scope: !230)
!235 = !DILocation(line: 129, column: 3, scope: !226)
!236 = !DILocation(line: 132, column: 34, scope: !237)
!237 = distinct !DILexicalBlock(scope: !230, file: !2, line: 129, column: 72)
!238 = !DILocation(line: 132, column: 5, scope: !237)
!239 = !DILocation(line: 133, column: 21, scope: !237)
!240 = !DILocation(line: 133, column: 5, scope: !237)
!241 = !DILocalVariable(name: "encrypt_result", scope: !237, file: !2, line: 136, type: !131)
!242 = !DILocation(line: 136, column: 9, scope: !237)
!243 = !DILocation(line: 136, column: 56, scope: !237)
!244 = !DILocation(line: 137, column: 11, scope: !237)
!245 = !DILocation(line: 137, column: 16, scope: !237)
!246 = !DILocation(line: 137, column: 24, scope: !237)
!247 = !DILocation(line: 137, column: 41, scope: !237)
!248 = !DILocation(line: 137, column: 67, scope: !237)
!249 = !DILocation(line: 137, column: 74, scope: !237)
!250 = !DILocation(line: 136, column: 26, scope: !237)
!251 = !DILocation(line: 139, column: 15, scope: !252)
!252 = distinct !DILexicalBlock(scope: !237, file: !2, line: 139, column: 9)
!253 = !DILocation(line: 139, column: 12, scope: !252)
!254 = !DILocation(line: 140, column: 7, scope: !255)
!255 = distinct !DILexicalBlock(scope: !252, file: !2, line: 139, column: 31)
!256 = !DILocation(line: 141, column: 7, scope: !255)
!257 = !DILocalVariable(name: "_eval_cycles_low", scope: !258, file: !2, line: 145, type: !259)
!258 = distinct !DILexicalBlock(scope: !237, file: !2, line: 145, column: 18)
!259 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !116, line: 26, baseType: !260)
!260 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !118, line: 42, baseType: !261)
!261 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!262 = !DILocation(line: 145, column: 18, scope: !258)
!263 = !DILocalVariable(name: "_eval_cycles_high", scope: !258, file: !2, line: 145, type: !259)
!264 = !{i64 2147804831, i64 2147804839, i64 2147804864, i64 2147804909, i64 2147804950}
!265 = !DILocation(line: 145, column: 16, scope: !237)
!266 = !DILocalVariable(name: "decrypt_result", scope: !237, file: !2, line: 148, type: !131)
!267 = !DILocation(line: 148, column: 9, scope: !237)
!268 = !DILocation(line: 148, column: 56, scope: !237)
!269 = !DILocation(line: 149, column: 17, scope: !237)
!270 = !DILocation(line: 149, column: 29, scope: !237)
!271 = !DILocation(line: 149, column: 44, scope: !237)
!272 = !DILocation(line: 149, column: 61, scope: !237)
!273 = !DILocation(line: 150, column: 11, scope: !237)
!274 = !DILocation(line: 150, column: 18, scope: !237)
!275 = !DILocation(line: 148, column: 26, scope: !237)
!276 = !DILocalVariable(name: "_eval_cycles_low", scope: !277, file: !2, line: 153, type: !259)
!277 = distinct !DILexicalBlock(scope: !237, file: !2, line: 153, column: 16)
!278 = !DILocation(line: 153, column: 16, scope: !277)
!279 = !DILocalVariable(name: "_eval_cycles_high", scope: !277, file: !2, line: 153, type: !259)
!280 = !{i64 2147805340, i64 2147805349, i64 2147805449, i64 2147805529, i64 2147805591}
!281 = !DILocation(line: 153, column: 14, scope: !237)
!282 = !DILocation(line: 155, column: 15, scope: !283)
!283 = distinct !DILexicalBlock(scope: !237, file: !2, line: 155, column: 9)
!284 = !DILocation(line: 155, column: 12, scope: !283)
!285 = !DILocation(line: 156, column: 7, scope: !286)
!286 = distinct !DILexicalBlock(scope: !283, file: !2, line: 155, column: 31)
!287 = !DILocation(line: 157, column: 7, scope: !286)
!288 = !DILocation(line: 160, column: 9, scope: !289)
!289 = distinct !DILexicalBlock(scope: !237, file: !2, line: 160, column: 9)
!290 = !DILocation(line: 160, column: 21, scope: !289)
!291 = !DILocation(line: 160, column: 18, scope: !289)
!292 = !DILocation(line: 161, column: 38, scope: !293)
!293 = distinct !DILexicalBlock(scope: !289, file: !2, line: 160, column: 33)
!294 = !DILocation(line: 161, column: 49, scope: !293)
!295 = !DILocation(line: 161, column: 47, scope: !293)
!296 = !DILocation(line: 161, column: 7, scope: !293)
!297 = !DILocation(line: 161, column: 13, scope: !293)
!298 = !DILocation(line: 161, column: 24, scope: !293)
!299 = !DILocation(line: 161, column: 22, scope: !293)
!300 = !DILocation(line: 161, column: 36, scope: !293)
!301 = !DILocation(line: 162, column: 5, scope: !293)
!302 = !DILocalVariable(name: "cmp_result", scope: !237, file: !2, line: 165, type: !131)
!303 = !DILocation(line: 165, column: 9, scope: !237)
!304 = !DILocation(line: 165, column: 29, scope: !237)
!305 = !DILocation(line: 165, column: 34, scope: !237)
!306 = !DILocation(line: 165, column: 49, scope: !237)
!307 = !DILocation(line: 165, column: 22, scope: !237)
!308 = !DILocation(line: 166, column: 14, scope: !309)
!309 = distinct !DILexicalBlock(scope: !237, file: !2, line: 166, column: 9)
!310 = !DILocation(line: 166, column: 11, scope: !309)
!311 = !DILocation(line: 167, column: 7, scope: !312)
!312 = distinct !DILexicalBlock(scope: !309, file: !2, line: 166, column: 26)
!313 = !DILocation(line: 168, column: 7, scope: !312)
!314 = !DILocation(line: 170, column: 3, scope: !237)
!315 = !DILocation(line: 129, column: 60, scope: !230)
!316 = !DILocation(line: 129, column: 3, scope: !230)
!317 = distinct !{!317, !235, !318, !319}
!318 = !DILocation(line: 170, column: 3, scope: !226)
!319 = !{!"llvm.loop.mustprogress"}
!320 = !DILocalVariable(name: "ccounts_out", scope: !128, file: !2, line: 172, type: !321)
!321 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !322, size: 64)
!322 = !DIDerivedType(tag: DW_TAG_typedef, name: "FILE", file: !323, line: 7, baseType: !324)
!323 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "571f9fb6223c42439075fdde11a0de5d")
!324 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_FILE", file: !325, line: 49, size: 1728, elements: !326)
!325 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "1bad07471b7974df4ecc1d1c2ca207e6")
!326 = !{!327, !328, !329, !330, !331, !332, !333, !334, !335, !336, !337, !338, !339, !342, !344, !345, !346, !349, !351, !353, !357, !360, !362, !365, !368, !369, !370, !373, !374}
!327 = !DIDerivedType(tag: DW_TAG_member, name: "_flags", scope: !324, file: !325, line: 51, baseType: !131, size: 32)
!328 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_ptr", scope: !324, file: !325, line: 54, baseType: !111, size: 64, offset: 64)
!329 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_end", scope: !324, file: !325, line: 55, baseType: !111, size: 64, offset: 128)
!330 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_base", scope: !324, file: !325, line: 56, baseType: !111, size: 64, offset: 192)
!331 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_base", scope: !324, file: !325, line: 57, baseType: !111, size: 64, offset: 256)
!332 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_ptr", scope: !324, file: !325, line: 58, baseType: !111, size: 64, offset: 320)
!333 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_end", scope: !324, file: !325, line: 59, baseType: !111, size: 64, offset: 384)
!334 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_base", scope: !324, file: !325, line: 60, baseType: !111, size: 64, offset: 448)
!335 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_end", scope: !324, file: !325, line: 61, baseType: !111, size: 64, offset: 512)
!336 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_base", scope: !324, file: !325, line: 64, baseType: !111, size: 64, offset: 576)
!337 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_backup_base", scope: !324, file: !325, line: 65, baseType: !111, size: 64, offset: 640)
!338 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_end", scope: !324, file: !325, line: 66, baseType: !111, size: 64, offset: 704)
!339 = !DIDerivedType(tag: DW_TAG_member, name: "_markers", scope: !324, file: !325, line: 68, baseType: !340, size: 64, offset: 768)
!340 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !341, size: 64)
!341 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_marker", file: !325, line: 36, flags: DIFlagFwdDecl)
!342 = !DIDerivedType(tag: DW_TAG_member, name: "_chain", scope: !324, file: !325, line: 70, baseType: !343, size: 64, offset: 832)
!343 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !324, size: 64)
!344 = !DIDerivedType(tag: DW_TAG_member, name: "_fileno", scope: !324, file: !325, line: 72, baseType: !131, size: 32, offset: 896)
!345 = !DIDerivedType(tag: DW_TAG_member, name: "_flags2", scope: !324, file: !325, line: 73, baseType: !131, size: 32, offset: 928)
!346 = !DIDerivedType(tag: DW_TAG_member, name: "_old_offset", scope: !324, file: !325, line: 74, baseType: !347, size: 64, offset: 960)
!347 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off_t", file: !118, line: 152, baseType: !348)
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
!361 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off64_t", file: !118, line: 153, baseType: !348)
!362 = !DIDerivedType(tag: DW_TAG_member, name: "_codecvt", scope: !324, file: !325, line: 91, baseType: !363, size: 64, offset: 1216)
!363 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !364, size: 64)
!364 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_codecvt", file: !325, line: 37, flags: DIFlagFwdDecl)
!365 = !DIDerivedType(tag: DW_TAG_member, name: "_wide_data", scope: !324, file: !325, line: 92, baseType: !366, size: 64, offset: 1280)
!366 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !367, size: 64)
!367 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_wide_data", file: !325, line: 38, flags: DIFlagFwdDecl)
!368 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_list", scope: !324, file: !325, line: 93, baseType: !343, size: 64, offset: 1344)
!369 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_buf", scope: !324, file: !325, line: 94, baseType: !112, size: 64, offset: 1408)
!370 = !DIDerivedType(tag: DW_TAG_member, name: "__pad5", scope: !324, file: !325, line: 95, baseType: !371, size: 64, offset: 1472)
!371 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !372, line: 18, baseType: !119)
!372 = !DIFile(filename: "build/lib/clang/22/include/__stddef_size_t.h", directory: "/home/rgangar/Documents/llvm-data-independent-timing", checksumkind: CSK_MD5, checksum: "2c44e821a2b1951cde2eb0fb2e656867")
!373 = !DIDerivedType(tag: DW_TAG_member, name: "_mode", scope: !324, file: !325, line: 96, baseType: !131, size: 32, offset: 1536)
!374 = !DIDerivedType(tag: DW_TAG_member, name: "_unused2", scope: !324, file: !325, line: 98, baseType: !375, size: 160, offset: 1568)
!375 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !376)
!376 = !{!377}
!377 = !DISubrange(count: 20)
!378 = !DILocation(line: 172, column: 9, scope: !128)
!379 = !DILocation(line: 172, column: 29, scope: !128)
!380 = !DILocation(line: 172, column: 23, scope: !128)
!381 = !DILocation(line: 173, column: 10, scope: !128)
!382 = !DILocation(line: 173, column: 22, scope: !128)
!383 = !DILocation(line: 173, column: 30, scope: !128)
!384 = !DILocation(line: 173, column: 3, scope: !128)
!385 = !DILocation(line: 176, column: 11, scope: !128)
!386 = !DILocation(line: 178, column: 4, scope: !128)
!387 = !DILocation(line: 178, column: 14, scope: !128)
!388 = !DILocation(line: 176, column: 3, scope: !128)
!389 = !DILocalVariable(name: "ii", scope: !390, file: !2, line: 179, type: !131)
!390 = distinct !DILexicalBlock(scope: !128, file: !2, line: 179, column: 3)
!391 = !DILocation(line: 179, column: 12, scope: !390)
!392 = !DILocation(line: 179, column: 8, scope: !390)
!393 = !DILocation(line: 179, column: 20, scope: !394)
!394 = distinct !DILexicalBlock(scope: !390, file: !2, line: 179, column: 3)
!395 = !DILocation(line: 179, column: 25, scope: !394)
!396 = !DILocation(line: 179, column: 23, scope: !394)
!397 = !DILocation(line: 179, column: 3, scope: !390)
!398 = !DILocation(line: 180, column: 12, scope: !399)
!399 = distinct !DILexicalBlock(scope: !394, file: !2, line: 179, column: 41)
!400 = !DILocation(line: 180, column: 42, scope: !399)
!401 = !DILocation(line: 180, column: 48, scope: !399)
!402 = !DILocation(line: 180, column: 4, scope: !399)
!403 = !DILocation(line: 181, column: 3, scope: !399)
!404 = !DILocation(line: 179, column: 35, scope: !394)
!405 = !DILocation(line: 179, column: 3, scope: !394)
!406 = distinct !{!406, !397, !407, !319}
!407 = !DILocation(line: 181, column: 3, scope: !390)
!408 = !DILocation(line: 182, column: 17, scope: !128)
!409 = !DILocation(line: 182, column: 10, scope: !128)
!410 = !DILocation(line: 182, column: 30, scope: !128)
!411 = !DILocation(line: 182, column: 37, scope: !128)
!412 = !DILocation(line: 182, column: 3, scope: !128)
!413 = !DILocation(line: 191, column: 3, scope: !128)
