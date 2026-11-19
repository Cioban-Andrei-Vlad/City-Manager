# City Reports Manager

A set of small Linux command-line programs in C for recording and analysing city infrastructure reports (potholes, broken street lights and so on), grouped by district. It was built for the Operating Systems course at Politehnica University of Timișoara, in three phases, each adding a new part of the POSIX API.

> **Linux only.** It uses POSIX system calls (`fork`, `exec`, `pipe`, `kill`, signals, file permissions), so it won't build on Windows. Use Linux, macOS or WSL.

## What's inside

| Program | What it does | Main concepts |
|---|---|---|
| `city_manager` | Adds, lists, views, filters and removes reports in a district | low-level file I/O (`open`, `read`, `write`, `lseek`), binary records, file permissions per role, symlinks, logging |
| `monitor_reports` | A background process that prints a notification whenever a report is added | signals (`SIGUSR1`, `SIGINT`), a PID file |
| `city_hub` | An interactive shell that starts the monitor and computes scores | `fork`, `execl`, `pipe`, `dup2`, reaping child processes with `waitpid` |
| `scorer` | Computes a score per inspector from a district's reports | reading binary records, aggregation |

Each district is a directory holding a binary file of fixed-size `Report` records, a config file with a severity threshold, and a log of every action. Managers and inspectors have different permissions.

## Build

Requirements: Linux, `gcc`, `cmake` (3.x or newer).

```bash
cmake -S . -B build
cmake --build build
cd build
```

Or without CMake:

```bash
gcc -std=c99 -o city_manager main.c
gcc -std=c99 -o monitor_reports monitor_reports.c
gcc -std=c99 -o city_hub city_hub.c
gcc -std=c99 -o scorer scorer.c
```

## Usage

```bash
# Add a report to a district (creates the district the first time)
./city_manager --role inspector --user ana --add central

# List, view, filter
./city_manager --role manager --user ion --list central
./city_manager --role manager --user ion --view central 3
./city_manager --role manager --user ion --filter central severity:>=:3

# Manager-only operations
./city_manager --role manager --user ion --update_threshold central 4
./city_manager --role manager --user ion --remove_report central 3
./city_manager --role manager --user ion --remove_district central
```

Interactive hub:

```bash
./city_hub
> start_monitor          # starts monitor_reports, output comes back through a pipe
> calculate_scores ...   # runs scorer for districts
> help
> quit
```

## Use of AI

I used Claude for some functions and documented exactly what was generated, what I reviewed and changed, and what I wrote myself, phase by phase, in [AI_usage-ALL-phases.md](AI_usage-ALL-phases.md).

## Author

Andrei-Vlad Cioban, Computer Engineering, Politehnica University of Timișoara
