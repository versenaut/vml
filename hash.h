/*
 * hash.h
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * A hash table might come in handy.
*/

typedef struct Hash	Hash;

typedef unsigned int (*HashFunc)(const void *key);
typedef int	     (*HashKeyEqFunc)(const void *key1, const void *key2);

/* Hashing function for ordinary nul-terminated C string keys. */
extern unsigned int	hash_hash_string(const void *key);

extern void	hash_init(void);

/* Create a new hash table. The <hfunc> should take a "key" and return an unsigned integer with
 * as a good distribution as possible. The <efunc> should answer if the given key belongs with the
 * given data or not, by returning 0 for non-match and non-zero for a match.
*/
extern Hash *	hash_new(HashFunc hfunc, HashKeyEqFunc kefunc);

/* Return a hash table for strings. Inserted elements must begin with a zero-terminated string. */
extern Hash *	hash_new_string(void);
extern void	hash_insert(Hash *hash, const void *key, void *data);
extern void *	hash_lookup(const Hash *hash, const void *key);
extern void	hash_remove(Hash *hash, const void *key);

extern size_t	hash_size(const Hash *hash);

extern void	hash_foreach(const Hash *hash, int (*func)(void *data, void *user), void *user);

extern void	hash_destroy(Hash *hash);
