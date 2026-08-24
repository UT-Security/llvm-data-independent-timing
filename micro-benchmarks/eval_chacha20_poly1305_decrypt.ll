; ModuleID = 'eval_chacha20_poly1305_decrypt.c'
source_filename = "eval_chacha20_poly1305_decrypt.c"
target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"
target triple = "x86_64-unknown-linux-gnu"

@.str = private unnamed_addr constant [145 x i8] c"Usage: %s <num_benchmark_iterations> <num_warmup_iterations> <message> <size_of_associated_data> <cycle_counts_file> [<dynamic_hitcounts_file>]\0A\00", align 1, !dbg !0
@.str.1 = private unnamed_addr constant [30 x i8] c"Error initializing lib sodium\00", align 1, !dbg !7
@.str.2 = private unnamed_addr constant [125 x i8] c"(sodium_init_success == sodium_init_result || sodium_already_initd == sodium_init_result) && \22Error initializing lib sodium\22\00", align 1, !dbg !12
@.str.3 = private unnamed_addr constant [33 x i8] c"eval_chacha20_poly1305_decrypt.c\00", align 1, !dbg !17
@__PRETTY_FUNCTION__.main = private unnamed_addr constant [23 x i8] c"int main(int, char **)\00", align 1, !dbg !22
@.str.4 = private unnamed_addr constant [71 x i8] c"Couldn't allocate opened_msg bytes in eval_chacha20-poly1305-decrypt.c\00", align 1, !dbg !28
@.str.5 = private unnamed_addr constant [87 x i8] c"opened_msg && \22Couldn't allocate opened_msg bytes in eval_chacha20-poly1305-decrypt.c\22\00", align 1, !dbg !33
@.str.6 = private unnamed_addr constant [71 x i8] c"Couldn't allocate signed msg bytes in eval_chacha20-poly1305-decrypt.c\00", align 1, !dbg !38
@.str.7 = private unnamed_addr constant [80 x i8] c"msg && \22Couldn't allocate signed msg bytes in eval_chacha20-poly1305-decrypt.c\22\00", align 1, !dbg !40
@.str.8 = private unnamed_addr constant [72 x i8] c"Couldn't allocate private key bytes in eval_chacha20-poly1305-decrypt.c\00", align 1, !dbg !45
@.str.9 = private unnamed_addr constant [83 x i8] c"privk && \22Couldn't allocate private key bytes in eval_chacha20-poly1305-decrypt.c\22\00", align 1, !dbg !50
@.str.10 = private unnamed_addr constant [74 x i8] c"Couldn't allocate decrypted_msg bytes in eval_chacha20-poly1305-decrypt.c\00", align 1, !dbg !55
@.str.11 = private unnamed_addr constant [93 x i8] c"decrypted_msg && \22Couldn't allocate decrypted_msg bytes in eval_chacha20-poly1305-decrypt.c\22\00", align 1, !dbg !60
@.str.12 = private unnamed_addr constant [80 x i8] c"Couldn't allocate array for benchmark times in eval_chacha20-poly1305-decrypt.c\00", align 1, !dbg !65
@.str.13 = private unnamed_addr constant [91 x i8] c"times && \22Couldn't allocate array for benchmark times in eval_chacha20-poly1305-decrypt.c\22\00", align 1, !dbg !67
@.str.14 = private unnamed_addr constant [92 x i8] c"FAILURE: eval_chacha20_poly1305_decrypt failed at crypto_aead_chacha20poly1305_ietf_encrypt\00", align 1, !dbg !72
@.str.15 = private unnamed_addr constant [92 x i8] c"FAILURE: eval_chacha20_poly1305_decrypt failed at crypto_aead_chacha20poly1305_ietf_decrypt\00", align 1, !dbg !77
@.str.16 = private unnamed_addr constant [82 x i8] c"FAILURE: eval_chacha20_poly1305_decrypt failed sanity check, decrypted msg != msg\00", align 1, !dbg !79
@.str.17 = private unnamed_addr constant [2 x i8] c"w\00", align 1, !dbg !84
@.str.18 = private unnamed_addr constant [44 x i8] c"Couldn't open cycle counts file for writing\00", align 1, !dbg !89
@.str.19 = private unnamed_addr constant [69 x i8] c"ccounts_out != NULL && \22Couldn't open cycle counts file for writing\22\00", align 1, !dbg !94
@.str.20 = private unnamed_addr constant [67 x i8] c"chacha20-poly1305-decrypt cycle counts (%d iterations, %d warmup)\0A\00", align 1, !dbg !99
@.str.21 = private unnamed_addr constant [5 x i8] c"%lu\0A\00", align 1, !dbg !104
@.str.22 = private unnamed_addr constant [33 x i8] c"Couldn't close cycle counts file\00", align 1, !dbg !109
@.str.23 = private unnamed_addr constant [65 x i8] c"fclose(ccounts_out) != EOF && \22Couldn't close cycle counts file\22\00", align 1, !dbg !111

; Function Attrs: noinline nounwind optnone uwtable
define dso_local void @ciocc_eval_rand_fill_buf(ptr noundef %buf, i32 noundef %buf_len) #0 !dbg !136 {
entry:
  %buf.addr = alloca ptr, align 8
  %buf_len.addr = alloca i32, align 4
  %ii = alloca i32, align 4
  store ptr %buf, ptr %buf.addr, align 8
    #dbg_declare(ptr %buf.addr, !141, !DIExpression(), !142)
  store i32 %buf_len, ptr %buf_len.addr, align 4
    #dbg_declare(ptr %buf_len.addr, !143, !DIExpression(), !144)
    #dbg_declare(ptr %ii, !145, !DIExpression(), !147)
  store i32 0, ptr %ii, align 4, !dbg !147
  br label %for.cond, !dbg !148

for.cond:                                         ; preds = %for.inc, %entry
  %0 = load i32, ptr %ii, align 4, !dbg !149
  %1 = load i32, ptr %buf_len.addr, align 4, !dbg !151
  %cmp = icmp slt i32 %0, %1, !dbg !152
  br i1 %cmp, label %for.body, label %for.end, !dbg !153

for.body:                                         ; preds = %for.cond
  %call = call i32 @rand() #7, !dbg !154
  %conv = trunc i32 %call to i8, !dbg !154
  %2 = load ptr, ptr %buf.addr, align 8, !dbg !156
  %3 = load i32, ptr %ii, align 4, !dbg !157
  %idxprom = sext i32 %3 to i64, !dbg !156
  %arrayidx = getelementptr inbounds i8, ptr %2, i64 %idxprom, !dbg !156
  store i8 %conv, ptr %arrayidx, align 1, !dbg !158
  br label %for.inc, !dbg !159

for.inc:                                          ; preds = %for.body
  %4 = load i32, ptr %ii, align 4, !dbg !160
  %inc = add nsw i32 %4, 1, !dbg !160
  store i32 %inc, ptr %ii, align 4, !dbg !160
  br label %for.cond, !dbg !161, !llvm.loop !162

for.end:                                          ; preds = %for.cond
  ret void, !dbg !165
}

; Function Attrs: nounwind
declare i32 @rand() #1

; Function Attrs: noinline nounwind optnone uwtable
define dso_local i32 @main(i32 noundef %argc, ptr noundef %argv) #0 !dbg !166 {
entry:
  %retval = alloca i32, align 4
  %argc.addr = alloca i32, align 4
  %argv.addr = alloca ptr, align 8
  %num_iter = alloca i32, align 4
  %num_warmup = alloca i32, align 4
  %sodium_init_success = alloca i32, align 4
  %sodium_already_initd = alloca i32, align 4
  %sodium_init_result = alloca i32, align 4
  %msg = alloca ptr, align 8
  %msg_sz = alloca i64, align 8
  %additional_data_sz = alloca i64, align 8
  %opened_msg = alloca ptr, align 8
  %additional_data = alloca ptr, align 8
  %ciphertext_sz = alloca i64, align 8
  %ciphertext = alloca ptr, align 8
  %privk = alloca ptr, align 8
  %decrypted_msg = alloca ptr, align 8
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
    #dbg_declare(ptr %argc.addr, !169, !DIExpression(), !170)
  store ptr %argv, ptr %argv.addr, align 8
    #dbg_declare(ptr %argv.addr, !171, !DIExpression(), !172)
  %0 = load i32, ptr %argc.addr, align 4, !dbg !173
  %cmp = icmp slt i32 %0, 6, !dbg !175
  br i1 %cmp, label %if.then, label %if.end, !dbg !175

if.then:                                          ; preds = %entry
  %1 = load ptr, ptr %argv.addr, align 8, !dbg !176
  %arrayidx = getelementptr inbounds ptr, ptr %1, i64 0, !dbg !176
  %2 = load ptr, ptr %arrayidx, align 8, !dbg !176
  %call = call i32 (ptr, ...) @printf(ptr noundef @.str, ptr noundef %2), !dbg !178
  call void @exit(i32 noundef -1) #8, !dbg !179
  unreachable, !dbg !179

if.end:                                           ; preds = %entry
    #dbg_declare(ptr %num_iter, !180, !DIExpression(), !181)
  %3 = load ptr, ptr %argv.addr, align 8, !dbg !182
  %arrayidx1 = getelementptr inbounds ptr, ptr %3, i64 1, !dbg !182
  %4 = load ptr, ptr %arrayidx1, align 8, !dbg !182
  %call2 = call i64 @strtol(ptr noundef %4, ptr noundef null, i32 noundef 10) #7, !dbg !183
  %conv = trunc i64 %call2 to i32, !dbg !183
  store i32 %conv, ptr %num_iter, align 4, !dbg !181
    #dbg_declare(ptr %num_warmup, !184, !DIExpression(), !185)
  %5 = load ptr, ptr %argv.addr, align 8, !dbg !186
  %arrayidx3 = getelementptr inbounds ptr, ptr %5, i64 2, !dbg !186
  %6 = load ptr, ptr %arrayidx3, align 8, !dbg !186
  %call4 = call i64 @strtol(ptr noundef %6, ptr noundef null, i32 noundef 10) #7, !dbg !187
  %conv5 = trunc i64 %call4 to i32, !dbg !187
  store i32 %conv5, ptr %num_warmup, align 4, !dbg !185
    #dbg_declare(ptr %sodium_init_success, !188, !DIExpression(), !190)
  store i32 0, ptr %sodium_init_success, align 4, !dbg !190
    #dbg_declare(ptr %sodium_already_initd, !191, !DIExpression(), !192)
  store i32 1, ptr %sodium_already_initd, align 4, !dbg !192
    #dbg_declare(ptr %sodium_init_result, !193, !DIExpression(), !194)
  %call6 = call i32 (...) @sodium_init(), !dbg !195
  store i32 %call6, ptr %sodium_init_result, align 4, !dbg !194
  %7 = load i32, ptr %sodium_init_result, align 4, !dbg !196
  %cmp7 = icmp eq i32 0, %7, !dbg !197
  br i1 %cmp7, label %land.lhs.true, label %lor.lhs.false, !dbg !198

lor.lhs.false:                                    ; preds = %if.end
  %8 = load i32, ptr %sodium_init_result, align 4, !dbg !199
  %cmp9 = icmp eq i32 1, %8, !dbg !200
  br i1 %cmp9, label %land.lhs.true, label %cond.false, !dbg !201

land.lhs.true:                                    ; preds = %lor.lhs.false, %if.end
  br i1 true, label %cond.true, label %cond.false, !dbg !202

cond.true:                                        ; preds = %land.lhs.true
  br label %cond.end, !dbg !202

cond.false:                                       ; preds = %land.lhs.true, %lor.lhs.false
  call void @__assert_fail(ptr noundef @.str.2, ptr noundef @.str.3, i32 noundef 96, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !202
  unreachable, !dbg !202

9:                                                ; No predecessors!
  br label %cond.end, !dbg !202

cond.end:                                         ; preds = %9, %cond.true
    #dbg_declare(ptr %msg, !203, !DIExpression(), !204)
  %10 = load ptr, ptr %argv.addr, align 8, !dbg !205
  %arrayidx11 = getelementptr inbounds ptr, ptr %10, i64 3, !dbg !205
  %11 = load ptr, ptr %arrayidx11, align 8, !dbg !205
  store ptr %11, ptr %msg, align 8, !dbg !204
    #dbg_declare(ptr %msg_sz, !206, !DIExpression(), !208)
  %12 = load ptr, ptr %argv.addr, align 8, !dbg !209
  %arrayidx12 = getelementptr inbounds ptr, ptr %12, i64 3, !dbg !209
  %13 = load ptr, ptr %arrayidx12, align 8, !dbg !209
  %call13 = call i64 @strlen(ptr noundef %13) #9, !dbg !210
  store i64 %call13, ptr %msg_sz, align 8, !dbg !208
    #dbg_declare(ptr %additional_data_sz, !211, !DIExpression(), !212)
  store i64 0, ptr %additional_data_sz, align 8, !dbg !212
    #dbg_declare(ptr %opened_msg, !213, !DIExpression(), !214)
  %14 = load i64, ptr %msg_sz, align 8, !dbg !215
  %call14 = call noalias ptr @malloc(i64 noundef %14) #10, !dbg !216
  store ptr %call14, ptr %opened_msg, align 8, !dbg !214
  %15 = load ptr, ptr %opened_msg, align 8, !dbg !217
  %tobool = icmp ne ptr %15, null, !dbg !217
  br i1 %tobool, label %land.lhs.true15, label %cond.false17, !dbg !218

land.lhs.true15:                                  ; preds = %cond.end
  br i1 true, label %cond.true16, label %cond.false17, !dbg !219

cond.true16:                                      ; preds = %land.lhs.true15
  br label %cond.end18, !dbg !219

cond.false17:                                     ; preds = %land.lhs.true15, %cond.end
  call void @__assert_fail(ptr noundef @.str.5, ptr noundef @.str.3, i32 noundef 104, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !219
  unreachable, !dbg !219

16:                                               ; No predecessors!
  br label %cond.end18, !dbg !219

cond.end18:                                       ; preds = %16, %cond.true16
    #dbg_declare(ptr %additional_data, !220, !DIExpression(), !221)
  store ptr null, ptr %additional_data, align 8, !dbg !221
    #dbg_declare(ptr %ciphertext_sz, !222, !DIExpression(), !223)
  %17 = load i64, ptr %msg_sz, align 8, !dbg !224
  %call19 = call i64 @crypto_aead_chacha20poly1305_ietf_abytes(), !dbg !225
  %add = add i64 %17, %call19, !dbg !226
  store i64 %add, ptr %ciphertext_sz, align 8, !dbg !223
    #dbg_declare(ptr %ciphertext, !227, !DIExpression(), !228)
  %18 = load i64, ptr %ciphertext_sz, align 8, !dbg !229
  %call20 = call noalias ptr @malloc(i64 noundef %18) #10, !dbg !230
  store ptr %call20, ptr %ciphertext, align 8, !dbg !228
  %19 = load ptr, ptr %msg, align 8, !dbg !231
  %tobool21 = icmp ne ptr %19, null, !dbg !231
  br i1 %tobool21, label %land.lhs.true22, label %cond.false24, !dbg !232

land.lhs.true22:                                  ; preds = %cond.end18
  br i1 true, label %cond.true23, label %cond.false24, !dbg !233

cond.true23:                                      ; preds = %land.lhs.true22
  br label %cond.end25, !dbg !233

cond.false24:                                     ; preds = %land.lhs.true22, %cond.end18
  call void @__assert_fail(ptr noundef @.str.7, ptr noundef @.str.3, i32 noundef 113, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !233
  unreachable, !dbg !233

20:                                               ; No predecessors!
  br label %cond.end25, !dbg !233

cond.end25:                                       ; preds = %20, %cond.true23
    #dbg_declare(ptr %privk, !234, !DIExpression(), !235)
  %call26 = call i64 @crypto_aead_chacha20poly1305_ietf_keybytes(), !dbg !236
  %call27 = call noalias ptr @malloc(i64 noundef %call26) #10, !dbg !237
  store ptr %call27, ptr %privk, align 8, !dbg !235
  %21 = load ptr, ptr %privk, align 8, !dbg !238
  %tobool28 = icmp ne ptr %21, null, !dbg !238
  br i1 %tobool28, label %land.lhs.true29, label %cond.false31, !dbg !239

land.lhs.true29:                                  ; preds = %cond.end25
  br i1 true, label %cond.true30, label %cond.false31, !dbg !240

cond.true30:                                      ; preds = %land.lhs.true29
  br label %cond.end32, !dbg !240

cond.false31:                                     ; preds = %land.lhs.true29, %cond.end25
  call void @__assert_fail(ptr noundef @.str.9, ptr noundef @.str.3, i32 noundef 117, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !240
  unreachable, !dbg !240

22:                                               ; No predecessors!
  br label %cond.end32, !dbg !240

cond.end32:                                       ; preds = %22, %cond.true30
    #dbg_declare(ptr %decrypted_msg, !241, !DIExpression(), !242)
  %23 = load i64, ptr %msg_sz, align 8, !dbg !243
  %call33 = call noalias ptr @malloc(i64 noundef %23) #10, !dbg !244
  store ptr %call33, ptr %decrypted_msg, align 8, !dbg !242
  %24 = load ptr, ptr %decrypted_msg, align 8, !dbg !245
  %tobool34 = icmp ne ptr %24, null, !dbg !245
  br i1 %tobool34, label %land.lhs.true35, label %cond.false37, !dbg !246

land.lhs.true35:                                  ; preds = %cond.end32
  br i1 true, label %cond.true36, label %cond.false37, !dbg !247

cond.true36:                                      ; preds = %land.lhs.true35
  br label %cond.end38, !dbg !247

cond.false37:                                     ; preds = %land.lhs.true35, %cond.end32
  call void @__assert_fail(ptr noundef @.str.11, ptr noundef @.str.3, i32 noundef 123, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !247
  unreachable, !dbg !247

25:                                               ; No predecessors!
  br label %cond.end38, !dbg !247

cond.end38:                                       ; preds = %25, %cond.true36
    #dbg_declare(ptr %nonce, !248, !DIExpression(), !249)
  %call39 = call i64 @crypto_aead_chacha20poly1305_ietf_npubbytes(), !dbg !250
  %call40 = call noalias ptr @malloc(i64 noundef %call39) #10, !dbg !251
  store ptr %call40, ptr %nonce, align 8, !dbg !249
    #dbg_declare(ptr %times, !252, !DIExpression(), !254)
  %26 = load i32, ptr %num_iter, align 4, !dbg !255
  %conv41 = sext i32 %26 to i64, !dbg !255
  %call42 = call noalias ptr @calloc(i64 noundef %conv41, i64 noundef 8) #11, !dbg !256
  store ptr %call42, ptr %times, align 8, !dbg !254
  %27 = load ptr, ptr %times, align 8, !dbg !257
  %tobool43 = icmp ne ptr %27, null, !dbg !257
  br i1 %tobool43, label %land.lhs.true44, label %cond.false46, !dbg !258

land.lhs.true44:                                  ; preds = %cond.end38
  br i1 true, label %cond.true45, label %cond.false46, !dbg !259

cond.true45:                                      ; preds = %land.lhs.true44
  br label %cond.end47, !dbg !259

cond.false46:                                     ; preds = %land.lhs.true44, %cond.end38
  call void @__assert_fail(ptr noundef @.str.13, ptr noundef @.str.3, i32 noundef 131, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !259
  unreachable, !dbg !259

28:                                               ; No predecessors!
  br label %cond.end47, !dbg !259

cond.end47:                                       ; preds = %28, %cond.true45
    #dbg_declare(ptr %start_time, !260, !DIExpression(), !262)
  store volatile i64 0, ptr %start_time, align 8, !dbg !262
    #dbg_declare(ptr %end_time, !263, !DIExpression(), !264)
  store volatile i64 0, ptr %end_time, align 8, !dbg !264
    #dbg_declare(ptr %cur_iter, !265, !DIExpression(), !267)
  store i32 0, ptr %cur_iter, align 4, !dbg !267
  br label %for.cond, !dbg !268

for.cond:                                         ; preds = %for.inc, %cond.end47
  %29 = load i32, ptr %cur_iter, align 4, !dbg !269
  %30 = load i32, ptr %num_iter, align 4, !dbg !271
  %31 = load i32, ptr %num_warmup, align 4, !dbg !272
  %add48 = add nsw i32 %30, %31, !dbg !273
  %cmp49 = icmp slt i32 %29, %add48, !dbg !274
  br i1 %cmp49, label %for.body, label %for.end, !dbg !275

for.body:                                         ; preds = %for.cond
  %32 = load ptr, ptr %privk, align 8, !dbg !276
  call void @crypto_aead_chacha20poly1305_ietf_keygen(ptr noundef %32), !dbg !278
  %33 = load ptr, ptr %nonce, align 8, !dbg !279
  call void @ciocc_eval_rand_fill_buf(ptr noundef %33, i32 noundef 8), !dbg !280
    #dbg_declare(ptr %encrypt_result, !281, !DIExpression(), !282)
  %34 = load ptr, ptr %ciphertext, align 8, !dbg !283
  %35 = load ptr, ptr %msg, align 8, !dbg !284
  %36 = load i64, ptr %msg_sz, align 8, !dbg !285
  %37 = load ptr, ptr %additional_data, align 8, !dbg !286
  %38 = load i64, ptr %additional_data_sz, align 8, !dbg !287
  %39 = load ptr, ptr %nonce, align 8, !dbg !288
  %40 = load ptr, ptr %privk, align 8, !dbg !289
  %call51 = call i32 @crypto_aead_chacha20poly1305_ietf_encrypt(ptr noundef %34, ptr noundef %ciphertext_sz, ptr noundef %35, i64 noundef %36, ptr noundef %37, i64 noundef %38, ptr noundef null, ptr noundef %39, ptr noundef %40), !dbg !290
  store i32 %call51, ptr %encrypt_result, align 4, !dbg !282
  %41 = load i32, ptr %encrypt_result, align 4, !dbg !291
  %cmp52 = icmp eq i32 -1, %41, !dbg !293
  br i1 %cmp52, label %if.then54, label %if.end56, !dbg !293

if.then54:                                        ; preds = %for.body
  %call55 = call i32 (ptr, ...) @printf(ptr noundef @.str.14), !dbg !294
  call void @exit(i32 noundef 0) #8, !dbg !296
  unreachable, !dbg !296

if.end56:                                         ; preds = %for.body
    #dbg_declare(ptr %_eval_cycles_low, !297, !DIExpression(), !302)
  store i32 0, ptr %_eval_cycles_low, align 4, !dbg !302
    #dbg_declare(ptr %_eval_cycles_high, !303, !DIExpression(), !302)
  store i32 0, ptr %_eval_cycles_high, align 4, !dbg !302
  %42 = call { i32, i32 } asm sideeffect "cpuid\0A\09rdtsc\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #7, !dbg !302, !srcloc !304
  %asmresult = extractvalue { i32, i32 } %42, 0, !dbg !302
  %asmresult57 = extractvalue { i32, i32 } %42, 1, !dbg !302
  store i32 %asmresult, ptr %_eval_cycles_low, align 4, !dbg !302
  store i32 %asmresult57, ptr %_eval_cycles_high, align 4, !dbg !302
  %43 = load i32, ptr %_eval_cycles_high, align 4, !dbg !302
  %conv58 = zext i32 %43 to i64, !dbg !302
  %shl = shl i64 %conv58, 32, !dbg !302
  %44 = load i32, ptr %_eval_cycles_low, align 4, !dbg !302
  %conv59 = zext i32 %44 to i64, !dbg !302
  %or = or i64 %shl, %conv59, !dbg !302
  store i64 %or, ptr %tmp, align 8, !dbg !302
  %45 = load i64, ptr %tmp, align 8, !dbg !302
  store volatile i64 %45, ptr %start_time, align 8, !dbg !305
    #dbg_declare(ptr %decrypt_result, !306, !DIExpression(), !307)
  %46 = load ptr, ptr %decrypted_msg, align 8, !dbg !308
  %47 = load ptr, ptr %ciphertext, align 8, !dbg !309
  %48 = load i64, ptr %ciphertext_sz, align 8, !dbg !310
  %49 = load ptr, ptr %additional_data, align 8, !dbg !311
  %50 = load i64, ptr %additional_data_sz, align 8, !dbg !312
  %51 = load ptr, ptr %nonce, align 8, !dbg !313
  %52 = load ptr, ptr %privk, align 8, !dbg !314
  %call60 = call i32 @crypto_aead_chacha20poly1305_ietf_decrypt(ptr noundef %46, ptr noundef %msg_sz, ptr noundef null, ptr noundef %47, i64 noundef %48, ptr noundef %49, i64 noundef %50, ptr noundef %51, ptr noundef %52), !dbg !315
  store i32 %call60, ptr %decrypt_result, align 4, !dbg !307
    #dbg_declare(ptr %_eval_cycles_low61, !316, !DIExpression(), !318)
  store i32 0, ptr %_eval_cycles_low61, align 4, !dbg !318
    #dbg_declare(ptr %_eval_cycles_high62, !319, !DIExpression(), !318)
  store i32 0, ptr %_eval_cycles_high62, align 4, !dbg !318
  %53 = call { i32, i32 } asm sideeffect "rdtscp\0A\09mov %edx, $1\0A\09mov %eax, $0\0A\09cpuid\0A\09", "=r,=r,~{rax},~{rbx},~{rcx},~{rdx},~{dirflag},~{fpsr},~{flags}"() #7, !dbg !318, !srcloc !320
  %asmresult63 = extractvalue { i32, i32 } %53, 0, !dbg !318
  %asmresult64 = extractvalue { i32, i32 } %53, 1, !dbg !318
  store i32 %asmresult63, ptr %_eval_cycles_low61, align 4, !dbg !318
  store i32 %asmresult64, ptr %_eval_cycles_high62, align 4, !dbg !318
  %54 = load i32, ptr %_eval_cycles_high62, align 4, !dbg !318
  %conv66 = zext i32 %54 to i64, !dbg !318
  %shl67 = shl i64 %conv66, 32, !dbg !318
  %55 = load i32, ptr %_eval_cycles_low61, align 4, !dbg !318
  %conv68 = zext i32 %55 to i64, !dbg !318
  %or69 = or i64 %shl67, %conv68, !dbg !318
  store i64 %or69, ptr %tmp65, align 8, !dbg !318
  %56 = load i64, ptr %tmp65, align 8, !dbg !318
  store volatile i64 %56, ptr %end_time, align 8, !dbg !321
  %57 = load i32, ptr %decrypt_result, align 4, !dbg !322
  %cmp70 = icmp eq i32 -1, %57, !dbg !324
  br i1 %cmp70, label %if.then72, label %if.end74, !dbg !324

if.then72:                                        ; preds = %if.end56
  %call73 = call i32 (ptr, ...) @printf(ptr noundef @.str.15), !dbg !325
  call void @exit(i32 noundef 0) #8, !dbg !327
  unreachable, !dbg !327

if.end74:                                         ; preds = %if.end56
  %58 = load i32, ptr %cur_iter, align 4, !dbg !328
  %59 = load i32, ptr %num_warmup, align 4, !dbg !330
  %cmp75 = icmp sge i32 %58, %59, !dbg !331
  br i1 %cmp75, label %if.then77, label %if.end80, !dbg !331

if.then77:                                        ; preds = %if.end74
  %60 = load volatile i64, ptr %end_time, align 8, !dbg !332
  %61 = load volatile i64, ptr %start_time, align 8, !dbg !334
  %sub = sub i64 %60, %61, !dbg !335
  %62 = load ptr, ptr %times, align 8, !dbg !336
  %63 = load i32, ptr %cur_iter, align 4, !dbg !337
  %64 = load i32, ptr %num_warmup, align 4, !dbg !338
  %sub78 = sub nsw i32 %63, %64, !dbg !339
  %idxprom = sext i32 %sub78 to i64, !dbg !336
  %arrayidx79 = getelementptr inbounds i64, ptr %62, i64 %idxprom, !dbg !336
  store i64 %sub, ptr %arrayidx79, align 8, !dbg !340
  br label %if.end80, !dbg !341

if.end80:                                         ; preds = %if.then77, %if.end74
    #dbg_declare(ptr %cmp_result, !342, !DIExpression(), !343)
  %65 = load ptr, ptr %msg, align 8, !dbg !344
  %66 = load ptr, ptr %decrypted_msg, align 8, !dbg !345
  %67 = load i64, ptr %msg_sz, align 8, !dbg !346
  %call81 = call i32 @memcmp(ptr noundef %65, ptr noundef %66, i64 noundef %67) #9, !dbg !347
  store i32 %call81, ptr %cmp_result, align 4, !dbg !343
  %68 = load i32, ptr %cmp_result, align 4, !dbg !348
  %cmp82 = icmp ne i32 0, %68, !dbg !350
  br i1 %cmp82, label %if.then84, label %if.end86, !dbg !350

if.then84:                                        ; preds = %if.end80
  %call85 = call i32 (ptr, ...) @printf(ptr noundef @.str.16), !dbg !351
  call void @exit(i32 noundef 0) #8, !dbg !353
  unreachable, !dbg !353

if.end86:                                         ; preds = %if.end80
  br label %for.inc, !dbg !354

for.inc:                                          ; preds = %if.end86
  %69 = load i32, ptr %cur_iter, align 4, !dbg !355
  %inc = add nsw i32 %69, 1, !dbg !355
  store i32 %inc, ptr %cur_iter, align 4, !dbg !355
  br label %for.cond, !dbg !356, !llvm.loop !357

for.end:                                          ; preds = %for.cond
    #dbg_declare(ptr %ccounts_out, !359, !DIExpression(), !417)
  %70 = load ptr, ptr %argv.addr, align 8, !dbg !418
  %arrayidx87 = getelementptr inbounds ptr, ptr %70, i64 5, !dbg !418
  %71 = load ptr, ptr %arrayidx87, align 8, !dbg !418
  %call88 = call noalias ptr @fopen(ptr noundef %71, ptr noundef @.str.17), !dbg !419
  store ptr %call88, ptr %ccounts_out, align 8, !dbg !417
  %72 = load ptr, ptr %ccounts_out, align 8, !dbg !420
  %cmp89 = icmp ne ptr %72, null, !dbg !421
  br i1 %cmp89, label %land.lhs.true91, label %cond.false93, !dbg !422

land.lhs.true91:                                  ; preds = %for.end
  br i1 true, label %cond.true92, label %cond.false93, !dbg !423

cond.true92:                                      ; preds = %land.lhs.true91
  br label %cond.end94, !dbg !423

cond.false93:                                     ; preds = %land.lhs.true91, %for.end
  call void @__assert_fail(ptr noundef @.str.19, ptr noundef @.str.3, i32 noundef 188, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !423
  unreachable, !dbg !423

73:                                               ; No predecessors!
  br label %cond.end94, !dbg !423

cond.end94:                                       ; preds = %73, %cond.true92
  %74 = load ptr, ptr %ccounts_out, align 8, !dbg !424
  %75 = load i32, ptr %num_iter, align 4, !dbg !425
  %76 = load i32, ptr %num_warmup, align 4, !dbg !426
  %call95 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %74, ptr noundef @.str.20, i32 noundef %75, i32 noundef %76) #7, !dbg !427
    #dbg_declare(ptr %ii, !428, !DIExpression(), !430)
  store i32 0, ptr %ii, align 4, !dbg !430
  br label %for.cond96, !dbg !431

for.cond96:                                       ; preds = %for.inc103, %cond.end94
  %77 = load i32, ptr %ii, align 4, !dbg !432
  %78 = load i32, ptr %num_iter, align 4, !dbg !434
  %cmp97 = icmp slt i32 %77, %78, !dbg !435
  br i1 %cmp97, label %for.body99, label %for.end105, !dbg !436

for.body99:                                       ; preds = %for.cond96
  %79 = load ptr, ptr %ccounts_out, align 8, !dbg !437
  %80 = load ptr, ptr %times, align 8, !dbg !439
  %81 = load i32, ptr %ii, align 4, !dbg !440
  %idxprom100 = sext i32 %81 to i64, !dbg !439
  %arrayidx101 = getelementptr inbounds i64, ptr %80, i64 %idxprom100, !dbg !439
  %82 = load i64, ptr %arrayidx101, align 8, !dbg !439
  %call102 = call i32 (ptr, ptr, ...) @fprintf(ptr noundef %79, ptr noundef @.str.21, i64 noundef %82) #7, !dbg !441
  br label %for.inc103, !dbg !442

for.inc103:                                       ; preds = %for.body99
  %83 = load i32, ptr %ii, align 4, !dbg !443
  %inc104 = add nsw i32 %83, 1, !dbg !443
  store i32 %inc104, ptr %ii, align 4, !dbg !443
  br label %for.cond96, !dbg !444, !llvm.loop !445

for.end105:                                       ; preds = %for.cond96
  %84 = load ptr, ptr %ccounts_out, align 8, !dbg !447
  %call106 = call i32 @fclose(ptr noundef %84), !dbg !448
  %cmp107 = icmp ne i32 %call106, -1, !dbg !449
  br i1 %cmp107, label %land.lhs.true109, label %cond.false111, !dbg !450

land.lhs.true109:                                 ; preds = %for.end105
  br i1 true, label %cond.true110, label %cond.false111, !dbg !451

cond.true110:                                     ; preds = %land.lhs.true109
  br label %cond.end112, !dbg !451

cond.false111:                                    ; preds = %land.lhs.true109, %for.end105
  call void @__assert_fail(ptr noundef @.str.23, ptr noundef @.str.3, i32 noundef 196, ptr noundef @__PRETTY_FUNCTION__.main) #8, !dbg !451
  unreachable, !dbg !451

85:                                               ; No predecessors!
  br label %cond.end112, !dbg !451

cond.end112:                                      ; preds = %85, %cond.true110
  ret i32 0, !dbg !452
}

declare i32 @printf(ptr noundef, ...) #2

; Function Attrs: noreturn nounwind
declare void @exit(i32 noundef) #3

; Function Attrs: nounwind
declare i64 @strtol(ptr noundef, ptr noundef, i32 noundef) #1

declare i32 @sodium_init(...) #2

; Function Attrs: noreturn nounwind
declare void @__assert_fail(ptr noundef, ptr noundef, i32 noundef, ptr noundef) #3

; Function Attrs: nounwind willreturn memory(read)
declare i64 @strlen(ptr noundef) #4

; Function Attrs: nounwind allocsize(0)
declare noalias ptr @malloc(i64 noundef) #5

declare i64 @crypto_aead_chacha20poly1305_ietf_abytes() #2

declare i64 @crypto_aead_chacha20poly1305_ietf_keybytes() #2

declare i64 @crypto_aead_chacha20poly1305_ietf_npubbytes() #2

; Function Attrs: nounwind allocsize(0,1)
declare noalias ptr @calloc(i64 noundef, i64 noundef) #6

declare void @crypto_aead_chacha20poly1305_ietf_keygen(ptr noundef) #2

declare i32 @crypto_aead_chacha20poly1305_ietf_encrypt(ptr noundef, ptr noundef, ptr noundef, i64 noundef, ptr noundef, i64 noundef, ptr noundef, ptr noundef, ptr noundef) #2

declare i32 @crypto_aead_chacha20poly1305_ietf_decrypt(ptr noundef, ptr noundef, ptr noundef, ptr noundef, i64 noundef, ptr noundef, i64 noundef, ptr noundef, ptr noundef) #2

; Function Attrs: nounwind willreturn memory(read)
declare i32 @memcmp(ptr noundef, ptr noundef, i64 noundef) #4

declare noalias ptr @fopen(ptr noundef, ptr noundef) #2

; Function Attrs: nounwind
declare i32 @fprintf(ptr noundef, ptr noundef, ...) #1

declare i32 @fclose(ptr noundef) #2

attributes #0 = { noinline nounwind optnone uwtable "min-legal-vector-width"="0" "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #1 = { nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #2 = { "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #3 = { noreturn nounwind "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #4 = { nounwind willreturn memory(read) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #5 = { nounwind allocsize(0) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #6 = { nounwind allocsize(0,1) "no-trapping-math"="true" "stack-protector-buffer-size"="8" "target-cpu"="x86-64" "target-features"="+cmov,+cx8,+fxsr,+mmx,+sse,+sse2,+x87" "tune-cpu"="generic" }
attributes #7 = { nounwind }
attributes #8 = { noreturn nounwind }
attributes #9 = { nounwind willreturn memory(read) }
attributes #10 = { nounwind allocsize(0) }
attributes #11 = { nounwind allocsize(0,1) }

!llvm.dbg.cu = !{!116}
!llvm.module.flags = !{!129, !130, !131, !132, !133, !134}
!llvm.ident = !{!135}

!0 = !DIGlobalVariableExpression(var: !1, expr: !DIExpression())
!1 = distinct !DIGlobalVariable(scope: null, file: !2, line: 75, type: !3, isLocal: true, isDefinition: true)
!2 = !DIFile(filename: "eval_chacha20_poly1305_decrypt.c", directory: "/home/rgangar/Documents/llvm-data-independent-timing/micro-benchmarks", checksumkind: CSK_MD5, checksum: "c8c8e958820f13cec75609f37e5a0b32")
!3 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1160, elements: !5)
!4 = !DIBasicType(name: "char", size: 8, encoding: DW_ATE_signed_char)
!5 = !{!6}
!6 = !DISubrange(count: 145)
!7 = !DIGlobalVariableExpression(var: !8, expr: !DIExpression())
!8 = distinct !DIGlobalVariable(scope: null, file: !2, line: 96, type: !9, isLocal: true, isDefinition: true)
!9 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 240, elements: !10)
!10 = !{!11}
!11 = !DISubrange(count: 30)
!12 = !DIGlobalVariableExpression(var: !13, expr: !DIExpression())
!13 = distinct !DIGlobalVariable(scope: null, file: !2, line: 94, type: !14, isLocal: true, isDefinition: true)
!14 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 1000, elements: !15)
!15 = !{!16}
!16 = !DISubrange(count: 125)
!17 = !DIGlobalVariableExpression(var: !18, expr: !DIExpression())
!18 = distinct !DIGlobalVariable(scope: null, file: !2, line: 94, type: !19, isLocal: true, isDefinition: true)
!19 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 264, elements: !20)
!20 = !{!21}
!21 = !DISubrange(count: 33)
!22 = !DIGlobalVariableExpression(var: !23, expr: !DIExpression())
!23 = distinct !DIGlobalVariable(scope: null, file: !2, line: 94, type: !24, isLocal: true, isDefinition: true)
!24 = !DICompositeType(tag: DW_TAG_array_type, baseType: !25, size: 184, elements: !26)
!25 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !4)
!26 = !{!27}
!27 = !DISubrange(count: 23)
!28 = !DIGlobalVariableExpression(var: !29, expr: !DIExpression())
!29 = distinct !DIGlobalVariable(scope: null, file: !2, line: 104, type: !30, isLocal: true, isDefinition: true)
!30 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 568, elements: !31)
!31 = !{!32}
!32 = !DISubrange(count: 71)
!33 = !DIGlobalVariableExpression(var: !34, expr: !DIExpression())
!34 = distinct !DIGlobalVariable(scope: null, file: !2, line: 104, type: !35, isLocal: true, isDefinition: true)
!35 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 696, elements: !36)
!36 = !{!37}
!37 = !DISubrange(count: 87)
!38 = !DIGlobalVariableExpression(var: !39, expr: !DIExpression())
!39 = distinct !DIGlobalVariable(scope: null, file: !2, line: 113, type: !30, isLocal: true, isDefinition: true)
!40 = !DIGlobalVariableExpression(var: !41, expr: !DIExpression())
!41 = distinct !DIGlobalVariable(scope: null, file: !2, line: 113, type: !42, isLocal: true, isDefinition: true)
!42 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 640, elements: !43)
!43 = !{!44}
!44 = !DISubrange(count: 80)
!45 = !DIGlobalVariableExpression(var: !46, expr: !DIExpression())
!46 = distinct !DIGlobalVariable(scope: null, file: !2, line: 117, type: !47, isLocal: true, isDefinition: true)
!47 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 576, elements: !48)
!48 = !{!49}
!49 = !DISubrange(count: 72)
!50 = !DIGlobalVariableExpression(var: !51, expr: !DIExpression())
!51 = distinct !DIGlobalVariable(scope: null, file: !2, line: 117, type: !52, isLocal: true, isDefinition: true)
!52 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 664, elements: !53)
!53 = !{!54}
!54 = !DISubrange(count: 83)
!55 = !DIGlobalVariableExpression(var: !56, expr: !DIExpression())
!56 = distinct !DIGlobalVariable(scope: null, file: !2, line: 123, type: !57, isLocal: true, isDefinition: true)
!57 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 592, elements: !58)
!58 = !{!59}
!59 = !DISubrange(count: 74)
!60 = !DIGlobalVariableExpression(var: !61, expr: !DIExpression())
!61 = distinct !DIGlobalVariable(scope: null, file: !2, line: 122, type: !62, isLocal: true, isDefinition: true)
!62 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 744, elements: !63)
!63 = !{!64}
!64 = !DISubrange(count: 93)
!65 = !DIGlobalVariableExpression(var: !66, expr: !DIExpression())
!66 = distinct !DIGlobalVariable(scope: null, file: !2, line: 131, type: !42, isLocal: true, isDefinition: true)
!67 = !DIGlobalVariableExpression(var: !68, expr: !DIExpression())
!68 = distinct !DIGlobalVariable(scope: null, file: !2, line: 130, type: !69, isLocal: true, isDefinition: true)
!69 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 728, elements: !70)
!70 = !{!71}
!71 = !DISubrange(count: 91)
!72 = !DIGlobalVariableExpression(var: !73, expr: !DIExpression())
!73 = distinct !DIGlobalVariable(scope: null, file: !2, line: 153, type: !74, isLocal: true, isDefinition: true)
!74 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 736, elements: !75)
!75 = !{!76}
!76 = !DISubrange(count: 92)
!77 = !DIGlobalVariableExpression(var: !78, expr: !DIExpression())
!78 = distinct !DIGlobalVariable(scope: null, file: !2, line: 171, type: !74, isLocal: true, isDefinition: true)
!79 = !DIGlobalVariableExpression(var: !80, expr: !DIExpression())
!80 = distinct !DIGlobalVariable(scope: null, file: !2, line: 181, type: !81, isLocal: true, isDefinition: true)
!81 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 656, elements: !82)
!82 = !{!83}
!83 = !DISubrange(count: 82)
!84 = !DIGlobalVariableExpression(var: !85, expr: !DIExpression())
!85 = distinct !DIGlobalVariable(scope: null, file: !2, line: 187, type: !86, isLocal: true, isDefinition: true)
!86 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 16, elements: !87)
!87 = !{!88}
!88 = !DISubrange(count: 2)
!89 = !DIGlobalVariableExpression(var: !90, expr: !DIExpression())
!90 = distinct !DIGlobalVariable(scope: null, file: !2, line: 188, type: !91, isLocal: true, isDefinition: true)
!91 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 352, elements: !92)
!92 = !{!93}
!93 = !DISubrange(count: 44)
!94 = !DIGlobalVariableExpression(var: !95, expr: !DIExpression())
!95 = distinct !DIGlobalVariable(scope: null, file: !2, line: 188, type: !96, isLocal: true, isDefinition: true)
!96 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 552, elements: !97)
!97 = !{!98}
!98 = !DISubrange(count: 69)
!99 = !DIGlobalVariableExpression(var: !100, expr: !DIExpression())
!100 = distinct !DIGlobalVariable(scope: null, file: !2, line: 191, type: !101, isLocal: true, isDefinition: true)
!101 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 536, elements: !102)
!102 = !{!103}
!103 = !DISubrange(count: 67)
!104 = !DIGlobalVariableExpression(var: !105, expr: !DIExpression())
!105 = distinct !DIGlobalVariable(scope: null, file: !2, line: 194, type: !106, isLocal: true, isDefinition: true)
!106 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 40, elements: !107)
!107 = !{!108}
!108 = !DISubrange(count: 5)
!109 = !DIGlobalVariableExpression(var: !110, expr: !DIExpression())
!110 = distinct !DIGlobalVariable(scope: null, file: !2, line: 196, type: !19, isLocal: true, isDefinition: true)
!111 = !DIGlobalVariableExpression(var: !112, expr: !DIExpression())
!112 = distinct !DIGlobalVariable(scope: null, file: !2, line: 196, type: !113, isLocal: true, isDefinition: true)
!113 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 520, elements: !114)
!114 = !{!115}
!115 = !DISubrange(count: 65)
!116 = distinct !DICompileUnit(language: DW_LANG_C11, file: !2, producer: "clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)", isOptimized: false, runtimeVersion: 0, emissionKind: FullDebug, retainedTypes: !117, globals: !128, splitDebugInlining: false, nameTableKind: None)
!117 = !{!118, !120, !121, !123}
!118 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !119, size: 64)
!119 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !4, size: 64)
!120 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: null, size: 64)
!121 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !122, size: 64)
!122 = !DIBasicType(name: "unsigned char", size: 8, encoding: DW_ATE_unsigned_char)
!123 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint64_t", file: !124, line: 27, baseType: !125)
!124 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/stdint-uintn.h", directory: "", checksumkind: CSK_MD5, checksum: "2bf2ae53c58c01b1a1b9383b5195125c")
!125 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint64_t", file: !126, line: 45, baseType: !127)
!126 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types.h", directory: "", checksumkind: CSK_MD5, checksum: "d108b5f93a74c50510d7d9bc0ab36df9")
!127 = !DIBasicType(name: "unsigned long", size: 64, encoding: DW_ATE_unsigned)
!128 = !{!0, !7, !12, !17, !22, !28, !33, !38, !40, !45, !50, !55, !60, !65, !67, !72, !77, !79, !84, !89, !94, !99, !104, !109, !111}
!129 = !{i32 7, !"Dwarf Version", i32 5}
!130 = !{i32 2, !"Debug Info Version", i32 3}
!131 = !{i32 1, !"wchar_size", i32 4}
!132 = !{i32 8, !"PIC Level", i32 2}
!133 = !{i32 7, !"PIE Level", i32 2}
!134 = !{i32 7, !"uwtable", i32 2}
!135 = !{!"clang version 22.0.0git (git@github.com:UT-Security/llvm-data-independent-timing.git eae817d26acfd9c1b236c55f06c0b4055462a81f)"}
!136 = distinct !DISubprogram(name: "ciocc_eval_rand_fill_buf", scope: !2, file: !2, line: 63, type: !137, scopeLine: 64, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !116, retainedNodes: !140)
!137 = !DISubroutineType(types: !138)
!138 = !{null, !121, !139}
!139 = !DIBasicType(name: "int", size: 32, encoding: DW_ATE_signed)
!140 = !{}
!141 = !DILocalVariable(name: "buf", arg: 1, scope: !136, file: !2, line: 63, type: !121)
!142 = !DILocation(line: 63, column: 41, scope: !136)
!143 = !DILocalVariable(name: "buf_len", arg: 2, scope: !136, file: !2, line: 63, type: !139)
!144 = !DILocation(line: 63, column: 50, scope: !136)
!145 = !DILocalVariable(name: "ii", scope: !146, file: !2, line: 65, type: !139)
!146 = distinct !DILexicalBlock(scope: !136, file: !2, line: 65, column: 2)
!147 = !DILocation(line: 65, column: 11, scope: !146)
!148 = !DILocation(line: 65, column: 7, scope: !146)
!149 = !DILocation(line: 65, column: 19, scope: !150)
!150 = distinct !DILexicalBlock(scope: !146, file: !2, line: 65, column: 2)
!151 = !DILocation(line: 65, column: 24, scope: !150)
!152 = !DILocation(line: 65, column: 22, scope: !150)
!153 = !DILocation(line: 65, column: 2, scope: !146)
!154 = !DILocation(line: 66, column: 13, scope: !155)
!155 = distinct !DILexicalBlock(scope: !150, file: !2, line: 65, column: 39)
!156 = !DILocation(line: 66, column: 3, scope: !155)
!157 = !DILocation(line: 66, column: 7, scope: !155)
!158 = !DILocation(line: 66, column: 11, scope: !155)
!159 = !DILocation(line: 67, column: 2, scope: !155)
!160 = !DILocation(line: 65, column: 33, scope: !150)
!161 = !DILocation(line: 65, column: 2, scope: !150)
!162 = distinct !{!162, !153, !163, !164}
!163 = !DILocation(line: 67, column: 2, scope: !146)
!164 = !{!"llvm.loop.mustprogress"}
!165 = !DILocation(line: 68, column: 1, scope: !136)
!166 = distinct !DISubprogram(name: "main", scope: !2, file: !2, line: 72, type: !167, scopeLine: 73, flags: DIFlagPrototyped, spFlags: DISPFlagDefinition, unit: !116, retainedNodes: !140)
!167 = !DISubroutineType(types: !168)
!168 = !{!139, !139, !118}
!169 = !DILocalVariable(name: "argc", arg: 1, scope: !166, file: !2, line: 72, type: !139)
!170 = !DILocation(line: 72, column: 10, scope: !166)
!171 = !DILocalVariable(name: "argv", arg: 2, scope: !166, file: !2, line: 72, type: !118)
!172 = !DILocation(line: 72, column: 23, scope: !166)
!173 = !DILocation(line: 74, column: 7, scope: !174)
!174 = distinct !DILexicalBlock(scope: !166, file: !2, line: 74, column: 7)
!175 = !DILocation(line: 74, column: 12, scope: !174)
!176 = !DILocation(line: 77, column: 58, scope: !177)
!177 = distinct !DILexicalBlock(scope: !174, file: !2, line: 74, column: 29)
!178 = !DILocation(line: 75, column: 5, scope: !177)
!179 = !DILocation(line: 78, column: 5, scope: !177)
!180 = !DILocalVariable(name: "num_iter", scope: !166, file: !2, line: 82, type: !139)
!181 = !DILocation(line: 82, column: 7, scope: !166)
!182 = !DILocation(line: 82, column: 34, scope: !166)
!183 = !DILocation(line: 82, column: 18, scope: !166)
!184 = !DILocalVariable(name: "num_warmup", scope: !166, file: !2, line: 86, type: !139)
!185 = !DILocation(line: 86, column: 7, scope: !166)
!186 = !DILocation(line: 86, column: 36, scope: !166)
!187 = !DILocation(line: 86, column: 20, scope: !166)
!188 = !DILocalVariable(name: "sodium_init_success", scope: !166, file: !2, line: 91, type: !189)
!189 = !DIDerivedType(tag: DW_TAG_const_type, baseType: !139)
!190 = !DILocation(line: 91, column: 13, scope: !166)
!191 = !DILocalVariable(name: "sodium_already_initd", scope: !166, file: !2, line: 92, type: !189)
!192 = !DILocation(line: 92, column: 13, scope: !166)
!193 = !DILocalVariable(name: "sodium_init_result", scope: !166, file: !2, line: 93, type: !139)
!194 = !DILocation(line: 93, column: 7, scope: !166)
!195 = !DILocation(line: 93, column: 28, scope: !166)
!196 = !DILocation(line: 94, column: 34, scope: !166)
!197 = !DILocation(line: 94, column: 31, scope: !166)
!198 = !DILocation(line: 94, column: 53, scope: !166)
!199 = !DILocation(line: 95, column: 28, scope: !166)
!200 = !DILocation(line: 95, column: 25, scope: !166)
!201 = !DILocation(line: 95, column: 48, scope: !166)
!202 = !DILocation(line: 94, column: 3, scope: !166)
!203 = !DILocalVariable(name: "msg", scope: !166, file: !2, line: 98, type: !121)
!204 = !DILocation(line: 98, column: 18, scope: !166)
!205 = !DILocation(line: 98, column: 40, scope: !166)
!206 = !DILocalVariable(name: "msg_sz", scope: !166, file: !2, line: 99, type: !207)
!207 = !DIBasicType(name: "unsigned long long", size: 64, encoding: DW_ATE_unsigned)
!208 = !DILocation(line: 99, column: 22, scope: !166)
!209 = !DILocation(line: 99, column: 38, scope: !166)
!210 = !DILocation(line: 99, column: 31, scope: !166)
!211 = !DILocalVariable(name: "additional_data_sz", scope: !166, file: !2, line: 100, type: !207)
!212 = !DILocation(line: 100, column: 22, scope: !166)
!213 = !DILocalVariable(name: "opened_msg", scope: !166, file: !2, line: 103, type: !121)
!214 = !DILocation(line: 103, column: 18, scope: !166)
!215 = !DILocation(line: 103, column: 38, scope: !166)
!216 = !DILocation(line: 103, column: 31, scope: !166)
!217 = !DILocation(line: 104, column: 10, scope: !166)
!218 = !DILocation(line: 104, column: 21, scope: !166)
!219 = !DILocation(line: 104, column: 3, scope: !166)
!220 = !DILocalVariable(name: "additional_data", scope: !166, file: !2, line: 107, type: !121)
!221 = !DILocation(line: 107, column: 18, scope: !166)
!222 = !DILocalVariable(name: "ciphertext_sz", scope: !166, file: !2, line: 111, type: !207)
!223 = !DILocation(line: 111, column: 22, scope: !166)
!224 = !DILocation(line: 111, column: 38, scope: !166)
!225 = !DILocation(line: 111, column: 47, scope: !166)
!226 = !DILocation(line: 111, column: 45, scope: !166)
!227 = !DILocalVariable(name: "ciphertext", scope: !166, file: !2, line: 112, type: !121)
!228 = !DILocation(line: 112, column: 18, scope: !166)
!229 = !DILocation(line: 112, column: 38, scope: !166)
!230 = !DILocation(line: 112, column: 31, scope: !166)
!231 = !DILocation(line: 113, column: 10, scope: !166)
!232 = !DILocation(line: 113, column: 14, scope: !166)
!233 = !DILocation(line: 113, column: 3, scope: !166)
!234 = !DILocalVariable(name: "privk", scope: !166, file: !2, line: 116, type: !121)
!235 = !DILocation(line: 116, column: 18, scope: !166)
!236 = !DILocation(line: 116, column: 33, scope: !166)
!237 = !DILocation(line: 116, column: 26, scope: !166)
!238 = !DILocation(line: 117, column: 10, scope: !166)
!239 = !DILocation(line: 117, column: 16, scope: !166)
!240 = !DILocation(line: 117, column: 3, scope: !166)
!241 = !DILocalVariable(name: "decrypted_msg", scope: !166, file: !2, line: 121, type: !121)
!242 = !DILocation(line: 121, column: 18, scope: !166)
!243 = !DILocation(line: 121, column: 41, scope: !166)
!244 = !DILocation(line: 121, column: 34, scope: !166)
!245 = !DILocation(line: 122, column: 10, scope: !166)
!246 = !DILocation(line: 122, column: 24, scope: !166)
!247 = !DILocation(line: 122, column: 3, scope: !166)
!248 = !DILocalVariable(name: "nonce", scope: !166, file: !2, line: 126, type: !121)
!249 = !DILocation(line: 126, column: 18, scope: !166)
!250 = !DILocation(line: 126, column: 33, scope: !166)
!251 = !DILocation(line: 126, column: 26, scope: !166)
!252 = !DILocalVariable(name: "times", scope: !166, file: !2, line: 129, type: !253)
!253 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !123, size: 64)
!254 = !DILocation(line: 129, column: 13, scope: !166)
!255 = !DILocation(line: 129, column: 28, scope: !166)
!256 = !DILocation(line: 129, column: 21, scope: !166)
!257 = !DILocation(line: 130, column: 10, scope: !166)
!258 = !DILocation(line: 130, column: 16, scope: !166)
!259 = !DILocation(line: 130, column: 3, scope: !166)
!260 = !DILocalVariable(name: "start_time", scope: !166, file: !2, line: 133, type: !261)
!261 = !DIDerivedType(tag: DW_TAG_volatile_type, baseType: !123)
!262 = !DILocation(line: 133, column: 21, scope: !166)
!263 = !DILocalVariable(name: "end_time", scope: !166, file: !2, line: 134, type: !261)
!264 = !DILocation(line: 134, column: 21, scope: !166)
!265 = !DILocalVariable(name: "cur_iter", scope: !266, file: !2, line: 137, type: !139)
!266 = distinct !DILexicalBlock(scope: !166, file: !2, line: 137, column: 3)
!267 = !DILocation(line: 137, column: 12, scope: !266)
!268 = !DILocation(line: 137, column: 8, scope: !266)
!269 = !DILocation(line: 137, column: 26, scope: !270)
!270 = distinct !DILexicalBlock(scope: !266, file: !2, line: 137, column: 3)
!271 = !DILocation(line: 137, column: 37, scope: !270)
!272 = !DILocation(line: 137, column: 48, scope: !270)
!273 = !DILocation(line: 137, column: 46, scope: !270)
!274 = !DILocation(line: 137, column: 35, scope: !270)
!275 = !DILocation(line: 137, column: 3, scope: !266)
!276 = !DILocation(line: 139, column: 46, scope: !277)
!277 = distinct !DILexicalBlock(scope: !270, file: !2, line: 137, column: 72)
!278 = !DILocation(line: 139, column: 5, scope: !277)
!279 = !DILocation(line: 142, column: 30, scope: !277)
!280 = !DILocation(line: 142, column: 5, scope: !277)
!281 = !DILocalVariable(name: "encrypt_result", scope: !277, file: !2, line: 144, type: !139)
!282 = !DILocation(line: 144, column: 9, scope: !277)
!283 = !DILocation(line: 145, column: 28, scope: !277)
!284 = !DILocation(line: 147, column: 21, scope: !277)
!285 = !DILocation(line: 148, column: 20, scope: !277)
!286 = !DILocation(line: 149, column: 29, scope: !277)
!287 = !DILocation(line: 150, column: 32, scope: !277)
!288 = !DILocation(line: 150, column: 58, scope: !277)
!289 = !DILocation(line: 150, column: 65, scope: !277)
!290 = !DILocation(line: 144, column: 26, scope: !277)
!291 = !DILocation(line: 152, column: 15, scope: !292)
!292 = distinct !DILexicalBlock(scope: !277, file: !2, line: 152, column: 9)
!293 = !DILocation(line: 152, column: 12, scope: !292)
!294 = !DILocation(line: 153, column: 7, scope: !295)
!295 = distinct !DILexicalBlock(scope: !292, file: !2, line: 152, column: 31)
!296 = !DILocation(line: 154, column: 7, scope: !295)
!297 = !DILocalVariable(name: "_eval_cycles_low", scope: !298, file: !2, line: 158, type: !299)
!298 = distinct !DILexicalBlock(scope: !277, file: !2, line: 158, column: 18)
!299 = !DIDerivedType(tag: DW_TAG_typedef, name: "uint32_t", file: !124, line: 26, baseType: !300)
!300 = !DIDerivedType(tag: DW_TAG_typedef, name: "__uint32_t", file: !126, line: 42, baseType: !301)
!301 = !DIBasicType(name: "unsigned int", size: 32, encoding: DW_ATE_unsigned)
!302 = !DILocation(line: 158, column: 18, scope: !298)
!303 = !DILocalVariable(name: "_eval_cycles_high", scope: !298, file: !2, line: 158, type: !299)
!304 = !{i64 2147805246, i64 2147805254, i64 2147805279, i64 2147805324, i64 2147805365}
!305 = !DILocation(line: 158, column: 16, scope: !277)
!306 = !DILocalVariable(name: "decrypt_result", scope: !277, file: !2, line: 160, type: !139)
!307 = !DILocation(line: 160, column: 9, scope: !277)
!308 = !DILocation(line: 161, column: 20, scope: !277)
!309 = !DILocation(line: 162, column: 20, scope: !277)
!310 = !DILocation(line: 163, column: 14, scope: !277)
!311 = !DILocation(line: 163, column: 29, scope: !277)
!312 = !DILocation(line: 164, column: 14, scope: !277)
!313 = !DILocation(line: 165, column: 14, scope: !277)
!314 = !DILocation(line: 165, column: 21, scope: !277)
!315 = !DILocation(line: 160, column: 26, scope: !277)
!316 = !DILocalVariable(name: "_eval_cycles_low", scope: !317, file: !2, line: 168, type: !299)
!317 = distinct !DILexicalBlock(scope: !277, file: !2, line: 168, column: 16)
!318 = !DILocation(line: 168, column: 16, scope: !317)
!319 = !DILocalVariable(name: "_eval_cycles_high", scope: !317, file: !2, line: 168, type: !299)
!320 = !{i64 2147805755, i64 2147805764, i64 2147805864, i64 2147805944, i64 2147806006}
!321 = !DILocation(line: 168, column: 14, scope: !277)
!322 = !DILocation(line: 170, column: 15, scope: !323)
!323 = distinct !DILexicalBlock(scope: !277, file: !2, line: 170, column: 9)
!324 = !DILocation(line: 170, column: 12, scope: !323)
!325 = !DILocation(line: 171, column: 7, scope: !326)
!326 = distinct !DILexicalBlock(scope: !323, file: !2, line: 170, column: 31)
!327 = !DILocation(line: 172, column: 7, scope: !326)
!328 = !DILocation(line: 175, column: 9, scope: !329)
!329 = distinct !DILexicalBlock(scope: !277, file: !2, line: 175, column: 9)
!330 = !DILocation(line: 175, column: 21, scope: !329)
!331 = !DILocation(line: 175, column: 18, scope: !329)
!332 = !DILocation(line: 176, column: 38, scope: !333)
!333 = distinct !DILexicalBlock(scope: !329, file: !2, line: 175, column: 33)
!334 = !DILocation(line: 176, column: 49, scope: !333)
!335 = !DILocation(line: 176, column: 47, scope: !333)
!336 = !DILocation(line: 176, column: 7, scope: !333)
!337 = !DILocation(line: 176, column: 13, scope: !333)
!338 = !DILocation(line: 176, column: 24, scope: !333)
!339 = !DILocation(line: 176, column: 22, scope: !333)
!340 = !DILocation(line: 176, column: 36, scope: !333)
!341 = !DILocation(line: 177, column: 5, scope: !333)
!342 = !DILocalVariable(name: "cmp_result", scope: !277, file: !2, line: 179, type: !139)
!343 = !DILocation(line: 179, column: 9, scope: !277)
!344 = !DILocation(line: 179, column: 29, scope: !277)
!345 = !DILocation(line: 179, column: 34, scope: !277)
!346 = !DILocation(line: 179, column: 49, scope: !277)
!347 = !DILocation(line: 179, column: 22, scope: !277)
!348 = !DILocation(line: 180, column: 14, scope: !349)
!349 = distinct !DILexicalBlock(scope: !277, file: !2, line: 180, column: 9)
!350 = !DILocation(line: 180, column: 11, scope: !349)
!351 = !DILocation(line: 181, column: 7, scope: !352)
!352 = distinct !DILexicalBlock(scope: !349, file: !2, line: 180, column: 26)
!353 = !DILocation(line: 182, column: 7, scope: !352)
!354 = !DILocation(line: 184, column: 3, scope: !277)
!355 = !DILocation(line: 137, column: 60, scope: !270)
!356 = !DILocation(line: 137, column: 3, scope: !270)
!357 = distinct !{!357, !275, !358, !164}
!358 = !DILocation(line: 184, column: 3, scope: !266)
!359 = !DILocalVariable(name: "ccounts_out", scope: !166, file: !2, line: 187, type: !360)
!360 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !361, size: 64)
!361 = !DIDerivedType(tag: DW_TAG_typedef, name: "FILE", file: !362, line: 7, baseType: !363)
!362 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "571f9fb6223c42439075fdde11a0de5d")
!363 = distinct !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_FILE", file: !364, line: 49, size: 1728, elements: !365)
!364 = !DIFile(filename: "/usr/include/x86_64-linux-gnu/bits/types/struct_FILE.h", directory: "", checksumkind: CSK_MD5, checksum: "1bad07471b7974df4ecc1d1c2ca207e6")
!365 = !{!366, !367, !368, !369, !370, !371, !372, !373, !374, !375, !376, !377, !378, !381, !383, !384, !385, !388, !390, !392, !396, !399, !401, !404, !407, !408, !409, !412, !413}
!366 = !DIDerivedType(tag: DW_TAG_member, name: "_flags", scope: !363, file: !364, line: 51, baseType: !139, size: 32)
!367 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_ptr", scope: !363, file: !364, line: 54, baseType: !119, size: 64, offset: 64)
!368 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_end", scope: !363, file: !364, line: 55, baseType: !119, size: 64, offset: 128)
!369 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_read_base", scope: !363, file: !364, line: 56, baseType: !119, size: 64, offset: 192)
!370 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_base", scope: !363, file: !364, line: 57, baseType: !119, size: 64, offset: 256)
!371 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_ptr", scope: !363, file: !364, line: 58, baseType: !119, size: 64, offset: 320)
!372 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_write_end", scope: !363, file: !364, line: 59, baseType: !119, size: 64, offset: 384)
!373 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_base", scope: !363, file: !364, line: 60, baseType: !119, size: 64, offset: 448)
!374 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_buf_end", scope: !363, file: !364, line: 61, baseType: !119, size: 64, offset: 512)
!375 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_base", scope: !363, file: !364, line: 64, baseType: !119, size: 64, offset: 576)
!376 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_backup_base", scope: !363, file: !364, line: 65, baseType: !119, size: 64, offset: 640)
!377 = !DIDerivedType(tag: DW_TAG_member, name: "_IO_save_end", scope: !363, file: !364, line: 66, baseType: !119, size: 64, offset: 704)
!378 = !DIDerivedType(tag: DW_TAG_member, name: "_markers", scope: !363, file: !364, line: 68, baseType: !379, size: 64, offset: 768)
!379 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !380, size: 64)
!380 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_marker", file: !364, line: 36, flags: DIFlagFwdDecl)
!381 = !DIDerivedType(tag: DW_TAG_member, name: "_chain", scope: !363, file: !364, line: 70, baseType: !382, size: 64, offset: 832)
!382 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !363, size: 64)
!383 = !DIDerivedType(tag: DW_TAG_member, name: "_fileno", scope: !363, file: !364, line: 72, baseType: !139, size: 32, offset: 896)
!384 = !DIDerivedType(tag: DW_TAG_member, name: "_flags2", scope: !363, file: !364, line: 73, baseType: !139, size: 32, offset: 928)
!385 = !DIDerivedType(tag: DW_TAG_member, name: "_old_offset", scope: !363, file: !364, line: 74, baseType: !386, size: 64, offset: 960)
!386 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off_t", file: !126, line: 152, baseType: !387)
!387 = !DIBasicType(name: "long", size: 64, encoding: DW_ATE_signed)
!388 = !DIDerivedType(tag: DW_TAG_member, name: "_cur_column", scope: !363, file: !364, line: 77, baseType: !389, size: 16, offset: 1024)
!389 = !DIBasicType(name: "unsigned short", size: 16, encoding: DW_ATE_unsigned)
!390 = !DIDerivedType(tag: DW_TAG_member, name: "_vtable_offset", scope: !363, file: !364, line: 78, baseType: !391, size: 8, offset: 1040)
!391 = !DIBasicType(name: "signed char", size: 8, encoding: DW_ATE_signed_char)
!392 = !DIDerivedType(tag: DW_TAG_member, name: "_shortbuf", scope: !363, file: !364, line: 79, baseType: !393, size: 8, offset: 1048)
!393 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 8, elements: !394)
!394 = !{!395}
!395 = !DISubrange(count: 1)
!396 = !DIDerivedType(tag: DW_TAG_member, name: "_lock", scope: !363, file: !364, line: 81, baseType: !397, size: 64, offset: 1088)
!397 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !398, size: 64)
!398 = !DIDerivedType(tag: DW_TAG_typedef, name: "_IO_lock_t", file: !364, line: 43, baseType: null)
!399 = !DIDerivedType(tag: DW_TAG_member, name: "_offset", scope: !363, file: !364, line: 89, baseType: !400, size: 64, offset: 1152)
!400 = !DIDerivedType(tag: DW_TAG_typedef, name: "__off64_t", file: !126, line: 153, baseType: !387)
!401 = !DIDerivedType(tag: DW_TAG_member, name: "_codecvt", scope: !363, file: !364, line: 91, baseType: !402, size: 64, offset: 1216)
!402 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !403, size: 64)
!403 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_codecvt", file: !364, line: 37, flags: DIFlagFwdDecl)
!404 = !DIDerivedType(tag: DW_TAG_member, name: "_wide_data", scope: !363, file: !364, line: 92, baseType: !405, size: 64, offset: 1280)
!405 = !DIDerivedType(tag: DW_TAG_pointer_type, baseType: !406, size: 64)
!406 = !DICompositeType(tag: DW_TAG_structure_type, name: "_IO_wide_data", file: !364, line: 38, flags: DIFlagFwdDecl)
!407 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_list", scope: !363, file: !364, line: 93, baseType: !382, size: 64, offset: 1344)
!408 = !DIDerivedType(tag: DW_TAG_member, name: "_freeres_buf", scope: !363, file: !364, line: 94, baseType: !120, size: 64, offset: 1408)
!409 = !DIDerivedType(tag: DW_TAG_member, name: "__pad5", scope: !363, file: !364, line: 95, baseType: !410, size: 64, offset: 1472)
!410 = !DIDerivedType(tag: DW_TAG_typedef, name: "size_t", file: !411, line: 18, baseType: !127)
!411 = !DIFile(filename: "build/lib/clang/22/include/__stddef_size_t.h", directory: "/home/rgangar/Documents/llvm-data-independent-timing", checksumkind: CSK_MD5, checksum: "2c44e821a2b1951cde2eb0fb2e656867")
!412 = !DIDerivedType(tag: DW_TAG_member, name: "_mode", scope: !363, file: !364, line: 96, baseType: !139, size: 32, offset: 1536)
!413 = !DIDerivedType(tag: DW_TAG_member, name: "_unused2", scope: !363, file: !364, line: 98, baseType: !414, size: 160, offset: 1568)
!414 = !DICompositeType(tag: DW_TAG_array_type, baseType: !4, size: 160, elements: !415)
!415 = !{!416}
!416 = !DISubrange(count: 20)
!417 = !DILocation(line: 187, column: 9, scope: !166)
!418 = !DILocation(line: 187, column: 29, scope: !166)
!419 = !DILocation(line: 187, column: 23, scope: !166)
!420 = !DILocation(line: 188, column: 10, scope: !166)
!421 = !DILocation(line: 188, column: 22, scope: !166)
!422 = !DILocation(line: 188, column: 30, scope: !166)
!423 = !DILocation(line: 188, column: 3, scope: !166)
!424 = !DILocation(line: 190, column: 11, scope: !166)
!425 = !DILocation(line: 192, column: 4, scope: !166)
!426 = !DILocation(line: 192, column: 14, scope: !166)
!427 = !DILocation(line: 190, column: 3, scope: !166)
!428 = !DILocalVariable(name: "ii", scope: !429, file: !2, line: 193, type: !139)
!429 = distinct !DILexicalBlock(scope: !166, file: !2, line: 193, column: 3)
!430 = !DILocation(line: 193, column: 12, scope: !429)
!431 = !DILocation(line: 193, column: 8, scope: !429)
!432 = !DILocation(line: 193, column: 20, scope: !433)
!433 = distinct !DILexicalBlock(scope: !429, file: !2, line: 193, column: 3)
!434 = !DILocation(line: 193, column: 25, scope: !433)
!435 = !DILocation(line: 193, column: 23, scope: !433)
!436 = !DILocation(line: 193, column: 3, scope: !429)
!437 = !DILocation(line: 194, column: 12, scope: !438)
!438 = distinct !DILexicalBlock(scope: !433, file: !2, line: 193, column: 41)
!439 = !DILocation(line: 194, column: 42, scope: !438)
!440 = !DILocation(line: 194, column: 48, scope: !438)
!441 = !DILocation(line: 194, column: 4, scope: !438)
!442 = !DILocation(line: 195, column: 3, scope: !438)
!443 = !DILocation(line: 193, column: 35, scope: !433)
!444 = !DILocation(line: 193, column: 3, scope: !433)
!445 = distinct !{!445, !436, !446, !164}
!446 = !DILocation(line: 195, column: 3, scope: !429)
!447 = !DILocation(line: 196, column: 17, scope: !166)
!448 = !DILocation(line: 196, column: 10, scope: !166)
!449 = !DILocation(line: 196, column: 30, scope: !166)
!450 = !DILocation(line: 196, column: 37, scope: !166)
!451 = !DILocation(line: 196, column: 3, scope: !166)
!452 = !DILocation(line: 205, column: 3, scope: !166)
