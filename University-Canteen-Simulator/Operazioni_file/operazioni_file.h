/*
 * IT: Dichiara le funzioni di lettura dei file usate da altri moduli.
 * EN: Declares file-reading functions shared between modules.
 * Nota / Note: commenti e identificatori originali sono preservati.
 */

#include "utilities.h"
/// @brief Funzione per leggere il file di configurazione delle costanti nel formato: NOME_COSTANTE VALORE
/// @param nomeFile Nome del file
/// @param costanti struct di destinazione dei valori letti
void leggiCostanti(char* nomeFile, Configurazione* costanti);

/// @brief Divide la stringa passata e restiutisce un array con le sottostringhe separate
/// @param stringa Stringa da dividere
/// @param delimitatore Carattere che separa le sottostringhe
/// @param numero_elementi Puntatore alla variabile che conterrà il numero di elementi trovati
/// @return Arraty di stringhe 
char** dividiStringa(char* stringa, char* delimitatore, int* numero_elementi);

/// @brief Legge il menu da file e lo carica in una struct 
/// @param nomeFile Nome del file da leggere
/// @param menu Puntatore alla struttura dove verrà caricato il menù letto
int leggiMenu(char* nomeFile, Menu** menu);


