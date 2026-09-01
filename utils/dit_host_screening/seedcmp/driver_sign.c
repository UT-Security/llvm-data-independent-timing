#include "hydrogen.h"
#include <stdio.h>

#define CONTEXT "Example"
#define MESSAGE "Test"
#define MESSAGE_LEN 4

int test(char const* argv[])
{
    hydro_sign_keypair key_pair;
//#pragma tainter taint(&key_pair)
    hydro_sign_keygen(&key_pair);

    uint8_t signature[hydro_sign_BYTES];

    FILE *fp = fopen(argv[1], "rb");
    fseek(fp, 0L, SEEK_END);
    int mlen = ftell(fp);

    char *message = malloc(mlen+1);

    fseek(fp, 0L, SEEK_SET);
    fread(message, mlen, 1, fp);

//#pragma tainter sinktaint(signature)
    /* Sign the message using the secret key */
    hydro_sign_create(signature, message, mlen, CONTEXT, key_pair.sk);

    /* Verify the signature using the public key */
    if (hydro_sign_verify(signature, message, mlen, CONTEXT, key_pair.pk) != 0) {
        /* forged */
        return 0;
    }
}

int main(int argc, char const* argv[]) {
    for (int i = 0; i < 100; i++)
        test(argv);
    return 0;
}
