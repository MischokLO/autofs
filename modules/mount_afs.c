#ident "$Id: mount_afs.c,v 1.2 2003/09/29 08:22:35 raven Exp $"
/*
 * mount_afs.c
 *
 * Module for Linux automountd to "mount" AFS filesystems.  We don't bother
 * with any of the things "attach" would do (making sure there are tokens,
 * subscribing to ops messages if Zephyr is installed), but it works for me.
 *
 */

#include <stdio.h>
#include <malloc.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <syslog.h>
#include <string.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/stat.h>

#define MODULE_MOUNT
#include "automount.h"

#ifdef DEBUG
#define DB(x)           do { x; } while(0)
#else
#define DB(x)           do { } while(0)
#endif

#define MODPREFIX "mount(afs): "
int mount_version = AUTOFS_MOUNT_VERSION;	/* Required by protocol */

int mount_init(void **context)
{
	return 0;
}

int mount_mount(const char *root, const char *name, int name_len,
		const char *what, const char *fstype, const char *options, void *context)
{
	char dest[PATH_MAX * 2];

	strcpy(dest, root);	/* Convert the name to a mount point. */
	strncat(dest, "/", sizeof(dest));
	strncat(dest, name, sizeof(dest));

	/* This was here just so I could figure out how this worked.
	   syslog(LOG_DEBUG, MODPREFIX "mount_mount called with root=\"%s\", "
	   "name=\"%s\", namelen=\"%d, what=\"%s\", fstype=\"%s\", options=\"%s\"",+       root, name, name_len, what, fstype, options); */

	DB(syslog(LOG_DEBUG, MODPREFIX "mounting AFS %s -> %s", dest, what));

	return symlink(what, dest);	/* Try it.  If it fails, return the error. */
}

int mount_done(void *context)
{
	return 0;
}