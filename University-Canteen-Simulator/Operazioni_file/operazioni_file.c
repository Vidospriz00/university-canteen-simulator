/*
 * IT: Contiene la lettura dei parametri e le operazioni sul menu.
 * EN: Handles configuration parsing and menu-file operations.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "operazioni_file.h"
#include "../utilities.h"

// EN: Parse configuration lines as KEY VALUE pairs.
void leggiCostanti(char* nomeFile, Configurazione* costanti){

    FILE *fp = fopen(nomeFile, "r");
    if (!fp) {
        perror("Errore apertura file variabili\n");
        exit(EXIT_FAILURE);
    }

    char riga[256];
    char chiave[256];
    char valore[256];

    while(fgets(riga, sizeof(riga), fp)){
        if (sscanf(riga, "%s %s", chiave, valore) == 2) {
            
           if (strcmp(chiave, "AVG_SRVC_PRIMI") == 0) {
                costanti->AVG_SRVC_PRIMI = atoi(valore);
            } else if (strcmp(chiave, "AVG_SRVC_MAIN_COURSE") == 0) {
                costanti->AVG_SRVC_MAIN_COURSE = atoi(valore);
            } else if (strcmp(chiave, "AVG_SRVC_COFFEE") == 0) {
                costanti->AVG_SRVC_COFFEE = atoi(valore);
            } else if (strcmp(chiave, "AVG_SRVC_CASSA") == 0) {
                costanti->AVG_SRVC_CASSA = atoi(valore);
            } else if (strcmp(chiave, "NOF_USERS") == 0) {
                costanti->NOF_USERS = atoi(valore);
            } else if (strcmp(chiave, "N_NEW_USERS") == 0) {
                costanti->N_NEW_USERS = atoi(valore);
            } else if (strcmp(chiave, "NOF_WK_SEATS_PRIMI") == 0) {
                costanti->NOF_WK_SEATS_PRIMI = atoi(valore);
            } else if (strcmp(chiave, "NOF_WK_STEATS_SECONDI") == 0) {
                costanti->NOF_WK_STEATS_SECONDI = atoi(valore);
            } else if (strcmp(chiave, "NOF_WK_STEATS_COFFEE") == 0) {
                costanti->NOF_WK_STEATS_COFFEE = atoi(valore);
            } else if (strcmp(chiave, "NOF_WK_SEATS_CASSA") == 0) {
                costanti->NOF_WK_SEATS_CASSA = atoi(valore);
            } else if (strcmp(chiave, "NOF_TABLE_SEATS") == 0) {
                costanti->NOF_TABLE_SEATS = atoi(valore);
            } else if (strcmp(chiave, "SIM_DURATION") == 0) {
                costanti->SIM_DURATION = atoi(valore);
            } else if (strcmp(chiave, "N_NANO_SECS") == 0) {
                costanti->N_NANO_SECS = atoi(valore);
            }else if (strcmp(chiave, "AVG_REFILL_PRIMI") == 0) {
                costanti->AVG_REFILL_PRIMI = atoi(valore);
            }else if (strcmp(chiave, "AVG_REFILL_SECONDI") == 0) {
                costanti->AVG_REFILL_SECONDI = atoi(valore);
            }else if (strcmp(chiave, "AVG_REFILL_TIME") == 0) {
                costanti->AVG_REFILL_TIME = atoi(valore);
            }else if (strcmp(chiave, "MAX_PORZIONI_PRIMI") == 0) {
                costanti->MAX_PORZIONI_PRIMI = atoi(valore);
            }else if (strcmp(chiave, "MAX_PORZIONI_SECONDI") == 0) {
                costanti->MAX_PORZIONI_SECONDI = atoi(valore);
            }else if (strcmp(chiave, "NOF_PAUSE") == 0) {
                costanti->NOF_PAUSE = atoi(valore);
            }else if (strcmp(chiave, "PRICE_PRIMI") == 0) {
                costanti->PRICE_PRIMI = atoi(valore);
            }else if (strcmp(chiave, "PRICE_SECONDI") == 0) {
                costanti->PRICE_SECONDI = atoi(valore);
            }else if (strcmp(chiave, "PRICE_COFFEE") == 0) {
                costanti->PRICE_COFFEE = atoi(valore);
            }else if (strcmp(chiave, "OVERLOAD_THRESHOLD") == 0) {
                costanti->OVERLOAD_THRESHOLD = atoi(valore);
            }else if (strcmp(chiave, "NOF_WORKERS") == 0) {
                costanti->NOF_WORKERS = atoi(valore);
            }else{
                printf("Trovato parametro sconosciuto\n");
            }

        }else{
            printf("Errore nella scrittura del parametro nel file, parametro non letto\n");
        }
    }

    fclose(fp);
}


// EN: Split a menu record and allocate memory for its entries.
char** dividiStringa(char* stringa, char* delimitatore, int* numero_elementi){

    //la funzione strtok restitusice una sezione della stringa da dividere fino al delimitatore
    char* elemento = strtok(stringa, delimitatore);
 
    //nel del menù la prima porzione di stringa divisa contiene il numero di piatti per quella portata
    *numero_elementi = atoi(elemento);
    if(*numero_elementi == 0){ //se fallisce la funzione atoi ritorna 0 e c'è un problema nella composizione del file 
        printf("Errore nella lettura delle portate del menu\n");
        return NULL;
    }

    //alloco array finale che conterrà le stringhe divise
    char** arr = malloc(*numero_elementi*sizeof(char*));
    int i = 0;
    int numero_stringhe_lette = 0;

    while (elemento != NULL && i< *numero_elementi) {
        elemento = strtok(NULL, ";"); //separo una stringa
        if(elemento == NULL || !strcmp(elemento, " ")){
            printf("Errore nella lettura delle portate del menu\n");
            return NULL;
        }else{
            numero_stringhe_lette++;
        }

        arr[i] = malloc((strlen(elemento)+1)*sizeof(char)); //alloco lo spazio per salvarla
        strcpy(arr[i], elemento); //la metto dell'array finale

        i++;
    }

    if(numero_stringhe_lette != *numero_elementi){
        return NULL;
    }

    return arr;
}
    
int leggiMenu(char* nomeFile, Menu** menu){
    //apro il file per leggere il menu
    FILE* fp = fopen(nomeFile, "r");
    if(fp == NULL){
        printf("Errore nell'aperura del file %s\n", nomeFile);
        return 0;
    }

    //parametri per la funzione getline()
    char* riga_letta = NULL;
    int n_caratteri_letti = 0;
    size_t capacita = 0;

    int numero_riga_file = 1;
    //per controllare che nel file siano presenti piatti per tutte le stazioni previste (Primi, Secondi, Caffe Dolci)
    int conta_portate_lette = 0; 

    while((n_caratteri_letti = getline(&riga_letta, &capacita, fp)) != -1){ //leggo una riga alla volta il file

        //il file è formato da 4 righe (1 per tipo di portata)
        switch(numero_riga_file){ //mappo la riga che sto leggendo per capire di che portata si tratta
            case 1:
                (*menu)->primi = dividiStringa(riga_letta, ";", &((*menu)->N_PRIMI));
                if((*menu)->primi == NULL){
                    return 0;
                }
                conta_portate_lette++;
                break;

            case 2:
                (*menu)->secondi = dividiStringa(riga_letta, ";", &((*menu)->N_SECONDI));
                if((*menu)->secondi == NULL){
                    return 0;
                }
                conta_portate_lette++;
                break;

            case 3:
                (*menu)->dolci = dividiStringa(riga_letta, ";", &((*menu)->N_DOLCI));
                if((*menu)->dolci == NULL){
                    return 0;
                }
                conta_portate_lette++;
                break;

            case 4:
                (*menu)->caffe = dividiStringa(riga_letta, ";", &((*menu)->N_CAFFE));
                if((*menu)->caffe == NULL){
                    return 0;
                }
                conta_portate_lette++;
                break;
            }

        numero_riga_file++;
    }
    
    free(riga_letta);
    fclose(fp);

    if(conta_portate_lette != 4){
        printf("Errore: Formato del file errato\n");
        return 0;
    }else{
        return 1;
    }
}