/* MIT License. */

/* This file is taken from TF Protocol source code.
    It may have variations to adapt it to EPOCH Protocol. */

/*  programmer: luis miguel
    email: lmdelbahia@gmail.com  */

#ifndef CRYPTO_H
#define CRYPTO_H

#include <openssl/ssl.h>
#include <inttypes.h>

/* Session key max length in bytes. Must be less than the RSA key length
    modulus minus used padding. In this case is 2048 / 8 - 42 = 214 bytes. */
#define KEYMAX 2048 / 8 - 42
/* Minimum session key length. */
#define KEYMIN 16
/* RSA key lenght modulus. */
#define RSA_KEYLEN 2048 / 8

/* The posible states of encrypt system. */
enum cryptst { CRYPT_OFF, CRYPT_ON };

/* Message packing mode. */
enum pack { CRYPT_PACK, CRYPT_UNPACK, CRYPT_NOPACK };

/* Structure to store encryption key and function pointer to 
    encrypt/decrypt. It uses symmetric xor encryption. */
struct crypto {
    void (*encrypt)(struct crypto *cryp, char *data, int len);
    char rndkey[KEYMAX];
    int rndlen;
    char *privkey;
    char enkey[RSA_KEYLEN];
    enum cryptst st;
    int64_t seed;
    enum pack pack;
};

/* Initialize function pointers and random key. */
void initcrypto(struct crypto *cryp);
/* Decrypt the session key sent by the client with server's private key. */
int derankey(struct crypto *crypt);
/* Duplicate crypt structure. */
void dup_crypt(struct crypto *to, struct crypto *from);

#endif
