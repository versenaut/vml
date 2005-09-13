/*
 * strutil.h
 * 
 * Copyright (C) 2004 PDC, KTH. See COPYING for license details.
 * 
 * String utility functions. Prefixed by stu* since str* is reserved by C.
*/

/* Not directly related, but it seems sensible to define these here. */
#if defined _WIN32
# define snprintf	_snprintf
# define vsnprintf	_vsnprintf
#endif

/* Duplicate a string using our encapsulated memory allocation function. Free using mem_free(). */
extern char *	stu_strdup(const char *str);

/* Duplicate a string, taking care to limit its length to <max> characters. */
extern char *	stu_strdup_maxlen(const char *str, size_t max);

/* Copy string using at most <max> characters at <dest>. This is what the standard should have, imo. */
extern char *	stu_strncpy(char *dest, size_t max, const char *src);

/* Like above, but if <src> is NULL, <dest> is made into an empty string (if space permits). */
extern char *	stu_strncpy_accept_null(char *dest, size_t max, const char *src);

/* Split the given string at every occurance of the split character. Returns a NULL-terminated vector
 * of dynamically allocated string pointers, pointing at each of the parts in sequence. Empty strings
 * are suppressed. Free the returned array with a single mem_free to release all memory used by it.
 * It is not possible to prevent (by "escaping" the split character) splitting.
*/
extern char **	stu_split(const char *string, char split);
