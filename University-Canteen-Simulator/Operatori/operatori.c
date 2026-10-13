/*
 * IT: Implementa il comportamento degli operatori assegnati alle stazioni della mensa.
 * EN: Implements canteen workers assigned to service stations.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#include "../utilities.h"
#include "../Operazioni_file/operazioni_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <errno.h>

/* Conversione del tempo simulato: 1 minuto simulato = N_NANO_SECS ns reali. */
static void attendi_secondi_simulati(long secondi, long n_nano_secs)
{
    long long ns_totali = ((long long)secondi * n_nano_secs) / 60LL;
    struct timespec ts;
    ts.tv_sec = ns_totali / 1000000000LL;
    ts.tv_nsec = ns_totali % 1000000000LL;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

static void attendi_minuti_simulati(long minuti, long n_nano_secs)
{
    long long ns_totali = (long long)minuti * n_nano_secs;
    struct timespec ts;
    ts.tv_sec = ns_totali / 1000000000LL;
    ts.tv_nsec = ns_totali % 1000000000LL;

    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}


// Range di variazione del tempo per ogni stazione
#define RANGE_PRIMI 0.5f   // ±50%
#define RANGE_SECONDI 0.5f // ±50%
#define RANGE_COFFEE 0.8f  // ±80%
#define RANGE_CASSA 0.2f   // ±20%

typedef struct {
    int tipo_piatto;
    long ricavo;
} AggiornamentoOperatore;

static void registra_evento_operatore(Statistiche *statistiche, const void *dato)
{
    const AggiornamentoOperatore *evento = dato;
    if (evento->tipo_piatto >= 0 && evento->tipo_piatto < 3) {
        statistiche->piatti_distribuiti[evento->tipo_piatto]++;
    }
    statistiche->ricavo += evento->ricavo;
}

static void registra_operatore_attivo(Statistiche *statistiche, const void *dato)
{
    (void)dato;
    statistiche->operatori_attivi++;
}

static void registra_pausa(Statistiche *statistiche, const void *dato)
{
    (void)dato;
    statistiche->pause_effettuate++;
}

//----------------------------------------------------------------------------------------------

// FUNZIONI ESCLUSIVE PER OPERATORE

/// @brief Calcola un tempo casuale nell'intorno ±range del valore medio.
/// @param avg
/// @param range
/// @return
int tempoErogazione(int avg, float range)
{
    int min = (int)(avg * (1.0f - range));
    int max = (int)(avg * (1.0f + range));

    if (max < min)
    {
        max = min;
    }

    return min + rand() % (max - min + 1);
}
/// @brief Restituisce il numero di postazioni disponibili per una stazione.
/// @param stazione Nome della stazione.
/// @param conf Puntatore alla struttura di configurazione.
/// @return Numero di postazioni della stazione, -1 se la stazione non è valida.
static int numero_postazioni_stazione(const char *stazione, const Configurazione *conf)
{
    if (strcmp(stazione, "PRIMI") == 0)
    {
        return conf->NOF_WK_SEATS_PRIMI;
    }
    if (strcmp(stazione, "SECONDI") == 0)
    {
        return conf->NOF_WK_STEATS_SECONDI;
    }
    if (strcmp(stazione, "COFFEE") == 0)
    {
        return conf->NOF_WK_STEATS_COFFEE;
    }
    if (strcmp(stazione, "CASSA") == 0)
    {
        return conf->NOF_WK_SEATS_CASSA;
    }
    return -1;
}
//--------------------------------------------------------------------------------------------------------

//--------------------------------------------------------------------------------------------------------//
//----------------------GESTIONE MEMORIA CONDIVISA--------------------------------------------------------//
//--------------------------------------------------------------------------------------------------------//
Stazione_Distribuzione *aggancia_stazione_distribuzione_da_shm(int shmid)
{
    Stazione_Distribuzione *ptr_stazione = (Stazione_Distribuzione *)shmat(shmid, NULL, 0);
    if (ptr_stazione == (void *)-1)
    {
        perror("ERRORE shmat");
        return NULL;
    }
    ptr_stazione->max_porzioni = (int *)((char *)((ptr_stazione)) + sizeof(Stazione_Distribuzione));
    ptr_stazione->porzioni_consumate = ptr_stazione->max_porzioni + ptr_stazione->n_piatti;
    ptr_stazione->porzioni_disponibili = ptr_stazione->porzioni_consumate + ptr_stazione->n_piatti;

    return ptr_stazione;
}

int sgancia_stazione_distribuzione_da_shm(Stazione_Distribuzione *ptr_stazione)
{
    if (!ptr_stazione)
    {
        return 0;
    }
    if (shmdt(ptr_stazione) == -1)
    {
        perror("ERRORE sgancia stazione operatore");
        return -1;
    }
    return 1;
}

int *aggancia_intero_da_shm(int shmid)
{
    int *ptr_intero = (int *)shmat(shmid, NULL, 0);
    if (ptr_intero == (void *)-1)
    {
        perror("ERRORE shmat intero");
        return NULL;
    }
    return ptr_intero;
}

int sgancia_intero_da_shm(int *ptr_intero)
{
    if (!ptr_intero)
    {
        return 0;
    }
    if (shmdt(ptr_intero) == -1)
    {
        perror("ERRORE sgancia intero operatore");
        return -1;
    }
    return 1;
}

void aggancia_stazione_distribuzione(Stazione_Distribuzione **stazione)
{
    (*stazione)->max_porzioni = (int *)((char *)((*stazione)) + sizeof(Stazione_Distribuzione));
    (*stazione)->porzioni_consumate = (*stazione)->max_porzioni + (*stazione)->n_piatti;
    (*stazione)->porzioni_disponibili = (*stazione)->porzioni_consumate + (*stazione)->n_piatti;
}

//--------------------------------------------------------------------------------------------------------//
//----------------------GESTIONE COMUNICAZIONNE OPERATORE-UTENTE------------------------------------------//
//--------------------------------------------------------------------------------------------------------//

// gestisce la conversazione con un utente su una stazione di distribuzione.
int gestisci_stazione_distribuzione(const char *stazione, int msgid, int semid, int shmid, unsigned short mutex_idx, int avg, float range, long n_nano_secs)
{
    Comanda richiesta;                           // messaggio che arriva dall’utente
    Comanda risposta;                            // messaggio che l’operatore rimanda indietro
    Stazione_Distribuzione *ptr_stazione = NULL; // puntatore alla stazione in shared memory
    int tempo_impiegato;                         // tempo di erogazione del piatto (fa la sleep dopo aver servito il piatto o alla fine della comunicazione)

    if (msgrcv(msgid, &richiesta, sizeof(Comanda) - sizeof(long), 1, 0) == -1)
    {
        perror("Errore msgrcv operatore distribuzione");
        return -1;
    }

    if (strcmp(stazione, "PRIMI") == 0 || strcmp(stazione, "SECONDI") == 0)
    {
        // Conversazione tra un operatore e un utente specifico
        pid_t pid_utente = richiesta.utente;
        pid_t pid_operatore = getpid();
        int fine_conversazione = 0;

        while (!fine_conversazione)
        {

            // pulisci la struct completamente prima di usarla
            // precauzione per evitare dati spazzatura
            memset(&risposta, 0, sizeof(risposta));

            risposta.mtype = pid_utente;
            risposta.n_piatto = richiesta.n_piatto;
            risposta.utente = pid_utente;
            risposta.operatore = pid_operatore;
            risposta.disponibile = 0;

            if (riserva_semaforo(semid, mutex_idx) == -1)
            {
                perror("Errore riserva_semaforo mutex operatori");
                sgancia_stazione_distribuzione_da_shm(ptr_stazione);
                return -1;
            }

            ptr_stazione = aggancia_stazione_distribuzione_da_shm(shmid);
            if (ptr_stazione == NULL)
            {
                return -1;
            }

            // Controllo se il piatto richiesto esiste ed è disponibile
            if (richiesta.n_piatto >= 0 && richiesta.n_piatto < ptr_stazione->n_piatti)
            {
                if (ptr_stazione->porzioni_disponibili[richiesta.n_piatto] > 0)
                {
                    ptr_stazione->porzioni_disponibili[richiesta.n_piatto]--;
                    ptr_stazione->porzioni_consumate[richiesta.n_piatto]++;
                    risposta.disponibile = 1;
                }
                else
                {
                }
            }

            if (sgancia_stazione_distribuzione_da_shm(ptr_stazione) == -1)
            {
                return -1;
            }

            /*Per ogni stazione:
            i semafori delle postazioni stanno nel suo set
            il mutex è un indice dentro il suo set*/
            if (libera_semaforo(semid, mutex_idx) == -1)
            {
                perror("Errore libera_semaforo mutex");
                sgancia_stazione_distribuzione_da_shm(ptr_stazione);
                return -1;
            }

            if (risposta.disponibile == 1)
            {
                AggiornamentoOperatore evento = {strcmp(stazione, "PRIMI") == 0 ? 0 : 1, 0};
                if (aggiorna_statistica(registra_evento_operatore, &evento) == -1) {
                    perror("aggiorna statistica piatto");
                }

                tempo_impiegato = tempoErogazione(avg, range);
                attendi_secondi_simulati(tempo_impiegato, n_nano_secs);
            }

            if (msgsnd(msgid, &risposta, sizeof(Comanda) - sizeof(long), 0) == -1)
            {
                perror("Errore msgsnd risposta operatore");
                sgancia_stazione_distribuzione_da_shm(ptr_stazione);
                return -1;
            }

            ptr_stazione = NULL;

            if (risposta.disponibile == 1)
            {
                fine_conversazione = 1;
            }
            else
            {
                Comanda nuova_richiesta;
                if (msgrcv(msgid, &nuova_richiesta, sizeof(Comanda) - sizeof(long), pid_operatore, 0) == -1)
                {
                    perror("Errore msgrcv nuova richiesta utente");
                    return -1;
                }

                if (nuova_richiesta.fine_comunicazione == 1)
                {
                    fine_conversazione = 1;
                }
                else
                {
                    richiesta = nuova_richiesta;
                }
            }
        }
    }
    else
    {
        memset(&risposta, 0, sizeof(risposta));
        risposta.mtype = richiesta.utente;
        risposta.n_piatto = richiesta.n_piatto;
        risposta.utente = richiesta.utente;
        risposta.operatore = getpid();
        risposta.disponibile = 1;

        AggiornamentoOperatore evento = {2, 0};
        if (aggiorna_statistica(registra_evento_operatore, &evento) == -1) {
            perror("aggiorna statistica dolce-caffe");
        }

        int tempo_impiegato = tempoErogazione(avg, range);
        attendi_secondi_simulati(tempo_impiegato, n_nano_secs);

        if (msgsnd(msgid, &risposta, sizeof(Comanda) - sizeof(long), 0) == -1)
        {
            perror("Errore msgsnd operatore semplice");
            return -1;
        }
    }

    return 0;
}

/// @brief Gestisce una richiesta di pagamento alla cassa.
/// @param msgid ID della coda di messaggi della cassa.
/// @param semid ID del set di semafori della cassa.
/// @param shmid ID della shared memory che contiene il ricavo totale.
/// @param mutex_idx Indice del semaforo usato come mutex per proteggere il ricavo.
/// @param conf Configurazione, usata per leggere i prezzi.
/// @param avg Tempo medio di servizio della cassa.
/// @param range Intervallo casuale attorno al tempo medio.
/// @return 0 gestito,-1 errore.
int gestisci_cassa(int msgid, int semid, int shmid, unsigned short mutex_idx, const Configurazione *conf, int avg, float range, long n_nano_secs)
{
    Comanda richiesta_pagamento;
    Comanda risposta_pagamento;

    int *ricavo_totale = NULL;
    int totale = 0;
    int tempo_impiegato;

    if (msgrcv(msgid, &richiesta_pagamento, sizeof(Comanda) - sizeof(long), 1, 0) == -1)
    {
        perror("Errore msgrcv operatore cassa");
        return -1;
    }

    memset(&risposta_pagamento, 0, sizeof(risposta_pagamento));

    risposta_pagamento.mtype = richiesta_pagamento.utente;
    risposta_pagamento.tipo_richiesta = 2;
    risposta_pagamento.utente = richiesta_pagamento.utente;
    risposta_pagamento.operatore = getpid();
    risposta_pagamento.disponibile = 1;

    /*
     * Stessa convenzione dell' utente:
     n_piatto           -> primo preso
     disponibile        -> secondo preso
     fine_comunicazione -> dolce preso
     vuole_caffe        -> caffè preso
     */

    if (richiesta_pagamento.n_piatto >= 0)
    {
        totale += conf->PRICE_PRIMI;
    }

    if (richiesta_pagamento.disponibile >= 0)
    {
        totale += conf->PRICE_SECONDI;
    }

    if (richiesta_pagamento.vuole_caffe >= 0)
    {
        totale += conf->PRICE_COFFEE;
    }

    risposta_pagamento.totale_pagato = totale;

    // per evitare che venga modificato
    if (riserva_semaforo(semid, mutex_idx) == -1)
    {
        perror("Errore riserva_semaforo mutex cassa");
        return -1;
    }

    ricavo_totale = aggancia_intero_da_shm(shmid);
    if (ricavo_totale == NULL)
    {
        libera_semaforo(semid, mutex_idx);
        return -1;
    }

    *ricavo_totale += totale;

    AggiornamentoOperatore evento = {-1, totale};
    if (aggiorna_statistica(registra_evento_operatore, &evento) == -1) {
        perror("aggiorna statistica ricavo");
    }

    if (sgancia_intero_da_shm(ricavo_totale) == -1)
    {
        libera_semaforo(semid, mutex_idx);
        return -1;
    }

    if (libera_semaforo(semid, mutex_idx) == -1)
    {
        perror("Errore libera_semaforo mutex cassa");
        return -1;
    }

    // tempo di servizio della cassa
    tempo_impiegato = tempoErogazione(avg, range);
    attendi_secondi_simulati(tempo_impiegato, n_nano_secs);

    if (msgsnd(msgid,
               &risposta_pagamento,
               sizeof(Comanda) - sizeof(long),
               0) == -1)
    {
        perror("Errore msgsnd risposta cassa");
        return -1;
    }

    return 0;
}

// FUNZIONE MAIN

// EN: Workers receive station name and IPC identifiers from the manager.
int main(int argc, char *argv[])
{
    // Controllo argomenti
    if (argc < 6)
    {
        fprintf(stderr, "Uso: %s <stazione> <indice> <shmid> <msgid> <semid>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *stazione = argv[1];
    int indice = atoi(argv[2]);
    int shmid = atoi(argv[3]);
    int msgid = atoi(argv[4]);
    int semid = atoi(argv[5]);

    // Ogni operatore ha un seed diverso
    srand(getpid() ^ time(NULL));

    // printf("[Operatore %d] Avviato → stazione %s | PID %d\n", indice, stazione, getpid());

    // Lettura variabili di configurazione
    char *config_file = getenv("CONFIG_FILE");
    if (config_file == NULL || config_file[0] == '\0')
    {
        config_file = "./variabili.txt";
    }
    Configurazione conf = {0};
    leggiCostanti(config_file, &conf);

    int pause_rimanenti = conf.NOF_PAUSE;
    int terminato = 0;

    int n_postazioni = numero_postazioni_stazione(stazione, &conf);
    if (n_postazioni <= 0)
    {
        fprintf(stderr, "Numero postazioni non valido per la stazione %s\n", stazione);
        exit(EXIT_FAILURE);
    }

    int postazione_idx = indice % n_postazioni;

    int semid_sinc = semget(KEY_SEM_SINC, 1, 0);
    if (semid_sinc == -1)
    {
        perror("semget sincronizzazione operatore");
        exit(EXIT_FAILURE);
    }
    if (riserva_semaforo(semid_sinc, 0) == -1)
    {
        perror("sincronizzazione operatore");
        exit(EXIT_FAILURE);
    }
    if (attendi_zero(semid_sinc, 0) == -1)
    {
        perror("attesa sincronizzazione operatore");
        exit(EXIT_FAILURE);
    }

    if (riserva_semaforo(semid, (unsigned short)postazione_idx) == -1)
    {
        perror("Errore occupazione postazione");
        exit(EXIT_FAILURE);
    }
    if (aggiorna_statistica(registra_operatore_attivo, NULL) == -1) {
        perror("aggiorna statistica operatore attivo");
    }

    // Setup dei parametri specifici in base al tipo di operatore
    int avg = 0;
    float range = 0.0f;

    if (strcmp(stazione, "PRIMI") == 0)
    {
        avg = conf.AVG_SRVC_PRIMI;
        range = RANGE_PRIMI;
    }
    else if (strcmp(stazione, "SECONDI") == 0)
    {
        avg = conf.AVG_SRVC_MAIN_COURSE;
        range = RANGE_SECONDI;
    }
    else if (strcmp(stazione, "COFFEE") == 0)
    {
        avg = conf.AVG_SRVC_COFFEE;
        range = RANGE_COFFEE;
    }
    else if (strcmp(stazione, "CASSA") == 0)
    {
        avg = conf.AVG_SRVC_CASSA;
        range = RANGE_CASSA;
    }
    else
    {
        fprintf(stderr, "[Operatore %d] Stazione sconosciuta: %s\n", indice, stazione);

        libera_semaforo(semid, (unsigned short)postazione_idx);
        exit(EXIT_FAILURE);
    }

    // l'operatore resta in ascolto sulla coda della propria stazione,
    // riceve una richiesta da un utente, simula il servizio e poi risponde.
    while (!terminato)
    {

        // Se ha finito tutte le pause, continua comunque a lavorare.
        if (pause_rimanenti > 0)
        {
            int va_in_pausa = rand() % 100 < 20; // 20% di probabilità di pausa

            if (va_in_pausa)
            {
                // controllo che non sia l'unico operatore rimasto attivo
                int occupate = 0;
                for (int i = 0; i < n_postazioni; i++)
                {
                    int val = semctl(semid, i, GETVAL);
                    if (val == -1)
                    {
                        perror("semctl GETVAL");
                        occupate = 0;
                        break;
                    }
                    if (val == 0)
                    {
                        occupate++;
                    }
                }

                if (occupate > 1)
                {
                    int durata_pausa = 1 + rand() % 3;
                    pause_rimanenti--;
                    if (aggiorna_statistica(registra_pausa, NULL) == -1) {
                        perror("aggiorna statistica pausa");
                    }

                    // libera la postazione durante la pausa
                    if (libera_semaforo(semid, (unsigned short)postazione_idx) == -1)
                    {
                        perror("Errore rilascio postazione per pausa");
                        break;
                    }

                    attendi_minuti_simulati(durata_pausa, conf.N_NANO_SECS);

                    // rioccupa la stessa postazione alla fine della pausa
                    if (riserva_semaforo(semid, (unsigned short)postazione_idx) == -1)
                    {
                        perror("Errore riacquisizione postazione dopo pausa");
                        break;
                    }

                    // printf("[Operatore %d] Fine pausa, rientrato alla postazione %d\n",indice, postazione_idx);
                }
            }
        }

        //--------------------------------------------------------------------------------------//
        //----------------------GESTIONE COMUNICAZIONI----------------------------------------- //
        //--------------------------------------------------------------------------------------//

        // ---------------- GESTIONE PRIMI ----------------
        if (strcmp(stazione, "PRIMI") == 0)
        {
            if (gestisci_stazione_distribuzione(stazione, msgid, semid, shmid, conf.NOF_WK_SEATS_PRIMI, avg, range, conf.N_NANO_SECS) == -1)
            {
                break;
            }
        }
        // ---------------- GESTIONE SECONDI ----------------
        else if (strcmp(stazione, "SECONDI") == 0)
        {
            if (gestisci_stazione_distribuzione(stazione, msgid, semid, shmid, conf.NOF_WK_STEATS_SECONDI, avg, range, conf.N_NANO_SECS) == -1)
            {
                break;
            }
        }
        // ---------------- GESTIONE COFFEE ----------------
        else if (strcmp(stazione, "COFFEE") == 0)
        {
            if (gestisci_stazione_distribuzione(stazione, msgid, semid, shmid, 0, avg, range, conf.N_NANO_SECS) == -1)
            {
                break;
            }
        }
        // ---------------- GESTIONE CASSA ----------------
        else if (strcmp(stazione, "CASSA") == 0)
        {
            if (gestisci_cassa(msgid,semid,shmid,conf.NOF_WK_SEATS_CASSA,&conf,avg,range,conf.N_NANO_SECS) == -1)
            {
                break;
            }
        }
    }

    if (libera_semaforo(semid, (unsigned short)postazione_idx) == -1)
    {
        perror("Errore rilascio finale postazione");
    }

    return 0;
}
