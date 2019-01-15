#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/inotify.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "err.h"

#define LOG_DIR  0
#define LOG_FILE 1

#define POLL_INOTIFY 0
#define POLL_TIMER   1

#define DEFALT_PATH "/var/log/qmail/qmail-smtpd/current"

static int log_fd; /* smtp log file descriptor */
static int ino_fd; /* inotify file descriptor */
static int wd[2];  /* Watch descriptors for inotify */
static int run;

enum event_result {
	ND_NOTHING,
	ND_REOPEN_LOG_FILE,
};

struct statistics {
	int tcp_ok;
	int tcp_deny;
	int tcp_status_sum;
	int tcp_status_count;
};

static
void sigexit(int i) {
	run = 0;
}

static
enum nd_err
set_wd_for_log_directory(const int fd, const char * file_name) {
	char * file_name_copy;

	file_name_copy = strdup(file_name);
	if (file_name_copy == NULL)
		return ND_ALLOC;

	wd[LOG_DIR] = inotify_add_watch(fd, dirname(file_name_copy), IN_CREATE);

	if (wd[LOG_DIR] == -1)
		return ND_INOTIFY;

	free(file_name_copy);

	return ND_SUCCUESS;
}

static
enum nd_err
set_wd_for_log_file(const int fd, const char * file_name) {
	wd[LOG_FILE] = inotify_add_watch(fd, file_name, IN_MODIFY);

	if (wd[LOG_FILE] == -1)
		return ND_INOTIFY;

	return ND_SUCCUESS;
}

static
enum nd_err
reopen_log_file(const char * file_name) {
	inotify_rm_watch(ino_fd, wd[LOG_FILE]);
	close(log_fd);

	wd[LOG_FILE] = inotify_add_watch(ino_fd, file_name, IN_MODIFY);

	if (wd[LOG_FILE] == -1)
		return ND_INOTIFY;

	fprintf(stderr, "D: reopening file\n");
	log_fd = open(file_name, O_RDONLY);
	if (log_fd == -1)
		return ND_FILE;

	return ND_SUCCUESS;
}

static
enum nd_err
init_inotifier(const char * file_name) {
	enum nd_err ret;

	ino_fd = inotify_init1(IN_CLOEXEC | IN_NONBLOCK);
	if (ino_fd == -1) {
		fprintf(stderr, "E: Cannot init inotify\n");
		return ND_INOTIFY;
	}

	ret = set_wd_for_log_directory(ino_fd, file_name);
	if (ret != ND_SUCCUESS) {
		fprintf(stderr, "E: Cannot watch log dir: %s\n", nd_err_to_str(ret));
		return ret;
	}

	ret = set_wd_for_log_file(ino_fd, file_name);
	if (ret != ND_SUCCUESS) {
		fprintf(stderr, "E: Cannot watch log file: %s\n", nd_err_to_str(ret));
		return ret;
	}

	return ND_SUCCUESS;
}

static
enum event_result
handle_inot_event(const struct inotify_event * event) {
	if (event->wd == wd[LOG_DIR]) {
		fprintf(stderr, "D: logdir event\n");
		if (event->len && !strcmp(event->name, "current")) {
			fprintf(stderr, "D: there is new 'current'\n");
			return ND_REOPEN_LOG_FILE;
		}
	} else if (event->wd == wd[LOG_FILE]) {
		fprintf(stderr, "D: logfile evnet\n");
	}

	return ND_NOTHING;
}

static
enum event_result
handle_inot_events() {
	enum event_result ret = ND_NOTHING;
	const struct inotify_event * event;
	char buf[BUFSIZ];
	ssize_t len;
	char * ptr;

	for (;;) {
		len = read(ino_fd, buf, sizeof buf);
		if (len == -1 && errno != EAGAIN) {
			perror("E: cannot read inofd");
			exit(1);
		}

		if (len <= 0)
			break;

		for (ptr = buf; ptr < buf + len; ptr += sizeof * event + event->len) {
			event = (const struct inotify_event *)ptr;
			if (handle_inot_event(event) == ND_REOPEN_LOG_FILE)
				ret = ND_REOPEN_LOG_FILE;
		}
	}

	return ret;
}

static
void
handle_timer(const int fd) {
	uint64_t expirations;
	ssize_t ret;

	while ((ret = read(fd, &expirations, sizeof expirations)) > 0) {
		fprintf(stderr, "D: time: %lu\n", expirations);
	}
}

static
void
process_log_data(const int fd, struct statistics * data) {
	char buf[BUFSIZ];
	ssize_t ret;
	char * ptr;
	int val;

	while ((ret = read(fd, buf, sizeof buf)) > 0) {
		fprintf(stderr, "D: data len %ld\n", ret);

		if (strstr(buf, "tcpserver: ok")) {
			data->tcp_ok++;
		}
		if (strstr(buf, "tcpserver: deny")) {
			data->tcp_deny++;
		}
		if ((ptr = strstr(buf, "tcpserver: status: "))) {
			val = strtoul(ptr + sizeof "tcpserver: status: " - 1, 0, 0);
			data->tcp_status_sum += val;
			data->tcp_status_count++;
			fprintf(stderr, "v: %d s: %d\n", val, data->tcp_status_sum);
		}
	}
}

static
void
free_data(struct statistics * data) {
	memset(data, 0, sizeof * data);
}

static
void
print_header() {
	puts("CHART qmail.smtpd '' 'Qmail SMTPD' '# smtpd connections'");
	puts("DIMENSION tcp_ok 'TCP OK' absolute 1 1");
	puts("DIMENSION tcp_deny 'TCP Deny' absolute 1 1");
	puts("DIMENSION tcp_status_average 'status average' absolute 1 100");
	fflush(stdout);
}

static
void
print_data(const struct statistics * data) {
	puts("BEGIN qmail.smtpd");
	printf("SET tcp_ok %d\n", data->tcp_ok);
	printf("SET tcp_deny %d\n", -data->tcp_deny);
	printf("SET tcp_status_average %d\n",
		data->tcp_status_count ? data->tcp_status_sum * 100 / data->tcp_status_count : 0);
	puts("END");
	fflush(stdout);
}

int
main(int argc, char * argv[]) {
	const char * file_name = DEFALT_PATH;
	enum event_result event_result;
	struct itimerspec timer_value;
	struct statistics data;
	struct pollfd pfd[2];
	const char * argv0;
	enum nd_err ret;
	int update = 1;
	int nfd;

	argv0 = *argv; argv++; argc--;

	if (argc >= 1) {
		update = atoi(*argv);
		argv++; argc--;
	} else {
		fprintf(stderr, "Usage: %s <update> [path]\n", argv0);
	}

	if (argc) {
		file_name = *argv;
		argv++; argc--;
	}

	signal(SIGTERM, sigexit);
	signal(SIGQUIT, sigexit);

	ret = init_inotifier(file_name);
	log_fd = open(file_name, O_RDONLY);
	if (log_fd == -1) {
		perror("E: Cannot open file");
		exit(1);
	}
	lseek(log_fd, 0, SEEK_END);

	pfd[POLL_INOTIFY].fd = ino_fd;
	pfd[POLL_INOTIFY].events = POLLIN;

	pfd[POLL_TIMER].fd = timerfd_create(CLOCK_REALTIME, TFD_CLOEXEC | TFD_NONBLOCK);
	pfd[POLL_TIMER].events = POLLIN;

	if (pfd[POLL_TIMER].fd == -1) {
		perror("E: Cannot create timer");
		exit(1);
	}

	memset(&timer_value, 0, sizeof timer_value);
	timer_value.it_interval.tv_sec = update;
	timer_value.it_value.tv_sec = update;
	ret = timerfd_settime(pfd[POLL_TIMER].fd, 0, &timer_value, NULL);
	if (ret == -1) {
		perror("E: Cannot set timer");
		exit(1);
	}

	free_data(&data);
	print_header();

	for (run = 1; run ;) {
		nfd = poll(pfd, 2, -1);
		if (nfd > 0) {
			if (pfd[POLL_INOTIFY].revents & POLLIN) {
				event_result = handle_inot_events();
				process_log_data(log_fd, &data);
				if (event_result == ND_REOPEN_LOG_FILE) {
					reopen_log_file(file_name);
				}
			}
			if (pfd[POLL_TIMER].revents & POLLIN) {
				handle_timer(pfd[POLL_TIMER].fd);
				print_data(&data);
				free_data(&data);
			}
		} else if (nfd == 0) {
			fprintf(stderr, "D: timeout\n");
			continue;
		} else {
			fprintf(stderr, "E: poll error\n");
			break;
		}

		fprintf(stderr, "D: loop\n");
	}

	fputs("D: Exiting\n", stderr);

	return ret;
}
