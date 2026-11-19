# 🏙️ City Reports Manager

A set of Linux command-line programs written in C for recording and analysing city infrastructure reports (potholes, broken street lights and so on), grouped by district. Built for the Operating Systems course at Politehnica University of Timișoara in three phases, each adding a new part of the POSIX API.

![C](https://img.shields.io/badge/C-00599C?style=for-the-badge&logo=c&logoColor=white)
![Linux](https://img.shields.io/badge/Linux-FCC624?style=for-the-badge&logo=linux&logoColor=black)
![CMake](https://img.shields.io/badge/CMake-064F8C?style=for-the-badge&logo=cmake&logoColor=white)
![POSIX](https://img.shields.io/badge/POSIX-333333?style=for-the-badge&logo=gnu&logoColor=white)

> ⚠️ **Linux only.** It uses POSIX system calls (`fork`, `exec`, `pipe`, `kill`, signals, file permissions), so it won't build on Windows. Use Linux, macOS or WSL.

---

## ✨ Features

### 📁 Report Management (`city_manager`)
- **Add, list, view and remove** reports stored as fixed-size binary records
- **Filtering** by field with conditions like `severity:>=:3`
- **Role-based permissions**: managers and inspectors can do different things
- **Per-district logging** of every action
- **Symlinks** to each district's report file, with dangling-link detection

### 🔔 Live Notifications (`monitor_reports`)
- Runs in the background and prints a notification whenever a report is added
- Woken by `SIGUSR1`, shuts down cleanly on `SIGINT`
- Found by other programs through a PID file

### 🖥️ Interactive Hub (`city_hub`)
- A small shell that starts the monitor and gets its output back through a pipe
- Runs one `scorer` process per district in parallel and combines the results

### 📊 Scoring (`scorer`)
- Computes a workload score per inspector from a district's reports

---

## 🧠 OS Concepts Covered

| Program | Concepts |
|---|---|
| `city_manager` | Low-level file I/O (`open`, `read`, `write`, `lseek`), binary records, `chmod` permissions, symlinks |
| `monitor_reports` | Signals (`sigaction`, `SIGUSR1`, `SIGINT`), PID files |
| `city_hub` | `fork`, `execl`, `pipe`, `dup2`, reaping children with `waitpid` |
| `scorer` | Reading binary records, aggregation |

---

## 🗂️ Data Layout

Each district is a directory that holds:
- a binary file of `Report` records (id, inspector, latitude, longitude, category, severity, timestamp, description)
- a config file with a severity threshold
- a log of every action performed in that district

---

## 📋 Prerequisites

- Linux, macOS or WSL
- `gcc`
- `cmake` 4.1 or newer (optional, you can also build with plain `gcc`)

---

## 🚀 Getting Started

### Build with CMake

```bash
cmake -S . -B build
cmake --build build
cd build
```

### Or build with gcc

```bash
gcc -std=c99 -o city_manager main.c
gcc -std=c99 -o monitor_reports monitor_reports.c
gcc -std=c99 -o city_hub city_hub.c
gcc -std=c99 -o scorer scorer.c
```

---

## 💻 Usage

### `city_manager`

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

### `city_hub`

```bash
./city_hub
> start_monitor                    # starts the monitor, output comes back through a pipe
> calculate_scores central north   # combined workload report for one or more districts
> help
> quit
```

---

## 📁 Project Structure

```
├── main.c               # city_manager
├── monitor_reports.c    # background notification monitor
├── city_hub.c           # interactive hub
├── scorer.c             # per-inspector scoring
├── CMakeLists.txt
└── AI_usage-ALL-phases.md
```

---

## 🤖 Use of AI

I used Claude for some functions and documented exactly what was generated, what I reviewed and changed, and what I wrote myself, phase by phase, in [AI_usage-ALL-phases.md](AI_usage-ALL-phases.md).

---

## 👤 Author

**Andrei-Vlad Cioban**, Computer Engineering, Politehnica University of Timișoara
