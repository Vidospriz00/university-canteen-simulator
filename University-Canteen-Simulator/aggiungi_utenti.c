/*
 * IT: Aggiunge processi utente a una simulazione gia avviata.
 * EN: Spawns additional customer processes in an already running simulation.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#include "utilities.h"
#include "Operazioni_file/operazioni_file.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/msg.h>
#include <sys/wait.h>

int main(int argc, char *argv[])
{
    if (argc > 2)
    {
        fprintf(stderr, "Uso: %s [file_configurazione]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *config_file = argc == 2 ? argv[1] : getenv("CONFIG_FILE");
    if (config_file == NULL || config_file[0] == '\0')
    {
        config_file = "./variabili.txt";
    }

    Configurazione conf = {0};
    leggiCostanti((char *)config_file, &conf);

    if (conf.N_NEW_USERS <= 0 || conf.N_NANO_SECS <= 0)
    {
        fprintf(stderr, "Configurazione nuovi utenti non valida\n");
        return EXIT_FAILURE;
    }

    // EN: Only add customers after the main simulation has created its IPC queues.
    if (msgget(KEY_PRIMI, 0) == -1 ||
        msgget(KEY_SECONDI, 0) == -1 ||
        msgget(KEY_COFFEE, 0) == -1 ||
        msgget(KEY_CASSA, 0) == -1)
    {
        fprintf(stderr, "Simulazione non attiva\n");
        return EXIT_FAILURE;
    }

    int semid_sinc = semget(KEY_SEM_SINC, 1, 0);
    if (semid_sinc == -1 || semctl(semid_sinc, 0, GETVAL) != 0)
    {
        fprintf(stderr, "Simulazione non ancora avviata\n");
        return EXIT_FAILURE;
    }

    char n_nano_secs_str[32];
    snprintf(n_nano_secs_str, sizeof(n_nano_secs_str), "%d", conf.N_NANO_SECS);

    pid_t *pids = malloc((size_t)conf.N_NEW_USERS * sizeof(pid_t));
    if (pids == NULL)
    {
        perror("malloc nuovi utenti");
        return EXIT_FAILURE;
    }

    // EN: Fork and launch extra customer processes independently.
    for (int i = 0; i < conf.N_NEW_USERS; i++)
    {
        pid_t pid = fork();

        if (pid == -1)
        {
            perror("fork nuovo utente");
            free(pids);
            return EXIT_FAILURE;
        }

        if (pid == 0)
        {
            char *args[] = {
                "./Utenti/utente",
                n_nano_secs_str,
                "extra",
                NULL
            };

            execv(args[0], args);
            perror("execv nuovo utente");
            _exit(EXIT_FAILURE);
        }

        pids[i] = pid;
    }

    printf("Aggiunti %d nuovi utenti.\n", conf.N_NEW_USERS);

    free(pids);
    return EXIT_SUCCESS;
}
