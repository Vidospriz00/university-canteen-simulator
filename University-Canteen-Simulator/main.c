/*
 * IT: Punto di ingresso: seleziona la configurazione e avvia il responsabile in un processo figlio.
 * EN: Entry point: selects a configuration and launches the canteen manager in a child process.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include "./Operazioni_file/operazioni_file.h"

extern char **environ;

int main(int argc, char *argv[]){
    if (argc > 2) {
        fprintf(stderr, "Uso: %s [file_configurazione]\n", argv[0]);
        return EXIT_FAILURE;
    }

    // EN: An optional command-line argument selects the configuration file.
    if (argc == 2) {
        if (setenv("CONFIG_FILE", argv[1], 1) == -1) {
            perror("setenv CONFIG_FILE");
            return EXIT_FAILURE;
        }
    }

    char *const args[] = { "mensa", NULL };

    sleep(1);
    // EN: Create a child process; the child replaces itself with the manager executable.
    pid_t pid = fork();
    if(pid == -1) {
        perror("Errore creazione processo");
        return EXIT_FAILURE;
    }
    else if(pid == 0){
        if (execve("Mensa/responsabile_mensa", args, environ) == -1) {
            perror("execve fallita");
            _exit(EXIT_FAILURE);
        }
    }

    int status = 0;
    // EN: Wait for completion and propagate the child exit status.
    if (waitpid(pid, &status, 0) == -1) {
        perror("waitpid");
        return EXIT_FAILURE;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return EXIT_FAILURE;
}
