#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#define INSPECTOR_LEN    64
#define CATEGORY_LEN     32
#define DESCRIPTION_LEN  128

typedef struct {
    int    id;
    char   inspector[INSPECTOR_LEN];
    double latitude;
    double longitude;
    char   category[CATEGORY_LEN];
    int    severity;
    time_t timestamp;
    char   description[DESCRIPTION_LEN];
} Report;

#define ROLE_MANAGER   0
#define ROLE_INSPECTOR 1

static int  g_role;
static char g_role_str[32];
static char g_user[64];

static void mode_to_str(mode_t m, char out[10])
{
    out[0] = (m & S_IRUSR) ? 'r' : '-';
    out[1] = (m & S_IWUSR) ? 'w' : '-';
    out[2] = (m & S_IXUSR) ? 'x' : '-';
    out[3] = (m & S_IRGRP) ? 'r' : '-';
    out[4] = (m & S_IWGRP) ? 'w' : '-';
    out[5] = (m & S_IXGRP) ? 'x' : '-';
    out[6] = (m & S_IROTH) ? 'r' : '-';
    out[7] = (m & S_IWOTH) ? 'w' : '-';
    out[8] = (m & S_IXOTH) ? 'x' : '-';
    out[9] = '\0';
}

static void log_action(const char *district, const char *action)
{
    char path[512];
    snprintf(path, sizeof path, "%s/logged_district", district);

    int fd = open(path, O_WRONLY | O_APPEND | O_CREAT, 0644);
    if (fd < 0) { perror("log_action: open"); return; }

    time_t now = time(NULL);
    char tbuf[32];
    strftime(tbuf, sizeof tbuf, "%Y-%m-%d %H:%M:%S", localtime(&now));

    char buf[512];
    int n = snprintf(buf, sizeof buf, "%s\t%s\t%s\t%s\n",
                     tbuf, g_user, g_role_str, action);
    write(fd, buf, n);
    close(fd);
}

/* Reads .monitor_pid, sends SIGUSR1 to the monitor, and logs the result. */
static void notify_monitor(const char *district)
{
    int fd = open(".monitor_pid", O_RDONLY);
    if (fd < 0) {
        log_action(district, "monitor not notified: no .monitor_pid file");
        return;
    }

    char buf[32];
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) {
        log_action(district, "monitor not notified: could not read PID");
        return;
    }
    buf[n] = '\0';

    pid_t mpid = (pid_t)atoi(buf);
    if (mpid <= 0) {
        log_action(district, "monitor not notified: invalid PID in file");
        return;
    }

    char action[128];
    if (kill(mpid, SIGUSR1) < 0) {
        snprintf(action, sizeof action,
                 "monitor not notified: kill(%d, SIGUSR1) failed: %s",
                 (int)mpid, strerror(errno));
    } else {
        snprintf(action, sizeof action,
                 "monitor notified: SIGUSR1 sent to PID %d", (int)mpid);
    }
    log_action(district, action);
}

static int next_report_id(const char *dat_path)
{
    int fd = open(dat_path, O_RDONLY);
    if (fd < 0) return 1;

    Report r;
    int max = 0;
    while (read(fd, &r, sizeof r) == (ssize_t)sizeof r)
        if (r.id > max) max = r.id;
    close(fd);
    return max + 1;
}

static void print_report(const Report *r)
{
    time_t ts = r->timestamp;
    char ts_str[32];
    strftime(ts_str, sizeof ts_str, "%Y-%m-%d %H:%M:%S", localtime(&ts));

    printf("  ID:          %d\n",           r->id);
    printf("  Inspector:   %s\n",           r->inspector);
    printf("  GPS:         (%.6f, %.6f)\n", r->latitude, r->longitude);
    printf("  Category:    %s\n",           r->category);
    printf("  Severity:    %d\n",           r->severity);
    printf("  Timestamp:   %s\n",           ts_str);
    printf("  Description: %s\n",           r->description);
}

static void update_symlink(const char *district)
{
    char link[256], target[512];
    snprintf(link,   sizeof link,   "active_reports-%s", district);
    snprintf(target, sizeof target, "%s/reports.dat",    district);

    struct stat lst;
    if (lstat(link, &lst) == 0) {
        if (S_ISLNK(lst.st_mode))
            unlink(link);
        else {
            fprintf(stderr,
                    "Warning: '%s' exists and is not a symlink; skipping creation\n",
                    link);
            return;
        }
    }
    if (symlink(target, link) < 0)
        perror("symlink");
}

static void check_symlink(const char *district)
{
    char link[256];
    snprintf(link, sizeof link, "active_reports-%s", district);

    struct stat lst;
    if (lstat(link, &lst) < 0) return;

    if (S_ISLNK(lst.st_mode)) {
        struct stat st;
        if (stat(link, &st) < 0)
            fprintf(stderr,
                    "Warning: symlink '%s' is dangling (target does not exist)\n",
                    link);
    }
}

static int ensure_district(const char *d)
{
    struct stat st;
    if (stat(d, &st) == 0) return 0;

    if (mkdir(d, 0750) < 0) { perror("mkdir"); return -1; }
    chmod(d, 0750);

    char p[512];
    int  fd;

    snprintf(p, sizeof p, "%s/reports.dat", d);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0664);
    if (fd < 0) { perror("create reports.dat"); return -1; }
    close(fd);
    chmod(p, 0664);

    snprintf(p, sizeof p, "%s/district.cfg", d);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0640);
    if (fd < 0) { perror("create district.cfg"); return -1; }
    const char *cfg_default = "severity_threshold=2\n";
    write(fd, cfg_default, strlen(cfg_default));
    close(fd);
    chmod(p, 0640);

    snprintf(p, sizeof p, "%s/logged_district", d);
    fd = open(p, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { perror("create logged_district"); return -1; }
    close(fd);
    chmod(p, 0644);

    update_symlink(d);
    return 0;
}

int parse_condition(const char *input, char *field, char *op, char *value)
{
    const char *c1 = strchr(input, ':');
    if (!c1) return -1;

    size_t flen = (size_t)(c1 - input);
    if (flen == 0 || flen >= 32) return -1;
    memcpy(field, input, flen);
    field[flen] = '\0';

    const char *c2 = strchr(c1 + 1, ':');
    if (!c2) return -1;

    size_t olen = (size_t)(c2 - (c1 + 1));
    if (olen == 0 || olen >= 4) return -1;
    memcpy(op, c1 + 1, olen);
    op[olen] = '\0';

    strncpy(value, c2 + 1, 63);
    value[63] = '\0';

    return 0;
}

int match_condition(Report *r, const char *field, const char *op, const char *value)
{
    if (strcmp(field, "severity") == 0) {
        long lv = atol(value);
        long rv = (long)r->severity;
        if (strcmp(op, "==") == 0) return rv == lv;
        if (strcmp(op, "!=") == 0) return rv != lv;
        if (strcmp(op, "<")  == 0) return rv <  lv;
        if (strcmp(op, "<=") == 0) return rv <= lv;
        if (strcmp(op, ">")  == 0) return rv >  lv;
        if (strcmp(op, ">=") == 0) return rv >= lv;

    } else if (strcmp(field, "timestamp") == 0) {
        long long lv = atoll(value);
        long long rv = (long long)r->timestamp;
        if (strcmp(op, "==") == 0) return rv == lv;
        if (strcmp(op, "!=") == 0) return rv != lv;
        if (strcmp(op, "<")  == 0) return rv <  lv;
        if (strcmp(op, "<=") == 0) return rv <= lv;
        if (strcmp(op, ">")  == 0) return rv >  lv;
        if (strcmp(op, ">=") == 0) return rv >= lv;

    } else if (strcmp(field, "category") == 0) {
        int c = strcmp(r->category, value);
        if (strcmp(op, "==") == 0) return c == 0;
        if (strcmp(op, "!=") == 0) return c != 0;
        if (strcmp(op, "<")  == 0) return c <  0;
        if (strcmp(op, "<=") == 0) return c <= 0;
        if (strcmp(op, ">")  == 0) return c >  0;
        if (strcmp(op, ">=") == 0) return c >= 0;

    } else if (strcmp(field, "inspector") == 0) {
        int c = strcmp(r->inspector, value);
        if (strcmp(op, "==") == 0) return c == 0;
        if (strcmp(op, "!=") == 0) return c != 0;
        if (strcmp(op, "<")  == 0) return c <  0;
        if (strcmp(op, "<=") == 0) return c <= 0;
        if (strcmp(op, ">")  == 0) return c >  0;
        if (strcmp(op, ">=") == 0) return c >= 0;
    }
    return 0;
}

static int cmd_add(const char *district)
{
    if (ensure_district(district) < 0) return -1;

    char dat[512];
    snprintf(dat, sizeof dat, "%s/reports.dat", district);

    struct stat st;
    if (stat(dat, &st) == 0) {
        if (g_role == ROLE_MANAGER && !(st.st_mode & S_IWUSR)) {
            fputs("Error: owner write bit not set on reports.dat\n", stderr);
            return -1;
        }
        if (g_role == ROLE_INSPECTOR && !(st.st_mode & S_IWGRP)) {
            fputs("Error: group write bit not set on reports.dat\n", stderr);
            return -1;
        }
    }

    Report r;
    memset(&r, 0, sizeof r);
    r.id        = next_report_id(dat);
    r.timestamp = time(NULL);
    strncpy(r.inspector, g_user, INSPECTOR_LEN - 1);

    double lat, lon;
    int    sev;
    char   cat[CATEGORY_LEN];
    char   desc[DESCRIPTION_LEN];

    printf("X: ");
    if (scanf("%lf", &lat) != 1)  { fputs("bad input\n", stderr); return -1; }
    printf("Y: ");
    if (scanf("%lf", &lon) != 1)  { fputs("bad input\n", stderr); return -1; }
    printf("Category (road/lighting/flooding/other): ");
    if (scanf("%31s", cat) != 1)  { fputs("bad input\n", stderr); return -1; }
    printf("Severity level (1/2/3): ");
    if (scanf("%d", &sev) != 1)   { fputs("bad input\n", stderr); return -1; }
    printf("Description: ");
    { int c; while ((c = getchar()) != '\n' && c != EOF); }
    if (!fgets(desc, DESCRIPTION_LEN, stdin)) { fputs("bad input\n", stderr); return -1; }
    size_t dl = strlen(desc);
    if (dl > 0 && desc[dl - 1] == '\n') desc[dl - 1] = '\0';

    r.latitude  = lat;
    r.longitude = lon;
    r.severity  = sev;
    strncpy(r.category,    cat,  CATEGORY_LEN    - 1);
    strncpy(r.description, desc, DESCRIPTION_LEN - 1);

    int fd = open(dat, O_WRONLY | O_APPEND);
    if (fd < 0) { perror("open reports.dat"); return -1; }
    if (write(fd, &r, sizeof r) != (ssize_t)sizeof r) {
        perror("write"); close(fd); return -1;
    }
    close(fd);
    chmod(dat, 0664);

    printf("Report %d added to district '%s'.\n", r.id, district);
    check_symlink(district);
    log_action(district, "add");
    notify_monitor(district);
    return 0;
}

static int cmd_list(const char *district)
{
    char dat[512];
    snprintf(dat, sizeof dat, "%s/reports.dat", district);

    struct stat st;
    if (stat(dat, &st) < 0) {
        fprintf(stderr, "Error: district '%s' not found\n", district);
        return -1;
    }

    if (g_role == ROLE_MANAGER && !(st.st_mode & S_IRUSR)) {
        fputs("Error: owner read bit not set on reports.dat\n", stderr);
        return -1;
    }
    if (g_role == ROLE_INSPECTOR && !(st.st_mode & S_IRGRP)) {
        fputs("Error: group read bit not set on reports.dat\n", stderr);
        return -1;
    }

    char perms[10];
    mode_to_str(st.st_mode, perms);
    char mtime_str[32];
    strftime(mtime_str, sizeof mtime_str, "%Y-%m-%d %H:%M:%S",
             localtime(&st.st_mtime));
    printf("reports.dat: %s  size=%lld bytes  modified=%s\n",
           perms, (long long)st.st_size, mtime_str);
    puts("------------------------------------------------------------");

    int fd = open(dat, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return -1; }

    Report r;
    int cnt = 0;
    while (read(fd, &r, sizeof r) == (ssize_t)sizeof r) {
        print_report(&r);
        puts("  --");
        cnt++;
    }
    close(fd);

    printf("%d report(s) in district '%s'.\n", cnt, district);
    check_symlink(district);
    log_action(district, "list");
    return 0;
}

static int cmd_view(const char *district, int report_id)
{
    char dat[512];
    snprintf(dat, sizeof dat, "%s/reports.dat", district);

    struct stat st;
    if (stat(dat, &st) < 0) {
        fprintf(stderr, "Error: district '%s' not found\n", district);
        return -1;
    }
    if (g_role == ROLE_MANAGER && !(st.st_mode & S_IRUSR)) {
        fputs("Error: owner read bit not set on reports.dat\n", stderr);
        return -1;
    }
    if (g_role == ROLE_INSPECTOR && !(st.st_mode & S_IRGRP)) {
        fputs("Error: group read bit not set on reports.dat\n", stderr);
        return -1;
    }

    int fd = open(dat, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return -1; }

    Report r;
    int found = 0;
    while (read(fd, &r, sizeof r) == (ssize_t)sizeof r) {
        if (r.id == report_id) { found = 1; break; }
    }
    close(fd);

    if (!found) {
        fprintf(stderr, "Error: report %d not found in district '%s'\n",
                report_id, district);
        return -1;
    }

    print_report(&r);
    log_action(district, "view");
    return 0;
}

static int cmd_remove_report(const char *district, int report_id)
{
    if (g_role != ROLE_MANAGER) {
        fputs("Error: only managers can remove reports\n", stderr);
        return -1;
    }

    char dat[512];
    snprintf(dat, sizeof dat, "%s/reports.dat", district);

    struct stat st;
    if (stat(dat, &st) < 0) {
        fprintf(stderr, "Error: district '%s' not found\n", district);
        return -1;
    }
    if (!(st.st_mode & S_IWUSR)) {
        fputs("Error: owner write bit not set on reports.dat\n", stderr);
        return -1;
    }

    int num = (int)(st.st_size / (off_t)sizeof(Report));

    int fd = open(dat, O_RDWR);
    if (fd < 0) { perror("open reports.dat"); return -1; }

    int pos = -1;
    Report r;
    for (int i = 0; i < num; i++) {
        lseek(fd, (off_t)i * (off_t)sizeof(Report), SEEK_SET);
        if (read(fd, &r, sizeof r) == (ssize_t)sizeof r && r.id == report_id) {
            pos = i;
            break;
        }
    }

    if (pos < 0) {
        fprintf(stderr, "Error: report %d not found in district '%s'\n",
                report_id, district);
        close(fd);
        return -1;
    }

    for (int i = pos + 1; i < num; i++) {
        Report tmp;
        lseek(fd, (off_t)i * (off_t)sizeof(Report), SEEK_SET);
        read(fd, &tmp, sizeof tmp);
        lseek(fd, (off_t)(i - 1) * (off_t)sizeof(Report), SEEK_SET);
        write(fd, &tmp, sizeof tmp);
    }

    ftruncate(fd, (off_t)(num - 1) * (off_t)sizeof(Report));
    close(fd);

    printf("Report %d removed from district '%s'.\n", report_id, district);
    log_action(district, "remove_report");
    return 0;
}

static int cmd_update_threshold(const char *district, int value)
{
    if (g_role != ROLE_MANAGER) {
        fputs("Error: only managers can update the severity threshold\n", stderr);
        return -1;
    }

    char cfg[512];
    snprintf(cfg, sizeof cfg, "%s/district.cfg", district);

    struct stat st;
    if (stat(cfg, &st) < 0) {
        fprintf(stderr, "Error: district.cfg not found in '%s'\n", district);
        return -1;
    }

    if ((st.st_mode & 0777) != 0640) {
        fprintf(stderr,
                "Error: district.cfg permissions are %03o (expected 640). Refusing.\n",
                (unsigned)(st.st_mode & 0777));
        return -1;
    }
    if (!(st.st_mode & S_IWUSR)) {
        fputs("Error: owner write bit not set on district.cfg\n", stderr);
        return -1;
    }

    int fd = open(cfg, O_WRONLY | O_TRUNC);
    if (fd < 0) { perror("open district.cfg"); return -1; }
    char buf[64];
    int n = snprintf(buf, sizeof buf, "severity_threshold=%d\n", value);
    write(fd, buf, n);
    close(fd);
    chmod(cfg, 0640);

    printf("Severity threshold updated to %d in district '%s'.\n", value, district);
    log_action(district, "update_threshold");
    return 0;
}

static int cmd_filter(const char *district, int nc, char **conds)
{
    char dat[512];
    snprintf(dat, sizeof dat, "%s/reports.dat", district);

    struct stat st;
    if (stat(dat, &st) < 0) {
        fprintf(stderr, "Error: district '%s' not found\n", district);
        return -1;
    }
    if (g_role == ROLE_MANAGER && !(st.st_mode & S_IRUSR)) {
        fputs("Error: owner read bit not set on reports.dat\n", stderr);
        return -1;
    }
    if (g_role == ROLE_INSPECTOR && !(st.st_mode & S_IRGRP)) {
        fputs("Error: group read bit not set on reports.dat\n", stderr);
        return -1;
    }

    char fields[16][32], ops[16][4], vals[16][64];
    for (int i = 0; i < nc; i++) {
        if (parse_condition(conds[i], fields[i], ops[i], vals[i]) < 0) {
            fprintf(stderr, "Error: malformed condition '%s'\n", conds[i]);
            return -1;
        }
    }

    int fd = open(dat, O_RDONLY);
    if (fd < 0) { perror("open reports.dat"); return -1; }

    Report r;
    int cnt = 0;
    while (read(fd, &r, sizeof r) == (ssize_t)sizeof r) {
        int ok = 1;
        for (int i = 0; i < nc; i++)
            if (!match_condition(&r, fields[i], ops[i], vals[i])) { ok = 0; break; }
        if (ok) { print_report(&r); puts("  --"); cnt++; }
    }
    close(fd);

    printf("%d matching report(s).\n", cnt);
    log_action(district, "filter");
    return 0;
}

/* Deletes a district directory (via fork+execl rm -rf) and its symlink. */
static int cmd_remove_district(const char *district)
{
    if (g_role != ROLE_MANAGER) {
        fputs("Error: only managers can remove districts\n", stderr);
        return -1;
    }

    struct stat st;
    if (stat(district, &st) < 0) {
        fprintf(stderr, "Error: district '%s' not found\n", district);
        return -1;
    }
    if (!S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Error: '%s' is not a district directory\n", district);
        return -1;
    }

    pid_t pid = fork();
    if (pid < 0) { perror("fork"); return -1; }

    if (pid == 0) {
        execl("/bin/rm", "rm", "-rf", district, (char *)NULL);
        perror("execl rm");
        _exit(1);
    }

    int wstatus;
    if (waitpid(pid, &wstatus, 0) < 0) { perror("waitpid"); return -1; }
    if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0) {
        fprintf(stderr, "Error: rm -rf '%s' failed\n", district);
        return -1;
    }

    char link[256];
    snprintf(link, sizeof link, "active_reports-%s", district);
    if (unlink(link) < 0 && errno != ENOENT)
        perror("unlink symlink");

    printf("District '%s' and its symlink removed.\n", district);
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr,
            "Usage: city_manager --role <role> --user <user> --<command> [args...]\n"
            "  Commands: add, list, view, remove_report, remove_district,\n"
            "            update_threshold, filter\n"
            "  Roles:    manager, inspector\n");
        return 1;
    }

    char *cmd     = NULL;
    int   cmd_idx = -1;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--role") == 0 && i + 1 < argc)
            strncpy(g_role_str, argv[++i], sizeof g_role_str - 1);
        else if (strcmp(argv[i], "--user") == 0 && i + 1 < argc)
            strncpy(g_user, argv[++i], sizeof g_user - 1);
        else if (strncmp(argv[i], "--", 2) == 0 && cmd == NULL) {
            cmd     = argv[i] + 2;
            cmd_idx = i;
        }
    }

    if (!g_role_str[0]) { fputs("Error: --role is required\n", stderr); return 1; }
    if (!g_user[0])     { fputs("Error: --user is required\n", stderr); return 1; }
    if (!cmd)           { fputs("Error: a command flag is required\n", stderr); return 1; }

    if      (strcmp(g_role_str, "manager")  == 0) g_role = ROLE_MANAGER;
    else if (strcmp(g_role_str, "inspector") == 0) g_role = ROLE_INSPECTOR;
    else {
        fprintf(stderr, "Error: unknown role '%s' (use 'manager' or 'inspector')\n",
                g_role_str);
        return 1;
    }

    char *district = (cmd_idx + 1 < argc) ? argv[cmd_idx + 1] : NULL;

    if (strcmp(cmd, "add") == 0) {
        if (!district) { fputs("Usage: --add <district>\n", stderr); return 1; }
        return cmd_add(district) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "list") == 0) {
        if (!district) { fputs("Usage: --list <district>\n", stderr); return 1; }
        return cmd_list(district) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "view") == 0) {
        if (!district || cmd_idx + 2 >= argc) {
            fputs("Usage: --view <district> <report_id>\n", stderr); return 1;
        }
        return cmd_view(district, atoi(argv[cmd_idx + 2])) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "remove_report") == 0) {
        if (!district || cmd_idx + 2 >= argc) {
            fputs("Usage: --remove_report <district> <report_id>\n", stderr); return 1;
        }
        return cmd_remove_report(district, atoi(argv[cmd_idx + 2])) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "remove_district") == 0) {
        if (!district) { fputs("Usage: --remove_district <district>\n", stderr); return 1; }
        return cmd_remove_district(district) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "update_threshold") == 0) {
        if (!district || cmd_idx + 2 >= argc) {
            fputs("Usage: --update_threshold <district> <value>\n", stderr); return 1;
        }
        return cmd_update_threshold(district, atoi(argv[cmd_idx + 2])) < 0 ? 1 : 0;
    }
    if (strcmp(cmd, "filter") == 0) {
        if (!district) { fputs("Usage: --filter <district> <cond...>\n", stderr); return 1; }
        int nc = argc - (cmd_idx + 2);
        if (nc < 1) { fputs("Error: at least one condition required\n", stderr); return 1; }
        return cmd_filter(district, nc, &argv[cmd_idx + 2]) < 0 ? 1 : 0;
    }

    fprintf(stderr, "Error: unknown command '--%s'\n", cmd);
    return 1;
}
