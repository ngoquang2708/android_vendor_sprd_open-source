/*
 * Copyright (C) 1995 Wolfgang Solfrank
 * Copyright (c) 1995 Martin Husemann
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Martin Husemann
 *	and Wolfgang Solfrank.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHORS ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */


#include <sys/cdefs.h>
#ifndef lint
__RCSID("$NetBSD: main.c,v 1.10 1997/10/01 02:18:14 enami Exp $");
static const char rcsid[] =
  "$FreeBSD: src/sbin/fsck_msdosfs/main.c,v 1.16 2009/06/10 19:02:54 avg Exp $";
#endif /* not lint */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <stdarg.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <time.h>

#include <sys/param.h>

#define pwarn printf
#define pfatal printf


#include "ext.h"

#define FAT_COMPARE_MAX_KB 4096

#define	SLOT_EMPTY	0x00		/* slot has never been used */
#define	SLOT_E5		0x05		/* the real value is 0xe5 */
#define	SLOT_DELETED	0xe5		/* file in this slot deleted */

#define	ATTR_NORMAL	0x00		/* normal file */
#define	ATTR_READONLY	0x01		/* file is readonly */
#define	ATTR_HIDDEN	0x02		/* file is hidden */
#define	ATTR_SYSTEM	0x04		/* file is a system file */
#define	ATTR_VOLUME	0x08		/* entry is a volume label */
#define	ATTR_DIRECTORY	0x10		/* entry is a directory name */
#define	ATTR_ARCHIVE	0x20		/* file is new or modified */

#define	ATTR_WIN95	0x0f		/* long name record */

static int scanAllDir = 0;
static int sectorNo = 0;
static char* dumpSectorFileName;

static int fragmentSize = 0;
static int printFatTable = 0;



typedef int (*MYFUN)(struct dosDirEntry*, struct bootblock*, struct fatEntry*); 
static MYFUN  myfunc = NULL;

void
dumpbootblock(struct bootblock *boot)
{
	if (boot->flags & FAT32)
		fprintf(stderr, "\nFAT32 file system found!\n");
	fprintf(stderr, "Media descriptor:                         0x%02X (%u)\n", boot->Media, boot->Media);
	fprintf(stderr, "Bytes per sector:                         %u\n", boot->BytesPerSec);
	fprintf(stderr, "Sectors per cluster:                      %u (%u KB)\n", boot->SecPerClust,
	       boot->ClusterSize / 1024);
	fprintf(stderr, "Reserved sectors:                         %u\n", boot->ResSectors);
	fprintf(stderr, "Number of FATs:                           %u\n", boot->FATs);
	/* For FAT12 and FAT16 always print the next two values or if any
	 * of them is non-zero for FAT32 */
	if (!(boot->flags & FAT32)
	    || boot->RootDirEnts || boot->FATsmall) {
		fprintf(stderr, "Root directory entries:                   %u\n", boot->RootDirEnts);
		fprintf(stderr, "Sectors per FAT:                          %u\n", boot->FATsmall);
	}
	if (boot->flags & FAT32) {
		fprintf(stderr, "Start cluster of root directory:          %u\n", boot->RootCl);
		fprintf(stderr, "FS information sector at sector:          %u\n", boot->FSInfo);
		fprintf(stderr, "Sectors per FAT:                          %u\n", boot->FATsecs);
	}
	fprintf(stderr, "Total number of sectors:                  %u (from %s)\n", boot->NumSectors,
	       (boot->Sectors) ? "Sectors" : "HugeSectors");

	fprintf(stderr, "Number of free clusters acc. FSInfo:      %u\n", boot->FSFree);
	fprintf(stderr, "Next free cluster acc. FSInfo:            %u\n", boot->FSNext);
	fprintf(stderr, "FAT1 start sector:                        %u\n", boot->ResSectors);
	fprintf(stderr, "FAT2 start sector:                        %u\n", boot->ResSectors + 1 * boot->FATsecs);
	fprintf(stderr, "DATA start sector:                        %u\n", boot->ResSectors + boot->FATs * boot->FATsecs);
	fprintf(stderr, "Cluster 0 start address:                  %u\n", boot->ClusterOffset);
	fprintf(stderr, "Total number of cluster:                  %u\n", boot->NumClusters);
	fprintf(stderr, "Number of root directory entries:         %u\n", boot->RootDirEnts);
	fprintf(stderr, "\n");

	fprintf(stderr, "---------------------------------------------------------------------------\n");
	fprintf(stderr, "|       RESV(sec)    |  FAT1(sec)  | FAT2(sec)  |          DATA(sec)      |\n");
	fprintf(stderr, "|DBR|FSINFO| OTHER   |             |            |                         |\n");
	fprintf(stderr, "|0                   |%-6d       |%-6d      |%-6d          %9d|\n", 
		boot->ResSectors, 
		boot->ResSectors + 1 * boot->FATsecs,
		boot->ResSectors + boot->FATs * boot->FATsecs,
		boot->NumSectors);
	fprintf(stderr, "---------------------------------------------------------------------------\n\n");
	//printf("Sectors per track:       %u\n", boot->SecPerTrack);
	//printf("Number of heads:         %u\n", boot->Heads);
	//printf("Hidden sectors:          %u\n", boot->HiddenSecs);
	//printf("Volume Label:            '%s'\n", boot->VolLabel);
	//printf("Filesystem type:         '%s'\n", boot->FSType);
}

/*
 * The stack of unread directories
 */
static struct dirTodoNode *pendingDirs = NULL;

/*
 * Global variables temporarily used during a directory scan
 */
static char longName[DOSLONGNAMELEN] = "";
static u_char *buffer = NULL;
static u_char *delbuf = NULL;

/*
 * Check a cluster number for valid value
 */
static int
checkclnum(struct bootblock *boot, int fat, cl_t cl, cl_t *next)
{
	if (*next >= (CLUST_RSRVD&boot->ClustMask))
		*next |= ~boot->ClustMask;
	if (*next == CLUST_FREE) {
		boot->NumFree++;
		return FSOK;
	}
	if (*next == CLUST_BAD) {
		boot->NumBad++;
		return FSOK;
	}
	if (*next < CLUST_FIRST
	    || (*next >= boot->NumClusters && *next < CLUST_EOFS)) {
		fprintf(stderr, "Cluster %u in FAT %d continues with %s cluster number %u\n",
		      cl, fat,
		      *next < CLUST_RSRVD ? "out of range" : "reserved",
		      *next&boot->ClustMask);
		if (ask(1, "Truncate")) {
			*next = CLUST_EOF;
			return FSFATMOD;
		}
		return FSERROR;
	}
	return FSOK;
}


/*
 * Read a FAT from disk. Returns 1 if successful, 0 otherwise.
 */
static int
_readfatTable(int fs, struct bootblock *boot, int no, u_char **buffer)
{
	off64_t off;
	off64_t ret64;

        fprintf(stderr, "Attempting to allocate %u KB for FAT\n",
                (boot->FATsecs * boot->BytesPerSec) / 1024);

	*buffer = malloc(boot->FATsecs * boot->BytesPerSec);
	if (*buffer == NULL) {
		perror("No space for FAT");
		return 0;
	}

	off = boot->ResSectors + no * boot->FATsecs;
	off *= boot->BytesPerSec;

	ret64 = lseek64(fs, off, SEEK_SET);
	if (ret64 != off) {
		perror("Unable to lseek FAT");
		goto err;
	}

	if (read(fs, *buffer, boot->FATsecs * boot->BytesPerSec)
	    != (int)(boot->FATsecs * boot->BytesPerSec)) {
		perror("Unable to read FAT");
		goto err;
	}

	return 1;

    err:
	free(*buffer);
	return 0;
}

/*
 * Read a FAT and decode it into internal format
 */
static int
readfatTable(int fs, struct bootblock *boot, int no, struct fatEntry **fp)
{
	struct fatEntry *fat;
	u_char *buffer, *p;
	cl_t cl;
	int ret = FSOK;
	int count = 0;

	boot->NumFree = boot->NumBad = 0;

	if (!_readfatTable(fs, boot, no, &buffer))
		return FSFATAL;
		
	fat = calloc(boot->NumClusters, sizeof(struct fatEntry));
	if (fat == NULL) {
		perror("No space for FAT");
		free(buffer);
		return FSFATAL;
	}

	if (buffer[0] != boot->Media
	    || buffer[1] != 0xff || buffer[2] != 0xff
	    || (boot->ClustMask == CLUST16_MASK && buffer[3] != 0xff)
	    || (boot->ClustMask == CLUST32_MASK
		&& ((buffer[3]&0x0f) != 0x0f
		    || buffer[4] != 0xff || buffer[5] != 0xff
		    || buffer[6] != 0xff || (buffer[7]&0x0f) != 0x0f))) {

		/* Windows 95 OSR2 (and possibly any later) changes
		 * the FAT signature to 0xXXffff7f for FAT16 and to
		 * 0xXXffff0fffffff07 for FAT32 upon boot, to know that the
		 * file system is dirty if it doesn't reboot cleanly.
		 * Check this special condition before errorring out.
		 */
		if (buffer[0] == boot->Media && buffer[1] == 0xff
		    && buffer[2] == 0xff
		    && ((boot->ClustMask == CLUST16_MASK && buffer[3] == 0x7f)
			|| (boot->ClustMask == CLUST32_MASK
			    && buffer[3] == 0x0f && buffer[4] == 0xff
			    && buffer[5] == 0xff && buffer[6] == 0xff
			    && buffer[7] == 0x07)))
			ret |= FSDIRTY;
		else {
			/* just some odd byte sequence in FAT */
				
			switch (boot->ClustMask) {
			case CLUST32_MASK:
				fprintf(stderr, "%s (%02x%02x%02x%02x%02x%02x%02x%02x)\n",
				      "FAT starts with odd byte sequence",
				      buffer[0], buffer[1], buffer[2], buffer[3],
				      buffer[4], buffer[5], buffer[6], buffer[7]);
				break;
			case CLUST16_MASK:
				fprintf(stderr, "%s (%02x%02x%02x%02x)\n",
				    "FAT starts with odd byte sequence",
				    buffer[0], buffer[1], buffer[2], buffer[3]);
				break;
			default:
				fprintf(stderr, "%s (%02x%02x%02x)\n",
				    "FAT starts with odd byte sequence",
				    buffer[0], buffer[1], buffer[2]);
				break;
			}

	
			if (ask(1, "Correct"))
				ret |= FSFIXFAT;
		}
	}
	switch (boot->ClustMask) {
	case CLUST32_MASK:
		p = buffer + 8;
		break;
	case CLUST16_MASK:
		p = buffer + 4;
		break;
	default:
		p = buffer + 3;
		break;
	}
	for (cl = CLUST_FIRST; cl < boot->NumClusters;) {
		switch (boot->ClustMask) {
		case CLUST32_MASK:
			fat[cl].next = p[0] + (p[1] << 8)
				       + (p[2] << 16) + (p[3] << 24);
			fat[cl].next &= boot->ClustMask;
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			p += 4;
			break;
		case CLUST16_MASK:
			fat[cl].next = p[0] + (p[1] << 8);
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			p += 2;
			break;
		default:
			fat[cl].next = (p[0] + (p[1] << 8)) & 0x0fff;
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			if (cl >= boot->NumClusters)
				break;
			fat[cl].next = ((p[1] >> 4) + (p[2] << 4)) & 0x0fff;
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			p += 3;
			break;
		}
	}

	free(buffer);
	*fp = fat;
	return ret;
}

/*
 * Read a FAT from disk. Returns 1 if successful, 0 otherwise.
 */
static int
_writefatTable(int fs, struct bootblock *boot, int no, u_char *buffer)
{
	off64_t off;

	off = boot->ResSectors + no * boot->FATsecs;
	off *= boot->BytesPerSec;

	if (lseek64(fs, off, SEEK_SET) != off) {
		perror("Unable to write FAT");
		goto err;
	}

	if (write(fs, buffer, boot->FATsecs * boot->BytesPerSec)
	    != (int)(boot->FATsecs * boot->BytesPerSec)) {
		perror("Unable to write FAT");
		goto err;
	}

	return 1;

    err:
	return 0;
}

static int traverseFatTable(int fs, struct bootblock *boot, int no)
{
	struct fatEntry *fat;
	u_char *buffer, *p;
	cl_t cl;
	int ret = FSOK;
	int count = 0;

	
	boot->NumFree = boot->NumBad = 0;
	
	if (!_readfatTable(fs, boot, no, &buffer))
		return FSFATAL;
		
	fat = calloc(boot->NumClusters, sizeof(struct fatEntry));
	if (fat == NULL) {
		perror("No space for FAT");
		free(buffer);
		return FSFATAL;
	}

	switch (boot->ClustMask) {
	case CLUST32_MASK:
		p = buffer + 8;
		break;
	case CLUST16_MASK:
		p = buffer + 4;
		break;
	default:
		p = buffer + 3;
		break;
	}
	for (cl = CLUST_FIRST; cl < boot->NumClusters;) {
		switch (boot->ClustMask) {
		case CLUST32_MASK:
			fat[cl].next = p[0] + (p[1] << 8)
				       + (p[2] << 16) + (p[3] << 24);
			fat[cl].next &= boot->ClustMask;
			if (printFatTable != 0){
				fprintf(stderr, "%u:%x ", cl, p[0] + (p[1] << 8)
				       + (p[2] << 16) + (p[3] << 24));
				if (cl % 10 == 0){
					fprintf(stderr, "\n");
				}
			}
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			if (fragmentSize > 0 && cl > boot->FSNext){
				if (fat[cl].next == 0){
					count++;
					if (count > fragmentSize){
						p[0] = 0xf7;
						p[1] = 0xff;
						p[2] = 0xff;
						p[3] = 0xf;
						count = 0;
					}
				}
			}
			else if (fragmentSize < 0 && cl > boot->FSNext){
				if (fat[cl].next == 0){
					if (count + fragmentSize > 0){
						count = 0;
					}
					else{
						p[0] = 0xf7;
						p[1] = 0xff;
						p[2] = 0xff;
						p[3] = 0xf;
					}
					count++;
				}
			}
			cl++;
			p += 4;
			break;
		case CLUST16_MASK:
			fat[cl].next = p[0] + (p[1] << 8);
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			p += 2;
			break;
		default:
			fat[cl].next = (p[0] + (p[1] << 8)) & 0x0fff;
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			if (cl >= boot->NumClusters)
				break;
			fat[cl].next = ((p[1] >> 4) + (p[2] << 4)) & 0x0fff;
			ret |= checkclnum(boot, no, cl, &fat[cl].next);
			cl++;
			p += 3;
			break;
		}
	}

	if (fragmentSize != 0){
		if (!_writefatTable(fs, boot, no, buffer))
			return FSFATAL;
	}

	free(buffer);
	return ret;

}

/*
 * Return the full pathname for a directory entry.
 */
static char *
fullpath(struct dosDirEntry *dir)
{
	static char namebuf[MAXPATHLEN + 1];
	char *cp, *np;
	int nl;

	cp = namebuf + sizeof namebuf - 1;
	*cp = '\0';
	do {
		np = dir->lname[0] ? dir->lname : dir->name;
		nl = strlen(np);
		if ((cp -= nl) <= namebuf + 1)
			break;
		memcpy(cp, np, nl);
		*--cp = '/';
	} while ((dir = dir->parent) != NULL);
	if (dir)
		*--cp = '?';
	else
		cp++;
	return cp;
}


/*
 * Get type of reserved cluster
 */
char *
rsrvdclustertype(cl_t cl)
{
	if (cl == CLUST_FREE)
		return "free";
	if (cl < CLUST_BAD)
		return "reserved";
	if (cl > CLUST_BAD)
		return "as EOF";
	return "bad";
}

static int
clustdiffer(cl_t cl, cl_t *cp1, cl_t *cp2, int fatnum)
{
	if (*cp1 == CLUST_FREE || *cp1 >= CLUST_RSRVD) {
		if (*cp2 == CLUST_FREE || *cp2 >= CLUST_RSRVD) {
			if ((*cp1 != CLUST_FREE && *cp1 < CLUST_BAD
			     && *cp2 != CLUST_FREE && *cp2 < CLUST_BAD)
			    || (*cp1 > CLUST_BAD && *cp2 > CLUST_BAD)) {
				fprintf(stderr, "Cluster %u is marked %s with different indicators\n",
				      cl, rsrvdclustertype(*cp1));
				if (ask(1, "Fix")) {
					*cp2 = *cp1;
					return FSFATMOD;
				}
				return FSFATAL;
			}
			fprintf(stderr, "Cluster %u is marked %s in FAT 0, %s in FAT %d\n",
			      cl, rsrvdclustertype(*cp1), rsrvdclustertype(*cp2), fatnum);
			if (ask(1, "Use FAT 0's entry")) {
				*cp2 = *cp1;
				return FSFATMOD;
			}
			if (ask(1, "Use FAT %d's entry", fatnum)) {
				*cp1 = *cp2;
				return FSFATMOD;
			}
			return FSFATAL;
		}
		fprintf(stderr, "Cluster %u is marked %s in FAT 0, but continues with cluster %u in FAT %d\n",
		      cl, rsrvdclustertype(*cp1), *cp2, fatnum);
		if (ask(1, "Use continuation from FAT %d", fatnum)) {
			*cp1 = *cp2;
			return FSFATMOD;
		}
		if (ask(1, "Use mark from FAT 0")) {
			*cp2 = *cp1;
			return FSFATMOD;
		}
		return FSFATAL;
	}
	if (*cp2 == CLUST_FREE || *cp2 >= CLUST_RSRVD) {
		fprintf(stderr, "Cluster %u continues with cluster %u in FAT 0, but is marked %s in FAT %d\n",
		      cl, *cp1, rsrvdclustertype(*cp2), fatnum);
		if (ask(1, "Use continuation from FAT 0")) {
			*cp2 = *cp1;
			return FSFATMOD;
		}
		if (ask(1, "Use mark from FAT %d", fatnum)) {
			*cp1 = *cp2;
			return FSFATMOD;
		}
		return FSERROR;
	}
	fprintf(stderr, "Cluster %u continues with cluster %u in FAT 0, but with cluster %u in FAT %d\n",
	      cl, *cp1, *cp2, fatnum);
	if (ask(1, "Use continuation from FAT 0")) {
		*cp2 = *cp1;
		return FSFATMOD;
	}
	if (ask(1, "Use continuation from FAT %d", fatnum)) {
		*cp1 = *cp2;
		return FSFATMOD;
	}
	return FSERROR;
}

/*
 * Compare two FAT copies in memory. Resolve any conflicts and merge them
 * into the first one.
 */
int
comparefattab(struct bootblock *boot, struct fatEntry *first, 
    struct fatEntry *second, int fatnum)
{
	cl_t cl;
	int ret = FSOK;

	for (cl = CLUST_FIRST; cl < boot->NumClusters; cl++)
		if (first[cl].next != second[cl].next)
			ret |= clustdiffer(cl, &first[cl].next, &second[cl].next, fatnum);
	return ret;
}

void
clearclusterchain(struct bootblock *boot, struct fatEntry *fat, cl_t head)
{
	cl_t p, q;

	for (p = head; p >= CLUST_FIRST && p < boot->NumClusters; p = q) {
		if (fat[p].head != head)
			break;
		q = fat[p].next;
		fat[p].next = fat[p].head = CLUST_FREE;
		fat[p].length = 0;
	}
}

int
tryclearchain(struct bootblock *boot, struct fatEntry *fat, cl_t head, cl_t *trunc)
{
	if (ask(1, "Clear chain starting at %u", head)) {
		clearclusterchain(boot, fat, head);
		return FSFATMOD;
	} else if (ask(1, "Truncate")) {
		*trunc = CLUST_EOF;
		return FSFATMOD;
	} else
		return FSERROR;
}

/*
 * Check a complete FAT in-memory for crosslinks
 */
int
checkfattab(struct bootblock *boot, struct fatEntry *fat)
{
	cl_t head, p, h, n, wdk;
	u_int len;
	int ret = 0;
	int conf;

	/*
	 * pass 1: figure out the cluster chains.
	 */
	for (head = CLUST_FIRST; head < boot->NumClusters; head++) {
		/* find next untravelled chain */
		if (fat[head].head != 0		/* cluster already belongs to some chain */
		    || fat[head].next == CLUST_FREE
		    || fat[head].next == CLUST_BAD)
			continue;		/* skip it. */

		/* follow the chain and mark all clusters on the way */
		for (len = 0, p = head;
			 p >= CLUST_FIRST && p < boot->NumClusters;
			 p = fat[p].next) {
				/* we have to check the len, to avoid infinite loop */
				if (len > boot->NumClusters) {
					printf("detect cluster chain loop: head %u for p %u\n", head, p);
					break;
			}

			fat[p].head = head;
			len++;
		}

		/* the head record gets the length */
		fat[head].length = fat[head].next == CLUST_FREE ? 0 : len;
	}

	/*
	 * pass 2: check for crosslinked chains (we couldn't do this in pass 1 because
	 * we didn't know the real start of the chain then - would have treated partial
	 * chains as interlinked with their main chain)
	 */
	for (head = CLUST_FIRST; head < boot->NumClusters; head++) {
		/* find next untravelled chain */
		if (fat[head].head != head)
			continue;

		/* follow the chain to its end (hopefully) */
		/* also possible infinite loop, that's why I insert wdk counter */
		for (p = head,wdk=boot->NumClusters;
		     (n = fat[p].next) >= CLUST_FIRST && n < boot->NumClusters && wdk;
				 p = n,wdk--) {
			if (fat[n].head != head)
				break;
		}

		if (n >= CLUST_EOFS)
			continue;

		if (n == CLUST_FREE || n >= CLUST_RSRVD) {
			fprintf(stderr, "Cluster chain starting at %u ends with cluster marked %s\n",
			      head, rsrvdclustertype(n));
			ret |= tryclearchain(boot, fat, head, &fat[p].next);
			continue;
		}
		if (n < CLUST_FIRST || n >= boot->NumClusters) {
			fprintf(stderr, "Cluster chain starting at %u ends with cluster out of range (%u)\n",
			      head, n);
			ret |= tryclearchain(boot, fat, head, &fat[p].next);
			continue;
		}
		fprintf(stderr, "Cluster chains starting at %u and %u are linked at cluster %u\n",
		      head, fat[n].head, n);
		conf = tryclearchain(boot, fat, head, &fat[p].next);
		if (ask(1, "Clear chain starting at %u", h = fat[n].head)) {
			if (conf == FSERROR) {
				/*
				 * Transfer the common chain to the one not cleared above.
				 */
				for (p = n;
				     p >= CLUST_FIRST && p < boot->NumClusters;
				     p = fat[p].next) {
					if (h != fat[p].head) {
						/*
						 * Have to reexamine this chain.
						 */
						head--;
						break;
					}
					fat[p].head = head;
				}
			}
			clearclusterchain(boot, fat, h);
			conf |= FSFATMOD;
		}
		ret |= conf;
	}

	return ret;
}


/*
 * Manage free dosDirEntry structures.
 */
static struct dosDirEntry *freede;

static struct dosDirEntry *
newDosDirEntry(void)
{
	struct dosDirEntry *de;

	if (!(de = freede)) {
		if (!(de = (struct dosDirEntry *)malloc(sizeof *de)))
			return 0;
	} else
		freede = de->next;
	return de;
}

static void
freeDosDirEntry(struct dosDirEntry *de)
{
	de->next = freede;
	freede = de;
}

/*
 * The same for dirTodoNode structures.
 */
static struct dirTodoNode *freedt;

static struct dirTodoNode *
newDirTodo(void)
{
	struct dirTodoNode *dt;

	if (!(dt = freedt)) {
		if (!(dt = (struct dirTodoNode *)malloc(sizeof *dt)))
			return 0;
	} else
		freedt = dt->next;
	return dt;
}

static void
freeDirTodo(struct dirTodoNode *dt)
{
	dt->next = freedt;
	freedt = dt;
}

static int
removede(int f, struct bootblock *boot, struct fatEntry *fat, u_char *start,
    u_char *end, cl_t startcl, cl_t endcl, cl_t curcl, char *path, int type)
{
	switch (type) {
	case 0:
		fprintf(stderr, "Invalid long filename entry for %s\n", path);
		break;
	case 1:
		fprintf(stderr, "Invalid long filename entry at end of directory %s\n", path);
		break;
	case 2:
		fprintf(stderr, "Invalid long filename entry for volume label\n");
		break;
	}
	
	return FSERROR;
}

/*
 * Calculate a checksum over an 8.3 alias name
 */
static u_char
calcShortSum(u_char *p)
{
	u_char sum = 0;
	int i;

	for (i = 0; i < 11; i++) {
		sum = (sum << 7)|(sum >> 1);	/* rotate right */
		sum += p[i];
	}

	return sum;
}


/*
 * Check an in-memory file entry
 */
static int
checksize(struct bootblock *boot, struct fatEntry *fat, u_char *p,
    struct dosDirEntry *dir)
{
	/*
	 * Check size on ordinary files
	 */
	int32_t physicalSize;

	if (dir->head == CLUST_FREE)
		physicalSize = 0;
	else {
		if (dir->head < CLUST_FIRST || dir->head >= boot->NumClusters)
			return FSERROR;
		physicalSize = fat[dir->head].length * boot->ClusterSize;
	}
	if (physicalSize < dir->size) {
		fprintf(stderr, "size of %s is %u, should at most be %u\n",
		      fullpath(dir), dir->size, physicalSize);
		if (ask(1, "Truncate")) {
			dir->size = physicalSize;
			p[28] = (u_char)physicalSize;
			p[29] = (u_char)(physicalSize >> 8);
			p[30] = (u_char)(physicalSize >> 16);
			p[31] = (u_char)(physicalSize >> 24);
			return FSDIRMOD;
		} else
			return FSERROR;
	} else if (physicalSize - dir->size >= boot->ClusterSize) {
		fprintf(stderr, "%s has too many clusters allocated. physicalSize %d\n",
		      fullpath(dir), physicalSize);
		if (ask(1, "Drop superfluous clusters")) {
			cl_t cl;
			u_int32_t sz = 0;

			for (cl = dir->head; (sz += boot->ClusterSize) < dir->size;)
				cl = fat[cl].next;
			clearclusterchain(boot, fat, fat[cl].next);
			fat[cl].next = CLUST_EOF;
			return FSFATMOD;
		} else
			return FSERROR;
	}
	return FSOK;
}


static u_char  dot_header[16]={0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00};
static u_char  dot_dot_header[16]={0x2E, 0x2E, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x20, 0x10, 0x00, 0x00, 0x00, 0x00};


/*
 * Check for missing or broken '.' and '..' entries.
 */
static int
check_dot_dot(int f, struct bootblock *boot, struct fatEntry *fat,struct dosDirEntry *dir)
{
	u_char *p, *buf;
	loff_t off;
	int last;
	cl_t cl;
	int rc=0, n_count;

	int dot, dotdot;
	dot = dotdot = 0;
	cl = dir->head;

	if (dir->parent && (cl < CLUST_FIRST || cl >= boot->NumClusters)) {
		return rc;
	}

	do {
		if (!(boot->flags & FAT32) && !dir->parent) {
			last = boot->RootDirEnts * 32;
			off = boot->ResSectors + boot->FATs * boot->FATsecs;
		} else {
			last = boot->SecPerClust * boot->BytesPerSec;
			off = cl * boot->SecPerClust + boot->ClusterOffset;
		}

		off *= boot->BytesPerSec;
		buf = malloc(last);
		if (!buf) {
			perror("Unable to malloc");
			return FSFATAL;
		}
		if (lseek64(f, off, SEEK_SET) != off) {
			fprintf(stderr, "off = %llu\n", off);
			perror("Unable to lseek64");
			free(buf);
			return FSFATAL;
		}
		if (read(f, buf, last) != last) {
			perror("Unable to read");
			free(buf);
			return FSFATAL;
		}
		last /= 32;
		p = buf;
		for (n_count=0, rc=0; n_count < 11; n_count++) {
			if (dot_header[n_count] != p[n_count]) {
				rc=-1;
				break;
			}
		}
		 if(!rc)
			dot=1;

		for (n_count = 0, rc = 0; n_count < 11; n_count++) {
			if (dot_dot_header[n_count] != p[n_count+32]) {
				rc=-1;
				break;
			}
		}
		if(!rc)
			dotdot=1;
		free(buf);
	} while ((cl = fat[cl].next) >= CLUST_FIRST && cl < boot->NumClusters);

	if (!dot || !dotdot) {
		if (!dot)
			fprintf(stderr, "%s: '.' absent for %s.\n",__func__,dir->name);

		if (!dotdot)
			fprintf(stderr, "%s: '..' absent for %s. \n",__func__,dir->name);
		return -1;
	}
	return 0;
}

static int printClusterNo(struct dosDirEntry* dir, struct bootblock *boot, struct fatEntry *fat)
{
	long long int startSec, endSec;
	cl_t curr = dir->head;

	fprintf(stderr, "\nFileName %s\n", fullpath(dir));
	fprintf(stderr, "ClusterNo[StartSecNo EndSecNo]:\n");
	while (1)
	{
		startSec = boot->ResSectors + boot->FATs * boot->FATsecs 
			+ (curr - CLUST_FIRST) * boot->SecPerClust;
		endSec = startSec + boot->SecPerClust;
		fprintf(stderr, "%d[%lld %lld] ", curr, startSec, endSec - 1);
		curr = fat[curr].next;
		if (curr == CLUST_EOF || curr == CLUST_EOFS || curr == CLUST_FREE)
		{
			if (curr == CLUST_EOF){
				fprintf(stderr, "CLUST_EOF");
			}
			else if (curr == CLUST_EOFS){
				fprintf(stderr, "CLUST_EOFS");
			}
			else if (curr == CLUST_FREE){
				fprintf(stderr, "CLUST_FREE");
			}
			break;
		}
	}
	fprintf(stderr, "\n");

	return 0;
}

static int findSectorNo(struct dosDirEntry* dir, struct bootblock *boot, struct fatEntry *fat)
{
	unsigned int startSec, endSec;
	cl_t curr = dir->head;

	while (1)
	{
		startSec = boot->ResSectors + boot->FATs * boot->FATsecs 
			+ (curr - CLUST_FIRST) * boot->SecPerClust;
		endSec = startSec + boot->SecPerClust;
		if (sectorNo < endSec && sectorNo >= startSec){
			fprintf(stderr, "\nFileName %s\n", fullpath(dir));
			fprintf(stderr, "ClusterNo[StartSecNo EndSecNo]:%d[%d %d]\n", curr, startSec, endSec - 1);
			return 1;
		}
		
		curr = fat[curr].next;
		if (curr == CLUST_EOF || curr == CLUST_EOFS || curr == CLUST_FREE)
		{
			if (curr == CLUST_EOF){
			}
			else if (curr == CLUST_EOFS){
			}
			else if (curr == CLUST_FREE){
			}
			break;
		}
	}

	return 0;
}

static int dumpFileSectors(struct dosDirEntry* dir, struct bootblock *boot, struct fatEntry *fat)
{
	unsigned int startSec, endSec, prevStartSec = 0, prevEndSec = 0;
	cl_t curr = dir->head;
	char* fileName;
	int ret, fd = -1;
	char sector[100];
	unsigned int totalSecs, currSecs = 0;

	fileName = fullpath(dir);
	if (strcmp(fileName, dumpSectorFileName) != 0){
		return 0;
	}

	if (dir->size == 0){
		fprintf(stderr, "dump file size is 0. \n");
		return 1;
	}

	sprintf(sector, "%9d, %9d\n", dir->size, boot->ClusterSize);
	printf("%s", sector);
	totalSecs = dir->size / 512 + 1;
	
	while (1) {
		startSec = boot->ResSectors + boot->FATs * boot->FATsecs 
			+ (curr - CLUST_FIRST) * boot->SecPerClust;
		endSec = startSec + boot->SecPerClust;

		if (currSecs + boot->SecPerClust > totalSecs){
			endSec = startSec + (totalSecs - currSecs);
			currSecs += (totalSecs - currSecs);
		}
		else {
			currSecs += boot->SecPerClust;
		}

		if (prevStartSec != 0 && prevEndSec != startSec) {
			sprintf(sector, "%9d, %9d\n", prevStartSec, prevEndSec - 1);
			printf("%s", sector);
			prevStartSec = startSec;
			prevEndSec = endSec;
		}
		else if (prevEndSec == startSec){
			prevEndSec = endSec;
		}

		if (prevStartSec == 0){
			prevStartSec = startSec;
			prevEndSec = endSec;
		}

		if (currSecs == totalSecs){
			break;
		}

		curr = fat[curr].next;
		if (curr == CLUST_EOF || curr == CLUST_EOFS || curr == CLUST_FREE)
		{
			if (curr == CLUST_EOF){
			}
			else if (curr == CLUST_EOFS){
			}
			else if (curr == CLUST_FREE){
			}
			break;
		}
	}

	//if (prevStartSec == startSec && prevEndSec == endSec){
	sprintf(sector, "%9d, %9d\n", prevStartSec, prevEndSec - 1);
	printf("%s", sector);
	//}

	close(fd);
	return 1;
}


/*
 * Read a directory and
 *   - resolve long name records
 *   - enter file and directory records into the parent's list
 *   - push directories onto the todo-stack
 */
static int
readDosDirSection(int f, struct bootblock *boot, struct fatEntry *fat,
    struct dosDirEntry *dir)
{
	struct dosDirEntry dirent, *d;
	u_char *p, *vallfn, *invlfn, *empty;
	loff_t off;
	int i, j, k, last;
	cl_t cl, valcl = ~0, invcl = ~0, empcl = ~0;
	char *t;
	u_int lidx = 0;
	int shortSum;
	int mod = FSOK;
	int n_count=0;
	int rc=0;
#define	THISMOD	0x8000			/* Only used within this routine */

	cl = dir->head;
	if (dir->parent && (cl < CLUST_FIRST || cl >= boot->NumClusters)) {
		/*
		 * Already handled somewhere else.
		 */
		return FSOK;
	}
	shortSum = -1;
	vallfn = invlfn = empty = NULL;
	int dot,dotdot;
	dot = dotdot = 0;

	do {
		if (!(boot->flags & FAT32) && !dir->parent) {
			last = boot->RootDirEnts * 32;
			off = boot->ResSectors + boot->FATs * boot->FATsecs;
		} else {
			last = boot->SecPerClust * boot->BytesPerSec;
			off = cl * boot->SecPerClust + boot->ClusterOffset;
		}

		fprintf(stderr, "readDosDirSection() cl %u, off %lld. \n", cl, off);

		off *= boot->BytesPerSec;
		if (lseek64(f, off, SEEK_SET) != off) {
                        fprintf(stderr, "off = %llu\n", off);
			perror("Unable to lseek64");
			return FSFATAL;
                }
        if (read(f, buffer, last) != last) {
			perror("Unable to read");
			return FSFATAL;
        }
		last /= 32;
		for (p = buffer, i = 0; i < last; i++, p += 32) {
			if (dir->fsckflags & DIREMPWARN) {
				*p = SLOT_EMPTY;
				continue;
			}

			if (*p == SLOT_EMPTY || *p == SLOT_DELETED) {
				if (*p == SLOT_EMPTY) {
					dir->fsckflags |= DIREMPTY;
					empty = p;
					empcl = cl;
				}
				continue;
			}

			if (dir->fsckflags & DIREMPTY) {
				if (!(dir->fsckflags & DIREMPWARN)) {
					fprintf(stderr, "%s has entries after end of directory\n",
					      fullpath(dir));
					if (ask(1, "Extend")) {
						u_char *q;

						dir->fsckflags &= ~DIREMPTY;
						if (delete(f, boot, fat,
							   empcl, empty - buffer,
							   cl, p - buffer, 1) == FSFATAL)
							return FSFATAL;
						q = empcl == cl ? empty : buffer;
						for (; q < p; q += 32)
							*q = SLOT_DELETED;
						mod |= THISMOD|FSDIRMOD;
					} else if (ask(1, "Truncate"))
						dir->fsckflags |= DIREMPWARN;
				}
				if (dir->fsckflags & DIREMPWARN) {
					*p = SLOT_DELETED;
					mod |= THISMOD|FSDIRMOD;
					continue;
				} else if (dir->fsckflags & DIREMPTY)
					mod |= FSERROR;
				empty = NULL;
			}

			if (p[11] == ATTR_WIN95) {
				if (*p & LRFIRST) {
					if (shortSum != -1) {
						if (!invlfn) {
							invlfn = vallfn;
							invcl = valcl;
						}
					}
					memset(longName, 0, sizeof longName);
					shortSum = p[13];
					vallfn = p;
					valcl = cl;
				} else if (shortSum != p[13]
					   || lidx != (*p & LRNOMASK)) {
					if (!invlfn) {
						invlfn = vallfn;
						invcl = valcl;
					}
					if (!invlfn) {
						invlfn = p;
						invcl = cl;
					}
					vallfn = NULL;
				}
				lidx = *p & LRNOMASK;
				t = longName + --lidx * 13;
				for (k = 1; k < 11 && t < longName + sizeof(longName); k += 2) {
					if (!p[k] && !p[k + 1])
						break;
					*t++ = p[k];
					/*
					 * Warn about those unusable chars in msdosfs here?	XXX
					 */
					if (p[k + 1])
						t[-1] = '?';
				}
				if (k >= 11)
					for (k = 14; k < 26 && t < longName + sizeof(longName); k += 2) {
						if (!p[k] && !p[k + 1])
							break;
						*t++ = p[k];
						if (p[k + 1])
							t[-1] = '?';
					}
				if (k >= 26)
					for (k = 28; k < 32 && t < longName + sizeof(longName); k += 2) {
						if (!p[k] && !p[k + 1])
							break;
						*t++ = p[k];
						if (p[k + 1])
							t[-1] = '?';
					}
				if (t >= longName + sizeof(longName)) {
					fprintf(stderr, "long filename too long\n");
					if (!invlfn) {
						invlfn = vallfn;
						invcl = valcl;
					}
					vallfn = NULL;
				}
				if (p[26] | (p[27] << 8)) {
					fprintf(stderr, "long filename record cluster start != 0\n");
					if (!invlfn) {
						invlfn = vallfn;
						invcl = cl;
					}
					vallfn = NULL;
				}
				continue;	/* long records don't carry further
						 * information */
			}

			/*
			 * This is a standard msdosfs directory entry.
			 */
			memset(&dirent, 0, sizeof dirent);

			/*
			 * it's a short name record, but we need to know
			 * more, so get the flags first.
			 */
			dirent.flags = p[11];

			/*
			 * Translate from 850 to ISO here		XXX
			 */
			for (j = 0; j < 8; j++)
				dirent.name[j] = p[j];
			dirent.name[8] = '\0';
			for (k = 7; k >= 0 && dirent.name[k] == ' '; k--)
				dirent.name[k] = '\0';
			if (dirent.name[k] != '\0')
				k++;
			if (dirent.name[0] == SLOT_E5)
				dirent.name[0] = 0xe5;

			if (dirent.flags & ATTR_VOLUME) {
				if (vallfn || invlfn) {
					mod |= removede(f, boot, fat,
							invlfn ? invlfn : vallfn, p,
							invlfn ? invcl : valcl, -1, 0,
							fullpath(dir), 2);
					vallfn = NULL;
					invlfn = NULL;
				}
				continue;
			}

			if (p[8] != ' ')
				dirent.name[k++] = '.';
			for (j = 0; j < 3; j++)
				dirent.name[k++] = p[j+8];
			dirent.name[k] = '\0';
			for (k--; k >= 0 && dirent.name[k] == ' '; k--)
				dirent.name[k] = '\0';

			if (vallfn && shortSum != calcShortSum(p)) {
				if (!invlfn) {
					invlfn = vallfn;
					invcl = valcl;
				}
				vallfn = NULL;
			}
			dirent.head = p[26] | (p[27] << 8);
			if (boot->ClustMask == CLUST32_MASK)
				dirent.head |= (p[20] << 16) | (p[21] << 24);
			dirent.size = p[28] | (p[29] << 8) | (p[30] << 16) | (p[31] << 24);
			if (vallfn) {
				strcpy(dirent.lname, longName);
				longName[0] = '\0';
				shortSum = -1;
			}

			dirent.parent = dir;
			dirent.next = dir->child;

			if (invlfn) {
				mod |= k = removede(f, boot, fat,
						    invlfn, vallfn ? vallfn : p,
						    invcl, vallfn ? valcl : cl, cl,
						    fullpath(&dirent), 0);
				if (mod & FSFATAL)
					return FSFATAL;
				if (vallfn
				    ? (valcl == cl && vallfn != buffer)
				    : p != buffer)
					if (k & FSDIRMOD)
						mod |= THISMOD;
			}

			vallfn = NULL; /* not used any longer */
			invlfn = NULL;

			if (dirent.size == 0 && !(dirent.flags & ATTR_DIRECTORY)) {
				if (dirent.head != 0) {
					fprintf(stderr, "%s has clusters, but size 0\n",
					      fullpath(&dirent));
					if (ask(1, "Drop allocated clusters")) {
						p[26] = p[27] = 0;
						if (boot->ClustMask == CLUST32_MASK)
							p[20] = p[21] = 0;
						clearclusterchain(boot, fat, dirent.head);
						dirent.head = 0;
						mod |= THISMOD|FSDIRMOD|FSFATMOD;
					} else
						mod |= FSERROR;
				}
			} else if (dirent.head == 0
				   && !strcmp(dirent.name, "..")
				   && dir->parent			/* XXX */
				   && !dir->parent->parent) {
				/*
				 *  Do nothing, the parent is the root
				 */
			} else if (dirent.head < CLUST_FIRST
				   || dirent.head >= boot->NumClusters
				   || fat[dirent.head].next == CLUST_FREE
				   || (fat[dirent.head].next >= CLUST_RSRVD
				       && fat[dirent.head].next < CLUST_EOFS)
				   || fat[dirent.head].head != dirent.head) {
				if (dirent.head == 0)
					fprintf(stderr, "%s has no clusters\n",
					      fullpath(&dirent));
				else if (dirent.head < CLUST_FIRST
					 || dirent.head >= boot->NumClusters)
					fprintf(stderr, "%s starts with cluster out of range(%u)\n",
					      fullpath(&dirent),
					      dirent.head);
				else if (fat[dirent.head].next == CLUST_FREE)
					fprintf(stderr, "%s starts with free cluster\n",
					      fullpath(&dirent));
				else if (fat[dirent.head].next >= CLUST_RSRVD)
					fprintf(stderr, "%s starts with cluster marked %s\n",
					      fullpath(&dirent),
					      rsrvdclustertype(fat[dirent.head].next));
				else
					fprintf(stderr, "%s doesn't start a cluster chain\n",
					      fullpath(&dirent));
				if (dirent.flags & ATTR_DIRECTORY) {
					if (ask(1, "Remove")) {
						*p = SLOT_DELETED;
						mod |= THISMOD|FSDIRMOD;
					} else
						mod |= FSERROR;
					continue;
				} else {
					if (ask(1, "Truncate")) {
						p[28] = p[29] = p[30] = p[31] = 0;
						p[26] = p[27] = 0;
						if (boot->ClustMask == CLUST32_MASK)
							p[20] = p[21] = 0;
						dirent.size = 0;
						mod |= THISMOD|FSDIRMOD;
					} else
						mod |= FSERROR;
				}
			}

			if (dirent.head >= CLUST_FIRST && dirent.head < boot->NumClusters)
				fat[dirent.head].flags |= FAT_USED;

			if (myfunc){
				if (myfunc(&dirent, boot, fat) != 0){
					exit(0);
				}
			}
			
			if (dirent.flags & ATTR_DIRECTORY) {
				/*
				 * gather more info for directories
				 */
				struct dirTodoNode *n;

				if (dirent.size) {
					fprintf(stderr, "Directory %s has size != 0\n",
					      fullpath(&dirent));
					if (ask(1, "Correct")) {
						p[28] = p[29] = p[30] = p[31] = 0;
						dirent.size = 0;
						mod |= THISMOD|FSDIRMOD;
					} else
						mod |= FSERROR;
				}
				/*
				 * handle '.' and '..' specially
				 */
				if (strcmp(dirent.name, ".") == 0) {
					if (dirent.head != dir->head) {
						fprintf(stderr, "'.' entry in %s has incorrect start cluster\n",
						      fullpath(dir));
						if (ask(1, "Correct")) {
							dirent.head = dir->head;
							p[26] = (u_char)dirent.head;
							p[27] = (u_char)(dirent.head >> 8);
							if (boot->ClustMask == CLUST32_MASK) {
								p[20] = (u_char)(dirent.head >> 16);
								p[21] = (u_char)(dirent.head >> 24);
							}
							mod |= THISMOD|FSDIRMOD;
						} else
							mod |= FSERROR;
					}
					continue;
                } else if (strcmp(dirent.name, "..") == 0) {
					if (dir->parent) {		/* XXX */
						if (!dir->parent->parent) {
							if (dirent.head) {
								fprintf(stderr, "'..' entry in %s has non-zero start cluster\n",
								      fullpath(dir));
								if (ask(1, "Correct")) {
									dirent.head = 0;
									p[26] = p[27] = 0;
									if (boot->ClustMask == CLUST32_MASK)
										p[20] = p[21] = 0;
									mod |= THISMOD|FSDIRMOD;
								} else
									mod |= FSERROR;
							}
						} else if (dirent.head != dir->parent->head) {
							fprintf(stderr, "'..' entry in %s has incorrect start cluster\n",
							      fullpath(dir));
							if (ask(1, "Correct")) {
								dirent.head = dir->parent->head;
								p[26] = (u_char)dirent.head;
								p[27] = (u_char)(dirent.head >> 8);
								if (boot->ClustMask == CLUST32_MASK) {
									p[20] = (u_char)(dirent.head >> 16);
									p[21] = (u_char)(dirent.head >> 24);
								}
								mod |= THISMOD|FSDIRMOD;
							} else
								mod |= FSERROR;
						}
					}
					continue;
				} else { //only one directory entry can point to dir->head, it's  '.'
					if (dirent.head == dir->head) {
						fprintf(stderr, "%s entry in %s has incorrect start cluster.remove\n",
								dirent.name, fullpath(dir));
						//we have to remove this directory entry rigth now rigth here
						if (ask(1, "Remove")) {
							*p = SLOT_DELETED;
							mod |= THISMOD|FSDIRMOD;
						} else
							mod |= FSERROR;
						continue;
					}
					/* Consistency checking. a directory must have at least two entries:
					   a dot (.) entry that points to itself, and a dot-dot (..)
					   entry that points to its parent.
					 */
					if (check_dot_dot(f,boot,fat,&dirent)) {
						//mark directory entry as deleted.
						if (ask(1, "Remove")) {
							*p = SLOT_DELETED;
							mod |= THISMOD|FSDIRMOD;
						} else
							mod |= FSERROR;
						continue;
                    }
				}

				/* create directory tree node */
				if (!(d = newDosDirEntry())) {
					perror("No space for directory");
					return FSFATAL;
				}
				memcpy(d, &dirent, sizeof(struct dosDirEntry));
				/* link it into the tree */
				dir->child = d;
#if 0
				fprintf(stderr, "%s: %s : 0x%02x:head %d, next 0x%0x parent 0x%0x child 0x%0x\n",
						__func__,d->name,d->flags,d->head,d->next,d->parent,d->child);
#endif
				/* Enter this directory into the todo list */
				if (!(n = newDirTodo())) {
					perror("No space for todo list");
					return FSFATAL;
				}
				n->next = pendingDirs;
				n->dir = d;
				pendingDirs = n;
			} else {
				mod |= k = checksize(boot, fat, p, &dirent);
				if (k & FSDIRMOD)
					mod |= THISMOD;
			}
			boot->NumFiles++;
		}
		if (mod & THISMOD) {
			last *= 32;
			if (lseek64(f, off, SEEK_SET) != off
			    || write(f, buffer, last) != last) {
				perror("Unable to write directory");
				return FSFATAL;
			}
			mod &= ~THISMOD;
		}
	} while ((cl = fat[cl].next) >= CLUST_FIRST && cl < boot->NumClusters);
	if (invlfn || vallfn)
		mod |= removede(f, boot, fat,
				invlfn ? invlfn : vallfn, p,
				invlfn ? invcl : valcl, -1, 0,
				fullpath(dir), 1);
	return mod & ~THISMOD;
}

static struct dosDirEntry *rootDirEntry;
static struct dosDirEntry *lostDir;

/*
 * Init internal state for a new directory scan.
 */
int
resetDosDirSection(struct bootblock *boot, struct fatEntry *fat)
{
	int b1, b2;
	cl_t cl;
	int ret = FSOK;

	b1 = boot->RootDirEnts * 32;
	b2 = boot->SecPerClust * boot->BytesPerSec;

	if (!(buffer = malloc(b1 > b2 ? b1 : b2))
	    || !(delbuf = malloc(b2))
	    || !(rootDirEntry = newDosDirEntry())) {
		perror("No space for directory");
		return FSFATAL;
	}
	memset(rootDirEntry, 0, sizeof *rootDirEntry);
	if (boot->flags & FAT32) {
		if (boot->RootCl < CLUST_FIRST || boot->RootCl >= boot->NumClusters) {
			pfatal("Root directory starts with cluster out of range(%u)",
			       boot->RootCl);
			return FSFATAL;
		}
		cl = fat[boot->RootCl].next;
		if (cl < CLUST_FIRST
		    || (cl >= CLUST_RSRVD && cl< CLUST_EOFS)
		    || fat[boot->RootCl].head != boot->RootCl) {
			if (cl == CLUST_FREE)
				fprintf(stderr, "Root directory starts with free cluster\n");
			else if (cl >= CLUST_RSRVD)
				fprintf(stderr, "Root directory starts with cluster marked %s\n",
				      rsrvdclustertype(cl));
			else {
				pfatal("Root directory doesn't start a cluster chain");
				return FSFATAL;
			}
			if (ask(1, "Fix")) {
				fat[boot->RootCl].next = CLUST_FREE;
				ret = FSFATMOD;
			} else
				ret = FSFATAL;
		}

		fat[boot->RootCl].flags |= FAT_USED;
		rootDirEntry->head = boot->RootCl;
	}

	return ret;
}


static int
scanDirTree(int dosfs, struct bootblock *boot, struct fatEntry *fat)
{
	int mod;

	mod = readDosDirSection(dosfs, boot, fat, rootDirEntry);
	if (mod & FSFATAL)
		return FSFATAL;

	/*
	 * process the directory todo list
	 */
	while (pendingDirs) {
		struct dosDirEntry *dir = pendingDirs->dir;
		struct dirTodoNode *n = pendingDirs->next;

		/*
		 * remove TODO entry now, the list might change during
		 * directory reads
		 */
		freeDirTodo(pendingDirs);
		pendingDirs = n;

		/*
		 * handle subdirectory
		 */
		mod |= readDosDirSection(dosfs, boot, fat, dir);
		if (mod & FSFATAL)
			return FSFATAL;
	}

	return mod;
}

/*
 * Cleanup after a directory scan
 */
void
finishDosDirSection(void)
{
	struct dirTodoNode *p, *np;
	struct dosDirEntry *d, *nd;

	for (p = pendingDirs; p; p = np) {
		np = p->next;
		freeDirTodo(p);
	}
	pendingDirs = 0;
	for (d = rootDirEntry; d; d = nd) {
		if ((nd = d->child) != NULL) {
			d->child = 0;
			continue;
		}
		if (!(nd = d->next))
			nd = d->parent;
		freeDosDirEntry(d);
	}
	rootDirEntry = lostDir = NULL;
	free(buffer);
	free(delbuf);
	buffer = NULL;
	delbuf = NULL;
}


int
getfatinfo(const char *fname)
{
	int dosfs;
	struct bootblock boot;
	struct fatEntry *fat = NULL;
	int i, finish_dosdirsection=0;
	int mod = 0;
	int ret = 8;
    int quiet = 0;
    int skip_fat_compare = 0;

	dosfs = open(fname, O_RDWR);
	if (dosfs < 0) {
		perror("Can't open");
		return 8;
	}

	if (readboot(dosfs, &boot) == FSFATAL) {
		close(dosfs);
		fprintf(stderr, "\n");
		return 8;
	}

	dumpbootblock(&boot);

	if (!scanAllDir){
		return 0;
	}

#if 0
	if (skipclean && preen && checkdirty(dosfs, &boot)) {
		fprintf(stderr, "%s: ", fname);
		fprintf(stderr, "FILESYSTEM CLEAN; SKIPPING CHECKS\n");
		ret = 0;
		goto out;
	}
#endif
    if (((boot.FATsecs * boot.BytesPerSec) / 1024) > FAT_COMPARE_MAX_KB)
        skip_fat_compare = 1;

	if (!quiet)  {
        if (skip_fat_compare) 
                fprintf(stderr, "** Phase 1 - Read FAT (compare skipped)\n");
		else if (boot.ValidFat < 0)
			fprintf(stderr, "** Phase 1 - Read and Compare FATs\n");
		else
			fprintf(stderr, "** Phase 1 - Read FAT\n");
	}

	if (fragmentSize != 0){
		for (i = 0; i < (int)boot.FATs; i++) {
			ret = traverseFatTable(dosfs, &boot, i);
			if (ret)
				fprintf(stderr, "Failed to traverseFatTable(). \n");
			
		}

		close(dosfs);
		return 0;
	}

	if (printFatTable == 1 || printFatTable == 2){
		ret = traverseFatTable(dosfs, &boot, printFatTable - 1);
		if (ret)
			fprintf(stderr, "Failed to traverseFatTable(). \n");
		close(dosfs);
		
		return 0;
	}
	
	mod |= readfatTable(dosfs, &boot, boot.ValidFat >= 0 ? boot.ValidFat : 0, &fat);
	if (mod & FSFATAL) {
		fprintf(stderr, "Fatal error during readfat()\n");
		close(dosfs);
		return 8;
	}

	if (!skip_fat_compare && boot.ValidFat < 0)
		for (i = 1; i < (int)boot.FATs; i++) {
			struct fatEntry *currentFat;

			mod |= readfatTable(dosfs, &boot, i, &currentFat);

			if (mod & FSFATAL) {
				fprintf(stderr, "Fatal error during readfat() for comparison\n");
				goto out;
			}

			mod |= comparefattab(&boot, fat, currentFat, i);
			free(currentFat);
			if (mod & FSFATAL) {
				fprintf(stderr, "Fatal error during FAT comparison\n");
				goto out;
			}
		}

	if (!quiet)
		fprintf(stderr, "** Phase 2 - Check Cluster Chains\n");

	mod |= checkfattab(&boot, fat);
	if (mod & FSFATAL) {
		fprintf(stderr, "Fatal error during FAT check\n");
		goto out;
	}
	/* delay writing FATs */

	if (!quiet)
		fprintf(stderr, "** Phase 3 - Checking Directories\n");

	mod |= resetDosDirSection(&boot, fat);
	finish_dosdirsection = 1;
	if (mod & FSFATAL) {
		fprintf(stderr, "Fatal error during resetDosDirSection()\n");
		goto out;
	}
	/* delay writing FATs */

	mod |= scanDirTree(dosfs, &boot, fat);
	if (mod & FSFATAL)
		goto out;

	if (!quiet)
		fprintf(stderr, "** Phase 4 - Checking for Lost Files\n");

	if (mod & (FSFATAL | FSERROR))
		goto out;

	ret = 0;

    out:
	if (finish_dosdirsection)
		finishDosDirSection();
	free(fat);
	close(dosfs);

	if (mod & (FSFATMOD|FSDIRMOD)) {
		fprintf(stderr, "\n***** FILE SYSTEM WAS MODIFIED *****\n");
		return 4; 
	}

	return ret;
}

static void
usage(void)
{

	fprintf(stderr, "%s\n%s\n%s\n%s\n%s\n%s\n",
	    "usage: -a print all dir tree info ...",
	    "       -n find file which has the sector...",
		"       -d dump all sectors number that belong to the file...",
		"       -r dump sectors data according to input filename...",
		"       -f print fat table entry...",
		"       -s fragment size...");
	exit(1);
}

static int
dumpDevSectors(char* inputFile, char* dev)
{
	char line[256], *p;
	FILE* filp;
	int ret = -1;
	unsigned int start_sec, end_sec;
	int file_size = 0;
	int writed_size = 0;
	unsigned int cluster_size = 0;
	unsigned int total_cluster = 0;
	int devFd;
	int outFd;
	char sector[512];
	unsigned long long int offset64;
	int writeLen;

	devFd = open(dev, O_RDONLY);
	if (devFd < 0){
		fprintf(stderr, "Failed to open dev %s. \n", dev);
		return -1;
	}

	outFd = open("./dumpedDevSectors", O_RDWR|O_CREAT, S_IRUSR|S_IWUSR);
	if (outFd < 0){
		fprintf(stderr, "Failed to open outfile. \n");
		return -1;
	}
	

	filp = fopen(inputFile, "r");
	if (!filp){
		fprintf(stderr, "Failed to fopen sectors file. \n");
		return -1;
	}

	while ((p = fgets(line, sizeof(line), filp)) != NULL) {
		while ((*p > '9' || *p < '0') && (*p != 0)){
			p++;
		}
		if (*p == 0){
			continue;
		}
		start_sec = 0;
		end_sec = 0;
		while (*p <= '9' && *p >= '0'){
			start_sec *= 10;
			start_sec += (*p - '0');
			p++;
		}
		while ((*p > '9' || *p < '0') && (*p != 0)){
			p++;
		}
		if (*p == 0){
			continue;
		}
		while (*p <= '9' && *p >= '0'){
			end_sec *= 10;
			end_sec += (*p - '0');
			p++;
		}
		fprintf(stderr, "startsec %d, endsec %d. \n", start_sec, end_sec);
		if (file_size == 0 || cluster_size == 0){
			file_size = start_sec;
			cluster_size = end_sec;

			if (file_size == 0 || cluster_size == 0){
				fprintf(stderr, "Invalid file size %u and cluster size %u\n", file_size, cluster_size);
				goto FAIL;
			}

			total_cluster = file_size / cluster_size + 1;

			fprintf(stderr, "fileSize %d, cluster size %d, total_cluster %d. \n", file_size, cluster_size, total_cluster);
			continue;
		}

		offset64 = (unsigned long long int)start_sec * 512llu;
		ret = lseek64(devFd, offset64, SEEK_SET);
		if (ret < 0){
			fprintf(stderr, "Failed to lseek64() .\n");
			goto FAIL;
		}

		for ( ; start_sec <= end_sec; start_sec++){
			ret = read(devFd, sector, sizeof(sector));
			if (ret != sizeof(sector)){
				fprintf(stderr, "Failed to read sectorfile. ret %d\n", ret);
				goto FAIL;
			}

			if (file_size - writed_size > 512){
				writeLen = sizeof(sector);
			}
			else{
				writeLen = file_size - writed_size;
			}

			ret = write(outFd, sector, writeLen);
			if (ret < 0){
				fprintf(stderr, "Failed to write dumpfile. \n");
				goto FAIL;
			}

			writed_size += writeLen;
			
			if (writeLen != 512){
				break;
			}
		}

		if (writed_size >= file_size){
			break;
		}

	}

	fclose(filp);
	close(devFd);
	close(outFd);

	return 0;
FAIL:
	fclose(filp);
	return -1;
}

int
main(int argc, char **argv)
{
	int ret = 0, erg;
	int ch;
	int dumpDevSecForFile = 0;
	char* sectorsFileName;

	while ((ch = getopt(argc, argv, "haf:n:d:r:s:")) != -1) {
		switch (ch) {
		case 'h':
			usage();
			break;
		case 'a':
			scanAllDir = 1;
			myfunc = printClusterNo;
			break;
		case 'n':
			scanAllDir = 1;
			myfunc = findSectorNo;
			sectorNo = atoi(optarg);
			fprintf(stderr, "To look for sectorNo %d\n", sectorNo);
			break;
		case 'd':
			scanAllDir = 1;
			dumpSectorFileName = optarg;
			myfunc = dumpFileSectors;
			break;
		case 'r':
			dumpDevSecForFile = 1;
			sectorsFileName = optarg;
			break;
		case 's':
			scanAllDir = 1;
			fragmentSize = atoi(optarg);
			break;
		case 'f':
			scanAllDir = 1;
			printFatTable = atoi(optarg);
			break;		
		default:
			usage();
			break;
		}
	}
	argc -= optind;
	argv += optind;

	if (!argc)
		usage();

	if (dumpDevSecForFile){
		ret = dumpDevSectors(sectorsFileName, *argv);
		if (ret < 0){
			fprintf(stderr, "dumpFileSectors() failed. \n");
		}
		return 0;
	}
	
	ret = getfatinfo(*argv);

	return ret;
}


/*VARARGS*/
int
ask(int def, const char *fmt, ...)
{
	return 0;
}


