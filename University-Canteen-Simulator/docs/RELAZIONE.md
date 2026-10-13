# Relazione tecnica — Simulazione di una mensa universitaria

## 1. Obiettivo

Il programma realizza una simulazione concorrente di una mensa tramite **processi distinti in C su Linux**. Lo scopo è modellare l'accesso degli utenti alle stazioni di servizio, il lavoro degli operatori, la disponibilità limitata di postazioni e tavoli e la raccolta di statistiche. Il progetto utilizza primitive UNIX e meccanismi IPC System V.

## 2. Architettura del codice

1. `main.c` controlla gli argomenti, imposta `CONFIG_FILE` quando viene passato un file alternativo e avvia il responsabile mediante `fork()` ed `execve()`. Attende la terminazione con `waitpid()`.
2. `Mensa/responsabile_mensa.c` legge la configurazione (di default `variabili.txt`), legge `menu.txt`, prepara le risorse IPC, coordina processi operatori e utenti e gestisce l'avanzamento delle giornate simulate.
3. `Operatori/operatori.c` gestisce i singoli operatori presso stazioni come primi, secondi, caffetteria e cassa, usando le risorse IPC fornite.
4. `Utenti/utente.c` simula i clienti, inclusi accessi alle stazioni, gestione del vassoio e uso delle risorse condivise.
5. `aggiungi_utenti.c` permette di creare altri `N_NEW_USERS` processi utente durante una simulazione già attiva.
6. `Operazioni_file/` implementa parsing di configurazione e menu; `utilities.h` raccoglie strutture e costanti condivise.

## 3. Sincronizzazione e comunicazione

- **Memoria condivisa (`shmget`, `shmat`)**: condivisione di dati fra processi.
- **Semafori System V (`semget`, `semop`, `semctl`)**: coordinamento e regolazione dell'accesso a risorse limitate.
- **Code di messaggi (`msgget`, ecc.)**: scambio di messaggi fra gli attori della mensa.
- **Processi (`fork`, `execv`/`execve`, `waitpid`)**: esecuzione concorrente delle componenti.

Il responsabile organizza la simulazione e produce statistiche giornaliere, parziali e finali (file `statistiche.csv` separato da `;`), incluse informazioni su utenti serviti, piatti, attese, operatori e ricavi. Le configurazioni permettono di provare scenari di timeout e sovraccarico.

## 4. Compilazione ed esecuzione

Richiede Linux, GCC e Make. Tutti i comandi seguenti vanno eseguiti **dalla cartella principale del repository**.

| Comando | Funzione |
|---|---|
| `make` | Compila tutti gli eseguibili |
| `make clean` | Elimina binari e file oggetto |
| `./main` | Avvia la simulazione con `variabili.txt` |
| `./main config_timeout.conf` | Avvia con la configurazione di timeout |
| `./main config_overload.conf` | Avvia con la configurazione di sovraccarico |
| `./main FILE` | Avvia usando i parametri definiti in `FILE` |
| `./aggiungi_utenti` | Aggiunge utenti alla simulazione attiva, con configurazione predefinita |
| `./aggiungi_utenti FILE` | Aggiunge utenti con il file di configurazione scelto |

**Sintassi dei programmi interni** (normalmente lanciati dal responsabile, non manualmente):

- `Mensa/responsabile_mensa`: senza argomenti; legge `CONFIG_FILE` o usa `./variabili.txt`.
- `Operatori/operatori <stazione> <indice> <shmid> <msgid> <semid>`: riceve stazione, indice e identificativi IPC.
- `Utenti/utente <N_NANO_SECS> [extra]`: riceve il parametro temporale e un flag opzionale per nuovi utenti.

## 5. Sintassi dei file di input

### `variabili.txt` e `config_*.conf`

Formato: **una coppia `CHIAVE VALORE` per riga**, per esempio:

```text
NOF_USERS 8
NOF_WORKERS 4
SIM_DURATION 1
N_NANO_SECS 10000000
```

| Chiave | Significato |
|---|---|
| `AVG_SRVC_PRIMI`, `AVG_SRVC_MAIN_COURSE`, `AVG_SRVC_COFFEE`, `AVG_SRVC_CASSA` | Tempi medi di servizio per le stazioni |
| `AVG_REFILL_PRIMI`, `AVG_REFILL_SECONDI`, `AVG_REFILL_TIME` | Quantità e tempi di rifornimento |
| `NOF_USERS`, `N_NEW_USERS`, `NOF_WORKERS` | Numero iniziale di utenti, nuovi utenti e operatori |
| `NOF_WK_SEATS_PRIMI`, `NOF_WK_STEATS_SECONDI`, `NOF_WK_STEATS_COFFEE`, `NOF_WK_SEATS_CASSA` | Postazioni per stazione (i nomi `STEATS` sono quelli originali) |
| `NOF_TABLE_SEATS` | Posti ai tavoli |
| `SIM_DURATION`, `N_NANO_SECS` | Durata e scala temporale della simulazione |
| `MAX_PORZIONI_PRIMI`, `MAX_PORZIONI_SECONDI` | Capacità massima delle porzioni |
| `NOF_PAUSE` | Parametro per le pause degli operatori |
| `PRICE_PRIMI`, `PRICE_SECONDI`, `PRICE_COFFEE` | Prezzi dei prodotti |
| `OVERLOAD_THRESHOLD` | Soglia di utenti in attesa per terminazione da sovraccarico |

Le chiavi sono sensibili alla grafia: preservare i nomi esatti presenti nei file. Alcune impostazioni devono rispettare vincoli controllati dal responsabile (es. operatori sufficienti, tempi positivi e porzioni massime adeguate).

### `menu.txt`

Quattro righe nel formato `NUMERO;Elemento1;Elemento2;...`. Nell'ordine: primi, secondi, dolci, caffè. Per esempio:

```text
2;Penne al sugo;Trofie al pesto
2;Milanese e patatine;Merluzzo alla piastra con carote
2;Torta al cioccolato;Muffin
4;Normale;Macchiato;Decaffeinato;Ginseng
```

Il responsabile verifica il numero minimo di elementi per categoria.

## 6. Output e scenari

Il programma stampa avanzamento e statistiche su terminale e scrive `statistiche.csv`, generato a ogni esecuzione. `config_timeout.conf` e `config_overload.conf` sono esempi utili per esplorare le condizioni di arresto. Essendo concorrente, il risultato può variare a ogni esecuzione.

## 7. English technical summary

The program models a university canteen using several UNIX processes. A launcher starts the manager, which parses a configuration and menu, initializes System V IPC resources, coordinates canteen employees and customers, and records daily and overall statistics. Workers and customers run in separate processes, synchronized with semaphores and communicating through shared memory and message queues. Command-line options select scenario files and optionally spawn extra customers while the main simulation is active. All original code identifiers and Italian comments remain unchanged; English commentary has been added for accessibility.
