/*
 * Copyright (c) 2016 Thomas Pornin <pornin@bolet.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be 
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, 
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND 
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>

#include "brssl.h"
#include "bearssl.h"

static void
print_int_text(const char *name, const unsigned char *buf, size_t len)
{
	size_t u;

	printf("%s = ", name);
	for (u = 0; u < len; u ++) {
		printf("%02X", buf[u]);
	}
	printf("\n");
}

static void
print_int_C(const char *name, const unsigned char *buf, size_t len)
{
	size_t u;

	printf("\nstatic const unsigned char %s[] = {", name);
	for (u = 0; u < len; u ++) {
		if (u != 0) {
			printf(",");
		}
		if (u % 12 == 0) {
			printf("\n\t");
		} else {
			printf(" ");
		}
		printf("0x%02X", buf[u]);
	}
	printf("\n};\n");
}

static void
print_rsa(const br_rsa_private_key *sk, int print_text, int print_C)
{
	if (print_text) {
		print_int_text("p ", sk->p, sk->plen);
		print_int_text("q ", sk->q, sk->qlen);
		print_int_text("dp", sk->dp, sk->dplen);
		print_int_text("dq", sk->dq, sk->dqlen);
		print_int_text("iq", sk->iq, sk->iqlen);
	}
	if (print_C) {
		print_int_C("RSA_P", sk->p, sk->plen);
		print_int_C("RSA_Q", sk->q, sk->qlen);
		print_int_C("RSA_DP", sk->dp, sk->dplen);
		print_int_C("RSA_DQ", sk->dq, sk->dqlen);
		print_int_C("RSA_IQ", sk->iq, sk->iqlen);
		printf("\nstatic const br_rsa_private_key RSA = {\n");
		printf("\t%lu,\n", (unsigned long)sk->n_bitlen);
		printf("\t(unsigned char *)RSA_P, sizeof RSA_P,\n");
		printf("\t(unsigned char *)RSA_Q, sizeof RSA_Q,\n");
		printf("\t(unsigned char *)RSA_DP, sizeof RSA_DP,\n");
		printf("\t(unsigned char *)RSA_DQ, sizeof RSA_DQ,\n");
		printf("\t(unsigned char *)RSA_IQ, sizeof RSA_IQ\n");
		printf("};\n");
	}
}

static void
print_ec(const br_ec_private_key *sk, int print_text, int print_C)
{
	if (print_text) {
		print_int_text("x", sk->x, sk->xlen);
	}
	if (print_C) {
		print_int_C("EC_X", sk->x, sk->xlen);
		printf("\nstatic const br_ec_private_key EC = {\n");
		printf("\t%d,\n", sk->curve);
		printf("\t(unsigned char *)EC_X, sizeof EC_X\n");
		printf("};\n");
	}
}

static int
parse_rsa_spec(const char *kgen_spec, unsigned *size, uint32_t *pubexp)
{
	const char *p;
	char *end;
	unsigned long ul;

	p = kgen_spec;
	if (*p != 'r' && *p != 'R') {
		return 0;
	}
	p ++;
	if (*p != 's' && *p != 'S') {
		return 0;
	}
	p ++;
	if (*p != 'a' && *p != 'A') {
		return 0;
	}
	p ++;
	if (*p == 0) {
		*size = 2048;
		*pubexp = 3;
		return 1;
	} else if (*p != ':') {
		return 0;
	}
	p ++;
	ul = strtoul(p, &end, 10);
	if (ul < 512 || ul > 32768) {
		return 0;
	}
	*size = ul;
	p = end;
	if (*p == 0) {
		*pubexp = 3;
		return 1;
	} else if (*p != ':') {
		return 0;
	}
	p ++;
	ul = strtoul(p, &end, 10);
	if ((ul & 1) == 0 || ul == 1 || ((ul >> 30) >> 2) != 0) {
		return 0;
	}
	*pubexp = ul;
	if (*end != 0) {
		return 0;
	}
	return 1;
}

static int
keygen_rsa(unsigned size, uint32_t pubexp, int print_text, int print_C)
{
	br_hmac_drbg_context rng;
	br_prng_seeder seeder;
	br_rsa_keygen kg;
	br_rsa_private_key sk;
	unsigned char *kbuf_priv;
	uint32_t r;

	seeder = br_prng_seeder_system(NULL);
	if (seeder == 0) {
		fprintf(stderr, "ERROR: no system source of randomness\n");
		return 0;
	}
	br_hmac_drbg_init(&rng, &br_sha256_vtable, NULL, 0);
	if (!seeder(&rng.vtable)) {
		fprintf(stderr, "ERROR: system source of randomness failed\n");
		return 0;
	}
	kbuf_priv = xmalloc(BR_RSA_KBUF_PRIV_SIZE(size));
	kg = br_rsa_keygen_get_default();
	r = kg(&rng.vtable, &sk, kbuf_priv, NULL, NULL, size, pubexp);
	if (!r) {
		fprintf(stderr, "ERROR: RSA key pair generation failed\n");
	} else {
		print_rsa(&sk, print_text, print_C);
	}
	xfree(kbuf_priv);
	return r;
}

static int
decode_key(const unsigned char *buf, size_t len, int print_text, int print_C)
{
	br_skey_decoder_context dc;
	int err;

	br_skey_decoder_init(&dc);
	br_skey_decoder_push(&dc, buf, len);
	err = br_skey_decoder_last_error(&dc);
	if (err != 0) {
		const char *errname, *errmsg;

		fprintf(stderr, "ERROR (decoding): err=%d\n", err);
		errname = find_error_name(err, &errmsg);
		if (errname != NULL) {
			fprintf(stderr, "  %s: %s\n", errname, errmsg);
		} else {
			fprintf(stderr, "  (unknown)\n");
		}
		return -1;
	}
	switch (br_skey_decoder_key_type(&dc)) {
		const br_rsa_private_key *rk;
		const br_ec_private_key *ek;

	case BR_KEYTYPE_RSA:
		rk = br_skey_decoder_get_rsa(&dc);
		printf("RSA key (%lu bits)\n", (unsigned long)rk->n_bitlen);
		print_rsa(rk, print_text, print_C);
		break;

	case BR_KEYTYPE_EC:
		ek = br_skey_decoder_get_ec(&dc);
		printf("EC key (curve = %d: %s)\n",
			ek->curve, ec_curve_name(ek->curve));
		print_ec(ek, print_text, print_C);
		break;

	default:
		fprintf(stderr, "Unknown key type: %d\n",
			br_skey_decoder_key_type(&dc));
		return -1;
	}

	return 0;
}

static void
usage_skey(void)
{
	fprintf(stderr,
"usage: brssl skey [ options ] file...\n");
	fprintf(stderr,
"options:\n");
	fprintf(stderr,
"   -q            suppress verbose messages\n");
	fprintf(stderr,
"   -text         print public key details (human-readable)\n");
	fprintf(stderr,
"   -C            print public key details (C code)\n");
	fprintf(stderr,
"   -gen spec     generate a new key using the provided key specification\n");
	fprintf(stderr,
"Key specification begins with a key type, followed by optional parameters\n");
	fprintf(stderr,
"that depend on the key type, separated by colon characters:\n");
	fprintf(stderr,
"   rsa[:size[:pubexep]]   RSA key (defaults: size = 2048, pubexp = 3)\n");
}

/* see brssl.h */
int
do_skey(int argc, char *argv[])
{
	int retcode;
	int verbose;
	int i, num_files;
	int print_text, print_C;
	unsigned char *buf;
	size_t len;
	pem_object *pos;
	const char *kgen_spec;

	retcode = 0;
	verbose = 1;
	print_text = 0;
	print_C = 0;
	num_files = 0;
	buf = NULL;
	pos = NULL;
	kgen_spec = NULL;
	for (i = 0; i < argc; i ++) {
		const char *arg;

		arg = argv[i];
		if (arg[0] != '-') {
			num_files ++;
			continue;
		}
		argv[i] = NULL;
		if (eqstr(arg, "-v") || eqstr(arg, "-verbose")) {
			verbose = 1;
		} else if (eqstr(arg, "-q") || eqstr(arg, "-quiet")) {
			verbose = 0;
		} else if (eqstr(arg, "-text")) {
			print_text = 1;
		} else if (eqstr(arg, "-C")) {
			print_C = 1;
		} else if (eqstr(arg, "-gen")) {
			if (++ i >= argc) {
				fprintf(stderr,
					"ERROR: no argument for '-gen'\n");
				usage_skey();
				goto skey_exit_error;
			}
			if (kgen_spec != NULL) {
				fprintf(stderr,
					"ERROR: multiple '-gen' options\n");
				usage_skey();
				goto skey_exit_error;
			}
			kgen_spec = argv[i];
			argv[i] = NULL;
		} else {
			fprintf(stderr, "ERROR: unknown option: '%s'\n", arg);
			usage_skey();
			goto skey_exit_error;
		}
	}
	if (kgen_spec != NULL) {
		unsigned rsa_size;
		uint32_t rsa_pubexp;

		if (num_files != 0) {
			fprintf(stderr,
				"ERROR: key files provided while generating\n");
			usage_skey();
			goto skey_exit_error;
		}

		if (parse_rsa_spec(kgen_spec, &rsa_size, &rsa_pubexp)) {
			keygen_rsa(rsa_size, rsa_pubexp, print_text, print_C);
		} else {
			fprintf(stderr,
				"ERROR: unknown key specification: '%s'\n",
				kgen_spec);
			usage_skey();
			goto skey_exit_error;
		}
	} else if (num_files == 0) {
		fprintf(stderr, "ERROR: no private key provided\n");
		usage_skey();
		goto skey_exit_error;
	}

	for (i = 0; i < argc; i ++) {
		const char *fname;

		fname = argv[i];
		if (fname == NULL) {
			continue;
		}
		buf = read_file(fname, &len);
		if (buf == NULL) {
			goto skey_exit_error;
		}
		if (looks_like_DER(buf, len)) {
			if (verbose) {
				fprintf(stderr, "File '%s': ASN.1/DER object\n",
					fname);
			}
			if (decode_key(buf, len, print_text, print_C) < 0) {
				goto skey_exit_error;
			}
		} else {
			size_t u, num;

			if (verbose) {
				fprintf(stderr, "File '%s': decoding as PEM\n",
					fname);
			}
			pos = decode_pem(buf, len, &num);
			if (pos == NULL) {
				goto skey_exit_error;
			}
			for (u = 0; pos[u].name; u ++) {
				const char *name;

				name = pos[u].name;
				if (eqstr(name, "RSA PRIVATE KEY")
					|| eqstr(name, "EC PRIVATE KEY")
					|| eqstr(name, "PRIVATE KEY"))
				{
					if (decode_key(pos[u].data,
						pos[u].data_len,
						print_text, print_C) < 0)
					{
						goto skey_exit_error;
					}
				} else {
					if (verbose) {
						fprintf(stderr,
							"(skipping '%s')\n",
							name);
					}
				}
			}
			for (u = 0; pos[u].name; u ++) {
				free_pem_object_contents(&pos[u]);
			}
			xfree(pos);
			pos = NULL;
		}
		xfree(buf);
		buf = NULL;
	}

	/*
	 * Release allocated structures.
	 */
skey_exit:
	xfree(buf);
	if (pos != NULL) {
		size_t u;

		for (u = 0; pos[u].name; u ++) {
			free_pem_object_contents(&pos[u]);
		}
		xfree(pos);
	}
	return retcode;

skey_exit_error:
	retcode = -1;
	goto skey_exit;
}
