#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <signal.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>

#define LINE_BUF      1024
#define MAX_TOKENS    64
#define MONITOR_EXE   "./monitor_reports"
#define SCORER_EXE    "./scorer"

static pid_t g_hub_mon_pid = -1;

/* Reap any background children that have already exited (non-blocking). */
static void reap_zombies(void)
{
    int status;
    pid_t p;
    while ((p = waitpid(-1, &status, WNOHANG)) > 0) {
        if (p == g_hub_mon_pid) {
            fprintf(stdout, "[city_hub] hub_mon (PID %d) has exited.\n", (int)p);
            fflush(stdout);
            g_hub_mon_pid = -1;
        }
    }
}

/* hub_mon: creates a pipe, forks the monitor with stdout dup2'd to the pipe
   write end, then reads from the pipe and prints lines to its own stdout.
   When read returns 0 (monitor's stdout closed -> monitor ended), prints a
   specific message and exits. */
static void hub_mon_run(void)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        perror("[hub_mon] pipe");
        _exit(1);
    }

    pid_t mpid = fork();
    if (mpid < 0) {
        perror("[hub_mon] fork");
        _exit(1);
    }

    if (mpid == 0) {
        /* child: become the monitor */
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("[hub_mon child] dup2");
            _exit(1);
        }
        close(pipefd[1]);
        execl(MONITOR_EXE, "monitor_reports", (char *)NULL);
        perror("[hub_mon child] execl monitor_reports");
        _exit(1);
    }

    /* parent: hub_mon proper */
    close(pipefd[1]);

    char buf[LINE_BUF];
    size_t used = 0;
    int monitor_alive = 1;

    while (monitor_alive) {
        ssize_t got = read(pipefd[0], buf + used, sizeof buf - 1 - used);
        if (got < 0) {
            if (errno == EINTR) continue;
            perror("[hub_mon] read");
            break;
        }
        if (got == 0) {
            monitor_alive = 0;
            break;
        }
        used += (size_t)got;
        buf[used] = '\0';

        /* Print every complete line, then carry the tail. Lines are
           variable-length but always terminated by '\n'. */
        char *start = buf;
        char *nl;
        while ((nl = memchr(start, '\n', (size_t)((buf + used) - start))) != NULL) {
            *nl = '\0';
            fprintf(stdout, "[hub_mon] %s\n", start);
            fflush(stdout);
            start = nl + 1;
        }
        size_t tail = (size_t)((buf + used) - start);
        memmove(buf, start, tail);
        used = tail;

        /* If a single line exceeds the buffer, flush what we have. */
        if (used >= sizeof buf - 1) {
            buf[used] = '\0';
            fprintf(stdout, "[hub_mon] %s\n", buf);
            fflush(stdout);
            used = 0;
        }
    }

    /* Flush any unterminated trailing data. */
    if (used > 0) {
        buf[used] = '\0';
        fprintf(stdout, "[hub_mon] %s\n", buf);
        fflush(stdout);
    }

    /* Reap the monitor and report how it ended. */
    int status = 0;
    waitpid(mpid, &status, 0);

    char endmsg[256];
    int n;
    if (WIFEXITED(status)) {
        n = snprintf(endmsg, sizeof endmsg,
                     "[hub_mon] Monitor process (PID %d) has ended (exit code %d).\n",
                     (int)mpid, WEXITSTATUS(status));
    } else if (WIFSIGNALED(status)) {
        n = snprintf(endmsg, sizeof endmsg,
                     "[hub_mon] Monitor process (PID %d) was terminated by signal %d.\n",
                     (int)mpid, WTERMSIG(status));
    } else {
        n = snprintf(endmsg, sizeof endmsg,
                     "[hub_mon] Monitor process (PID %d) ended (raw status %d).\n",
                     (int)mpid, status);
    }
    write(STDOUT_FILENO, endmsg, (size_t)n);

    close(pipefd[0]);
    _exit(0);
}

static int cmd_start_monitor(void)
{
    if (g_hub_mon_pid > 0 && kill(g_hub_mon_pid, 0) == 0) {
        fprintf(stderr,
                "Error: hub_mon already running as PID %d. "
                "Stop it first (e.g. send SIGINT to the monitor).\n",
                (int)g_hub_mon_pid);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork hub_mon"); return -1; }
    if (pid == 0) {
        hub_mon_run();
        _exit(0);
    }
    g_hub_mon_pid = pid;
    printf("Started hub_mon as PID %d (monitor is its child).\n", (int)pid);
    return 0;
}

/* Spawn a scorer for one district: create a pipe, fork, in the child
   dup2 the write end to stdout and exec the scorer. Returns the child PID
   and writes the read end of the pipe to *read_fd. */
static pid_t spawn_scorer(const char *district, int *read_fd)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) { perror("pipe"); return -1; }

    pid_t pid = fork();
    if (pid < 0) {
        perror("fork scorer");
        close(pipefd[0]); close(pipefd[1]);
        return -1;
    }

    if (pid == 0) {
        close(pipefd[0]);
        if (dup2(pipefd[1], STDOUT_FILENO) < 0) {
            perror("dup2 scorer");
            _exit(1);
        }
        close(pipefd[1]);
        execl(SCORER_EXE, "scorer", district, (char *)NULL);
        perror("execl scorer");
        _exit(1);
    }

    close(pipefd[1]);
    *read_fd = pipefd[0];
    return pid;
}

static int cmd_calculate_scores(int n, char **districts)
{
    if (n < 1) {
        fputs("Usage: calculate_scores <district> [<district> ...]\n", stderr);
        return -1;
    }

    int   *fds  = calloc((size_t)n, sizeof(int));
    pid_t *pids = calloc((size_t)n, sizeof(pid_t));
    if (!fds || !pids) {
        perror("calloc");
        free(fds); free(pids);
        return -1;
    }

    for (int i = 0; i < n; i++) {
        struct stat st;
        if (stat(districts[i], &st) < 0 || !S_ISDIR(st.st_mode)) {
            fprintf(stderr, "Warning: district '%s' not found, skipping.\n",
                    districts[i]);
            fds[i]  = -1;
            pids[i] = -1;
            continue;
        }
        int fd;
        pid_t pid = spawn_scorer(districts[i], &fd);
        pids[i] = pid;
        fds[i]  = (pid > 0) ? fd : -1;
    }

    printf("\n===== Combined Workload Report =====\n");
    for (int i = 0; i < n; i++) {
        if (fds[i] < 0) {
            printf("\n--- District: %s --- (skipped)\n", districts[i]);
            continue;
        }
        printf("\n--- District: %s ---\n", districts[i]);
        char buf[1024];
        ssize_t r;
        while ((r = read(fds[i], buf, sizeof buf)) > 0)
            write(STDOUT_FILENO, buf, (size_t)r);
        close(fds[i]);

        int status;
        waitpid(pids[i], &status, 0);
        if (WIFEXITED(status) && WEXITSTATUS(status) != 0)
            printf("(scorer for '%s' exited with code %d)\n",
                   districts[i], WEXITSTATUS(status));
    }
    printf("====================================\n\n");

    free(fds);
    free(pids);
    return 0;
}

static int dispatch(char *line)
{
    /* tokenize on whitespace */
    char *tokens[MAX_TOKENS];
    int ntok = 0;
    char *p = line;
    while (*p && ntok < MAX_TOKENS - 1) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
        if (!*p) break;
        tokens[ntok++] = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r') p++;
        if (*p) *p++ = '\0';
    }
    tokens[ntok] = NULL;
    if (ntok == 0) return 0;

    if (strcmp(tokens[0], "start_monitor") == 0)
        return cmd_start_monitor();

    if (strcmp(tokens[0], "calculate_scores") == 0)
        return cmd_calculate_scores(ntok - 1, &tokens[1]);

    if (strcmp(tokens[0], "help") == 0) {
        printf("Commands:\n");
        printf("  start_monitor                       Spawn hub_mon, which forks the monitor\n");
        printf("  calculate_scores <d> [<d> ...]      Per-district inspector workload report\n");
        printf("  help                                Show this help\n");
        printf("  quit | exit                         Leave city_hub\n");
        return 0;
    }

    if (strcmp(tokens[0], "quit") == 0 || strcmp(tokens[0], "exit") == 0)
        return -2;

    fprintf(stderr, "Unknown command: %s (type 'help')\n", tokens[0]);
    return -1;
}

int main(void)
{
    /* Don't let writes to a closed pipe terminate the hub. */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGPIPE, &sa, NULL);

    char line[LINE_BUF];

    printf("city_hub interactive shell. Type 'help' for commands.\n");
    while (1) {
        reap_zombies();
        printf("city_hub> ");
        fflush(stdout);
        if (!fgets(line, sizeof line, stdin)) {
            printf("\n");
            break;
        }
        int r = dispatch(line);
        if (r == -2) break;
    }

    /* Best-effort cleanup: wait briefly for hub_mon if still around. */
    reap_zombies();
    return 0;
}
