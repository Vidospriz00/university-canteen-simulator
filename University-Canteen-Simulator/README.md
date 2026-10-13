# University Canteen Simulator | Simulatore di Mensa Universitaria

**C · Linux · POSIX processes · System V IPC · concurrency**

University operating-systems project implementing a **multi-process canteen simulation** in C. The original source code, identifiers and Italian comments are preserved; additional English comments and documentation make the project easier to explore.

> **IT:** Progetto universitario di Sistemi Operativi: simulazione multiprocesso di una mensa. Codice e commenti italiani originali mantenuti, con note aggiuntive in inglese.

## Overview / Panoramica

A parent launcher creates the canteen manager. The manager coordinates workers and customers distributed across first courses, main courses, coffee/desserts, and checkout. Processes use **System V shared memory, semaphores and message queues** to coordinate activity. The program collects daily and overall statistics and writes them to `statistiche.csv`.

```text
main
  └── Mensa/responsabile_mensa (manager / responsabile)
        ├── Operatori/operatori  (workers / operatori)
        ├── Utenti/utente       (customers / utenti)
        └── System V IPC        (shared memory, semaphores, message queues)

aggiungi_utenti (optional separate launcher / avvio opzionale separato)
  └── Utenti/utente (extra customers / utenti aggiuntivi)
```

## Requirements / Requisiti

- **Linux** with System V IPC enabled (e.g. Ubuntu; WSL2 with Ubuntu also suitable)
- `gcc`, `make`, standard C / POSIX development headers
- Run commands **from the project root**: relative paths to `menu.txt` and child executables are used.

Ubuntu / Debian:

```bash
sudo apt update
sudo apt install build-essential
```

## Build / Compilazione

```bash
make
```

Compiles these executables: `main`, `Mensa/responsabile_mensa`, `Operatori/operatori`, `Utenti/utente`, and `aggiungi_utenti`.

To delete compiled artifacts / Per eliminare i file compilati:

```bash
make clean
```

## Run / Esecuzione

**Default configuration / Configurazione predefinita** (`variabili.txt`):

```bash
./main
```

**Timeout scenario / Scenario timeout:**

```bash
./main config_timeout.conf
```

**Overload scenario / Scenario sovraccarico:**

```bash
./main config_overload.conf
```

**Other configuration / Altra configurazione:**

```bash
./main percorso/del/tuo_file.conf
```

The launcher sets the `CONFIG_FILE` environment variable so that child processes use the selected configuration. The menu comes from `menu.txt` in the working directory. On completion, output statistics are saved to `statistiche.csv` (semicolon-separated). This generated file is ignored by Git.

### Add customers while the simulation is running / Aggiungere utenti durante la simulazione

Open a **second terminal in the same folder**, while `./main` is still executing:

```bash
./aggiungi_utenti
# Or pass the same custom configuration used in the first terminal:
./aggiungi_utenti config_timeout.conf
```

`aggiungi_utenti` starts `N_NEW_USERS` additional customer processes. It requires the IPC queues and synchronization objects created by the running simulation; **do not run it standalone when the simulation is stopped**. With short configurations the simulation may end before additional users can be added.

## Project structure / Struttura

| Path / Percorso | Purpose / Funzione |
|---|---|
| `main.c` | Entry point; configuration selection, `fork()` / `execve()` and `waitpid()` |
| `Mensa/responsabile_mensa.c` | Manager; initialization, processes, IPC resources, daily loop and statistics |
| `Operatori/operatori.c` | Station worker implementation |
| `Utenti/utente.c` | Customer process implementation |
| `aggiungi_utenti.c` | Optional launcher for additional customers |
| `Operazioni_file/operazioni_file.c`, `.h` | Configuration and menu file parsing |
| `utilities.h` | Shared types, IPC keys and helpers |
| `variabili.txt` | Default `KEY VALUE` configuration |
| `config_timeout.conf`, `config_overload.conf` | Example runtime scenarios |
| `menu.txt` | Semicolon-separated menu |
| `Makefile` | Build all executables and clean up build outputs |
| `docs/RELAZIONE.md` | Short Italian report and bilingual technical reference |

## Input syntax / Sintassi dei file

Configuration files use one `KEY VALUE` pair per line (e.g. `NOF_USERS 8`). `menu.txt` contains four menu rows, each starting with the number of items followed by `;`-separated names. See [docs/RELAZIONE.md](docs/RELAZIONE.md) for the complete parameter table, command syntax, and explanation of IPC.

## Verification / Verifica

```bash
make clean && make
./main config_timeout.conf
# statistics are produced in statistiche.csv
```

Linux is required; the simulation may vary between executions due to concurrent scheduling and randomization. For the original assignment's assessment, follow the course rules before publishing the source code.

## Implementation notes

Source logic was **not intentionally refactored** in this GitHub packaging. Original Italian comments remain, and English explanations have been added to the most important modules and entry points. This repository includes no Git history from the uploaded archive.
