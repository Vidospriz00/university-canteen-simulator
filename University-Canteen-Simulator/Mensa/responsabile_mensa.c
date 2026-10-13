/*
 * IT: Coordina la simulazione, le risorse IPC, i processi e la raccolta delle statistiche.
 * EN: Coordinates simulation lifecycle, IPC resources, worker processes, and statistics collection.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#include "../Operazioni_file/operazioni_file.h"
#include "utilities.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/shm.h>
#include <string.h>
#include <sys/sem.h>
#include <sys/msg.h>
#include <signal.h>
#include <errno.h>

#define MINUTI_GIORNO_SIMULATO 1440

static void attendi_minuti_simulati(long minuti, long n_nano_secs)
{
    long long ns_totali = (long long)minuti * n_nano_secs;
    struct timespec ts;
    ts.tv_sec = ns_totali / 1000000000LL;
    ts.tv_nsec = ns_totali % 1000000000LL;
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR) { }
}

static int tempo_refill_casuale(int media)
{
    int minimo = media * 80 / 100;
    int massimo = media * 120 / 100;

    if (minimo < 1)
        minimo = 1;

    if (massimo < minimo)
        massimo = minimo;

    return minimo + rand() % (massimo - minimo + 1);
}

#define N_STAZIONI 4
const char *STAZIONI[N_STAZIONI] = {"PRIMI", "SECONDI", "COFFEE", "CASSA"};

// Funzione per convertire in char
void int_to_str(int value, char *buffer, size_t size)
{
    snprintf(buffer, size, "%d", value);
}

// ------------------------ DEALLOCAZIONE ------------------------ //

void libera_array(char **arr,int n){
    if(!arr){
        return;
    }
    for(int i = 0; i < n;i ++){
        free(arr[i]);
    } 
    free(arr);
}

void libera_menu(Menu *menu){
    if(!menu){
        return;
    }
    libera_array(menu->primi,menu->N_PRIMI);
    libera_array(menu->secondi,menu->N_SECONDI);
    libera_array(menu->caffe,menu->N_CAFFE);
    libera_array(menu->dolci,menu->N_DOLCI);

    free(menu);
}

//DEBUG
void rimuovi_coda_messaggi(key_t key)
{
    int id = msgget(key, 0);

    if (id != -1)
    {
        msgctl(id, IPC_RMID, NULL);
    }
}

void rimuovi_semaforo(key_t key)
{
    int id = semget(key, 1, 0);

    if (id != -1)
    {
        semctl(id, 0, IPC_RMID);
    }
}

void rimuovi_memoria_condivisa(key_t key)
{
    int id = shmget(key, 1, 0);

    if (id != -1)
    {
        shmctl(id, IPC_RMID, NULL);
    }
}

//manca la deallocazione di Menu*
//questa è la funzione completa da usare nel main in caso di problema, per la deallcoazione di tutte le risorse ipc create
void dealloca_risorse_ipc() 
{

    rimuovi_coda_messaggi(KEY_PRIMI);
    rimuovi_coda_messaggi(KEY_SECONDI);
    rimuovi_coda_messaggi(KEY_COFFEE);
    rimuovi_coda_messaggi(KEY_CASSA);

    rimuovi_semaforo(KEY_PRIMI);
    rimuovi_semaforo(KEY_SECONDI);
    rimuovi_semaforo(KEY_COFFEE);
    rimuovi_semaforo(KEY_CASSA);
    rimuovi_semaforo(KEY_MENU);
    rimuovi_semaforo(KEY_TAVOLI);
    rimuovi_semaforo(KEY_SEM_SINC);
    rimuovi_semaforo(KEY_SEM_STATISTICHE);

    rimuovi_memoria_condivisa(KEY_MENU);
    rimuovi_memoria_condivisa(KEY_PRIMI);
    rimuovi_memoria_condivisa(KEY_SECONDI);
    rimuovi_memoria_condivisa(KEY_COFFEE);
    rimuovi_memoria_condivisa(KEY_DOLCI);
    rimuovi_memoria_condivisa(KEY_CASSA);
    rimuovi_memoria_condivisa(KEY_STATISTICHE);
}

// Funzione refill
void refill_stazione(int shmid, int semid, unsigned short m_idx, int quantita_refill)
{
    Stazione_Distribuzione *stazione = (Stazione_Distribuzione *)shmat(shmid, NULL, 0);
    if (stazione == (void *)-1)
    {
        perror("shmat refill stazione");
        return;
    }
    if (riserva_semaforo(semid, m_idx) == -1)
    {
        perror("Riserva semaforo refill");
        shmdt(stazione);
        return;
    }

    stazione->max_porzioni = (int *)((char *)stazione + sizeof(Stazione_Distribuzione));
    stazione->porzioni_consumate = stazione->max_porzioni + stazione->n_piatti;
    stazione->porzioni_disponibili = stazione->porzioni_consumate + stazione->n_piatti;

    for (int i = 0; i < stazione->n_piatti; i++)
    {
        stazione->porzioni_disponibili[i] += quantita_refill;
        if (stazione->porzioni_disponibili[i] > stazione->max_porzioni[i])
        {
            stazione->porzioni_disponibili[i] = stazione->max_porzioni[i];
        }
    }
    stazione->ultimo_refill = time(NULL);
    if (libera_semaforo(semid, m_idx) == -1)
    {
        perror("libera_semaforo,refill");
    }
    if (shmdt(stazione) == -1)
    {
        perror("shmdt refill stazione");
    }
}

/// @brief Crea una stazione di distribuzione
/// @param n_piatti Numero di piatti nel menù
/// @param max_porzioni Numero massimo di porzioni
/// @param n_porzioni_refill Numero di porzioni disponibili per gli utenti
/// @return Puntatore alla stazione di distribuzione creata
void crea_Stazione_Distribuzione(Stazione_Distribuzione **destinazione, int n_piatti, int max_porzioni, int n_porzioni_refill)
{
    (*destinazione)->n_piatti = n_piatti;
    (*destinazione)->ultimo_refill = time(NULL);

    (*destinazione)->max_porzioni = (int *)((char *)(*destinazione) + sizeof(Stazione_Distribuzione));

    (*destinazione)->porzioni_consumate = (*destinazione)->max_porzioni + n_piatti;

    (*destinazione)->porzioni_disponibili = (*destinazione)->porzioni_consumate + n_piatti;

    for (int i = 0; i < n_piatti; i++)
    {
        (*destinazione)->max_porzioni[i] = max_porzioni;
        (*destinazione)->porzioni_consumate[i] = 0;
        (*destinazione)->porzioni_disponibili[i] = n_porzioni_refill;
    }
}


//-------------------------------- CREAZIONE RISORSE IPC ----------------------------

// --- CODE DI MESSAGGI ---

int crea_coda(key_t key, const char *nome)
{
    int msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1)
    {
        perror(nome);
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }
    return msgid;
}
// ------

// --- SEMAFORI ---

void inizializza_valori_semafori(int n_semafori_set, int semid, int valore)
{
    unsigned short *valori = malloc(n_semafori_set * sizeof(unsigned short));
    if (valori == NULL)
    {
        perror("malloc valori semafori");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < n_semafori_set; i++)
    {
        valori[i] = valore;
    }

    union semun arg;
    arg.array = valori;

    if (semctl(semid, 0, SETALL, arg) == -1)
    {
        perror("inizializzazione semafori\n");
        free(valori);
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    free(valori);
}

//restituisce id semaforo (per ora)
int crea_semaforo(key_t key, int numero_semafori_set, int valore_default_semafori){
    int semid = semget(key, numero_semafori_set, IPC_CREAT | 0666);

    if (semid == -1)
    {
        perror("Creazione semaforo\n");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    inizializza_valori_semafori(numero_semafori_set, semid, valore_default_semafori);
    return semid;
}
// ------

// --- MEMORIA CONDIVISA ---

// Crea (o recupera) un segmento condiviso e lo allega, restituendo il puntatore
int crea_e_allega_shm(key_t key, size_t size, void** ptr)
{
    int shmid = shmget(key, size, IPC_CREAT | 0666);
    if (shmid == -1)
    {
        perror("shmget");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    *ptr = shmat(shmid, NULL, 0);
    if (*ptr == (void*)-1)
    {
        perror("shmat");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    return shmid;
}

void stacca_shm(void* shm_ptr, const char* nome_segmento)
{
    if (shmdt(shm_ptr) == -1)
    {
        fprintf(stderr, "Errore nella shmdt di %s\n", nome_segmento);
        perror("shmdt");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }
}

// -----

//---------------------------------------------------------------------------------

void crea_piano_operatori(const Configurazione *conf, const char **piano_operatori)
{
    int tempi_medi[N_STAZIONI] = {
        conf->AVG_SRVC_PRIMI,
        conf->AVG_SRVC_MAIN_COURSE,
        conf->AVG_SRVC_COFFEE,
        conf->AVG_SRVC_CASSA
    };

    int operatori_assegnati[N_STAZIONI] = {0};

    if (conf->NOF_WORKERS < N_STAZIONI)
    {
        printf("Numero di operatori insufficiente.\n");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    if (conf->NOF_WK_SEATS_PRIMI <= 0 ||
        conf->NOF_WK_STEATS_SECONDI <= 0 ||
        conf->NOF_WK_STEATS_COFFEE <= 0 ||
        conf->NOF_WK_SEATS_CASSA <= 0)
    {
        fprintf(stderr, "ogni stazione deve avere almeno una postazione.\n");
        dealloca_risorse_ipc();
        exit(EXIT_FAILURE);
    }

    for (int i = 0; i < N_STAZIONI; i++)
    {
        if (tempi_medi[i] <= 0)
        {
            fprintf(stderr,
                    "Errore: tempo medio non valido per la stazione %s.\n",
                    STAZIONI[i]);
                    dealloca_risorse_ipc();
            exit(EXIT_FAILURE);
        }
    }

    //assegno almeno un operatore a ogni stazione.
    for (int i = 0; i < N_STAZIONI; i++)
    {
        piano_operatori[i] = STAZIONI[i];
        operatori_assegnati[i] = 1;
    }

    //le stazioni più lente ricevono più operatori,
    for (int op = N_STAZIONI; op < conf->NOF_WORKERS; op++)
    {
        int migliore = 0;

        for (int i = 1; i < N_STAZIONI; i++)
        {
            int valore_i = tempi_medi[i] * operatori_assegnati[migliore];
            int valore_migliore = tempi_medi[migliore] * operatori_assegnati[i];

            if (valore_i > valore_migliore)
            {
                migliore = i;
            }
        }

        piano_operatori[op] = STAZIONI[migliore];
        operatori_assegnati[migliore]++;
    }

    printf("\n=== Piano assegnazione operatori ===\n");
    for (int i = 0; i < N_STAZIONI; i++)
    {
        printf("%s -> %d operatori\n", STAZIONI[i], operatori_assegnati[i]);
    }
    printf("===================================\n\n");
}

//----------------------------------------------------------------------------------------------
//SIM DURATION

//azzera una stazione all'inizio di ogni giornata
static void azzera_stazione_giornaliera(int shmid, int semid, unsigned short mutex_idx, int porzioni_iniziali)
{
    Stazione_Distribuzione *stazione = shmat(shmid, NULL, 0);
    if (stazione == (void *)-1)
    {
        perror("shmat azzera stazione");
        exit(EXIT_FAILURE);
    }

    if (riserva_semaforo(semid, mutex_idx) == -1)
    {
        perror("riserva mutex azzera stazione");
        shmdt(stazione);
        exit(EXIT_FAILURE);
    }

    stazione->max_porzioni = (int *)((char *)stazione + sizeof(Stazione_Distribuzione));
    stazione->porzioni_consumate = stazione->max_porzioni + stazione->n_piatti;
    stazione->porzioni_disponibili = stazione->porzioni_consumate + stazione->n_piatti;

    for (int i = 0; i < stazione->n_piatti; i++)
    {
        stazione->porzioni_disponibili[i] = porzioni_iniziali;
        stazione->porzioni_consumate[i] = 0;
    }
    stazione->ultimo_refill = time(NULL);

    if (libera_semaforo(semid, mutex_idx) == -1)
    {
        perror("libera mutex azzera stazione");
        shmdt(stazione);
        exit(EXIT_FAILURE);
    }

    if (shmdt(stazione) == -1)
    {
        perror("shmdt azzera stazione");
        exit(EXIT_FAILURE);
    }
}

static void azzera_contatore_condiviso(int shmid, const char *nome)
{
    int *valore = shmat(shmid, NULL, 0);
    if (valore == (void *)-1)
    {
        perror(nome);
        exit(EXIT_FAILURE);
    }

    *valore = 0;

    if (shmdt(valore) == -1)
    {
        perror("shmdt contatore condiviso");
        exit(EXIT_FAILURE);
    }
}

static void azzera_statistiche(int shmid, int semid)
{
    Statistiche *statistiche = shmat(shmid, NULL, 0);
    if (statistiche == (void *)-1) {
        perror("shmat statistiche");
        exit(EXIT_FAILURE);
    }
    if (riserva_semaforo(semid, 0) == -1) {
        perror("mutex statistiche");
        shmdt(statistiche);
        exit(EXIT_FAILURE);
    }
    memset(statistiche, 0, sizeof(*statistiche));
    if (libera_semaforo(semid, 0) == -1 || shmdt(statistiche) == -1) {
        perror("reset statistiche");
        exit(EXIT_FAILURE);
    }
}

static Statistiche leggi_statistiche(int shmid, int semid)
{
    Statistiche copia = {0};
    Statistiche *statistiche = shmat(shmid, NULL, 0);
    if (statistiche == (void *)-1 || riserva_semaforo(semid, 0) == -1) {
        perror("lettura statistiche");
        exit(EXIT_FAILURE);
    }
    copia = *statistiche;
    if (libera_semaforo(semid, 0) == -1 || shmdt(statistiche) == -1) {
        perror("rilascio statistiche");
        exit(EXIT_FAILURE);
    }
    return copia;
}

static void somma_statistiche(Statistiche *totali, const Statistiche *giornaliere)
{
    totali->utenti_serviti += giornaliere->utenti_serviti;
    totali->operatori_attivi += giornaliere->operatori_attivi;
    totali->pause_effettuate += giornaliere->pause_effettuate;
    totali->ricavo += giornaliere->ricavo;
    totali->attesa_totale_ns += giornaliere->attesa_totale_ns;
    for (int i = 0; i < 3; i++) {
        totali->piatti_distribuiti[i] += giornaliere->piatti_distribuiti[i];
        totali->piatti_avanzati[i] += giornaliere->piatti_avanzati[i];
    }
    for (int i = 0; i < 4; i++) {
        totali->attesa_per_stazione_ns[i] += giornaliere->attesa_per_stazione_ns[i];
        totali->accessi_stazione[i] += giornaliere->accessi_stazione[i];
    }
    totali->utenti_in_attesa = giornaliere->utenti_in_attesa;
    if (giornaliere->picco_utenti_in_attesa > totali->picco_utenti_in_attesa)
    {
        totali->picco_utenti_in_attesa = giornaliere->picco_utenti_in_attesa;
    }
}

static double attesa_media_secondi_simulati(long attesa_ns, long accessi, long n_nano_secs)
{
    if (accessi == 0 || n_nano_secs <= 0)
        return 0.0;

    return ((double)attesa_ns / (double)accessi) * 60.0 / (double)n_nano_secs;
}

static void scrivi_statistiche_csv(FILE *file_csv, const char *tipo_record, int giorno, const Statistiche *s, int giorni, long n_nano_secs);

static void stampa_statistiche(const char *titolo, const Statistiche *s, int giorni, long n_nano_secs)
{
    static const char *nomi_stazioni[] = {"primi", "secondi-contorni", "dolci-caffe", "cassa"};
    long piatti = s->piatti_distribuiti[0] + s->piatti_distribuiti[1] + s->piatti_distribuiti[2];
    long avanzati = s->piatti_avanzati[0] + s->piatti_avanzati[1] + s->piatti_avanzati[2];
    long accessi = s->accessi_stazione[0] + s->accessi_stazione[1] + s->accessi_stazione[2] + s->accessi_stazione[3];
    printf("\n=== STATISTICHE %s ===\n", titolo);
    printf("Utenti serviti: %ld (media/giorno: %.2f)\n", s->utenti_serviti, (double)s->utenti_serviti / giorni);
    printf("Piatti distribuiti: %ld [primi=%ld, secondi=%ld, dolci-caffe=%ld] (media/giorno: totale=%.2f, primi=%.2f, secondi=%.2f, dolci-caffe=%.2f)\n", piatti, s->piatti_distribuiti[0], s->piatti_distribuiti[1], s->piatti_distribuiti[2], (double)piatti / giorni, (double)s->piatti_distribuiti[0] / giorni, (double)s->piatti_distribuiti[1] / giorni, (double)s->piatti_distribuiti[2] / giorni);
    printf("Piatti avanzati: %ld [primi=%ld, secondi=%ld, dolci-caffe=%ld] (media/giorno: totale=%.2f, primi=%.2f, secondi=%.2f, dolci-caffe=%.2f)\n", avanzati, s->piatti_avanzati[0], s->piatti_avanzati[1], s->piatti_avanzati[2], (double)avanzati / giorni, (double)s->piatti_avanzati[0] / giorni, (double)s->piatti_avanzati[1] / giorni, (double)s->piatti_avanzati[2] / giorni);
    printf("Attesa media complessiva: %.2f secondi simulati\n", attesa_media_secondi_simulati(s->attesa_totale_ns, accessi, n_nano_secs));
    for (int i = 0; i < 4; i++) {
        printf("Attesa media %s: %.2f secondi simulati\n", nomi_stazioni[i], attesa_media_secondi_simulati(s->attesa_per_stazione_ns[i], s->accessi_stazione[i], n_nano_secs));
    }
    printf("Operatori attivi: %ld | pause: %ld (media/giorno: %.2f)\n", s->operatori_attivi, s->pause_effettuate, (double)s->pause_effettuate / giorni);
    printf("Ricavo: %ld (media/giorno: %.2f)\n", s->ricavo, (double)s->ricavo / giorni);
    printf("Utenti in attesa a fine giornata: %ld | picco utenti in attesa: %ld\n", s->utenti_in_attesa, s->picco_utenti_in_attesa);
}
static void svuota_coda(int msgid)
{
    Comanda messaggio;

    while (msgrcv(msgid, &messaggio, sizeof(Comanda) - sizeof(long), 0, IPC_NOWAIT) != -1)
    {
    }

    if (errno != ENOMSG)
    {
        perror("svuota coda");
        exit(EXIT_FAILURE);
    }
}

int main()
{
    // EN: Load runtime configuration; defaults to variabili.txt.
    // LETTURA VARIABILI CONFIGURAZIONE
    char *config_file = getenv("CONFIG_FILE");
    if (config_file == NULL || config_file[0] == '\0')
    {
        config_file = "./variabili.txt";
    }
    Configurazione conf = {0};
    leggiCostanti(config_file, &conf);

    if (conf.AVG_REFILL_TIME <= 0 ||
        conf.N_NEW_USERS < 0 ||
        conf.N_NANO_SECS <= 0 ||
        conf.SIM_DURATION <= 0 ||
        conf.NOF_USERS <= 0 ||
        conf.NOF_WORKERS < N_STAZIONI ||
        conf.NOF_TABLE_SEATS <= 0 ||
        conf.OVERLOAD_THRESHOLD < 0 ||
        conf.AVG_REFILL_PRIMI <= 0 ||
        conf.AVG_REFILL_SECONDI <= 0 ||
        conf.MAX_PORZIONI_PRIMI < conf.AVG_REFILL_PRIMI ||
        conf.MAX_PORZIONI_SECONDI < conf.AVG_REFILL_SECONDI ||
        conf.NOF_PAUSE < 0)
    {
        fprintf(stderr, "Configurazione completa non valida\n");
        exit(EXIT_FAILURE);
    }

    // EN: Read and validate the menu before starting the simulation.
    // LETTURA MENU
    Menu *menu = malloc(sizeof(Menu));
    if (!leggiMenu("./menu.txt", &menu))
    {
        libera_menu(menu);
        exit(EXIT_FAILURE);
    }

    if (menu->N_PRIMI < 2 || menu->N_SECONDI < 2 || menu->N_CAFFE < 4 || menu->N_DOLCI < 1)
    {
        fprintf(stderr, "Menu non conforme alla consegna\n");
        libera_menu(menu);
        exit(EXIT_FAILURE);
    }

    srand(time(NULL));

    printf("=== Responsabile Mensa avviato ===\n");
    printf("Operatori: %d\n", conf.NOF_WORKERS);
    printf("Utenti:    %d\n", conf.NOF_USERS);

    // Array dove salviamo il PID di ogni operatore creato.
    // Serve dopo per aspettare che finiscano con waitpid.
    pid_t *pids = malloc(conf.NOF_WORKERS * sizeof(pid_t));
    if (pids == NULL)
    {
        perror("malloc pids");
        free(pids);
        libera_menu(menu);
        exit(EXIT_FAILURE);
    }

    //----------------------------------------------------------------------------------//
    // EN: Set up System V shared-memory segments for concurrent processes.
    //--------------------------CREAZIONE SHARED MEMORY-----------------------------------
    //----------------------------------------------------------------------------------//
    
    // ------------------------ MENU ------------------------
    //dichiaro il segmento di memoria condivisa
    Menu* sh_menu = NULL;
    int shmid_menu = crea_e_allega_shm(KEY_MENU, sizeof(Menu), (void**)&sh_menu);
    
    //riempio il segmento di memoria condivisa
    *sh_menu = *menu;
    // printf("Prova\n");

    // stacco il processo dalla shmemory
    stacca_shm(sh_menu, "menu");
    
    // ------------------------ PRIMI ------------------------

    size_t size_mem_primi = sizeof(Stazione_Distribuzione) + 3 * menu->N_PRIMI * sizeof(int);
    //dichiaro il segmento di memoria condivisa
    Stazione_Distribuzione* shm_primi = NULL;
    int shmid_primi = crea_e_allega_shm(KEY_PRIMI, size_mem_primi, (void**)&shm_primi);
    
    //riempio il segmento di memoria condivisa
    crea_Stazione_Distribuzione(&shm_primi, menu->N_PRIMI, conf.MAX_PORZIONI_PRIMI, conf.AVG_REFILL_PRIMI);

    // stacco il processo dalla shmemory
    stacca_shm(shm_primi, "primi");

    // ------------------------ SECONDI ------------------------
    
    size_t size_mem_secondi = sizeof(Stazione_Distribuzione) + 3 * menu->N_SECONDI * sizeof(int);
    //dichiaro il segmento di memoria condivisa
    Stazione_Distribuzione* shm_secondi = NULL;
    int shmid_secondi = crea_e_allega_shm(KEY_SECONDI, size_mem_secondi, (void**)&shm_secondi);
    
    //riempio il segmento di memoria condivisa
    crea_Stazione_Distribuzione(&shm_secondi, menu->N_SECONDI, conf.MAX_PORZIONI_SECONDI, conf.AVG_REFILL_SECONDI);

    // stacco il processo dalla shmemory
    stacca_shm(shm_secondi, "secondi");

    // ------------------------ DOLCI ------------------------

    //dichiaro il segmento di memoria condivisa
    int* shm_dolci = NULL;
    int shmid_dolci = crea_e_allega_shm(KEY_DOLCI, sizeof(int), (void**)&shm_dolci);
    
    //riempio il segmento di memoria condivisa
    *shm_dolci = 0;

    // stacco il processo dalla shmemory
    stacca_shm(shm_dolci, "dolci");

    // ------------------------ CAFFE ------------------------

    //dichiaro il segmento di memoria condivisa
    int* shm_caffe = NULL;
    int shmid_caffe = crea_e_allega_shm(KEY_COFFEE, sizeof(int), (void**)&shm_caffe);
    
    //riempio il segmento di memoria condivisa
    *shm_caffe = 0;

    // stacco il processo dalla shmemory
    stacca_shm(shm_caffe, "caffe");

    // ------------------------ CASSA ------------------------

    //dichiaro il segmento di memoria condivisa
    int* shm_cassa = NULL;
    int shmid_cassa = crea_e_allega_shm(KEY_CASSA, sizeof(int), (void**)&shm_cassa);
    
    //riempio il segmento di memoria condivisa
    *shm_cassa = 0;

    // stacco il processo dalla shmemory
    stacca_shm(shm_cassa, "cassa");

    // ------------------------ STATISTICHE ------------------------
    Statistiche *shm_statistiche = NULL;
    int shmid_statistiche = crea_e_allega_shm(KEY_STATISTICHE, sizeof(Statistiche), (void **)&shm_statistiche);
    memset(shm_statistiche, 0, sizeof(*shm_statistiche));
    stacca_shm(shm_statistiche, "statistiche");

    //--------------------------------------------------------------------------------------//
    //---------------------------CREAZIONE SEMAFORI-------------------------------------------
    //--------------------------------------------------------------------------------------//

    //--- PRIMI ---
    int semid_primi = crea_semaforo(KEY_PRIMI, conf.NOF_WK_SEATS_PRIMI + 1, 1);

    //--- SECONDI ---
    int semid_secondi = crea_semaforo(KEY_SECONDI, conf.NOF_WK_STEATS_SECONDI + 1, 1);

    //--- CAFFE ---
    int semid_caffe = crea_semaforo(KEY_COFFEE, conf.NOF_WK_STEATS_COFFEE + 1, 1);

    //--- CASSA ---
    int semid_cassa = crea_semaforo(KEY_CASSA, conf.NOF_WK_SEATS_CASSA + 1, 1);

    //--- SINCRONIZZAZIONE Per l'attesa dell'inizializzazione dei processi ---
    int semid_sinc = crea_semaforo(KEY_SEM_SINC, 1, conf.NOF_USERS + conf.NOF_WORKERS);

    //--- MENU ---
    int semid_menu = crea_semaforo(KEY_MENU, 1, 1);

    //--- TAVOLI DA REFEZIONE ---
    int semid_tavoli = crea_semaforo(KEY_TAVOLI, 1, conf.NOF_TABLE_SEATS);

    // Protegge il segmento statistiche, aggiornato da utenti e operatori.
    int semid_statistiche = crea_semaforo(KEY_SEM_STATISTICHE, 1, 1);

    //--------------------------------------------------------------------------------------//
    //---------------------------CREAZIONE CODE DI MESSAGGI----------------------------------
    //--------------------------------------------------------------------------------------//

    int msgid_primi = crea_coda(KEY_PRIMI, "st.Primi");
    int msgid_secondi = crea_coda(KEY_SECONDI, "st.Secondi");
    int msgid_coffee = crea_coda(KEY_COFFEE, "st.Caffe");
    int msgid_cassa = crea_coda(KEY_CASSA, "st.Cassa");

    //--------------------------------------------------------------------------------------//
    //---------------------------CREAZIONE OPERATORI------------------------------------------
    //--------------------------------------------------------------------------------------//

    const char **piano_operatori = malloc(conf.NOF_WORKERS * sizeof(char *));
    if (piano_operatori == NULL)
    {
        perror("malloc piano_operatori");
        dealloca_risorse_ipc();
        free(pids);
        libera_menu(menu);
        exit(EXIT_FAILURE);
    }

    //----------------------------------------------------------------------------------------------------//
    //-----------------------------------------CREAZIONE UTENTI---------------------------------------------
    //----------------------------------------------------------------------------------------------------//

    pid_t *pids_utenti = malloc(conf.NOF_USERS * sizeof(pid_t));
    if (!pids_utenti)
    {
        perror("Malloc fallita");
        free(piano_operatori);
        dealloca_risorse_ipc();
        free(pids);
        libera_menu(menu);
        exit(EXIT_FAILURE);
    }

    //-----------------------------------------------//
    //----------------GIORNATE-----------------------//
    //-----------------------------------------------//

    // + IPC restano create per tutta la simulazione; a ogni giornata vengono solo reinizializzate
    Statistiche statistiche_totali = {0};
    // EN: Persist daily, partial, and final simulation statistics as CSV.
    FILE *file_csv = fopen("statistiche.csv", "w");
    if (file_csv == NULL)
    {
        perror("apertura statistiche.csv");
        dealloca_risorse_ipc();
        free(piano_operatori);
        free(pids);
        free(pids_utenti);
        libera_menu(menu);
        exit(EXIT_FAILURE);
    }
    fprintf(file_csv, "Tipo record;Giorno;Utenti serviti;Media utenti/giorno;Piatti totali;Primi distribuiti;Secondi distribuiti;Dolci-caffe distribuiti;Media piatti/giorno;Media primi/giorno;Media secondi/giorno;Media dolci-caffe/giorno;Avanzi totali;Avanzi primi;Avanzi secondi;Avanzi dolci-caffe;Media avanzi/giorno;Media avanzi primi/giorno;Media avanzi secondi/giorno;Media avanzi dolci-caffe/giorno;Attesa media totale (secondi simulati);Attesa primi (secondi simulati);Attesa secondi (secondi simulati);Attesa dolci-caffe (secondi simulati);Attesa cassa (secondi simulati);Operatori attivi;Pause totali;Media pause/giorno;Ricavo;Ricavo medio/giorno;Utenti in attesa a fine giornata;Picco utenti in attesa\n");

    int terminazione_overload = 0;
    int giorni_eseguiti = 0;

    for (int giorno_corrente = 1;
        giorno_corrente <= conf.SIM_DURATION;
        giorno_corrente++)
    {
        printf("\n=== INIZIO GIORNATA %d ===\n", giorno_corrente);

        // + Elimina eventuali messaggi rimasti dalla giornata precedente
        svuota_coda(msgid_primi);
        svuota_coda(msgid_secondi);
        svuota_coda(msgid_coffee);
        svuota_coda(msgid_cassa);

        // + Ripristina tutte le postazioni mutex 
        inizializza_valori_semafori(
            conf.NOF_WK_SEATS_PRIMI + 1,
            semid_primi,
            1
        );

        inizializza_valori_semafori(
            conf.NOF_WK_STEATS_SECONDI + 1,
            semid_secondi,
            1
        );

        inizializza_valori_semafori(
            conf.NOF_WK_STEATS_COFFEE + 1,
            semid_caffe,
            1
        );

        inizializza_valori_semafori(
            conf.NOF_WK_SEATS_CASSA + 1,
            semid_cassa,
            1
        );

        inizializza_valori_semafori(
            1,
            semid_sinc,
            conf.NOF_USERS + conf.NOF_WORKERS
        );

        inizializza_valori_semafori(
            1,
            semid_menu,
            1
        );

        inizializza_valori_semafori(
            1,
            semid_tavoli,
            conf.NOF_TABLE_SEATS
        );

        // + Ripristina porzioni e contatori giornalieri.
        azzera_stazione_giornaliera(
            shmid_primi,
            semid_primi,
            (unsigned short)conf.NOF_WK_SEATS_PRIMI,
            conf.AVG_REFILL_PRIMI
        );

        azzera_stazione_giornaliera(
            shmid_secondi,
            semid_secondi,
            (unsigned short)conf.NOF_WK_STEATS_SECONDI,
            conf.AVG_REFILL_SECONDI
        );

        azzera_contatore_condiviso(
            shmid_dolci,
            "shmat azzera dolci"
        );

        azzera_contatore_condiviso(
            shmid_caffe,
            "shmat azzera caffe"
        );

        azzera_contatore_condiviso(
            shmid_cassa,
            "shmat azzera cassa"
        );

        azzera_statistiche(shmid_statistiche, semid_statistiche);

        // + rifaccio inizio di ogni giornata
        crea_piano_operatori(&conf, piano_operatori);

        for (int i = 0; i < conf.NOF_WORKERS; i++)
        {
            const char *stazione = piano_operatori[i];

            pid_t pid = fork();

            if (pid < 0)
            {
                perror("Errore fork operatore");

                for (int j = 0; j < i; j++)
                {
                    kill(pids[j], SIGTERM);
                    waitpid(pids[j], NULL, 0);
                }

                free(piano_operatori);
                dealloca_risorse_ipc();
                free(pids);
                free(pids_utenti);
                libera_menu(menu);
                exit(EXIT_FAILURE);
            }

            if (pid == 0)
            {
                char indice_str[16];
                char shmid_str[16];
                char msgid_str[16];
                char semid_str[16];

                int_to_str(i, indice_str, sizeof(indice_str));

                if (strcmp(stazione, "PRIMI") == 0)
                {
                    int_to_str(shmid_primi, shmid_str, sizeof(shmid_str));
                    int_to_str(msgid_primi, msgid_str, sizeof(msgid_str));
                    int_to_str(semid_primi, semid_str, sizeof(semid_str));
                }
                else if (strcmp(stazione, "SECONDI") == 0)
                {
                    int_to_str(shmid_secondi, shmid_str, sizeof(shmid_str));
                    int_to_str(msgid_secondi, msgid_str, sizeof(msgid_str));
                    int_to_str(semid_secondi, semid_str, sizeof(semid_str));
                }
                else if (strcmp(stazione, "COFFEE") == 0)
                {
                    int_to_str(shmid_caffe, shmid_str, sizeof(shmid_str));
                    int_to_str(msgid_coffee, msgid_str, sizeof(msgid_str));
                    int_to_str(semid_caffe, semid_str, sizeof(semid_str));
                }
                else if (strcmp(stazione, "CASSA") == 0)
                {
                    int_to_str(shmid_cassa, shmid_str, sizeof(shmid_str));
                    int_to_str(msgid_cassa, msgid_str, sizeof(msgid_str));
                    int_to_str(semid_cassa, semid_str, sizeof(semid_str));
                }
                else
                {
                    fprintf(stderr, "Errore: stazione non riconosciuta: %s\n", stazione);
                    _exit(EXIT_FAILURE);
                }

                char *args[] = {
                    "./Operatori/operatori",
                    (char *)stazione,
                    indice_str,
                    shmid_str,
                    msgid_str,
                    semid_str,
                    NULL
                };

                execv(args[0], args);

                perror("Errore execv operatore");
                _exit(EXIT_FAILURE);
            }

            pids[i] = pid;

        }

        for (int i = 0; i < conf.NOF_USERS; i++)
        {
            pid_t pid = fork();

            if (pid < 0)
            {
                perror("Errore nella creazioni degli utenti responsabile mensa");

                for (int j = 0; j < i; j++)
                {
                    kill(pids_utenti[j], SIGTERM);
                    waitpid(pids_utenti[j], NULL, 0);
                }

                for (int j = 0; j < conf.NOF_WORKERS; j++)
                {
                    kill(pids[j], SIGTERM);
                    waitpid(pids[j], NULL, 0);
                }

                free(piano_operatori);
                dealloca_risorse_ipc();
                free(pids);
                free(pids_utenti);
                libera_menu(menu);
                exit(EXIT_FAILURE);
            }

            if (pid == 0)
            {
                char n_nano_secs_str[32];
                snprintf(n_nano_secs_str, sizeof(n_nano_secs_str),
                         "%d", conf.N_NANO_SECS);

                char *args[] = {
                    "./Utenti/utente",
                    n_nano_secs_str,
                    NULL
                };

                execv(args[0], args);
                perror("Errore exec");
                _exit(EXIT_FAILURE);
            }

            pids_utenti[i] = pid;
        }

        if (attendi_zero(semid_sinc, 0) == -1)
        {
            perror("attesa inizializzazione processi");
            exit(EXIT_FAILURE);
        }

        pid_t pid_timer = fork();
        if (pid_timer == -1)
        {
            perror("fork timer");
        }
        else if (pid_timer == 0)
        {
            attendi_minuti_simulati(MINUTI_GIORNO_SIMULATO,
                                    conf.N_NANO_SECS);
            _exit(EXIT_SUCCESS);
        }

        pid_t pid_refill = fork();
        if (pid_refill == -1)
        {
            perror("fork refill");
        }
        else if (pid_refill == 0)
        {
            srand(getpid() ^ time(NULL));

            while (1)
            {
                int tempo_refill = tempo_refill_casuale(conf.AVG_REFILL_TIME);
                attendi_minuti_simulati(tempo_refill, conf.N_NANO_SECS);
                refill_stazione(shmid_primi, semid_primi, (unsigned short)conf.NOF_WK_SEATS_PRIMI, conf.AVG_REFILL_PRIMI);
                refill_stazione(shmid_secondi, semid_secondi, (unsigned short)conf.NOF_WK_STEATS_SECONDI, conf.AVG_REFILL_SECONDI);
            }
        }

        /*
         * Il responsabile non si blocca su waitpid().
         * Controlla periodicamente sia gli utenti sia il timer della giornata.
         */
        printf("\n[Responsabile] Monitoraggio utenti e timer giornata...\n");

        int utenti_terminati = 0;
        int giornata_finita = 0;

        while (!giornata_finita)
        {
            /* Controllo non bloccante del timer della giornata. */
            if (pid_timer > 0)
            {
                pid_t risultato_timer = waitpid(pid_timer, NULL, WNOHANG);

                if (risultato_timer == pid_timer)
                {
                    giornata_finita = 1;
                    printf("\n[Responsabile] Tempo della giornata terminato.\n");
                }
                else if (risultato_timer == -1 && errno == ECHILD)
                {
                    giornata_finita = 1;
                }
            }

            /* Controllo non bloccante degli utenti. */
            for (int i = 0; i < conf.NOF_USERS; i++)
            {
                if (pids_utenti[i] <= 0)
                    continue;

                pid_t risultato = waitpid(pids_utenti[i], NULL, WNOHANG);

                if (risultato == pids_utenti[i])
                {
                    pids_utenti[i] = 0;
                    utenti_terminati++;
                }
            }

            /*
             * Evita busy waiting. Questa pausa è brevissima e NON rappresenta
             * il tempo simulato: il tempo della simulazione è gestito dal timer.
             */
            if (!giornata_finita)
            {
                struct timespec pausa_controllo;
                pausa_controllo.tv_sec = 0;
                pausa_controllo.tv_nsec = 1000000; /* 1 ms reale */
                nanosleep(&pausa_controllo, NULL);
            }
        }
        if (giornata_finita)
        {
            Statistiche stato_fine_giornata = leggi_statistiche(shmid_statistiche, semid_statistiche);
            long numero_utenti_attesa = stato_fine_giornata.utenti_in_attesa;

            printf("[Responsabile] Utenti in attesa a fine giornata: %ld\n",
                numero_utenti_attesa);

            if (numero_utenti_attesa > conf.OVERLOAD_THRESHOLD)
            {
                printf("\n====================================\n");
                printf(" TERMINAZIONE PER OVERLOAD\n");
                printf(" Utenti in attesa: %ld\n", numero_utenti_attesa);
                printf(" OVERLOAD_THRESHOLD: %d\n", conf.OVERLOAD_THRESHOLD);
                printf("====================================\n\n");

                terminazione_overload = 1;
            }
        }
        for (int i = 0; i < conf.NOF_USERS; i++)
        {
            if (pids_utenti[i] > 0)
            {
                if (kill(pids_utenti[i], SIGTERM) == -1 && errno != ESRCH)
                {
                    perror("terminazione utente a fine giornata");
                }
                if (waitpid(pids_utenti[i], NULL, 0) == -1 && errno != ECHILD)
                {
                    perror("waitpid utente a fine giornata");
                }
                pids_utenti[i] = 0;
                utenti_terminati++;
            }
        }

        /* La giornata termina: interrompo l'eventuale refill ancora in attesa. */
        if (pid_refill > 0)
        {
            kill(pid_refill, SIGTERM);
            waitpid(pid_refill, NULL, 0);
        }


        // Per ora termino forzatamente gli operatori,
        for (int i = 0; i < conf.NOF_WORKERS; i++)
        {
            kill(pids[i], SIGTERM);
        }

        for (int i = 0; i < conf.NOF_WORKERS; i++)
        {
            waitpid(pids[i], NULL, 0);
        }

        inizializza_valori_semafori(conf.NOF_WK_SEATS_PRIMI + 1, semid_primi, 1);
        inizializza_valori_semafori(conf.NOF_WK_STEATS_SECONDI + 1, semid_secondi, 1);
        inizializza_valori_semafori(conf.NOF_WK_STEATS_COFFEE + 1, semid_caffe, 1);
        inizializza_valori_semafori(conf.NOF_WK_SEATS_CASSA + 1, semid_cassa, 1);

        Statistiche statistiche_giornaliere = leggi_statistiche(shmid_statistiche, semid_statistiche);

        /* Le porzioni rimaste a fine giornata sono gli avanzi. */
        Stazione_Distribuzione *primi = shmat(shmid_primi, NULL, 0);
        Stazione_Distribuzione *secondi = shmat(shmid_secondi, NULL, 0);
        if (primi == (void *)-1 || secondi == (void *)-1) {
            perror("shmat avanzi");
            exit(EXIT_FAILURE);
        }
        primi->max_porzioni = (int *)((char *)primi + sizeof(*primi));
        primi->porzioni_consumate = primi->max_porzioni + primi->n_piatti;
        primi->porzioni_disponibili = primi->porzioni_consumate + primi->n_piatti;
        secondi->max_porzioni = (int *)((char *)secondi + sizeof(*secondi));
        secondi->porzioni_consumate = secondi->max_porzioni + secondi->n_piatti;
        secondi->porzioni_disponibili = secondi->porzioni_consumate + secondi->n_piatti;
        for (int i = 0; i < primi->n_piatti; i++) statistiche_giornaliere.piatti_avanzati[0] += primi->porzioni_disponibili[i];
        for (int i = 0; i < secondi->n_piatti; i++) statistiche_giornaliere.piatti_avanzati[1] += secondi->porzioni_disponibili[i];
        if (shmdt(primi) == -1 || shmdt(secondi) == -1) {
            perror("shmdt avanzi");
            exit(EXIT_FAILURE);
        }
        somma_statistiche(&statistiche_totali, &statistiche_giornaliere);
        stampa_statistiche("GIORNATA", &statistiche_giornaliere, 1, conf.N_NANO_SECS);
        scrivi_statistiche_csv(file_csv, "giornaliera", giorno_corrente, &statistiche_giornaliere, 1, conf.N_NANO_SECS);
        stampa_statistiche("SIMULAZIONE (PARZIALI)", &statistiche_totali, giorno_corrente, conf.N_NANO_SECS);
        scrivi_statistiche_csv(file_csv, "parziale", giorno_corrente, &statistiche_totali, giorno_corrente, conf.N_NANO_SECS);
        fflush(file_csv);
        printf("=== FINE GIORNATA %d ===\n", giorno_corrente);
        giorni_eseguiti = giorno_corrente;

        if (terminazione_overload)
        {
            break;
        }
    }

    scrivi_statistiche_csv(file_csv, "finale", giorni_eseguiti, &statistiche_totali, giorni_eseguiti, conf.N_NANO_SECS);
    if (fclose(file_csv) == EOF)
    {
        perror("chiusura statistiche.csv");
    }

    dealloca_risorse_ipc();
    free(piano_operatori);
    free(pids);
    free(pids_utenti);
    libera_menu(menu);

   stampa_statistiche("FINALI", &statistiche_totali, giorni_eseguiti, conf.N_NANO_SECS);

    if (terminazione_overload)
    {
        printf("=== CAUSA TERMINAZIONE: OVERLOAD ===\n");
    }
    else
    {
        printf("=== CAUSA TERMINAZIONE: TIMEOUT ===\n");
    }

    printf("=== Fine simulazione ===\n");

        return 0;
    }

static void scrivi_statistiche_csv(FILE *file_csv, const char *tipo_record, int giorno, const Statistiche *s, int giorni, long n_nano_secs)
{
    long piatti = s->piatti_distribuiti[0] + s->piatti_distribuiti[1] + s->piatti_distribuiti[2];
    long avanzati = s->piatti_avanzati[0] + s->piatti_avanzati[1] + s->piatti_avanzati[2];
    long accessi = s->accessi_stazione[0] + s->accessi_stazione[1] + s->accessi_stazione[2] + s->accessi_stazione[3];
    int divisore = giorni > 0 ? giorni : 1;

    fprintf(file_csv, "%s;%d;%ld;%.2f;%ld;%ld;%ld;%ld;%.2f;%.2f;%.2f;%.2f;%ld;%ld;%ld;%ld;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%.2f;%ld;%ld;%.2f;%ld;%.2f;%ld;%ld\n",
            tipo_record,
            giorno,
            s->utenti_serviti,
            (double)s->utenti_serviti / divisore,
            piatti,
            s->piatti_distribuiti[0],
            s->piatti_distribuiti[1],
            s->piatti_distribuiti[2],
            (double)piatti / divisore,
            (double)s->piatti_distribuiti[0] / divisore,
            (double)s->piatti_distribuiti[1] / divisore,
            (double)s->piatti_distribuiti[2] / divisore,
            avanzati,
            s->piatti_avanzati[0],
            s->piatti_avanzati[1],
            s->piatti_avanzati[2],
            (double)avanzati / divisore,
            (double)s->piatti_avanzati[0] / divisore,
            (double)s->piatti_avanzati[1] / divisore,
            (double)s->piatti_avanzati[2] / divisore,
            attesa_media_secondi_simulati(s->attesa_totale_ns, accessi, n_nano_secs),
            attesa_media_secondi_simulati(s->attesa_per_stazione_ns[0], s->accessi_stazione[0], n_nano_secs),
            attesa_media_secondi_simulati(s->attesa_per_stazione_ns[1], s->accessi_stazione[1], n_nano_secs),
            attesa_media_secondi_simulati(s->attesa_per_stazione_ns[2], s->accessi_stazione[2], n_nano_secs),
            attesa_media_secondi_simulati(s->attesa_per_stazione_ns[3], s->accessi_stazione[3], n_nano_secs),
            s->operatori_attivi,
            s->pause_effettuate,
            (double)s->pause_effettuate / divisore,
            s->ricavo,
            (double)s->ricavo / divisore,
            s->utenti_in_attesa,
            s->picco_utenti_in_attesa);
}
