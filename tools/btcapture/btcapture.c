/*
 * block queue tracing application
 *
 * Rewrite to capture blktrace when anr accur on sprd paltform
 *	Justin Wang <justin.wang@spreadtrum.com> - Sep. 2014
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <getopt.h>
#include <sched.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <pthread.h>
#include <locale.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <sys/mman.h>
#include <sys/param.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/sendfile.h>

#include "list.h"
#include "btcapture.h"

/*
 * You may want to increase this even more, if you are logging at a high
 * rate and see skipped/missed events
 */
#define BUF_SIZE		(512 * 1024)
#define BUF_NR			(4)

#define FILE_VBUF_SIZE		(128 * 1024)

#define DEBUGFS_TYPE		(0x64626720)
#define TRACE_NET_PORT		(8462)


/*
 * Generic stats collected: nevents can be _roughly_ estimated by data_read
 * (discounting pdu...)
 *
 * These fields are updated w/ pdc_dr_update & pdc_nev_update below.
 */
struct pdc_stats {
	unsigned long long data_read;
	unsigned long long nevents;
};

struct devpath {
	struct list_head head;
	char *path;			/* path to device special file */
	char *buts_name;		/* name returned from bt kernel code */
	struct pdc_stats *stats;
	int fd, ncpus;
	unsigned long long drops;

	/*
	 * For piped output only:
	 *
	 * Each tracer will have a tracer_devpath_head that it will add new
	 * data onto. It's list is protected above (tracer_devpath_head.mutex)
	 * and it will signal the processing thread using the dp_cond,
	 * dp_mutex & dp_entries variables above.
	 */
	struct tracer_devpath_head *heads;

	/*
	 * For network server mode only:
	 */
	struct cl_host *ch;
	u32 cl_id;
	time_t cl_connect_time;
	struct io_info *ios;
};


struct tracer_devpath_head {
	pthread_mutex_t mutex;
	struct list_head head;
	struct trace_buf *prev;
};

/*
 * Used to handle the mmap() interfaces for output file (containing traces)
 */
struct mmap_info {
	void *fs_buf;
	unsigned long long fs_size, fs_max_size, fs_off, fs_buf_len;
	unsigned long buf_size, buf_nr;
	int pagesize;
};

/*
 * Each thread doing work on a (client) side of blktrace will have one
 * of these. The ios array contains input/output information, pfds holds
 * poll() data. The volatile's provide flags to/from the main executing
 * thread.
 */
struct tracer {
	struct list_head head;
	struct io_info *ios;
	struct pollfd *pfds;
	pthread_t thread;
	int cpu, nios;
	volatile int status, is_done;
};

/*
 * This structure is (generically) used to providide information
 * for a read-to-write set of values.
 *
 * ifn & ifd represent input information
 *
 * ofn, ofd, ofp, obuf & mmap_info are used for output file (optionally).
 */
struct io_info {
	struct devpath *dpp;
	FILE *ofp;
	char *obuf;
	struct cl_conn *nc;	/* Server network connection */

	/*
	 * mmap controlled output files
	 */
	struct mmap_info mmap_info;

	/*
	 * Client network fields
	 */
	unsigned int ready;
	unsigned long long data_queued;

	/*
	 * Input/output file descriptors & names
	 */
	int ifd, ofd;
	char ifn[MAXPATHLEN + 64];
	char ofn[MAXPATHLEN + 64];
};

static char blktrace_version[] = "2.0.0";

/*
 * Linkage to blktrace helper routines (trace conversions)
 */
int data_is_native = -1;

static int ndevs;
static int ncpus;
static int pagesize;
static int act_mask = ~0U;
static int kill_running_trace;
static int capture_running_trace;

static char *debugfs_path = "/sys/kernel/debug";
static char *output_name;
static char *output_dir;

static unsigned long buf_size = BUF_SIZE;
static unsigned long buf_nr = BUF_NR;


static LIST_HEAD(devpaths);
static LIST_HEAD(tracers);


static int (*handle_pfds)(struct tracer *, int, int);


#define S_OPTS	"d:a:A:r:ckvVb:n:D:I:"
static struct option l_opts[] = {
	{
		.name = "dev",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'd'
	},
	{
		.name = "input-devs",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'I'
	},
	{
		.name = "act-mask",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'a'
	},
	{
		.name = "set-mask",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'A'
	},
	{
		.name = "relay",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'r'
	},
	{
		.name = "kill",
		.has_arg = no_argument,
		.flag = NULL,
		.val = 'k'
	},
	{
		.name = "capture",
		.has_arg = no_argument,
		.flag = NULL,
		.val = 'c'
	},
	{
		.name = "version",
		.has_arg = no_argument,
		.flag = NULL,
		.val = 'v'
	},
	{
		.name = "version",
		.has_arg = no_argument,
		.flag = NULL,
		.val = 'V'
	},
	{
		.name = "buffer-size",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'b'
	},
	{
		.name = "num-sub-buffers",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'n'
	},
	{
		.name = "output-dir",
		.has_arg = required_argument,
		.flag = NULL,
		.val = 'D'
	},
	{
		.name = NULL,
	}
};

static char usage_str[] = "\n\n" \
	"-d <dev>             | --dev=<dev>\n" \
        "[ -r <debugfs path>  | --relay=<debugfs path> ]\n" \
        "[ -D <dir>           | --output-dir=<dir>\n" \
        "[ -a <action field>  | --act-mask=<action field>]\n" \
        "[ -A <action mask>   | --set-mask=<action mask>]\n" \
        "[ -b <size>          | --buffer-size]\n" \
        "[ -n <number>        | --num-sub-buffers=<number>]\n" \
        "[ -c                 | --capture]\n" \
        "[ -I <devs file>     | --input-devs=<devs file>]\n" \
        "[ -v <version>       | --version]\n" \
        "[ -V <version>       | --version]\n" \

	"\t-d Use specified device. May also be given last after options\n" \
	"\t-r Path to mounted debugfs, defaults to /sys/kernel/debug\n" \
	"\t-D Directory to prepend to output file names\n" \
	"\t-a Only trace specified actions. See documentation\n" \
	"\t-A Give trace mask as a single value. See documentation\n" \
	"\t-b Sub buffer size in KiB (default 512)\n" \
	"\t-n Number of sub buffers (default 4)\n" \
	"\t-c Run blktrace capture and restart monitor\n" \
	"\t-I Add devices found in <devs file>\n" \
	"\t-v Print program version info\n" \
	"\t-V Print program version info\n\n";

extern int find_mask_map(char *string);
extern int valid_act_opt(int x);

static void clear_events(struct pollfd *pfd)
{
	pfd->events = 0;
	pfd->revents = 0;
}

static void show_usage(char *prog)
{
	fprintf(stderr, "Usage: %s %s", prog, usage_str);
}

static void init_mmap_info(struct mmap_info *mip)
{
	mip->buf_size = buf_size;
	mip->buf_nr = buf_nr;
	mip->pagesize = pagesize;
}

static void dpp_free(struct devpath *dpp)
{
	if (dpp->stats)
		free(dpp->stats);
	if (dpp->ios)
		free(dpp->ios);
	if (dpp->path)
		free(dpp->path);
	if (dpp->buts_name)
		free(dpp->buts_name);
	free(dpp);
}

#ifndef _ANDROID_
static int increase_limit(int resource, rlim_t increase)
{
	struct rlimit rlim;
	int save_errno = errno;

	if (!getrlimit(resource, &rlim)) {
		rlim.rlim_cur += increase;
		if (rlim.rlim_cur >= rlim.rlim_max)
			rlim.rlim_max = rlim.rlim_cur + increase;

		if (!setrlimit(resource, &rlim))
			return 1;
	}

	errno = save_errno;
	return 0;
}
#endif

static int handle_open_failure(void)
{
	if (errno == ENFILE || errno == EMFILE)
#ifndef _ANDROID_
		return increase_limit(RLIMIT_NOFILE, 16);
#else
		return -ENOSYS;
#endif
	return 0;
}

static int handle_mem_failure(size_t length)
{
	if (errno == ENFILE)
		return handle_open_failure();
	else if (errno == ENOMEM)
#ifndef _ANDROID_
		return increase_limit(RLIMIT_MEMLOCK, 2 * length);
#else
		return -ENOSYS;
#endif
	return 0;
}

static FILE *my_fopen(const char *path, const char *mode)
{
	FILE *fp;

	do {
		fp = fopen(path, mode);
	} while (fp == NULL && handle_open_failure());

	return fp;
}

static int my_open(const char *path, int flags)
{
	int fd;

	do {
		fd = open(path, flags);
	} while (fd < 0 && handle_open_failure());

	return fd;
}

static void *my_mmap(void *addr, size_t length, int prot, int flags, int fd,
		     off_t offset)
{
	void *new;

	do {
		new = mmap(addr, length, prot, flags, fd, offset);
	} while (new == MAP_FAILED && handle_mem_failure(length));

	return new;
}

static int my_mlock(struct tracer *tp,
		    const void *addr, size_t len)
{
	int ret, retry = 0;

	do {
		ret = mlock(addr, len);
		if ((retry >= 10) && tp && tp->is_done)
			break;
		retry++;
	} while (ret < 0 && handle_mem_failure(len));

	return ret;
}

static int setup_mmap(int fd, unsigned int maxlen,
		      struct mmap_info *mip,
		      struct tracer *tp)
{
	if (mip->fs_off + maxlen > mip->fs_buf_len) {
		unsigned long nr = max(16, mip->buf_nr);

		if (mip->fs_buf) {
			munlock(mip->fs_buf, mip->fs_buf_len);
			munmap(mip->fs_buf, mip->fs_buf_len);
			mip->fs_buf = NULL;
		}

		mip->fs_off = mip->fs_size & (mip->pagesize - 1);
		mip->fs_buf_len = (nr * mip->buf_size) - mip->fs_off;
		mip->fs_max_size += mip->fs_buf_len;

		if (ftruncate(fd, mip->fs_max_size) < 0) {
			perror("setup_mmap: ftruncate");
			return 1;
		}

		mip->fs_buf = my_mmap(NULL, mip->fs_buf_len, PROT_WRITE,
				      MAP_SHARED, fd,
				      mip->fs_size - mip->fs_off);
		if (mip->fs_buf == MAP_FAILED) {
			perror("setup_mmap: mmap");
			return 1;
		}
		if (my_mlock(tp, mip->fs_buf, mip->fs_buf_len) < 0) {
			perror("setup_mlock: mlock");
			return 1;
		}
	}

	return 0;
}

static int __stop_trace(int fd)
{
	/*
	 * Should be stopped, don't complain if it isn't
	 */
	ioctl(fd, BLKTRACESTOP);
	return ioctl(fd, BLKTRACETEARDOWN);
}

static void setup_buts(void)
{
	struct list_head *p;

	__list_for_each(p, &devpaths) {
		struct blk_user_trace_setup buts;
		struct devpath *dpp = list_entry(p, struct devpath, head);

		memset(&buts, 0, sizeof(buts));
		buts.buf_size = buf_size;
		buts.buf_nr = buf_nr;
		buts.act_mask = act_mask;

		if (ioctl(dpp->fd, BLKTRACESETUP, &buts) >= 0) {
			dpp->ncpus = ncpus;
			if(dpp->buts_name)
				free(dpp->buts_name);
			dpp->buts_name = strdup(buts.name);
		} else
			fprintf(stderr, "BLKTRACESETUP(2) %s failed: %d/%s\n",
				dpp->path, errno, strerror(errno));
	}
}

static void start_buts(void)
{
	struct list_head *p;

	__list_for_each(p, &devpaths) {
		struct devpath *dpp = list_entry(p, struct devpath, head);

		if (ioctl(dpp->fd, BLKTRACESTART) < 0) {
			fprintf(stderr, "BLKTRACESTART %s failed: %d/%s\n",
				dpp->path, errno, strerror(errno));
		}
	}
}

static void stop_buts(void)
{
	struct list_head *p;

	__list_for_each(p, &devpaths) {
		struct devpath *dpp = list_entry(p, struct devpath, head);

		if (ioctl(dpp->fd, BLKTRACESTOP) < 0) {
			fprintf(stderr, "BLKTRACESTOP %s failed: %d/%s\n",
				dpp->path, errno, strerror(errno));
		}
	}
}

static int get_drops(struct devpath *dpp)
{
	int fd, drops = 0;
	char fn[MAXPATHLEN + 64], tmp[256];

	snprintf(fn, sizeof(fn), "%s/block/%s/dropped", debugfs_path,
		 dpp->buts_name);

	fd = my_open(fn, O_RDONLY);
	if (fd < 0) {
		/*
		 * This may be ok: the kernel may not support
		 * dropped counts.
		 */
		if (errno != ENOENT)
			fprintf(stderr, "Could not open %s: %d/%s\n",
				fn, errno, strerror(errno));
		return 0;
	} else if (read(fd, tmp, sizeof(tmp)) < 0) {
		fprintf(stderr, "Could not read %s: %d/%s\n",
			fn, errno, strerror(errno));
	} else
		drops = atoi(tmp);
	close(fd);

	return drops;
}

static void get_all_drops(void)
{
	struct list_head *p;

	__list_for_each(p, &devpaths) {
		struct devpath *dpp = list_entry(p, struct devpath, head);

		dpp->drops = get_drops(dpp);
	}
}

/**
 * FIXME
 * 
 * We can get bd name from the file -
 * /sys/class/block/-/uevent
 * @specified field "DEVNAME="
 * or 
 * add a ioctl to get bd name.
 **/
static int set_bd_name(struct devpath *dpp)
{
	char *nm;
	char resolved_path[PATH_MAX];

	if(!dpp->path) {
		printf("dpp path invalid.\n");
		return 1;
	}
	realpath(dpp->path, resolved_path);
	nm = strrchr(resolved_path,'/');
	if(!nm) {
		printf("set_bd_name: set dev name failed!\n");
		return 1;
	}
	dpp->buts_name = strdup(nm+strlen("/"));

	return 0;
}

static int add_devpath(char *path)
{
	int fd;
	struct devpath *dpp;
	struct list_head *p;

	/*
	 * Verify device is not duplicated
	 */
	__list_for_each(p, &devpaths) {
	       struct devpath *tmp = list_entry(p, struct devpath, head);
	       if (!strcmp(tmp->path, path))
		        return 0;
	}
	/*
	 * Verify device is valid before going too far
	 */
	fd = my_open(path, O_RDONLY | O_NONBLOCK);
	if (fd < 0) {
		fprintf(stderr, "Invalid path %s specified: %d/%s\n",
			path, errno, strerror(errno));
		return 1;
	}

	dpp = malloc(sizeof(*dpp));
	memset(dpp, 0, sizeof(*dpp));
	dpp->path = strdup(path);
	dpp->fd = fd;
	if(set_bd_name(dpp))
		return 1;
	ndevs++;
	list_add_tail(&dpp->head, &devpaths);

	return 0;
}

static void rel_devpaths(void)
{
	struct list_head *p, *q;

	list_for_each_safe(p, q, &devpaths) {
		struct devpath *dpp = list_entry(p, struct devpath, head);

		list_del(&dpp->head);
		//__stop_trace(dpp->fd);
		close(dpp->fd);
#if 0
		if (dpp->heads)
			free_tracer_heads(dpp);
#endif
		dpp_free(dpp);
		ndevs--;
	}
}

static inline void read_err(int cpu, char *ifn)
{
	if (errno != EAGAIN)
		fprintf(stderr, "Thread %d failed read of %s: %d/%s\n",
			cpu, ifn, errno, strerror(errno));
}

static int fill_ofname(struct io_info *iop, int cpu)
{
	int len;
	struct stat sb;

	if (output_dir)
		len = snprintf(iop->ofn, sizeof(iop->ofn), "%s/", output_dir);
	else
		len = snprintf(iop->ofn, sizeof(iop->ofn), "./");

	if (stat(iop->ofn, &sb) < 0) {
		if (errno != ENOENT) {
			fprintf(stderr,
				"Destination dir %s stat failed: %d/%s\n",
				iop->ofn, errno, strerror(errno));
			return 1;
		}
		/*
		 * There is no synchronization between multiple threads
		 * trying to create the directory at once.  It's harmless
		 * to let them try, so just detect the problem and move on.
		 */
		if (mkdir(iop->ofn, 0755) < 0 && errno != EEXIST) {
			fprintf(stderr,
				"Destination dir %s can't be made: %d/%s\n",
				iop->ofn, errno, strerror(errno));
			return 1;
		}
	}

	if (output_name)
		snprintf(iop->ofn + len, sizeof(iop->ofn), "%s.blktrace.%d",
			 output_name, cpu);
	else
		snprintf(iop->ofn + len, sizeof(iop->ofn), "%s.blktrace.%d",
			 iop->dpp->buts_name, cpu);

	return 0;
}

static int set_vbuf(struct io_info *iop, int mode, size_t size)
{
	iop->obuf = malloc(size);
	if (setvbuf(iop->ofp, iop->obuf, mode, size) < 0) {
		fprintf(stderr, "setvbuf(%s, %d) failed: %d/%s\n",
			iop->dpp->path, (int)size, errno,
			strerror(errno));
		free(iop->obuf);
		return 1;
	}

	return 0;
}

static int iop_open(struct io_info *iop, int cpu)
{
	iop->ofd = -1;
	if (fill_ofname(iop, cpu))
		return 1;

	iop->ofp = my_fopen(iop->ofn, "w+");
	if (iop->ofp == NULL) {
		fprintf(stderr, "Open output file %s failed: %d/%s\n",
			iop->ofn, errno, strerror(errno));
		return 1;
	}

	if (set_vbuf(iop, _IOLBF, FILE_VBUF_SIZE)) {
		fprintf(stderr, "set_vbuf for file %s failed: %d/%s\n",
			iop->ofn, errno, strerror(errno));
		fclose(iop->ofp);
		return 1;
	}

	iop->ofd = fileno(iop->ofp);
	return 0;
}

static void close_iop(struct io_info *iop)
{
	struct mmap_info *mip = &iop->mmap_info;

	if (mip->fs_buf)
		munmap(mip->fs_buf, mip->fs_buf_len);

	if (ftruncate(fileno(iop->ofp), mip->fs_size) < 0) {
		fprintf(stderr,
			"Ignoring err: ftruncate(%s): %d/%s\n",
			iop->ofn, errno, strerror(errno));
	}

	if (iop->ofp)
		fclose(iop->ofp);
	if (iop->obuf)
		free(iop->obuf);
}

static void close_ios(struct tracer *tp)
{
	while (tp->nios > 0) {
		struct io_info *iop = &tp->ios[--tp->nios];

		iop->dpp->drops = 0;//get_drops(iop->dpp);
		if (iop->ifd >= 0)
			close(iop->ifd);

		if (iop->ofp)
			close_iop(iop);
		else if (iop->ofd >= 0) {
			//struct devpath *dpp = iop->dpp;

			//net_send_close(iop->ofd, dpp->buts_name, dpp->drops);
			//net_close_connection(&iop->ofd);
		}
	}

	free(tp->ios);
	free(tp->pfds);
}

static int open_ios(struct tracer *tp)
{
	struct pollfd *pfd;
	struct io_info *iop;
	struct list_head *p;

	tp->ios = calloc(ndevs, sizeof(struct io_info));
	memset(tp->ios, 0, ndevs * sizeof(struct io_info));

	tp->pfds = calloc(ndevs, sizeof(struct pollfd));
	memset(tp->pfds, 0, ndevs * sizeof(struct pollfd));

	tp->nios = 0;
	iop = tp->ios;
	pfd = tp->pfds;
	__list_for_each(p, &devpaths) {
		struct devpath *dpp = list_entry(p, struct devpath, head);

		iop->dpp = dpp;
		iop->ofd = -1;
		snprintf(iop->ifn, sizeof(iop->ifn), "%s/block/%s/trace%d",
			debugfs_path, dpp->buts_name, tp->cpu);

		iop->ifd = my_open(iop->ifn, O_RDONLY | O_NONBLOCK);
		if (iop->ifd < 0) {
			#if 0 //ignore the error which may caused by cpu hotplug
			fprintf(stderr, "For cpu %d failed open %s: %d/%s\n",
				tp->cpu, iop->ifn, errno, strerror(errno));
			return 1;
			#endif
		}

		init_mmap_info(&iop->mmap_info);

		pfd->fd = iop->ifd;
		pfd->events = POLLIN;

		if (iop_open(iop, tp->cpu))
			goto err;

		pfd++;
		iop++;
		tp->nios++;
	}

	return 0;

err:
	close(iop->ifd);	/* tp->nios _not_ bumped */
	close_ios(tp);
	return 1;
}

static int handle_pfds_file(struct tracer *tp, int nevs, int force_read)
{
	struct mmap_info *mip;
	int i, ret, nentries = 0;
	struct pollfd *pfd = tp->pfds;
	struct io_info *iop = tp->ios;

	for (i = 0; nevs > 0 && i < ndevs; i++, pfd++, iop++) {
		if (pfd->revents & POLLIN || force_read) {
			mip = &iop->mmap_info;

			ret = setup_mmap(iop->ofd, buf_size, mip, tp);
			if (ret < 0) {
				pfd->events = 0;
				break;
			}

			ret = read(iop->ifd, mip->fs_buf + mip->fs_off,
				   buf_size);
			if (ret > 0) {
				mip->fs_size += ret;
				mip->fs_off += ret;
				nentries++;
			} else if (ret == 0) {
				/*
				 * Short reads after we're done stop us
				 * from trying reads.
				 */
				if (tp->is_done)
					clear_events(pfd);
			} else {
				read_err(tp->cpu, iop->ifn);
				if (errno != EAGAIN || tp->is_done)
					clear_events(pfd);
			}
			nevs--;
		}
	}

	return nentries;
}

static int alloc_tracer(int cpu)
{
	struct tracer *tp;

	tp = malloc(sizeof(*tp));
	memset(tp, 0, sizeof(*tp));

	INIT_LIST_HEAD(&tp->head);
	tp->status = 0;
	tp->cpu = cpu;

	list_add_tail(&tp->head, &tracers);
	return 0;
}

static void alloc_tracers(void)
{
	int cpu;

	for (cpu = 0; cpu < ncpus; cpu++)
		if (alloc_tracer(cpu))
			break;
}

static void free_tracers(void)
{
	struct list_head *p, *q;

	list_for_each_safe(p, q, &tracers) {
		struct tracer *tp = list_entry(p, struct tracer, head);

		list_del(&tp->head);
		free(tp);
	}
}

static void exit_tracing(void)
{
	signal(SIGINT, SIG_IGN);
	signal(SIGHUP, SIG_IGN);
	signal(SIGTERM, SIG_IGN);
	signal(SIGALRM, SIG_IGN);
#if 0
	stop_tracers();
	free_tracers();
	rel_devpaths();
#endif
}

static void handle_sigint(__attribute__((__unused__)) int sig)
{
#if 0
	stop_tracers();
#endif
}

static void signal_register(void)
{
	signal(SIGINT, handle_sigint);
	signal(SIGHUP, handle_sigint);
	signal(SIGTERM, handle_sigint);
	signal(SIGALRM, handle_sigint);
	signal(SIGPIPE, SIG_IGN);
	atexit(exit_tracing);
}

static int handle_args(int argc, char *argv[])
{
	int c, i;
	struct statfs st;
	int act_mask_tmp = 0;

	while ((c = getopt_long(argc, argv, S_OPTS, l_opts, NULL)) >= 0) {
		switch (c) {
		case 'a':
			i = find_mask_map(optarg);
			if (i < 0) {
				fprintf(stderr, "Invalid action mask %s\n",
					optarg);
				return 1;
			}
			act_mask_tmp |= i;
			break;

		case 'A':
			if ((sscanf(optarg, "%x", &i) != 1) ||
							!valid_act_opt(i)) {
				fprintf(stderr,
					"Invalid set action mask %s/0x%x\n",
					optarg, i);
				return 1;
			}
			act_mask_tmp = i;
			break;

		case 'd':
			if (add_devpath(optarg) != 0)
				return 1;
			break;

		case 'I': {
			char dev_line[256];
			FILE *ifp = my_fopen(optarg, "r");

			if (!ifp) {
				fprintf(stderr,
					"Invalid file for devices %s\n",
					optarg);
				return 1;
			}

			while (fscanf(ifp, "%s\n", dev_line) == 1) {
				if (add_devpath(dev_line) != 0) {
					fclose(ifp);
					return 1;
				}
			}
			fclose(ifp);
			break;
		}

		case 'r':
			debugfs_path = optarg;
			break;
		case 'k':
			kill_running_trace = 1;
			break;
		case 'V':
		case 'v':
			printf("%s version %s\n", argv[0], blktrace_version);
			exit(0);
			/*NOTREACHED*/
		case 'b':
			buf_size = strtoul(optarg, NULL, 10);
			if (buf_size <= 0 || buf_size > 16*1024) {
				fprintf(stderr, "Invalid buffer size (%lu)\n",
					buf_size);
				return 1;
			}
			buf_size <<= 10;
			break;
		case 'n':
			buf_nr = strtoul(optarg, NULL, 10);
			if (buf_nr <= 0) {
				fprintf(stderr,
					"Invalid buffer nr (%lu)\n", buf_nr);
				return 1;
			}
			break;
		case 'D':
			output_dir = optarg;
			break;
		case 'c':
			capture_running_trace = 1;
			break;

		default:
			show_usage(argv[0]);
			exit(1);
			/*NOTREACHED*/
		}
	}

	while (optind < argc)
		if (add_devpath(argv[optind++]) != 0)
			return 1;

	if (ndevs == 0 ) {
		show_usage(argv[0]);
		return 1;
	}

	if (statfs(debugfs_path, &st) < 0) {
		fprintf(stderr, "Invalid debug path %s: %d/%s\n",
			debugfs_path, errno, strerror(errno));
		return 1;
	}

	if (st.f_type != (long)DEBUGFS_TYPE) {
		fprintf(stderr, "Debugfs is not mounted at %s\n", debugfs_path);
		return 1;
	}

	if (act_mask_tmp != 0)
		act_mask = act_mask_tmp;

	/*
	 * Set up for file PFD handler.
	 */
	handle_pfds = handle_pfds_file;

	return 0;
}

static void capture_tracers(void)
{
	struct list_head *p;

	__list_for_each(p, &tracers) {
		struct tracer *tp = list_entry(p, struct tracer, head);

		if(open_ios(tp)) {
			fprintf(stderr, "open ios error, failed to capture traces.\n");
			/* ios have already closed, just return*/
			return;
		}
		tp->is_done = 1;

		/*
		 * Trace is stopped, pull data until we get a short read
		 */
		while (handle_pfds(tp, ndevs, 1) > 0)
			;

		close_ios(tp);
	}
}

static void run_bt_capture(void)
{
	/* stop the tracing */
	stop_buts();

	alloc_tracers();
	capture_tracers();

	/* restart blktrace monitor */
	start_buts();

	free_tracers();
	return;
}

static void run_bt_precap(void)
{
	setup_buts();
	start_buts();
	return;
}

int main(int argc, char *argv[])
{
	int ret = 0;

	setlocale(LC_NUMERIC, "en_US");
	pagesize = getpagesize();
	ncpus = sysconf(_SC_NPROCESSORS_CONF);
	if (ncpus < 0) {
		fprintf(stderr, "sysconf(_SC_NPROCESSORS_CONF) failed %d/%s\n",
			errno, strerror(errno));
		ret = 1;
		goto out;
	} else if (handle_args(argc, argv)) {
		ret = 1;
		goto out;
	}

	if (kill_running_trace && capture_running_trace) {
		fprintf(stderr, "-c -k not supported simultaneous \n");
		ret = 1;
		goto out;
	}

	signal_register();

	if (capture_running_trace)
		run_bt_capture();
	else if (kill_running_trace) {
		struct devpath *dpp;
		struct list_head *p;

		__list_for_each(p, &devpaths) {
			dpp = list_entry(p, struct devpath, head);
			if (__stop_trace(dpp->fd)) {
				fprintf(stderr,
					"BLKTRACETEARDOWN %s failed: %d/%s\n",
					dpp->path, errno, strerror(errno));
			}
		}
	}
	else
		run_bt_precap();

out:
	rel_devpaths();
	return ret;
}

