#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>

#define START_CYCLE_TIMER						\
  ({									\
    uint32_t _eval_cycles_low = 0;					\
   uint32_t _eval_cycles_high = 0;					\
   __asm__ volatile("cpuid\n\t"						\
		    "rdtsc\n\t"						\
		    "mov %%edx, %[cycles_high]\n\t"			\
		    "mov %%eax, %[cycles_low]\n\t"			\
		    : [cycles_low] "=r" (_eval_cycles_low),		\
		       [cycles_high] "=r" (_eval_cycles_high)		\
		    :							\
		    : "%rax", "%rbx", "%rcx", "%rdx");			\
   (((uint64_t)_eval_cycles_high) << 32 | (uint64_t)_eval_cycles_low);	\
})

#define STOP_CYCLE_TIMER						\
  ({									\
    uint32_t _eval_cycles_low = 0;					\
    uint32_t _eval_cycles_high = 0;					\
    __asm__ volatile(							\
        "rdtscp\n\t"                                                           \
        "mov %%edx, %[cycles_high]\n\t"                                        \
        "mov %%eax, %[cycles_low]\n\t"                                         \
        "cpuid\n\t"                                                            \
        : [cycles_low] "=r"(_eval_cycles_low),				\
          [cycles_high] "=r"(_eval_cycles_high)					\
        :                                                                      \
        : "%rax", "%rbx", "%rcx", "%rdx");                                     \
    (((uint64_t)_eval_cycles_high) << 32 | (uint64_t)_eval_cycles_low);        \
  })

#define EXPECTED_ARGC 6
#define NUM_BENCH_ITER_ARG_IDX 1
#define NUM_WARMUP_ITER_ARG_IDX 2
#define MSG_ARG_IDX 3
#define AD_SIZE_ARG_IDX 4
#define CYCLE_COUNTS_FILE 5
#define DYNAMIC_HITCOUNTS_FILE 6

extern int strncmp(const char *str1, const char *str2, size_t n);
extern size_t crypto_aead_aes256gcm_keybytes(void);
extern size_t crypto_aead_aes256gcm_npubbytes(void);
extern size_t crypto_aead_aes256gcm_abytes(void);
extern int sodium_init();
extern void randombytes_buf(void * const buf, const size_t size);
extern int crypto_aead_aes256gcm_is_available(void);
extern int crypto_aead_aes256gcm_keygen(unsigned char *k);
extern int crypto_aead_aes256gcm_encrypt(unsigned char *c,
          unsigned long long *clen_p, const unsigned char *m,
          unsigned long long m_len, const unsigned char *ad,
          unsigned long long ad_len, const unsigned char *nsec,
          const unsigned char *npub, const unsigned char *k);
extern int crypto_aead_aes256gcm_decrypt(unsigned char *m,
          unsigned long long *mlen_p, unsigned char *nsec,
          const unsigned char *c, unsigned long long clen,
          const unsigned char *ad, unsigned long long adlen,
          const unsigned char *npub, const unsigned char *k);

int
main(int argc, char** argv)
{
  if (argc < EXPECTED_ARGC) {
    printf("Usage: %s <num_benchmark_iterations> <num_warmup_iterations>"
	   " <messagee> <size_of_associated_data>"
     " <cycle_counts_file> [<dynamic_hitcounts_file>]\n", argv[0]);
    exit(-1);
  }

  // parse args
  int num_iter = strtol(/*src=*/ argv[NUM_BENCH_ITER_ARG_IDX],
			/*endptr=*/ (char**) NULL,
			/*base=*/ 10);
  
  int num_warmup = strtol(/*src=*/ argv[NUM_WARMUP_ITER_ARG_IDX],
			/*endptr=*/ (char**) NULL,
			/*base=*/ 10);

  unsigned char* msg = (unsigned char*)argv[MSG_ARG_IDX];
  unsigned long long msg_sz = strlen(argv[MSG_ARG_IDX]);
  unsigned long long additional_data_sz = 0; // strtol(argv[AD_SIZE_ARG_IDX], (char**) NULL, 10);

  // init libsodium, must be called before other libsodium functions are called
  const int sodium_init_success = 0;
  const int sodium_already_initd = 1;
  int sodium_init_result = sodium_init();
  assert((sodium_init_success == sodium_init_result ||
	  sodium_already_initd == sodium_init_result) &&
	 "Error initializing lib sodium");

  // Make sure AES is available
  // assert(crypto_aead_aes256gcm_is_available() && "AES not available on this CPU");

  // allocate space for additional data
  unsigned char* additional_data = NULL; // malloc(additional_data_sz);
  // assert(additional_data && "Couldn't allocate msg bytes in eval_aesni256gcm_decrypt.c");

  // allocate space for decrypted message
  unsigned char* decrypted_msg = malloc(msg_sz);
  assert(decrypted_msg && "Couldn't allocate decrypted_msg bytes in eval_aesni256gcm_decrypt.c");

  // allocate space for ciphertext
  unsigned long long ciphertext_sz = msg_sz + crypto_aead_aes256gcm_abytes();
  unsigned char* ciphertext = malloc(ciphertext_sz);
  assert(msg && "Couldn't allocate ciphertext bytes in eval_aesni256gcm_decrypt.c");

  // allocate space for key
  unsigned char* key = malloc(crypto_aead_aes256gcm_keybytes());
  assert(key && "Couldn't allocate key bytes in eval_aesni256gcm_decrypt.c");

  // allocate space for nonce
  unsigned char* nonce = malloc(crypto_aead_aes256gcm_npubbytes());
  assert(nonce && "Couldn't allocate key bytes in eval_aesni256gcm_decrypt.c");

  // allocate space for timer reads
  uint64_t* times = calloc(num_iter, sizeof(uint64_t));
  assert(times &&
	 "Couldn't allocate array for benchmark times in eval_aesni256gcm_decrypt.c");

  volatile uint64_t start_time = 0;
  volatile uint64_t end_time = 0;

  // main loop
  for (int cur_iter = 0; cur_iter < num_iter + num_warmup; ++cur_iter) {
    // generate key
    // generate nonce
    crypto_aead_aes256gcm_keygen(key);
    randombytes_buf(nonce, sizeof nonce);

    // encrypt message
    int encrypt_result = crypto_aead_aes256gcm_encrypt(ciphertext, &ciphertext_sz,
          msg, msg_sz, additional_data, additional_data_sz, NULL, nonce, key);

    if (-1 == encrypt_result) {
      printf("FAILURE: eval_aesni256gcm_decrypt failed at crypto_aead_aes256gcm_encrypt");
      exit(0);
    }

    // start counting cycles
    start_time = START_CYCLE_TIMER;

    // decrypt the message
    int decrypt_result = crypto_aead_aes256gcm_decrypt(decrypted_msg, &msg_sz,
          NULL, ciphertext, ciphertext_sz, additional_data, additional_data_sz,
          nonce, key);

    // stop counting cycles
    end_time = STOP_CYCLE_TIMER;

    if (-1 == decrypt_result) {
      printf("FAILURE: eval_aesni256gcm_decrypt failed at crypto_aead_aes256gcm_decrypt");
      exit(0);
    }

    if (cur_iter >= num_warmup) {
      times[cur_iter - num_warmup] = end_time - start_time;
    }

    // verify decrypted message is same as original for sanity check
    int cmp_result = memcmp(msg, decrypted_msg, msg_sz);
    if (0 != cmp_result) {
      printf("FAILURE: eval_aesni256gcm_decrypt failed sanity check, decrypted msg != msg");
      exit(0);
    }
  }

  FILE* ccounts_out = fopen(argv[CYCLE_COUNTS_FILE], "w");
  assert(ccounts_out != NULL && "Couldn't open cycle counts file for writing");
  
  // output the timer results
  fprintf(ccounts_out,
	  "aesni256gcm_decrypt cycle counts (%d iterations, %d warmup)\n",
	  num_iter, num_warmup);
  for (int ii = 0; ii < num_iter; ++ii) {
	  fprintf(ccounts_out, "%" PRIu64 "\n", times[ii]);
  }
  assert(fclose(ccounts_out) != EOF && "Couldn't close cycle counts file");

  #ifndef NO_DYN_HIT_COUNTS
  // record dynamic hitcounts, if applicable
//   if (argc > DYNAMIC_HITCOUNTS_FILE) {
//     print_dynamic_hitcounts(argv[DYNAMIC_HITCOUNTS_FILE]);
//   }
  #endif

  return 0;
}
