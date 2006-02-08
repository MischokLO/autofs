#ident "$Id: cache.c,v 1.18 2006/02/08 16:49:20 raven Exp $"
/* ----------------------------------------------------------------------- *
 *   
 *  cache.c - mount entry cache management routines
 *
 *   Copyright 2002-2005 Ian Kent <raven@themaw.net> - All Rights Reserved
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version; incorporated herein by reference.
 *
 * ----------------------------------------------------------------------- */


#include <stdio.h>
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "automount.h"

extern int kproto_version;	/* Kernel protocol major version */
extern int kproto_sub_version;	/* Kernel protocol minor version */

#define HASHSIZE      77

pthread_rwlock_t cache_rwlock;
static struct mapent_cache *mapent_hash[HASHSIZE];
static unsigned long cache_ino_index[HASHSIZE];

void cache_dump_multi(struct list_head *list)
{
	struct list_head *p;
	struct mapent_cache *me;

	list_for_each(p, list) {
		me = list_entry(p, struct mapent_cache, multi_list);
		info("key = %s", me->key);
	}
}

static char *cache_fullpath(const char *root, const char *key)
{
	int l;
	char *path;

	if (*key == '/') {
		l = strlen(key) + 1;
		if (l > KEY_MAX_LEN)
			return NULL;
		path = malloc(l);
		strcpy(path, key);
	} else {
		l = strlen(key) + 1 + strlen(root) + 1; 
		if (l > KEY_MAX_LEN)
			return NULL;
		path = malloc(l);
		sprintf(path, "%s/%s", root, key);
	}

	return path;
}

void cache_init(void)
{
	int i, status;

	cache_release();

	status = pthread_rwlock_init(&cache_rwlock, NULL);
	if (status)
		error("mapent cache init rwlock lock failed");

	status = pthread_rwlock_wrlock(&cache_rwlock);
	if (status)
		error("mapent cache init rwlock lock failed");

	for (i = 0; i < HASHSIZE; i++) {
		mapent_hash[i] = NULL;
		cache_ino_index[i] = -1;
	}

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");
}

static unsigned int hash(const char *key)
{
	unsigned long hashval;
	char *s = (char *) key;

	for (hashval = 0; *s != '\0';)
		hashval += *s++;

	return hashval % HASHSIZE;
}

static unsigned int ino_hash(dev_t dev, ino_t ino)
{
	unsigned long hashval;

	hashval = dev + ino;

	return hashval % HASHSIZE;
}

void cache_set_ino_index(const char *key, dev_t dev, ino_t ino)
{
	unsigned int ino_index = ino_hash(dev, ino);
	unsigned int key_hash = hash(key);

	cache_ino_index[ino_index] = key_hash;

	return;
}

void cache_set_ino(struct mapent_cache *me, dev_t dev, ino_t ino)
{
	if (!me) {
		warn("can't set index for entry");
		return;
	}

	me->dev = dev;
	me->ino = ino;
}

struct mapent_cache *cache_lookup_ino(dev_t dev, ino_t ino)
{
	struct mapent_cache *me = NULL;
	unsigned int ino_index;
	unsigned int index;
	int status;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		return NULL;
	}

	ino_index = ino_hash(dev, ino);
	index = cache_ino_index[ino_index];
	if (index == -1)
		goto done;

	for (me = mapent_hash[index]; me != NULL; me = me->next) {
		if (me->dev != dev || me->ino != ino)
			continue;
		goto done;
	}
done:
	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return me;
}

static struct mapent_cache *__cache_lookup_first(void)
{
	struct mapent_cache *me = NULL;
	int i;

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (me != NULL)
			break;
	}
	return me;
}

struct mapent_cache *cache_lookup_first(void)
{
	struct mapent_cache *me = NULL;
	int status;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock lock failed");

	me = __cache_lookup_first();

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return me;
}

static struct mapent_cache *__cache_lookup(const char *key)
{
	struct mapent_cache *me = NULL;

	for (me = mapent_hash[hash(key)]; me != NULL; me = me->next) {
		if (strcmp(key, me->key) == 0)
			goto done;
	}

	me = __cache_lookup_first();
	if (me != NULL) {
		/* Can't have wildcard in direct map */
		if (*me->key == '/') {
			me = NULL;
			goto done;
		}

		for (me = mapent_hash[hash("*")]; me != NULL; me = me->next)
			if (strcmp("*", me->key) == 0)
				goto done;
	}
done:
	return me;
}

struct mapent_cache *cache_lookup(const char *key)
{
	struct mapent_cache *me = NULL;
	int status;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock lock failed");

	me = __cache_lookup(key);

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return me;
}

static struct mapent_cache *__cache_lookup_next(struct mapent_cache *me)
{
	struct mapent_cache *next = me->next;

	while (next != NULL) {
		if (!strcmp(me->key, next->key))
			return next;
		next = next->next;
	}
	return NULL;
}

struct mapent_cache *cache_lookup_next(struct mapent_cache *me)
{
	struct mapent_cache *ne;
	int status;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock lock failed");

	ne = __cache_lookup_next(me);

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return ne;
}

/* Lookup an offset within a multi-mount entry */
struct mapent_cache *cache_lookup_offset(const char *prefix, const char *offset, int start, struct list_head *head)
{
	struct list_head *p;
	struct mapent_cache *this = NULL;
	int plen = strlen(prefix);
	char *o_key;

	/* root offset duplicates "/" */
	if (plen > 1) {
		o_key = alloca(plen + strlen(offset) + 1);
		strcpy(o_key, prefix);
		strcat(o_key, offset);
	} else {
		o_key = alloca(strlen(offset) + 1);
		strcpy(o_key, offset);
	}

	list_for_each(p, head) {
		this = list_entry(p, struct mapent_cache, multi_list);
		if (!strcmp(&this->key[start], o_key))
			return this;
	}
	return NULL;
}

struct mapent_cache *cache_partial_match(const char *prefix)
{
	struct mapent_cache *me = NULL;
	int len = strlen(prefix);
	int status;
	int i;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		return NULL;
	}

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (me == NULL)
			continue;

		if (len < strlen(me->key) &&
		    (strncmp(prefix, me->key, len) == 0) && me->key[len] == '/')
			goto done;

		me = me->next;
		while (me != NULL) {
			if (len < strlen(me->key) &&
			    strncmp(prefix, me->key, len) == 0 && me->key[len] == '/')
				goto done;
			me = me->next;
		}
	}
done:
	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return me;
}

int cache_add(const char *key, const char *mapent, time_t age)
{
	struct mapent_cache *me, *existing = NULL;
	char *pkey, *pent;
	unsigned int hashval = hash(key);
	int status;

	me = (struct mapent_cache *) malloc(sizeof(struct mapent_cache));
	if (!me)
		return CHE_FAIL;

	pkey = malloc(strlen(key) + 1);
	if (!pkey) {
		free(me);
		return CHE_FAIL;
	}
	me->key = strcpy(pkey, key);

	if (mapent) {
		pent = malloc(strlen(mapent) + 1);
		if (!pent) {
			free(me);
			free(pkey);
			return CHE_FAIL;
		}
		me->mapent = strcpy(pent, mapent);
	} else
		pent = NULL;

	status = pthread_mutex_init(&me->mutex, NULL);
	if (status) {
		error("mapent entry mutex init failed");
		free(me);
		free(pkey);
		if (pent)
			free(pent);
		return CHE_FAIL;
	}
	me->age = age;
	INIT_LIST_HEAD(&me->multi_list);
	me->multi = NULL;
	me->ioctlfd = -1;
	me->dev = -1;
	me->ino = -1;

	/* 
	 * We need to add to the end if values exist in order to
	 * preserve the order in which the map was read on lookup.
	 */
	status = pthread_rwlock_wrlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		free(me);
		free(pkey);
		if (pent)
			free(pent);
		return CHE_FAIL;
	}

	existing = __cache_lookup(key);
	if (!existing || *existing->key == '*') {
		me->next = mapent_hash[hashval];
		mapent_hash[hashval] = me;
	} else {
		while (1) {
			struct mapent_cache *next;
		
			next = __cache_lookup_next(existing);
			if (!next)
				break;

			existing = next;
		}
		me->next = existing->next;
		existing->next = me;
	}

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return CHE_OK;
}

static void cache_add_ordered_offset(struct mapent_cache *me, struct list_head *head)
{
	struct list_head *p;
	struct mapent_cache *this;
	int status;

	status = pthread_mutex_lock(&me->mutex);
	if (status) {
		error("mapent cache entry lock failed");
		return;
	}

	list_for_each(p, head) {
		int eq, tlen;

		this = list_entry(p, struct mapent_cache, multi_list);
		tlen = strlen(this->key);

		eq = strncmp(this->key, me->key, tlen);
		if (!eq && tlen == strlen(me->key))
			goto done;

		if (eq > 0) {
			list_add_tail(&me->multi_list, p);
			goto done;
		}
	}
	list_add_tail(&me->multi_list, p);
done:
	status = pthread_mutex_unlock(&me->mutex);
	if (status)
		error("mapent cache entry lock failed");

	return;
}

int cache_add_offset(const char *mkey, const char *key, const char *mapent, time_t age)
{
	struct mapent_cache *me, *owner;
	int ret = CHE_OK;

	owner = cache_lookup(mkey);
	if (!owner)
		return CHE_FAIL;

	cache_add(key, mapent, age);
	me = cache_lookup(key);
	if (me) {
		cache_add_ordered_offset(me, &owner->multi_list);
		me->multi = owner;
		goto done;
	}
	ret = CHE_FAIL;
done:
	return ret; 
}

int cache_update(const char *key, const char *mapent, time_t age)
{
	struct mapent_cache *me = NULL;
	char *pent;
	int ret = CHE_OK;

	me = cache_lookup(key);
	if (!me) {
		ret = cache_add(key, mapent, age);
		if (!ret) {
			debug("failed for %s", key);
			return CHE_FAIL;
		}
		ret = CHE_UPDATED;
	} else {
		if (!me->mapent || strcmp(me->mapent, mapent) != 0) {
			pent = malloc(strlen(mapent) + 1);
			if (pent == NULL) {
				return CHE_FAIL;
			}
			if (me->mapent)
				free(me->mapent);
			me->mapent = strcpy(pent, mapent);
			ret = CHE_UPDATED;
		}
		me->age = age;
	}
	return ret;
}

int cache_delete(const char *table, const char *root, const char *key, int rmpath)
{
	struct mapent_cache *me = NULL, *pred;
	unsigned int hashval = hash(key);
	int ret = CHE_OK;
	char *path;
	int status;

	path = cache_fullpath(root, key);
	if (!path)
		return CHE_FAIL;

	status = pthread_rwlock_wrlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		return CHE_FAIL;
	}

	me = mapent_hash[hashval];
	if (!me) {
		ret = CHE_FAIL;
		goto done;
	}

	while (me->next != NULL) {
		pred = me;
		me = me->next;
		if (strcmp(key, me->key) == 0) {
			pred->next = me->next;
			if (me->multi && !list_empty(&me->multi_list)) {
				ret = CHE_FAIL;
				goto done;
			}
			free(me->key);
			if (me->mapent)
				free(me->mapent);
			pthread_mutex_destroy(&me->mutex);
			free(me);
			me = pred;
		}
	}

	me = mapent_hash[hashval];
	if (!me)
		goto done;

	if (strcmp(key, me->key) == 0) {
		mapent_hash[hashval] = me->next;
		if (me->multi && !list_empty(&me->multi_list)) {
			ret = CHE_FAIL;
			goto done;
		}
		free(me->key);
		if (me->mapent)
			free(me->mapent);
		pthread_mutex_destroy(&me->mutex);
		free(me);
	}

	if (rmpath)
		rmdir_path(path);
done:
	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");
	free(path);
	return ret;
}

int cache_delete_offset_list(const char *table, const char *root, const char *key)
{
	struct mapent_cache *me;
	struct mapent_cache *this;
	struct list_head *head, *next;
	int remain = 0;
	int status;

	me = cache_lookup(key);
	if (!me)
		return CHE_FAIL;

	/* Not offset list owner */
	if (me->multi != me)
		return CHE_OK;

	status = pthread_mutex_lock(&me->mutex);
	if (status) {
		error("mapent cache entry lock failed");
		return CHE_FAIL;
	}

	head = &me->multi_list;
	next = head->next;
	while (next != head) {
		this = list_entry(next, struct mapent_cache, multi_list);
		next = next->next;
		list_del_init(&this->multi_list);
		status = cache_delete(table, root, this->key, 0);
		if (status == CHE_FAIL)
			remain++;
	}

	if (!remain) {
		list_del_init(&me->multi_list);
		me->multi = NULL;
	}

	status = pthread_mutex_unlock(&me->mutex);
	if (status)
		error("mapent cache entry lock failed");

	return remain;
}

void cache_clean(const char *table, const char *root, time_t age)
{
	struct mapent_cache *me, *pred;
	char *path;
	int status;
	int i;

	status = pthread_rwlock_wrlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		return;
	}

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];
		if (!me)
			continue;

		while (me->next != NULL) {
			pred = me;
			me = me->next;

			/* We treat multi-mount offsets seperatetly */
			if (me->multi && !list_empty(&me->multi_list))
				continue;

			path = cache_fullpath(root, me->key);
			if (!path)
				goto done;

			if (me->age < age) {
				pred->next = me->next;
				free(me->key);
				if (me->mapent)
					free(me->mapent);
				pthread_mutex_destroy(&me->mutex);
				free(me);
				me = pred;
				rmdir_path(path);
			}

			free(path);
		}

		me = mapent_hash[i];
		if (!me)
			continue;

		/* We treat multi-mount offsets seperatetly */
		if (me->multi && !list_empty(&me->multi_list))
			continue;

		path = cache_fullpath(root, me->key);
		if (!path)
			goto done;

		if (me->age < age) {
			mapent_hash[i] = me->next;
			rmdir_path(path);
			free(me->key);
			if (me->mapent)
				free(me->mapent);
			pthread_mutex_destroy(&me->mutex);
			free(me);
			rmdir_path(path);
		}

		free(path);
	}
done:
	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");
}

void cache_release(void)
{
	struct mapent_cache *me, *next;
	int status;
	int i;

	status = pthread_rwlock_wrlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		return;
	}

	for (i = 0; i < HASHSIZE; i++) {
		cache_ino_index[i] = -1;
		me = mapent_hash[i];
		if (me == NULL)
			continue;
		mapent_hash[i] = NULL;
		next = me->next;
		free(me->key);
		if (me->mapent)
			free(me->mapent);
		pthread_mutex_destroy(&me->mutex);
		free(me);

		while (next != NULL) {
			me = next;
			next = me->next;
			free(me->key);
			if (me->mapent)
				free(me->mapent);
			pthread_mutex_destroy(&me->mutex);
			free(me);
		}
	}

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	status = pthread_rwlock_destroy(&cache_rwlock);
	if (status)
		error("mapent cache rwlock destroy failed");
}

int cache_enumerate(int (*fn)(struct mapent_cache *, int), int arg)
{
	struct mapent_cache *me;
	int status;
	int at_least_one = 0;
	int ret;
	int i;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status) {
		error("mapent cache rwlock lock failed");
		return 0;
	}

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];

		if (me == NULL)
			continue;

		while (me) {
			/* Skip over multi-mount offsets */
			if (me->multi && me->multi != me)
				goto cont;
			status = pthread_rwlock_unlock(&cache_rwlock);
			if (status)
				error("mapent cache rwlock unlock failed");
			ret = fn(me, arg);
			status = pthread_rwlock_rdlock(&cache_rwlock);
			if (status)
				error("mapent cache rwlock lock failed");
			if (ret)
				at_least_one++;
		cont:
			me = me->next;
		}
	}

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rwlock unlock failed");

	return at_least_one;
}

int cache_ghost(const char *root, int ghosted)
{
	struct mapent_cache *me;
	char buf[MAX_ERR_BUF];
	char *fullpath;
	struct stat st;
	int status;
	int i;

	if (!ghosted)
		return -1;

	status = pthread_rwlock_rdlock(&cache_rwlock);
	if (status) {
		error("mapent cache rdlock lock failed");
		return 0;
	}

	for (i = 0; i < HASHSIZE; i++) {
		me = mapent_hash[i];

		if (me == NULL)
			continue;

		while (me) {
			struct mapent_cache *this = me;

			me = me->next;

			/* only consider the top of the multi-mount */
			if (this->multi && this->multi != this)
				continue;

			if (*this->key == '*')
				continue;
			
			if (*this->key == '/') {
				error("invalid key %s", this->key);
				continue;
			}

			fullpath = alloca(strlen(this->key) + strlen(root) + 3);
			sprintf(fullpath, "%s/%s", root, this->key);

			if (stat(fullpath, &st) == -1 && errno == ENOENT) {
				if (mkdir_path(fullpath, 0555) < 0) {
					if (strerror_r(errno, buf, MAX_ERR_BUF))
						strcpy(buf, "strerror_r failed");
					warn("mkdir_path %s failed: %s",
					     fullpath, buf);
					continue;
				}
			}

			if (stat(fullpath, &st) != -1) {
				this->dev = st.st_dev;
				this->ino = st.st_ino;
			}
		}
	}

	status = pthread_rwlock_unlock(&cache_rwlock);
	if (status)
		error("mapent cache rdlock unlock failed");

	return 0;
}

/*
 * Get each offset from list head under prefix.
 * Maintain traversal current position in pos for subsequent calls. 
 * Return each offset into offset.
 * TODO: length check on offset.
 */
char *cache_get_offset(const char *prefix, char *offset, int start,
			struct list_head *head, struct list_head **pos)
{
	struct list_head *next;
	struct mapent_cache *this;
	int plen = strlen(prefix);
	int len = 0;

	if (*pos == head)
		return NULL;

	/* Find an offset */
	*offset = '\0';
	next = *pos ? (*pos)->next : head->next;
	while (next != head) {
		char *offset_start, *pstart, *pend;

		this = list_entry(next, struct mapent_cache, multi_list);
		*pos = next;
		next = next->next;

		offset_start = &this->key[start];
		if (strlen(offset_start) <= plen)
			continue;

		if (!strncmp(prefix, offset_start, plen)) {
			/* "/" doesn't count for root offset */
			if (plen == 1)
				pstart = &offset_start[plen - 1];
			else
				pstart = &offset_start[plen];

			/* not part of this sub-tree */
			if (*pstart != '/')
				continue;

			/* get next offset */
			pend = pstart;
			while (*pend++) ;
			len = pend - pstart - 1;
			strncpy(offset, pstart, len);
			offset[len] ='\0';
			break;
		}
	}

	/* Seek to next offset */
	while (next != head) {
		char *offset_start, *pstart;

		this = list_entry(next, struct mapent_cache, multi_list);

		offset_start = &this->key[start];
		if (strlen(offset_start) <= plen + len)
			break;

		/* "/" doesn't count for root offset */
		if (plen == 1)
			pstart = &offset_start[plen - 1];
		else
			pstart = &offset_start[plen];

		/* not part of this sub-tree */
		if (*pstart != '/')
			break;

		/* new offset */
		if (!*(pstart + len + 1))
			break;

		/* compare offset */
		if (pstart[len] != '/' || strncmp(offset, pstart, len))
			break;

		*pos = next;
		next = next->next;
	}

	return *offset ? offset : NULL;
}

