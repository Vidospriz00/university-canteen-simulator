/*
 * IT: Simula il percorso di un utente tra le stazioni e le risorse condivise.
 * EN: Simulates a customer moving between stations and using shared resources.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#define _POSIX_C_SOURCE 200809L
#include "utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <time.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <errno.h>
#include <string.h>
#include <bits/time.h>

typedef struct {
    int variazione;
} AggiornamentoCoda;

static void registra_variazione_coda(Statistiche *statistiche, const void *dato)
{
    const AggiornamentoCoda *aggiornamento = dato;
    statistiche->utenti_in_attesa += aggiornamento->variazione;
    if (statistiche->utenti_in_attesa > statistiche->picco_utenti_in_attesa)
    {
        statistiche->picco_utenti_in_attesa = statistiche->utenti_in_attesa;
    }
}

static void aggiorna_coda(int variazione)
{
    AggiornamentoCoda aggiornamento = {variazione};
    if (aggiorna_statistica(registra_variazione_coda, &aggiornamento) == -1)
    {
        perror("aggiorna utenti in attesa");
        exit(EXIT_FAILURE);
    }
}

static void attendi_minuti_simulati(long minuti, long n_nano_secs)
{
    long long ns_totali = (long long)minuti * n_nano_secs;
    struct timespec ts;
    ts.tv_sec = ns_totali / 1000000000LL;
    ts.tv_nsec = ns_totali % 1000000000LL;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

typedef struct {
    int stazione;
    long durata_ns;
} AggiornamentoAttesa;

static void registra_attesa(Statistiche *statistiche, const void *dato)
{
    const AggiornamentoAttesa *attesa = dato;
    statistiche->attesa_totale_ns += attesa->durata_ns;
    statistiche->attesa_per_stazione_ns[attesa->stazione] += attesa->durata_ns;
    statistiche->accessi_stazione[attesa->stazione]++;
}

static void registra_utente_servito(Statistiche *statistiche, const void *dato)
{
    (void)dato;
    statistiche->utenti_serviti++;
}

static long differenza_ns(const struct timespec *inizio, const struct timespec *fine)
{
    return (fine->tv_sec - inizio->tv_sec) * 1000000000L + fine->tv_nsec - inizio->tv_nsec;
}

int estrai_numero(int min, int max)
{
    if (max <= min)
    {
        return min;
    }
    return min + rand() % (max - min);
}

int estrai_numero_controllato(int min, int max, int *flag)
{
    int numero = -1;

    if (flag != NULL)
    {
        do
        {
            numero = estrai_numero(min, max);
        } while (flag[numero] == 1);
    }
    else
    {
        numero = estrai_numero(min, max);
    }

    return numero;
}

/// @brief conta le celle dell'array che hanno valore 1
/// @param arr array
/// @param length lunghezza dell'array
/// @return restituisce il numero delle celle con valore 1
int conta_flag(int *arr, int length)
{
    int i = 0;
    int contatore_flag = 0;

    while (i < length)
    {
        if (arr[i] == 1)
        {
            contatore_flag++;
            ;
        }
        i++;
    }

    return contatore_flag;
}

// ritorna il numero del piatto scelto dall'utente, -1 se nessun piatto nella Stazione è disponibile
int ordina_piatto_stazione(int piatto, int numero_piatti_stazione, int id_coda_messaggi, int indice_stazione)
{
    struct timespec inizio_attesa;
    clock_gettime(CLOCK_MONOTONIC, &inizio_attesa);
    int *tentativi_ordine_piatti = calloc(numero_piatti_stazione, sizeof(int));
    // int piatto_scelto = estrai_numero_controllato(0, numero_piatti_stazione, tentativi_ordine_piatti);
    int piatti_non_disponibili = 0; // per il controllo dei piatti ordinati che non sono disponibli

    Comanda ordine_utente;
    Comanda messaggio_operatore;

    ordine_utente.mtype = 1;
    ordine_utente.tipo_richiesta = 1;
    ordine_utente.n_piatto = piatto;
    ordine_utente.utente = getpid(); // manda il suo processID
                                     //'- sizeof(long)' per togliere mtype dal messaggio inviato
    ordine_utente.fine_comunicazione = 0;

    if (msgsnd(id_coda_messaggi, &ordine_utente, sizeof(Comanda) - sizeof(long), 0) == -1)
    { // metto 0 così il processo apsetta che si livera spazio nella coda se è piena
        perror("Errore msgsnd processo Utente");
        free(tentativi_ordine_piatti);
        return -1;
    }
    aggiorna_coda(1);
    // 1 = perchè mette mtype a 1 (aspetta una risposta dal primo operatore libero, qualunque sia)
    // 0 perchè aspetta la risposta
    if (msgrcv(id_coda_messaggi, &messaggio_operatore, sizeof(Comanda) - sizeof(long), getpid(), 0) == -1)
    {
        aggiorna_coda(-1);
        perror("Errore msgrcv utente");
        free(tentativi_ordine_piatti);
        return -1;
    }

    // in caso il piatto richiesto dall'utente non è disponibile, se nessun piatto è disponibile l'utente passa alla stazione successiva
    if (messaggio_operatore.disponibile == 0)
    { // il piatto richiesto non è disponibile
        tentativi_ordine_piatti[ordine_utente.n_piatto] = 1;
    }
    else
    {
        // printf("[Utente %d] Utente prende piatto con indice %d\n", getpid(), ordine_utente.n_piatto);
    }

    // quando l'utente ha provato ad ordine tutti i piatti per quella stazione ma nessuno è disponibile
    int flag_fine_ordine = 0;

    while (messaggio_operatore.disponibile == 0 && flag_fine_ordine == 0)
    {

        // //l'utente rilegge il menu per scegliere un'altro piatto

        // if (riserva_semaforo(semid_menu, 0) == -1) {
        //     perror("Errore riserva_semaforo mutex");
        //     exit(EXIT_FAILURE);
        // }

        // //L'utente legge il menu e sceglie i piatti
        // Menu* menu = (Menu*)shmat(shmid_menu,NULL,0);
        // if(menu == (void *)-1){
        //     perror("ERRORE shmat utente");
        //     return NULL;
        // }

        // if (libera_semaforo(semid_menu, 0) == -1) {
        //     perror("Errore libera_semaforo mutex");
        //     exit(EXIT_FAILURE);
        // }

        ordine_utente.mtype = messaggio_operatore.operatore;
        ordine_utente.tipo_richiesta = 1;
        ordine_utente.n_piatto = estrai_numero_controllato(0, numero_piatti_stazione, tentativi_ordine_piatti);
        ordine_utente.utente = getpid(); // manda il suo processID
        ordine_utente.operatore = messaggio_operatore.operatore;

        //'- sizeof(long)' per togliere mtype dal messaggio inviato
        if (msgsnd(id_coda_messaggi, &ordine_utente, sizeof(Comanda) - sizeof(long), 0) == -1)
        { // metto 0 così il processo apsetta che si libera spazio nella coda se è piena
            perror("Errore msgsnd processo utente");
        }

        // 1 = perchè mette mtype a 1 (aspetta una risposta dal primo operatore libero, qualunque sia)
        // 0 perchè aspetta la risposta                                              //comunico con l'operatore che mi ha preso la richiesto
        if (msgrcv(id_coda_messaggi, &messaggio_operatore, sizeof(Comanda) - sizeof(long), getpid(), 0) == -1)
        {
            aggiorna_coda(-1);
            perror("Errore msgrcv processo");
            free(tentativi_ordine_piatti);
            return -1;
        }

        // in caso il piatto richiesto dall'utente non è disponibile, se nessun piatto è disponibile l'utente passa alla stazione successiva
        if (messaggio_operatore.disponibile == 0)
        { // il piatto richiesto non è disponibile
            tentativi_ordine_piatti[ordine_utente.n_piatto] = 1;

            // --- controllo se l'utente ha provato ad ordinare tutti i piatti sul menù ---
            piatti_non_disponibili = conta_flag(tentativi_ordine_piatti, numero_piatti_stazione);

            if (piatti_non_disponibili == numero_piatti_stazione)
            {
                // l'utente termina la comunicazione e libera operatore
                ordine_utente.fine_comunicazione = 1;
                if (msgsnd(id_coda_messaggi, &ordine_utente, sizeof(Comanda) - sizeof(long), 0) == -1)
                { // metto 0 così il processo apsetta che si libera spazio nella coda se è piena
                    perror("Errore msgsnd processo utente");
                }

                flag_fine_ordine = 1;

                ordine_utente.n_piatto = -1;
            }
        }
    }
    aggiorna_coda(-1);
    struct timespec fine_attesa;
    clock_gettime(CLOCK_MONOTONIC, &fine_attesa);
    AggiornamentoAttesa attesa = {indice_stazione, differenza_ns(&inizio_attesa, &fine_attesa)};
    if (aggiorna_statistica(registra_attesa, &attesa) == -1) {
        perror("aggiorna statistica attesa");
    }
    free(tentativi_ordine_piatti);
    return ordine_utente.n_piatto;
}

/// @brief Invia alla cassa il contenuto del vassoio e attende il totale calcolato dal cassiere.
/// @param vassoio_utente Piatti ottenuti dall'utente.
/// @param id_coda_cassa ID della coda di messaggi della cassa.
/// @return Totale pagato, -1 in caso di errore.
int paga_in_cassa(const Vassoio *vassoio_utente, int id_coda_cassa)
{
    struct timespec inizio_attesa;
    clock_gettime(CLOCK_MONOTONIC, &inizio_attesa);
    // crea due messaggi
    // {0} serve per inizializzare tutti i campi a zero
    Comanda richiesta_pagamento = {0};
    Comanda risposta_cassa = {0};

    //mtype = 1  richiesta generica verso un operatore
    richiesta_pagamento.mtype = 1;
    /*tipo_richiesta = 1    ordine normale di un piatto
    tipo_richiesta = 2    pagamento in cassa*/
    richiesta_pagamento.tipo_richiesta = 2;
    richiesta_pagamento.utente = getpid();

    /*
    n_piatto           -> indice del primo preso, oppure -1
    fine_comunicazione -> indice del dolce preso, oppure -1
    disponibile        -> indice del secondo preso, oppure -1
    vuole_caffe        -> indice del caffè preso, oppure -1
    */
    richiesta_pagamento.n_piatto = vassoio_utente->piatto_primo;
    richiesta_pagamento.disponibile = vassoio_utente->piatto_secondo;
    richiesta_pagamento.fine_comunicazione = vassoio_utente->dolce;
    richiesta_pagamento.vuole_caffe = vassoio_utente->caffe;

    //mando il messaggio alla cassa
    //id_coda_cassa = id della coda di messaggi della cassa.
    if (msgsnd(id_coda_cassa, &richiesta_pagamento, sizeof(Comanda) - sizeof(long), 0) == -1)
    {
        perror("Errore msgsnd pagamento utente");
        return -1;
    }
    aggiorna_coda(1);
    if (msgrcv(id_coda_cassa, &risposta_cassa, sizeof(Comanda) - sizeof(long), getpid(), 0) == -1)
    {
        aggiorna_coda(-1);
        perror("Errore msgrcv risposta cassa utente");
        return -1;
    }
    aggiorna_coda(-1);
    struct timespec fine_attesa;
    clock_gettime(CLOCK_MONOTONIC, &fine_attesa);
    AggiornamentoAttesa attesa = {3, differenza_ns(&inizio_attesa, &fine_attesa)};
    if (aggiorna_statistica(registra_attesa, &attesa) == -1) {
        perror("aggiorna statistica attesa cassa");
    }

    return risposta_cassa.totale_pagato;
}

int calcola_tempo_vassoio(Vassoio *vassoio){
    int tempo = 0;
    if(vassoio ->piatto_primo != -1){
        tempo = tempo + estrai_numero(2,5);
    }
    if(vassoio ->piatto_secondo != -1){
        tempo = tempo + estrai_numero(2,5);
    }
    if(vassoio ->dolce != -1){
        tempo = tempo +estrai_numero(2,4);
    }
    if(vassoio ->caffe != -1){
        tempo = tempo +estrai_numero(2,3);
    }
    return tempo;
}


int ottieni_id_coda_messaggi(int key)
{

    int msgid = msgget(key, 0);
    if (msgid == -1)
    {
        perror("Errore msgget utente\n");
        exit(EXIT_FAILURE);
    }

    return msgid;
}

// EN: Customers receive simulation timing and an optional extra-user flag.
int main(int argc, char *argv[])
{
    if (argc < 2)
    {
        fprintf(stderr, "Uso: %s <N_NANO_SECS>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    char *endptr = NULL;
    long n_nano_secs = strtol(argv[1], &endptr, 10);
    if (*argv[1] == '\0' || *endptr != '\0' || n_nano_secs <= 0)
    {
        fprintf(stderr, "N_NANO_SECS non valido: %s\n", argv[1]);
        exit(EXIT_FAILURE);
    }

    int utente_aggiunto = argc >= 3 && strcmp(argv[2], "extra") == 0;

    // per far si che venga generato un numero diverso per ogni processo Utente
    srand(getpid() ^ time(NULL));

    int msgid_primi = ottieni_id_coda_messaggi(KEY_PRIMI);
    int msgid_secondi = ottieni_id_coda_messaggi(KEY_SECONDI);
    int msgid_dolci_caffe = ottieni_id_coda_messaggi(KEY_COFFEE);
    int msgid_cassa = ottieni_id_coda_messaggi(KEY_CASSA);

    // per ottenere l'id del semaforo per poter accedere al menu e scegliere i piatti
    int semid_menu = semget(KEY_MENU, 1, 0);
    int semid_sinc = semget(KEY_SEM_SINC, 1, 0);
    int semid_tavoli = semget(KEY_TAVOLI,1,0);
    if(semid_tavoli == -1){
        perror("semget tavoli");
        exit(EXIT_FAILURE);
    }
    // id della memoria per poter scegliere i piatti dal menu
    int shmid_menu = shmget(KEY_MENU, 0, 0666);

    int numero_piatti_primi = 0;
    int numero_piatti_secondi = 0;
    int numero_dolci = 0;
    int numero_caffe = 0;

    // vassoio in cui vengono registrati i piatti presi dell'utente
    Vassoio vassoio_utente;

    // finita l'inizializzazione, aspetto che gli altri processi siano inzializzati
    if (!utente_aggiunto)
    {
        if (riserva_semaforo(semid_sinc, 0) == -1)
        {
            perror("Errore riserva_semaforo sincronizzazione utente");
            exit(EXIT_FAILURE);
        }

        fflush(stdout);
        attendi_zero(semid_sinc, 0);
    }

        // l'utente decide se recarsi in mensa
    int numero = estrai_numero(1, 11);
    if(numero % 2 == 1){
        printf("L'utente %d non va in mensa\n", getpid());
        exit(EXIT_SUCCESS);
    }


    // ------------------------- SCELTA DEL MENU -------------------------------

    if (riserva_semaforo(semid_menu, 0) == -1)
    {
        perror("riserva_semaforo mutex utente");
        exit(EXIT_FAILURE);
    }

    // L'utente legge il menu e sceglie i piatti
    Menu *menu = (Menu *)shmat(shmid_menu, NULL, 0);
    if (menu == (void *)-1)
    {
        perror("ERRORE shmat utente");
        exit(EXIT_FAILURE);
    }

    vassoio_utente.piatto_primo = estrai_numero(0, menu->N_PRIMI);
    vassoio_utente.piatto_secondo = estrai_numero(0, menu->N_SECONDI);

    // scelta del dolce o del caffè
    if (estrai_numero(0, 2) == 0)
    {
        vassoio_utente.caffe = estrai_numero(0, menu->N_CAFFE);
        vassoio_utente.dolce = -1;
    }
    else
    {
        vassoio_utente.dolce = estrai_numero(0, menu->N_DOLCI);
        vassoio_utente.caffe = -1;
    }

    numero_piatti_primi = menu->N_PRIMI;
    numero_piatti_secondi = menu->N_SECONDI;
    numero_dolci = menu->N_DOLCI;
    numero_caffe = menu->N_CAFFE;

    if (shmdt(menu) == -1)
    {
        perror("ERRORE sgancia menu utente");
        return -1;
    }

    if (libera_semaforo(semid_menu, 0) == -1)
    {
        perror("Errore libera_semaforo mutex");
        exit(EXIT_FAILURE);
    }

    // -------------------------------------------------------------------------

    // --- PRIMI ---
    vassoio_utente.piatto_primo = ordina_piatto_stazione(vassoio_utente.piatto_primo, numero_piatti_primi, msgid_primi, 0);

    // --- SECONDO ---
    vassoio_utente.piatto_secondo = ordina_piatto_stazione(vassoio_utente.piatto_secondo, numero_piatti_secondi, msgid_secondi, 1);

    if (vassoio_utente.piatto_primo == -1 && vassoio_utente.piatto_secondo == -1)
    {
        exit(EXIT_SUCCESS);
    }
    // --- CAFFE E DOLCE ---
    if (vassoio_utente.caffe != -1)
    {
        vassoio_utente.caffe = ordina_piatto_stazione(
            vassoio_utente.caffe,
            numero_caffe,
            msgid_dolci_caffe, 2);
    }
    else
    {
        vassoio_utente.dolce = ordina_piatto_stazione(
            vassoio_utente.dolce,
            numero_dolci,
            msgid_dolci_caffe, 2);
    }

    // --- CASSA ---
    int totale_pagato = paga_in_cassa(&vassoio_utente, msgid_cassa);

    if (totale_pagato == -1)
    {
        exit(EXIT_FAILURE);
    }
    if (aggiorna_statistica(registra_utente_servito, NULL) == -1) {
        perror("aggiorna statistica utente servito");
    }

    printf("[UTENTE %d] vassoio finale -> primo: %d | secondo: %d | dolce: %d | caffe: %d | totale: %d\n",
           getpid(),
           vassoio_utente.piatto_primo,
           vassoio_utente.piatto_secondo,
           vassoio_utente.dolce,
           vassoio_utente.caffe,
           totale_pagato);
    int tempo_consumo = calcola_tempo_vassoio(&vassoio_utente);
    if(riserva_semaforo(semid_tavoli,0) == -1){
        perror("riserva sem tavoli utente");
        exit(EXIT_FAILURE);
    }
    attendi_minuti_simulati(tempo_consumo, n_nano_secs);
    if(libera_semaforo(semid_tavoli,0) == -1){
        perror("libera semaforo tavoli");
        exit(EXIT_FAILURE);
    }

    //printf("[UTENTE %d] ha ordinato piatto primo: %d\npiatto secondo: %d\n", getpid(), vassoio_utente.piatto_primo, vassoio_utente.piatto_secondo);

    exit(EXIT_SUCCESS);
}
