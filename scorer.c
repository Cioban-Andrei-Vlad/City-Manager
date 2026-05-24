#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>

#define INSPECTOR_LEN    64
#define CATEGORY_LEN     32
#define DESCRIPTION_LEN  128
#define MAX_INSPECTORS   256

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

typedef struct {
    char inspector[INSPECTOR_LEN];
    long score;
    int  count;
} Entry;

int main(int argc, char *argv[])
{
    if (argc < 2) {
        fprintf(stderr, "Usage: scorer <district>\n");
        return 1;
    }
    const char *district = argv[1];

    struct stat st;
    if (stat(district, &st) < 0 || !S_ISDIR(st.st_mode)) {
        printf("[SCORER %s] error: district directory not found\n", district);
        return 1;
    }

    char dat[512];
    snprintf(dat, sizeof dat, "%s/reports.dat", district);

    int fd = open(dat, O_RDONLY);
    if (fd < 0) {
        printf("[SCORER %s] error: cannot open %s\n", district, dat);
        return 1;
    }

    Entry table[MAX_INSPECTORS];
    int n = 0;

    Report r;
    int total = 0;
    while (read(fd, &r, sizeof r) == (ssize_t)sizeof r) {
        total++;
        int idx = -1;
        for (int i = 0; i < n; i++) {
            if (strcmp(table[i].inspector, r.inspector) == 0) { idx = i; break; }
        }
        if (idx < 0) {
            if (n >= MAX_INSPECTORS) continue;
            idx = n++;
            strncpy(table[idx].inspector, r.inspector, INSPECTOR_LEN - 1);
            table[idx].inspector[INSPECTOR_LEN - 1] = '\0';
            table[idx].score = 0;
            table[idx].count = 0;
        }
        table[idx].score += r.severity;
        table[idx].count += 1;
    }
    close(fd);

    printf("[SCORER %s] reports=%d inspectors=%d\n", district, total, n);
    for (int i = 0; i < n; i++) {
        printf("[SCORER %s] inspector=%s reports=%d workload_score=%ld\n",
               district, table[i].inspector, table[i].count, table[i].score);
    }
    printf("[SCORER %s] end\n", district);
    return 0;
}