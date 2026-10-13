/*
 * IT: Definisce le strutture dati condivise e gli helper per le risorse IPC.
 * EN: Defines shared data structures and IPC resource helpers.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

//Costanti di configurazione
// 1. Controlla se UTILITIES_H NON è ancora stato definito
#ifndef UTILITIES_H  
// 2. Se non lo è, definiscilo ora
#define UTILITIES_H
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <errno.h>
#include <stddef.h>


//Costanti di configurazione
typedef struct
{
    //tempo medio erogazione di primi
    int AVG_SRVC_PRIMI;
    //tempo medio erogazione dei secondi
    int AVG_SRVC_MAIN_COURSE;
    //tempo medio erogazione di caffe
    int AVG_SRVC_COFFEE;
    //tempo medio erogazione cassa
    int AVG_SRVC_CASSA;
    // Numero postazioni stazione Primi
    int NOF_WK_SEATS_PRIMI;
    // Numero postazioni stazione Secondi
    int NOF_WK_STEATS_SECONDI;
    // Numero postazioni stazione Caffe e Dolci
    int NOF_WK_STEATS_COFFEE;
    // Numero postazioni stazione cassa
    int NOF_WK_SEATS_CASSA;
    //Numero di risorse di tipo stazione_refezione (tavoli)
    int NOF_TABLE_SEATS;
    //Duranta (in giorni) della simulazione
    int SIM_DURATION;
    //Durata di ogni minuti della simulazione (in nanosecondi)
    int N_NANO_SECS;
    //Numero di utenti
    int NOF_USERS;
    int N_NEW_USERS;
    //Numero di porzioni di primi disponibili
    int AVG_REFILL_PRIMI;
    //Numero di porzioni di secondi disponibili
    int AVG_REFILL_SECONDI;
    int AVG_REFILL_TIME;
    //Numero massimo di porzioni di secondi disponibili
    int MAX_PORZIONI_PRIMI;
    //Numero massimo di porzioni di secondi disponibili
    int MAX_PORZIONI_SECONDI;
    //tempo pausa
    int NOF_PAUSE;
    //prezzo primi
    int PRICE_PRIMI;
    //prezzo secondi
    int PRICE_SECONDI;
    //prezzo caffe
    int PRICE_COFFEE;
    //Numero massimo di utenti in attesa
    int OVERLOAD_THRESHOLD;
    // numero processi operatore
    int NOF_WORKERS;
}Configurazione;

typedef struct{
    char** primi;
    char** secondi;
    char** dolci;
    char** caffe;
    int N_PRIMI;
    int N_SECONDI;
    int N_DOLCI;
    int N_CAFFE;
}Menu;

typedef struct{
   
    //porzioni messe a disposizione dagli operatori
    int* porzioni_disponibili;
    int* max_porzioni;
    int* porzioni_consumate;
    int n_piatti;
    time_t ultimo_refill;
}Stazione_Distribuzione;


// union , per inizializzare i semafori (responsabile_mensa riga 47)
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
};


static int riserva_semaforo(int semid, unsigned short sem_num) {
    struct sembuf op;

    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;

    return semop(semid, &op, 1); //primo parrametro : id del set semafori
                                 //numero del semaforo del set
                                // 1 per eseguire un operazione sul semaforo in questo caso decrementare
}

/// @brief Libera una risorsa/postazione (+1) su un semaforo del set.
/// @param semid ID del set di semafori.
/// @param sem_num Indice del semaforo nel set.
/// @return 0 se l'operazione va a buon fine, -1 in caso di errore.
static int libera_semaforo(int semid, unsigned short sem_num) {
    struct sembuf op;

    op.sem_num = sem_num;
    op.sem_op = +1;
    op.sem_flg = 0;

    return semop(semid, &op, 1);
}

// Questa funzione blocca il processo finché il semaforo non è esattamente 0
static int attendi_zero(int semid, unsigned short sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 0;    // Operazione: Aspetta che diventi zero
    op.sem_flg = 0;
    return semop(semid, &op, 1);
}


//CODE DI MESSAGGI 
typedef struct mymsg {
    long mtype;      // 1 convenzione
    int tipo_richiesta; // 1 = servizio stazione, 2 = pagamento cassa
    int n_piatto;
    int disponibile; // Disponibilta del piatto (1 = disponibile)(0 = non disponibile)
    int vuole_caffe;
    int totale_pagato;
    int fine_comunicazione; //la comunicazione tra utente e operatore finisce se non ce nessun piatto scelto dall utente
    pid_t utente;
    pid_t operatore;
}Comanda;

//STATISTICHE
typedef struct statistiche{
    long utenti_serviti;
    long piatti_distribuiti[3]; /* primi, secondi-contorni, dolci-caffe */
    long piatti_avanzati[3];
    long attesa_totale_ns;
    long attesa_per_stazione_ns[4]; /* primi, secondi, coffee, cassa */
    long accessi_stazione[4];
    long operatori_attivi;
    long pause_effettuate;
    long utenti_in_attesa;
    long picco_utenti_in_attesa;
    long ricavo;
}Statistiche;

//KEY
#define KEY_MENU 1208090625
#define KEY_PRIMI 1090650113
#define KEY_SECONDI 1107427329
#define KEY_COFFEE 1124204545
#define KEY_DOLCI 1140981761
#define KEY_CASSA 1258422273
#define KEY_TAVOLI 1237989892
#define KEY_SEM_SINC 1275199489
#define KEY_SEM_VIA ftok("/tmp", 'V')
#define KEY_STATISTICHE 1286730591
#define KEY_SEM_STATISTICHE 1286730592

//Vassoio con i piatti presi dall'utente
typedef struct vassoio{
    int piatto_primo;
    int piatto_secondo;
    int dolce;
    int caffe;
}Vassoio;

typedef struct Stazione_refezione{
    int posti_totali; //noftable
    int posti_occupati;
}Stazione_refezione;

// Aggiorna una statistica condivisa
static int aggiorna_statistica(void (*aggiorna)(Statistiche *, const void *), const void *dato)
{
    int shmid = shmget(KEY_STATISTICHE, sizeof(Statistiche), 0);
    int semid = semget(KEY_SEM_STATISTICHE, 1, 0);
    if (shmid == -1 || semid == -1) {
        return -1;
    }
    if (riserva_semaforo(semid, 0) == -1) {
        return -1;
    }
    Statistiche *statistiche = (Statistiche *)shmat(shmid, NULL, 0);
    if (statistiche == (void *)-1) {
        (void)libera_semaforo(semid, 0);
        return -1;
    }
    aggiorna(statistiche, dato);
    int risultato = shmdt(statistiche);
    if (libera_semaforo(semid, 0) == -1) {
        return -1;
    }
    return risultato;
}

#endif
