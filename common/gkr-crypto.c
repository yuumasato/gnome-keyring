/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* gkr-crypto.c - common crypto functionality

   Copyright (C) 2007 Stefan Walter

   The Gnome Keyring Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Keyring Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: Stef Walter <stef@memberwebs.com>
*/

#include "config.h"

#include "gkr-crypto.h"

#include "common/gkr-secure-memory.h"

#include <glib.h>

#include <gcrypt.h>

#include <ctype.h>
#include <stdarg.h>

/* -----------------------------------------------------------------------------
 * UTILITIES
 */
 
static gboolean gcrypt_initialized = FALSE;

static void
log_handler (gpointer unused, int unknown, const gchar *msg, va_list va)
{
	/* TODO: Figure out additional arguments */
	g_logv ("gcrypt", G_LOG_LEVEL_MESSAGE, msg, va);
}

static int 
no_mem_handler (gpointer unused, size_t sz, unsigned int unknown)
{
	/* TODO: Figure out additional arguments */
	g_error ("couldn't allocate %d bytes of memory", sz);
	return 0;
}

static void
fatal_handler (gpointer unused, int unknown, const gchar *msg)
{
	/* TODO: Figure out additional arguments */
	g_log ("gcrypt", G_LOG_LEVEL_ERROR, msg);
}

void
gkr_crypto_setup (void)
{
	if (gcrypt_initialized)
		return;
		
	gcry_check_version (LIBGCRYPT_VERSION);
	gcry_set_log_handler (log_handler, NULL);
	gcry_set_outofcore_handler (no_mem_handler, NULL);
	gcry_set_fatalerror_handler (fatal_handler, NULL);
	gcry_set_allocation_handler ((gcry_handler_alloc_t)g_malloc, 
	                             (gcry_handler_alloc_t)gkr_secure_alloc, 
	                             gkr_secure_check, 
	                             (gcry_handler_realloc_t)gkr_secure_realloc, 
	                             gkr_secure_free);
	                             
	gcrypt_initialized = TRUE;
}

static const char HEXC[] = "0123456789abcdef";

gboolean
gkr_crypto_hex_encode (const guchar *data, gsize n_data, 
                       gchar *encoded, gsize *n_encoded)
{
	guchar j;
	
	g_return_val_if_fail (*n_encoded >= n_data * 2 + 1, FALSE);
	
	while(n_data > 0) {
		j = *(data) >> 4 & 0xf;
		*(encoded++) = HEXC[j];
    
		j = *(data++) & 0xf;
		*(encoded++) = HEXC[j];
    
		n_data--;
	}
	
	return TRUE;
}

gboolean
gkr_crypto_hex_decode (const gchar *data, gsize n_data, 
                       guchar *decoded, gsize *n_decoded)
{
	gushort j;
	gint state = 0;
	const gchar* pos;
    
	g_assert (data);
	g_assert (decoded);
	g_assert (n_decoded);
    
	g_return_val_if_fail (*n_decoded >= n_data / 2, FALSE);
	*n_decoded = 0;

	while (n_data > 0) 
    	{
    		if (!isspace (*data)) {
    			
	        	/* Find the position */
			pos = strchr (HEXC, tolower (*data));
			if (pos == 0)
				break;

			j = pos - HEXC;
			if(!state) {
				*decoded = (j & 0xf) << 4;
				state = 1;
			} else {      
				*decoded |= (j & 0xf);
				(*n_decoded)++;
				decoded++;
				state = 0;
			}
    		}
      
      		++data;
      		--n_data;
	}
  
  	g_return_val_if_fail (state == 0, FALSE);
  	
  	return TRUE;
}

/* -----------------------------------------------------------------------------
 * PASSWORD TO KEY/IV
 */

gboolean
gkr_crypto_generate_symkey_simple (int cipher_algo, int hash_algo, 
                                   const gchar *password, const guchar *salt, 
                                   gsize n_salt, int iterations, guchar **key, 
                                   guchar **iv)
{
	gcry_md_hd_t mdh;
	gcry_error_t gcry;
	guchar *digest;
	guchar *digested;
	guint n_digest;
	gint pass, i;
	gint needed_iv, needed_key;
	guchar *at_iv, *at_key;

	g_assert (cipher_algo);
	g_assert (hash_algo);

	g_return_val_if_fail (iterations >= 1, FALSE);
	
	/* 
	 * If cipher algo needs more bytes than hash algo has available
	 * then the entire hashing process is done again (with the previous
	 * hash bytes as extra input), and so on until satisfied.
	 */ 
	
	needed_key = gcry_cipher_get_algo_keylen (cipher_algo);
	needed_iv = gcry_cipher_get_algo_blklen (cipher_algo);
	
	gcry = gcry_md_open (&mdh, hash_algo, 0);
	if (gcry) {
		g_warning ("couldn't create '%s' hash context: %s", 
			   gcry_md_algo_name (hash_algo), gcry_strerror (gcry));
		return FALSE;
	}

	n_digest = gcry_md_get_algo_dlen (hash_algo);
	g_return_val_if_fail (n_digest > 0, FALSE);
	
	digest = gkr_secure_alloc (n_digest);
	g_return_val_if_fail (digest, FALSE);
	if (key) {
		*key = gkr_secure_alloc (needed_key);
		g_return_val_if_fail (*key, FALSE);
	}
	if (iv) 
		*iv = g_new0 (guchar, needed_iv);

	at_key = key ? *key : NULL;
	at_iv = iv ? *iv : NULL;

	for (pass = 0; TRUE; ++pass) {
		gcry_md_reset (mdh);
		
		/* Hash in the previous buffer on later passes */
		if (pass > 0)
			gcry_md_write (mdh, digest, n_digest);

		if (password)
			gcry_md_write (mdh, password, strlen (password));
		if (salt && n_salt)
			gcry_md_write (mdh, salt, n_salt);
		gcry_md_final (mdh);
		digested = gcry_md_read (mdh, 0);
		g_return_val_if_fail (digested, FALSE);
		memcpy (digest, digested, n_digest);
		
		for (i = 1; i < iterations; ++i) {
			gcry_md_reset (mdh);
			gcry_md_write (mdh, digest, n_digest);
			gcry_md_final (mdh);
			digested = gcry_md_read (mdh, 0);
			g_return_val_if_fail (digested, FALSE);
			memcpy (digest, digested, n_digest);
		}
		
		/* Copy as much as possible into the destinations */
		i = 0; 
		while (needed_key && i < n_digest) {
			if (at_key)
				*(at_key++) = digest[i];
			needed_key--;
			i++;
		}
		while (needed_iv && i < n_digest) {
			if (at_iv) 
				*(at_iv++) = digest[i];
			needed_iv--;
			i++;
		}
		
		if (needed_key == 0 && needed_iv == 0)
			break;
	}

	gkr_secure_free (digest);
	gcry_md_close (mdh);
	
	return TRUE;
}

gboolean
gkr_crypto_generate_symkey_pbe (int cipher_algo, int hash_algo, const gchar *password, 
                                const guchar *salt, gsize n_salt, int iterations, 
                                guchar **key, guchar **iv)
{
	gcry_md_hd_t mdh;
	gcry_error_t gcry;
	guchar *digest;
	guchar *digested;
	guint i, n_digest;
	gint needed_iv, needed_key;

	g_assert (cipher_algo);
	g_assert (hash_algo);

	g_return_val_if_fail (iterations >= 1, FALSE);
	
	/* 
	 * We only do one pass here.
	 * 
	 * The key ends up as the first needed_key bytes of the hash buffer.
	 * The iv ends up as the last needed_iv bytes of the hash buffer. 
	 * 
	 * The IV may overlap the key (which is stupid) if the wrong pair of 
	 * hash/cipher algorithms are chosen.
	 */ 

	n_digest = gcry_md_get_algo_dlen (hash_algo);
	g_return_val_if_fail (n_digest > 0, FALSE);
	
	needed_key = gcry_cipher_get_algo_keylen (cipher_algo);
	needed_iv = gcry_cipher_get_algo_blklen (cipher_algo);
	if (needed_iv + needed_key > 16 || needed_iv + needed_key > n_digest) {
		g_warning ("using PBE symkey generation with %s using an algorithm that needs " 
		           "too many bytes of key and/or IV: %s",
		           gcry_cipher_algo_name (hash_algo), 
		           gcry_cipher_algo_name (cipher_algo));
		return FALSE;
	}
	
	gcry = gcry_md_open (&mdh, hash_algo, 0);
	if (gcry) {
		g_warning ("couldn't create '%s' hash context: %s", 
			   gcry_md_algo_name (hash_algo), gcry_strerror (gcry));
		return FALSE;
	}

	digest = gkr_secure_alloc (n_digest);
	g_return_val_if_fail (digest, FALSE);
	if (key) {
		*key = gkr_secure_alloc (needed_key);
		g_return_val_if_fail (*key, FALSE);
	}
	if (iv) 
		*iv = g_new0 (guchar, needed_iv);

	if (password)
		gcry_md_write (mdh, password, strlen (password));
	if (salt && n_salt)
		gcry_md_write (mdh, salt, n_salt);
	gcry_md_final (mdh);
	digested = gcry_md_read (mdh, 0);
	g_return_val_if_fail (digested, FALSE);
	memcpy (digest, digested, n_digest);
		
	for (i = 1; i < iterations; ++i)
		gcry_md_hash_buffer (hash_algo, digest, digest, n_digest);
	
	/* The first x bytes are the key */
	if (key) {
		g_assert (needed_key <= n_digest);
		memcpy (*key, digest, needed_key);
	}
	
	/* The last 16 - x bytes are the iv */
	if (iv) {
		g_assert (needed_iv <= n_digest && n_digest >= 16);
		memcpy (*iv, digest + (16 - needed_iv), needed_iv);
	}
		
	gkr_secure_free (digest);
	gcry_md_close (mdh);
	
	return TRUE;	
}

static gboolean
generate_pkcs12 (int hash_algo, int type, const gchar *utf8_password, 
                 const guchar *salt, gsize n_salt, int iterations,
                 guchar *output, gsize n_output)
{
	gcry_mpi_t num_b1, num_ij;
	guchar *hash, *buf_i, *buf_b;
	gcry_md_hd_t mdh;
	const gchar *p2;
	guchar *p;
	gsize n_hash, i;
	gunichar unich;
	gcry_error_t gcry;
	
	num_b1 = num_ij = NULL;
	
	n_hash = gcry_md_get_algo_dlen (hash_algo);
	g_return_val_if_fail (n_hash > 0, FALSE);
	
	gcry = gcry_md_open (&mdh, hash_algo, 0);
	if (gcry) {
		g_warning ("couldn't create '%s' hash context: %s", 
		           gcry_md_algo_name (hash_algo), gcry_strerror (gcry));
		return FALSE;
	}

	/* Reqisition me a buffer */
	hash = gkr_secure_alloc (n_hash);
	buf_i = gkr_secure_alloc (128);
	buf_b = gkr_secure_alloc (64);
	g_return_val_if_fail (hash && buf_i && buf_b, FALSE);
		
	/* Bring in the salt */
	p = buf_i;
	if (salt) {
		for (i = 0; i < 64; ++i)
			*(p++) = salt[i % n_salt];
	} else {
		memset (p, 0, 64);
		p += 64;
	}
	
	/* Empty password is treated as a NULL password */
	if(!utf8_password[0])
		utf8_password = NULL;
	
	/* Bring in the password, as 16bits per character BMP string, ie: UCS2 */
	if (utf8_password) {
		p2 = utf8_password;
		for (i = 0; i < 64; i += 2) {
			unich = g_utf8_get_char (p2);
			*(p++) = (unich & 0xFF00) >> 8;
			*(p++) = (unich & 0xFF);
			if (*p2) /* Loop back to beginning if more bytes are needed */
				p2 = g_utf8_next_char (p2);
			else
				p2 = utf8_password;
		}
	} else {
		memset (p, 0, 64);
		p += 64;
	}
	
	/* Hash and bash */
	for (;;) {
		gcry_md_reset (mdh);

		/* Put in the PKCS#12 type of key */
		for (i = 0; i < 64; ++i)
			gcry_md_putc (mdh, type);
			
		/* Bring in the password */
		gcry_md_write (mdh, buf_i, utf8_password ? 128 : 64);
		
		/* First iteration done */
		memcpy (hash, gcry_md_read (mdh, hash_algo), n_hash);
		
		/* All the other iterations */
		for (i = 1; i < iterations; i++)
			gcry_md_hash_buffer (hash_algo, hash, hash, n_hash);
		
		/* Take out as much as we need */
		for (i = 0; i < n_hash && n_output; ++i) {
			*(output++) = hash[i];
			--n_output;
		}
		
		/* Is that enough generated keying material? */
		if (!n_output)
			break;
			
		/* Need more bytes, do some voodoo */
		for (i = 0; i < 64; ++i)
			buf_b[i] = hash[i % n_hash];
		gcry = gcry_mpi_scan (&num_b1, GCRYMPI_FMT_USG, buf_b, 64, NULL);
		g_return_val_if_fail (gcry == 0, FALSE);
		gcry_mpi_add_ui (num_b1, num_b1, 1);
		for (i = 0; i < 128; i += 64) {
			gcry = gcry_mpi_scan (&num_ij, GCRYMPI_FMT_USG, buf_i + i, 64, NULL);
			g_return_val_if_fail (gcry == 0, FALSE);
			gcry_mpi_add (num_ij, num_ij, num_b1);
			gcry_mpi_clear_highbit (num_ij, 64 * 8);
			gcry = gcry_mpi_print (GCRYMPI_FMT_USG, buf_i + i, 64, NULL, num_ij);
			g_return_val_if_fail (gcry == 0, FALSE);
			gcry_mpi_release (num_ij);
		}
	}  
	
	gkr_secure_free (buf_i);
	gkr_secure_free (buf_b);
	gkr_secure_free (hash);
	gcry_mpi_release (num_b1);
	gcry_md_close (mdh);
	
	return TRUE;
}

gboolean
gkr_crypto_generate_symkey_pkcs12 (int cipher_algo, int hash_algo, const gchar *password, 
                                   const guchar *salt, gsize n_salt,
                                   int iterations, guchar **key, guchar **iv)
{
	gsize n_block, n_key;
	gboolean ret = TRUE;
	
	g_return_val_if_fail (cipher_algo, FALSE);
	g_return_val_if_fail (hash_algo, FALSE);
	g_return_val_if_fail (password, FALSE);
	g_return_val_if_fail (iterations > 0, FALSE);
	
	n_key = gcry_cipher_get_algo_keylen (cipher_algo);
	n_block = gcry_cipher_get_algo_blklen (cipher_algo);
	
	if (!g_utf8_validate (password, -1, NULL)) {
		g_warning ("invalid non-UTF8 password");
		g_return_val_if_reached (FALSE);
	}
	
	if (key)
		*key = NULL;
	if (iv)
		*iv = NULL;
	
	/* Generate us an key */
	if (key) {
		*key = gkr_secure_alloc (n_key);
		g_return_val_if_fail (*key != NULL, FALSE);
		ret = generate_pkcs12 (hash_algo, 1, password, salt, n_salt, 
		                       iterations, *key, n_key);
	} 
	
	/* Generate us an iv */
	if (ret && iv) {
		if (n_block > 1) {
			*iv = g_malloc (n_block);
			ret = generate_pkcs12 (hash_algo, 2, password, salt, n_salt, 
			                       iterations, *iv, n_block);
		} else {
			*iv = NULL;
		}
	}
	
	/* Cleanup in case of failure */
	if (!ret) {
		g_free (iv ? *iv : NULL);
		g_free (key ? *key : NULL);
	}
	
	return ret;
}

static gboolean
generate_pbkdf2 (int hash_algo, const gchar *password, gsize n_password,
		 const guchar *salt, gsize n_salt, guint iterations,
		 guchar *output, gsize n_output)
{
	gcry_md_hd_t mdh;
	guint u, l, r, i, k;
	gcry_error_t gcry;
	guchar *U, *T, *buf;
	gsize n_buf, n_hash;
	
	g_return_val_if_fail (hash_algo > 0, FALSE);
	g_return_val_if_fail (iterations > 0, FALSE);
	g_return_val_if_fail (n_output > 0, FALSE);
	g_return_val_if_fail (n_output < G_MAXUINT32, FALSE);

	n_hash = gcry_md_get_algo_dlen (hash_algo);
	g_return_val_if_fail (n_hash > 0, FALSE);
	
	gcry = gcry_md_open (&mdh, hash_algo, GCRY_MD_FLAG_HMAC);
	if (gcry != 0) {
		g_warning ("couldn't create '%s' hash context: %s", 
		           gcry_md_algo_name (hash_algo), gcry_strerror (gcry));
		return FALSE;
	}

	/* Get us a temporary buffers */
	T = gkr_secure_alloc (n_hash);
	U = gkr_secure_alloc (n_hash);
	n_buf = n_salt + 4;
	buf = gkr_secure_alloc (n_buf);
	g_return_val_if_fail (buf && T && U, FALSE);

	/* n_hash blocks in output, rounding up */
	l = ((n_output - 1) / n_hash) + 1;
	
	/* number of bytes in last, rounded up, n_hash block */
	r = n_output - (l - 1) * n_hash;
	
	memcpy (buf, salt, n_salt);
	for (i = 1; i <= l; i++) {
		memset (T, 0, n_hash);
		for (u = 1; u <= iterations; u++) {
			gcry_md_reset (mdh);

			gcry = gcry_md_setkey (mdh, password, n_password);
			g_return_val_if_fail (gcry == 0, FALSE);
			
			/* For first iteration on each block add 4 extra bytes */
			if (u == 1) {
				buf[n_salt + 0] = (i & 0xff000000) >> 24;
				buf[n_salt + 1] = (i & 0x00ff0000) >> 16;
				buf[n_salt + 2] = (i & 0x0000ff00) >> 8;
				buf[n_salt + 3] = (i & 0x000000ff) >> 0;
				
				gcry_md_write (mdh, buf, n_buf);
		
			/* Other iterations, any block */
			} else {
				gcry_md_write (mdh, U, n_hash);
			}
			
			memcpy (U, gcry_md_read (mdh, hash_algo), n_hash);

			for (k = 0; k < n_hash; k++)
				T[k] ^= U[k];
		}

		memcpy (output + (i - 1) * n_hash, T, i == l ? r : n_hash);
	}
	
	gkr_secure_free (T);
	gkr_secure_free (U);
	gkr_secure_free (buf);
	gcry_md_close (mdh);
	return TRUE;
}

gboolean
gkr_crypto_generate_symkey_pbkdf2 (int cipher_algo, int hash_algo, 
                                   const gchar *password, const guchar *salt, 
                                   gsize n_salt, int iterations, 
                                   guchar **key, guchar **iv)
{
	gsize n_key, n_block, n_password;
	gboolean ret = TRUE;
	
	g_return_val_if_fail (hash_algo, FALSE);
	g_return_val_if_fail (cipher_algo, FALSE);
	g_return_val_if_fail (password, FALSE);
	g_return_val_if_fail (iterations > 0, FALSE);
	
	n_key = gcry_cipher_get_algo_keylen (cipher_algo);
	n_block = gcry_cipher_get_algo_blklen (cipher_algo);
	
	if (key)
		*key = NULL;
	if (iv)
		*iv = NULL;
		
	n_password = password ? strlen (password) : 0;
	
	/* Generate us an key */
	if (key) {
		*key = gkr_secure_alloc (n_key);
		g_return_val_if_fail (*key != NULL, FALSE);
		ret = generate_pbkdf2 (hash_algo, password, n_password, salt, n_salt, 
		                       iterations, *key, n_key);
	} 
	
	/* Generate us an iv */
	if (ret && iv) {
		if (n_block > 1) {
			*iv = g_malloc (n_block);
			gcry_create_nonce (*iv, n_block);
		} else {
			*iv = NULL;
		}
	}
	
	/* Cleanup in case of failure */
	if (!ret) {
		g_free (iv ? *iv : NULL);
		g_free (key ? *key : NULL);
	}
	
	return ret;
}

/* -----------------------------------------------------------------------------
 * MPI HELPERS
 */
 
static gcry_sexp_t
sexp_get_childv (gcry_sexp_t sexp, va_list va)
{
	gcry_sexp_t at = NULL;
	gcry_sexp_t child;
	const char *name;
	
	for(;;) {
		name = va_arg (va, const char*);
		if (!name)
			break;

		child = gcry_sexp_find_token (at ? at : sexp, name, 0);
		gcry_sexp_release (at);
		at = child;
		if (at == NULL)
			break;
	}
	
	va_end (va);

	return at;
}
 
gcry_sexp_t
gkr_crypto_sexp_get_child (gcry_sexp_t sexp, ...)
{
	gcry_sexp_t child; 
	va_list va;
		
	va_start (va, sexp);
	child = sexp_get_childv (sexp, va);
	va_end (va);
	
	return child;
}

gboolean
gkr_crypto_sexp_extract_mpi (gcry_sexp_t sexp, gcry_mpi_t *mpi, ...)
{
	gcry_sexp_t at = NULL;
	va_list va;
	
	g_assert (sexp);
	g_assert (mpi);
	
	va_start (va, mpi);
	at = sexp_get_childv (sexp, va);
	va_end (va);
	
	*mpi = NULL;
	if (at)
		*mpi = gcry_sexp_nth_mpi (at ? at : sexp, 1, GCRYMPI_FMT_USG);
	if (at)
		gcry_sexp_release (at);

	return (*mpi) ? TRUE : FALSE;
}


void
gkr_crypto_sexp_dump (gcry_sexp_t sexp)
{
	gsize len;
	gchar *buf;
	
	len = gcry_sexp_sprint (sexp, GCRYSEXP_FMT_ADVANCED, NULL, 0);
	buf = g_malloc (len);
	gcry_sexp_sprint (sexp, GCRYSEXP_FMT_ADVANCED, buf, len);
	g_printerr (buf);
	g_free (buf);
}

#define PUBLIC_KEY "public-key"
#define PUBLIC_KEY_L 10
#define PRIVATE_KEY "private-key"
#define PRIVATE_KEY_L 11

gboolean
gkr_crypto_skey_parse (gcry_sexp_t s_key, int *algorithm, gboolean *is_priv, 
                       gcry_sexp_t *numbers)
{
	gboolean ret = FALSE;
	gcry_sexp_t child = NULL;
	gchar *str = NULL;
  	const gchar *data;
  	gsize n_data;
  	gboolean priv;
  	int algo;

	data = gcry_sexp_nth_data (s_key, 0, &n_data);
	if (!data) 
		goto done;

	if (n_data == PUBLIC_KEY_L && strncmp (data, PUBLIC_KEY, PUBLIC_KEY_L) == 0)
		priv = FALSE;
	else if (n_data == PRIVATE_KEY_L && strncmp (data, PRIVATE_KEY, PRIVATE_KEY_L) == 0)
		priv = TRUE;
	else
		goto done;

	child = gcry_sexp_nth (s_key, 1);
	if (!child)
		goto done;
		
	data = gcry_sexp_nth_data (child, 0, &n_data);
	if (!data)
		goto done;
		
	str = g_alloca (n_data + 1);
	memcpy (str, data, n_data);
	str[n_data] = 0;
	
	algo = gcry_pk_map_name (str);
	if (!algo)
		goto done;

	/* Yay all done */
	if (algorithm)
		*algorithm = algo;
	if (numbers) {
		*numbers = child;
		child = NULL;
	}
	if (is_priv)
		*is_priv = priv;

	ret = TRUE;
	
done:
	gcry_sexp_release (child);
	return ret;
}

gkrunique
gkr_crypto_skey_make_id (gcry_sexp_t s_key)
{
	guchar hash[20];
	
	if (!gcry_pk_get_keygrip (s_key, hash))
		g_return_val_if_reached (NULL);
	
	return gkr_unique_new (hash, sizeof (hash));
}

static gcry_sexp_t
rsa_numbers_to_public (gcry_sexp_t rsa)
{
	gcry_sexp_t pubkey = NULL;
	gcry_mpi_t n, e;
	gcry_error_t gcry;
	
	n = e = NULL;
	
	gkr_crypto_sexp_dump (rsa);
	
	if (!gkr_crypto_sexp_extract_mpi (rsa, &n, "n", NULL) || 
	    !gkr_crypto_sexp_extract_mpi (rsa, &e, "e", NULL))
	    	goto done;
	    	
	gcry = gcry_sexp_build (&pubkey, NULL, "(public-key (rsa (n %m) (e %m)))",
	                        n, e);
	if (gcry)
		goto done;
	g_assert (pubkey);
	
done:
	gcry_mpi_release (n);
	gcry_mpi_release (e);

	/* This should have worked */
	g_return_val_if_fail (pubkey != NULL, NULL);
	return pubkey;
}

static gcry_sexp_t
dsa_numbers_to_public (gcry_sexp_t dsa)
{
	gcry_mpi_t p, q, g, y;
	gcry_sexp_t pubkey = NULL;
	gcry_error_t gcry;
	
	p = q = g = y = NULL;
	
	if (!gkr_crypto_sexp_extract_mpi (dsa, &p, "p", NULL) || 
	    !gkr_crypto_sexp_extract_mpi (dsa, &q, "q", NULL) ||
	    !gkr_crypto_sexp_extract_mpi (dsa, &g, "g", NULL) ||
	    !gkr_crypto_sexp_extract_mpi (dsa, &y, "y", NULL))
	    	goto done;
	    	
	gcry = gcry_sexp_build (&pubkey, NULL, "(public-key (dsa (p %m) (q %m) (g %m) (y %m)))",
	                        p, q, g, y);
	if (gcry)
		goto done;
	g_assert (pubkey);
	
done:
	gcry_mpi_release (p);
	gcry_mpi_release (q);
	gcry_mpi_release (g);
	gcry_mpi_release (y);

	/* This should have worked */	
	g_return_val_if_fail (pubkey != NULL, NULL);
	return pubkey;
}

gboolean
gkr_crypto_skey_private_to_public (gcry_sexp_t privkey, gcry_sexp_t *pubkey)
{
	gcry_sexp_t numbers;
	int algorithm;

	if (!gkr_crypto_skey_parse (privkey, &algorithm, NULL, &numbers))
		g_return_val_if_reached (FALSE);
		
	switch (algorithm) {
	case GCRY_PK_RSA:
		*pubkey = rsa_numbers_to_public (numbers);
		break;
	case GCRY_PK_DSA:
		*pubkey = dsa_numbers_to_public (numbers);
		break;
	default:
		g_return_val_if_reached (FALSE);
	} 
	
	gcry_sexp_release (numbers);
	return *pubkey ? TRUE : FALSE;
}