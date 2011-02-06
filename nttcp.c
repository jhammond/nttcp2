/*
 *	T T C P . C
 *
 * Test TCP connection.  Makes a connection on port 5001
 * and transfers fabricated buffers or data copied from stdin.
 *
 * Usable on 4.2, 4.3, and 4.1a systems by defining one of
 * BSD42 BSD43 (BSD41a)
 * Machines using System V with BSD sockets should define SYSV.
 *
 * Modified for operation under 4.2BSD, 18 Dec 84
 *      T.C. Slattery, USNA
 * Minor improvements, Mike Muuss and Terry Slattery, 16-Oct-85.
 *
 * Distribution Status -
 *      Public Domain.  Distribution Unlimited.
 */
/* Modernized C usage, Linuxized, slightly cleaned-up, John Hammond, Feb 6 2011. */
#define BSD43
/* #define BSD42 */
/* #define BSD41a */
#if defined(sgi)
#define SYSV
#endif
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <sys/time.h>		/* struct timeval */
#include <sys/resource.h>

static int Nwrite(int fd, char *buf, int count);
static int Nread(int fd, char *buf, int count);
static int mread(int fd, char *bufp, unsigned n);
static int delay(int us);
static void prusage(struct rusage *r0, struct rusage *r1, struct timeval *e, struct timeval *b, char *outp);
static void tvadd(struct timeval *tsum, struct timeval *t0, struct timeval *t1);
static void tvsub(struct timeval *tsum, struct timeval *t0, struct timeval *t1);
static void psecs(long l, char *cp);
static void prep_timer(void);
static void read_timer(char *str, int len);

struct sockaddr_in sinme;
struct sockaddr_in sinhim;

int fd;				/* fd of network socket */
size_t buf_size = 64 * 1024;		/* length of buffer */
char *buf;			/* ptr to dynamic buffer */
int nbuf = 2 * 1024;		/* number of buffers to send in sinkmode */

/*  nick code  */
int sendwin, rcvwin, maxseg;
socklen_t optlen;
/*  end nick code  */

int udp = 0;			/* 0 = tcp, !0 = udp */
int options = 0;		/* socket options */
int one = 1;			/* for 4.3 BSD style setsockopt() */
short port = 5001;		/* TCP port number */
char *host;			/* ptr to name of host */
int trans;			/* 0=receive, !0=transmit mode */
int sinkmode = 1;		/* 0=normal I/O, !0=sink/source mode */
int verbose = 0;		/* 0=print basic info, 1=print cpu rate, proc
				 * resource usage. */
int nodelay = 0;		/* set TCP_NODELAY socket option */
int window = 0;			/* 0=use default   1=set to specified size */

struct hostent *addr;

char stats[128];
long nbytes;			/* bytes on net */
int numCalls = 0;		/* # of NRead/NWrite calls. */
int b_flag = 0;			/* use mread() */

double cput, realt;		/* user, real time (seconds) */

static void err(char *s)
{
	fprintf(stderr, "ttcp%s: ", trans ? "-t" : "-r");
	perror(s);
	fprintf(stderr, "errno=%d\n", errno);
	exit(1);
}

static void mes(char *s)
{
	fprintf(stderr, "ttcp%s: %s\n", trans ? "-t" : "-r", s);
}

static void pattern(char *cp, int cnt)
{
	char c;
	c = 0;
	while (cnt-- > 0) {
		while (!isprint((c & 0x7F)))
			c++;
		*cp++ = (c++ & 0x7F);
	}
}

static void sigpipe(int signo)
{
}

int main(int argc, char *argv[])
{
	unsigned long addr_tmp;
/*  nick code  */
	optlen = sizeof(maxseg);
	sendwin = 32 * 1024;
	rcvwin = 32 * 1024;
/* end of nick code  */

	if (argc < 2)
		goto usage;

	argv++;
	argc--;
	while (argc > 0 && argv[0][0] == '-') {
		switch (argv[0][1]) {

		case 'B':
			b_flag = 1;
			break;
		case 't':
			trans = 1;
			break;
		case 'r':
			trans = 0;
			break;
		case 'd':
			options |= SO_DEBUG;
			break;
		case 'D':
			nodelay = 1;
			break;
		case 'n':
			nbuf = atoi(&argv[0][2]);
			break;
		case 'l':
			buf_size = atoi(&argv[0][2]);
			break;
		case 'w':
			window = 1;
			sendwin = 1024 * atoi(&argv[0][2]);
			rcvwin = sendwin;
			break;
		case 's':
			sinkmode = 0;	/* sink/source data */
			break;
		case 'p':
			port = atoi(&argv[0][2]);
			break;
		case 'u':
			udp = 1;
			buf_size = 8192;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			goto usage;
		}
		argv++;
		argc--;
	}
	if (trans) {
		/* xmitr */
		if (argc != 1)
			goto usage;
		memset(&sinhim, 0, sizeof(sinhim));
		host = argv[0];
		if (atoi(host) > 0) {
			/* Numeric */
			sinhim.sin_family = AF_INET;
#ifdef cray
			addr_tmp = inet_addr(host);
			sinhim.sin_addr = addr_tmp;
#else
			sinhim.sin_addr.s_addr = inet_addr(host);
#endif
		} else {
			if ((addr = gethostbyname(host)) == NULL)
				err("bad hostname");
			sinhim.sin_family = addr->h_addrtype;
			bcopy(addr->h_addr, (char *)&addr_tmp, addr->h_length);
#ifdef cray
			sinhim.sin_addr = addr_tmp;
#else
			sinhim.sin_addr.s_addr = addr_tmp;
#endif
		}
		sinhim.sin_port = htons(port);
		sinme.sin_port = 0;	/* free choice */
	} else {
		/* rcvr */
		sinme.sin_port = htons(port);
	}

	if (udp && buf_size < 5) {
		buf_size = 5;	/* send more than the sentinel size */
	}

	buf = malloc(buf_size);
	if (buf == NULL)
		err("malloc");

	if (trans) {
		fprintf(stdout,
			"ttcp-t: buf_size=%zu, nbuf=%d, port=%d %s  -> %s\n",
			buf_size, nbuf, port, udp ? "udp" : "tcp", argv[0]);
	} else {
		fprintf(stdout, "ttcp-r: buf_size=%zu, nbuf=%d, port=%d %s\n",
			buf_size, nbuf, port, udp ? "udp" : "tcp");
	}

	if ((fd = socket(AF_INET, udp ? SOCK_DGRAM : SOCK_STREAM, 0)) < 0)
		err("socket");

	mes("socket");

	if (bind(fd, (struct sockaddr*) &sinme, sizeof(sinme)) < 0)
		err("bind");

	if (!udp) {
		signal(SIGPIPE, sigpipe);
		if (trans) {
			/* We are the client if transmitting */
			if (window) {
				if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
					       &sendwin, sizeof(sendwin)) < 0)
					err("setsockopt");
				if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
					       &rcvwin, sizeof(rcvwin)) < 0)
					err("setsockopt");
			}
			if (nodelay) {
				struct protoent *p;
				p = getprotobyname("tcp");
				if (p && setsockopt(fd, p->p_proto, TCP_NODELAY,
						    &one, sizeof(one)) < 0)
					err("setsockopt: nodelay");
				mes("nodelay");
			}
			if (connect(fd, (struct sockaddr*) &sinhim, sizeof(sinhim)) < 0)
				err("connect");
			mes("connect");
		} else {
			/* otherwise, we are the server and
			 * should listen for the connections
			 */
			listen(fd, 5);	/* allow a queue of 5 */
			if (options) {
#ifdef BSD42
				if (setsockopt(fd, SOL_SOCKET, options, 0, 0) < 0)
#else				/* BSD43 */
				if (setsockopt(fd, SOL_SOCKET, options, &one, sizeof(one)) < 0)
#endif
					err("setsockopt");
			}

			struct sockaddr_in peer;
			socklen_t peer_len = sizeof(peer);
			if ((fd = accept(fd, (struct sockaddr*) &peer, &peer_len)) < 0)
				err("accept");

			if (window) {
				if (setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
				               &sendwin, sizeof(sendwin)) < 0)
					err("setsockopt");

				if (setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
				               &rcvwin, sizeof(rcvwin)) < 0)
					err("setsockopt");
			}
			fprintf(stderr, "ttcp-r: accept from %s\n", inet_ntoa(peer.sin_addr));
		}
	}

	if (getsockopt(fd, SOL_SOCKET, SO_SNDBUF, &sendwin, &optlen) < 0)
		printf("cannot get send window size: %m\n");
	else
		printf("send window size = %d\n", sendwin);

	if (getsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rcvwin, &optlen) < 0)
		printf("cannot get receive window size: %m\n");
	else
		printf("receive window size = %d\n", rcvwin);

	prep_timer();
	errno = 0;
	if (sinkmode) {
		int cnt;
		if (trans) {
			pattern(buf, buf_size);
			if (udp)
				(void)Nwrite(fd, buf, 4);	/* rcvr start */
			while (nbuf-- && Nwrite(fd, buf, buf_size) == buf_size)
				nbytes += buf_size;
			if (udp)
				(void)Nwrite(fd, buf, 4);	/* rcvr end */
		} else {
			if (udp) {
				while ((cnt = Nread(fd, buf, buf_size)) > 0) {
					static int going = 0;
					if (cnt <= 4) {
						if (going)
							break;	/* "EOF" */
						going = 1;
						prep_timer();
					} else {
						nbytes += cnt;
					}
				}
			} else {
				while ((cnt = Nread(fd, buf, buf_size)) > 0) {
					nbytes += cnt;
				}
			}
		}
	} else {
		int cnt;
		if (trans) {
			while ((cnt = read(0, buf, buf_size)) > 0 &&
			       Nwrite(fd, buf, cnt) == cnt)
				nbytes += cnt;
		} else {
			while ((cnt = Nread(fd, buf, buf_size)) > 0 &&
			       write(1, buf, cnt) == cnt)
				nbytes += cnt;
		}
	}
	if (errno)
		err("IO");

	read_timer(stats, sizeof(stats));

	if (udp && trans) {
		(void)Nwrite(fd, buf, 4);	/* rcvr end */
		(void)Nwrite(fd, buf, 4);	/* rcvr end */
		(void)Nwrite(fd, buf, 4);	/* rcvr end */
		(void)Nwrite(fd, buf, 4);	/* rcvr end */
	}
	if (cput <= 0.0)
		cput = 0.001;
	if (realt <= 0.0)
		realt = 0.001;
	fprintf(stdout,
		"ttcp%s: %ld bytes in %.2f real seconds = %.2f KB/sec = %.4f Mb/s\n",
		trans ? "-t" : "-r",
		nbytes, realt, ((double)nbytes) / realt / 1024,
		((double)nbytes) / realt / 128000);
	if (verbose) {
		fprintf(stdout,
			"ttcp%s: %ld bytes in %.2f CPU seconds = %.2f KB/cpu sec\n",
			trans ? "-t" : "-r",
			nbytes, cput, ((double)nbytes) / cput / 1024);
	}
	fprintf(stdout,
		"ttcp%s: %d I/O calls, msec/call = %.2f, calls/sec = %.2f\n",
		trans ? "-t" : "-r",
		numCalls,
		1024.0 * realt / ((double)numCalls),
		((double)numCalls) / realt);

	fprintf(stdout, "ttcp%s: %s\n", trans ? "-t" : "-r", stats);
	exit(0);

 usage:
	fprintf(stderr,
		"Usage: ttcp -t [-options] host [ <in ]\n"
		"-l##	length of bufs written to network (default 8192)\n"
		"-s	don't source a pattern to network, use stdin\n"
		"-n##	number of source bufs written to network (default 2048)\n"
		"-p##	port number to send to (default 5001)\n"
		"-u	use UDP instead of TCP\n"
		"-D	don't buffer TCP writes (sets TCP_NODELAY socket option)\n"
		"-L	set SO_LONGER socket option\n"
		"Usage: ttcp -r [-options >out]\n"
		"-l##	length of network read buf (default 8192)\n"
		"-s	don't sink (discard): prints all data from network to stdout\n"
		"-p##	port number to listen at (default 5001)\n"
		"-B	Only output full blocks, as specified in -l## (for TAR)\n"
		"-u	use UDP instead of TCP\n");
	exit(1);
}

static struct timeval time0;	/* Time at which timing started */
static struct rusage ru0;	/* Resource utilization at the start */

/*
 *			P R E P _ T I M E R
 */
static void prep_timer(void)
{
	gettimeofday(&time0, (struct timezone *) NULL);
	getrusage(RUSAGE_SELF, &ru0);
}

/*
 *			R E A D _ T I M E R
 * 
 */
static void read_timer(char *str, int len)
{
	struct timeval timedol;
	struct rusage ru1;
	struct timeval td;
	struct timeval tend, tstart;
	char line[132];

	getrusage(RUSAGE_SELF, &ru1);
	gettimeofday(&timedol, (struct timezone *) NULL);
	prusage(&ru0, &ru1, &timedol, &time0, line);
	strncpy(str, line, len);

	/* Get real time */
	tvsub(&td, &timedol, &time0);
	realt = td.tv_sec + ((double)td.tv_usec) / 1000000;

	/* Get CPU time (user+sys) */
	tvadd(&tend, &ru1.ru_utime, &ru1.ru_stime);
	tvadd(&tstart, &ru0.ru_utime, &ru0.ru_stime);
	tvsub(&td, &tend, &tstart);
	cput = td.tv_sec + ((double)td.tv_usec) / 1000000;
	if (cput < 0.00001)
		cput = 0.00001;
}

static void prusage(struct rusage *r0, struct rusage *r1, struct timeval *e, struct timeval *b, char *outp)
{
	struct timeval tdiff;
	time_t t;
	char *cp;
	int i;
	int ms;

	t = (r1->ru_utime.tv_sec - r0->ru_utime.tv_sec) * 100 +
	    (r1->ru_utime.tv_usec - r0->ru_utime.tv_usec) / 10000 +
	    (r1->ru_stime.tv_sec - r0->ru_stime.tv_sec) * 100 +
	    (r1->ru_stime.tv_usec - r0->ru_stime.tv_usec) / 10000;
	ms = (e->tv_sec - b->tv_sec) * 100 + (e->tv_usec - b->tv_usec) / 10000;

#define END(x)	{while(*x) x++;}
#if defined(SYSV)
	cp = "%Uuser %Ssys %Ereal %P";
#else
	cp = "%Uuser %Ssys %Ereal %P %Xi+%Dd %Mmaxrss %F+%Rpf %Ccsw";
#endif
	for (; *cp; cp++) {
		if (*cp != '%')
			*outp++ = *cp;
		else if (cp[1])
			switch (*++cp) {

			case 'U':
				tvsub(&tdiff, &r1->ru_utime, &r0->ru_utime);
				sprintf(outp, "%ld.%01ld", (long) tdiff.tv_sec,
					(long) tdiff.tv_usec / 100000);
				END(outp);
				break;

			case 'S':
				tvsub(&tdiff, &r1->ru_stime, &r0->ru_stime);
				sprintf(outp, "%ld.%01ld", (long) tdiff.tv_sec,
					(long) tdiff.tv_usec / 100000);
				END(outp);
				break;

			case 'E':
				psecs(ms / 100, outp);
				END(outp);
				break;

			case 'P':
				sprintf(outp, "%d%%",
					(int)(t * 100 / ((ms ? ms : 1))));
				END(outp);
				break;

#if !defined(SYSV)
			case 'W':
				i = r1->ru_nswap - r0->ru_nswap;
				sprintf(outp, "%d", i);
				END(outp);
				break;

			case 'X':
				sprintf(outp, "%ld",
					t ==
					0 ? 0 : (r1->ru_ixrss -
						 r0->ru_ixrss) / t);
				END(outp);
				break;

			case 'D':
				sprintf(outp, "%ld", t == 0 ? 0 :
					(r1->ru_idrss + r1->ru_isrss -
					 (r0->ru_idrss + r0->ru_isrss)) / t);
				END(outp);
				break;

			case 'K':
				sprintf(outp, "%ld", t == 0 ? 0 :
					((r1->ru_ixrss + r1->ru_isrss +
					  r1->ru_idrss) - (r0->ru_ixrss +
							   r0->ru_idrss +
							   r0->ru_isrss)) / t);
				END(outp);
				break;

			case 'M':
				sprintf(outp, "%ld", r1->ru_maxrss / 2);
				END(outp);
				break;

			case 'F':
				sprintf(outp, "%ld",
					r1->ru_majflt - r0->ru_majflt);
				END(outp);
				break;

			case 'R':
				sprintf(outp, "%ld",
					r1->ru_minflt - r0->ru_minflt);
				END(outp);
				break;

			case 'I':
				sprintf(outp, "%ld",
					r1->ru_inblock - r0->ru_inblock);
				END(outp);
				break;

			case 'O':
				sprintf(outp, "%ld",
					r1->ru_oublock - r0->ru_oublock);
				END(outp);
				break;
			case 'C':
				sprintf(outp, "%ld+%ld",
					r1->ru_nvcsw - r0->ru_nvcsw,
					r1->ru_nivcsw - r0->ru_nivcsw);
				END(outp);
				break;
#endif
			}
	}
	*outp = '\0';
}

static void tvadd(struct timeval *tsum, struct timeval *t0, struct timeval *t1)
{

	tsum->tv_sec = t0->tv_sec + t1->tv_sec;
	tsum->tv_usec = t0->tv_usec + t1->tv_usec;
	if (tsum->tv_usec > 1000000)
		tsum->tv_sec++, tsum->tv_usec -= 1000000;
}

static void tvsub(struct timeval *tdiff, struct timeval *t0, struct timeval *t1)
{

	tdiff->tv_sec = t1->tv_sec - t0->tv_sec;
	tdiff->tv_usec = t1->tv_usec - t0->tv_usec;
	if (tdiff->tv_usec < 0)
		tdiff->tv_sec--, tdiff->tv_usec += 1000000;
}

static void psecs(long l, char *cp)
{
	int i;

	i = l / 3600;
	if (i) {
		sprintf(cp, "%d:", i);
		END(cp);
		i = l % 3600;
		sprintf(cp, "%d%d", (i / 60) / 10, (i / 60) % 10);
		END(cp);
	} else {
		i = l;
		sprintf(cp, "%d", i / 60);
		END(cp);
	}
	i %= 60;
	*cp++ = ':';
	sprintf(cp, "%d%d", i / 10, i % 10);
}

/*
 *			N R E A D
 */
static int Nread(int fd, char *buf, int count)
{
	struct sockaddr_in from;
	socklen_t len = sizeof(from);
	int cnt;
	if (udp) {
		cnt = recvfrom(fd, buf, count, 0, (struct sockaddr*) &from, &len);
		numCalls++;
	} else {
		if (b_flag)
			cnt = mread(fd, buf, count);	/* fill buf */
		else {
			cnt = read(fd, buf, count);
			numCalls++;
		}
	}
	return (cnt);
}

/*
 *			N W R I T E
 */
static int Nwrite(int fd, char *buf, int count)
{
	int cnt;

	errno = 0;
	if (udp) {
 again:
	  cnt = sendto(fd, buf, count, 0, (struct sockaddr*) &sinhim, sizeof(sinhim));
		numCalls++;
		if (cnt < 0 && errno == ENOBUFS) {
			delay(18000);
			errno = 0;
			goto again;
		}
	} else {
		int total = 0;
		while (total < count) {
			int rc = write(fd, buf + total, count - total);
			numCalls++;

			if (rc < 0) {
				if (errno == EINTR)
					continue;
				break;
			}

			total += rc;
		}
		return total;
	}

	return (cnt);
}

static int delay(int us)
{
	struct timeval tv;

	tv.tv_sec = 0;
	tv.tv_usec = us;
	select(1, NULL, NULL, NULL, &tv);
	return 1;
}

/*
 *			M R E A D
 *
 * This function performs the function of a read(II) but will
 * call read(II) multiple times in order to get the requested
 * number of characters.  This can be necessary because
 * network connections don't deliver data with the same
 * grouping as it is written with.  Written by Robert S. Miles, BRL.
 */
static int mread(int fd, char *bufp, unsigned n)
{
	unsigned count = 0;
	int nread;

 again:
	errno = 0;
	do {
		nread = read(fd, bufp, n - count);
		numCalls++;
		if (nread < 0) {
			if (errno == EINTR)
				goto again;
			perror("ttcp_mread");
			return -1;
		}
		if (nread == 0)
			return count;
		count += (unsigned) nread;
		bufp += nread;
	} while (count < n);

	return count;
}
