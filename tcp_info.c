#define _GNU_SOURCE
#include <error.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/tcp.h>

extern int fd; /* nntcp's network socket. */
extern long nbytes;
static struct timeval prev_time;
static long prev_nbytes;

static void init_tcp_info(void) __attribute__((constructor));

static void write_tcp_info(int out_fd, int sock_fd)
{
  struct tcp_info info;
  socklen_t info_len = sizeof(info);

  if (getsockopt(sock_fd, SOL_TCP, TCP_INFO, &info, &info_len) < 0)
    return;

  dprintf(out_fd,
#define X(mem) "%-20s %10u\n"
#include "tcp_info.x"
#undef X
#define X(mem) , #mem, info.mem
#include "tcp_info.x"
#undef X
          );
}

static void
timeval_sub(struct timeval *r, struct timeval *x, struct timeval *y)
{
  /* Perform the carry for the later subtraction by updating y. */
  if (x->tv_usec < y->tv_usec) {
    int nsec = (y->tv_usec - x->tv_usec) / 1000000 + 1;
    y->tv_usec -= 1000000 * nsec;
    y->tv_sec += nsec;
  }
  if (x->tv_usec - y->tv_usec > 1000000) {
    int nsec = (x->tv_usec - y->tv_usec) / 1000000;
    y->tv_usec += 1000000 * nsec;
    y->tv_sec -= nsec;
  }

  /* Compute the time remaining to wait.
     tv_usec is certainly positive. */
  r->tv_sec = x->tv_sec - y->tv_sec;
  r->tv_usec = x->tv_usec - y->tv_usec;
}

static void write_transfer_rate(int out_fd)
{
  struct timeval now, diff;
  double db, dt;

  gettimeofday(&now, NULL);
  timeval_sub(&diff, &now, &prev_time);

  db = nbytes - prev_nbytes;
  dt = diff.tv_sec + ((double) diff.tv_usec) / 1000000.0;

  /* transfer_rate is since receipt of last SIGUSR2 or start. */
  dprintf(out_fd, "transfer_rate %.4f KB/s\n",
	  db / 1024.0 / dt);

  prev_time = now;
  prev_nbytes = nbytes;
}

static void signal_handler(int signo)
{
  if (signo == SIGUSR1)
    write_tcp_info(2, fd);
  else
    write_transfer_rate(2);
}

static void init_tcp_info(void)
{
  gettimeofday(&prev_time, NULL);

  struct sigaction action = {
    .sa_handler = &signal_handler,
    .sa_flags = SA_RESTART,
  };

  sigaction(SIGUSR1, &action, NULL);
  sigaction(SIGUSR2, &action, NULL);
}
