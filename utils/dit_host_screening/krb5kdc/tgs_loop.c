/* Experiment 13 load generator: one AS-REQ for a TGT into a memory ccache, then N
 * TGS-REQs for the same service with KRB5_GC_NO_STORE so every one goes to the KDC.
 * usage: tgs_loop user password service N */
#include <krb5.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(int argc, char **argv) {
    krb5_context ctx; krb5_ccache cc; krb5_principal me, svc; krb5_creds creds, in, *out;
    krb5_get_init_creds_opt *opt; krb5_error_code r; int i, n;
    if (argc != 5) { fprintf(stderr, "usage: tgs_loop user password service N\n"); return 1; }
    n = atoi(argv[4]);
    if ((r = krb5_init_context(&ctx))) { fprintf(stderr, "init_context: %d\n", (int)r); return 1; }
    if ((r = krb5_cc_new_unique(ctx, "MEMORY", NULL, &cc))) { fprintf(stderr, "cc: %s\n", krb5_get_error_message(ctx, r)); return 1; }
    if ((r = krb5_parse_name(ctx, argv[1], &me))) { fprintf(stderr, "parse: %s\n", krb5_get_error_message(ctx, r)); return 1; }
    krb5_get_init_creds_opt_alloc(ctx, &opt);
    krb5_get_init_creds_opt_set_out_ccache(ctx, opt, cc);
    if ((r = krb5_get_init_creds_password(ctx, &creds, me, argv[2], NULL, NULL, 0, NULL, opt))) {
        fprintf(stderr, "AS-REQ: %s\n", krb5_get_error_message(ctx, r)); return 1; }
    if ((r = krb5_parse_name(ctx, argv[3], &svc))) { fprintf(stderr, "parse svc: %s\n", krb5_get_error_message(ctx, r)); return 1; }
    for (i = 0; i < n; i++) {
        memset(&in, 0, sizeof(in)); in.client = me; in.server = svc;
        if ((r = krb5_get_credentials(ctx, KRB5_GC_NO_STORE, cc, &in, &out))) {
            fprintf(stderr, "TGS-REQ %d: %s\n", i, krb5_get_error_message(ctx, r)); return 2; }
        krb5_free_creds(ctx, out);
    }
    printf("ok %d\n", n);
    return 0;
}
