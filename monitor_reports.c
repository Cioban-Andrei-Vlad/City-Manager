#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>

#define PID_FILE ".monitor_pid"

static volatile sig_atomic_t g_got_sigint = 0;

static void handle_sigusr1(int sig)
{
    (void)sig;
    const char msg[] = "[NOTIFY] New report added.\n";
    write(STDOUT_FILENO, msg, sizeof msg - 1);
}

static void handle_sigint(int sig)
{
    (void)sig;
    g_got_sigint = 1;
}

/* Returns 1 if .monitor_pid contains a PID of a still-running process and
   stores it in *out; otherwise returns 0 (file missing, unreadable, or stale). */
static int existing_monitor_pid(pid_t *out)
{
    int fd = open(PID_FILE, O_RDONLY);
    if (fd < 0) return 0;

    char buf[32];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;
    buf[n] = '\0';

    pid_t p = (pid_t)atoi(buf);
    if (p <= 0) return 0;

    if (kill(p, 0) < 0 && errno == ESRCH) return 0;

    *out = p;
    return 1;
}

int main(void)
{
    pid_t existing;
    if (existing_monitor_pid(&existing)) {
        char err[160];
        int n = snprintf(err, sizeof err,
                         "[ERROR] Another monitor is already running with PID %d. Exiting.\n",
                         (int)existing);
        write(STDOUT_FILENO, err, n);
        return 1;
    }

    pid_t my_pid = getpid();

    int fd = open(PID_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        const char msg[] = "[ERROR] Failed to open .monitor_pid for writing.\n";
        write(STDOUT_FILENO, msg, sizeof msg - 1);
        return 1;
    }
    char pbuf[32];
    int pn = snprintf(pbuf, sizeof pbuf, "%d\n", (int)my_pid);
    write(fd, pbuf, pn);
    close(fd);

    char start[160];
    int sn = snprintf(start, sizeof start,
                      "[INFO] Monitor started (PID %d). Waiting for signals...\n",
                      (int)my_pid);
    write(STDOUT_FILENO, start, sn);

    struct sigaction sa;

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handle_sigusr1;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    sigaction(SIGUSR1, &sa, NULL);

    memset(&sa, 0, sizeof sa);
    sa.sa_handler = handle_sigint;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);

    while (!g_got_sigint)
        pause();

    const char shut[] = "[SHUTDOWN] Monitor received SIGINT. Shutting down.\n";
    write(STDOUT_FILENO, shut, sizeof shut - 1);
    unlink(PID_FILE);
    return 0;
}