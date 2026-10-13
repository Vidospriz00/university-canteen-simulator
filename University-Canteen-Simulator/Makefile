CC      = gcc
CFLAGS  = -Wvla -Wextra -Werror -D_GNU_SOURCE

# -I. aggiunge la cartella root al search path degli header
# (serve per trovare utilities.h da qualsiasi sottocartella)
INCL    = -I.

# -----------------------------------------------------------------------------
# Sorgenti per ogni eseguibile
# -----------------------------------------------------------------------------

# main  ← main.c + operazioni_file.c
SRCS_MAIN     = main.c \
                Operazioni_file/operazioni_file.c

# Mensa/responsabile_mensa  ← responsabile_mensa.c + operazioni_file.c
SRCS_MENSA    = Mensa/responsabile_mensa.c \
                Operazioni_file/operazioni_file.c

# Operatori/operatori  ← standalone
SRCS_OP = Operatori/operatori.c \
          Operazioni_file/operazioni_file.c

# Utenti/utente  ← standalone
SRCS_UTENTE   = Utenti/utente.c

SRCS_NEW_USERS = aggiungi_utenti.c \
                 Operazioni_file/operazioni_file.c

# -----------------------------------------------------------------------------
# Object files (stesso percorso dei .c, estensione .o)
# -----------------------------------------------------------------------------
OBJS_MAIN     = $(SRCS_MAIN:.c=.o)
OBJS_MENSA    = $(SRCS_MENSA:.c=.o)
OBJS_OP       = $(SRCS_OP:.c=.o)
OBJS_UTENTE   = $(SRCS_UTENTE:.c=.o)
OBJS_NEW_USERS = $(SRCS_NEW_USERS:.c=.o)

# -----------------------------------------------------------------------------
# Eseguibili
# -----------------------------------------------------------------------------
TARGET        = main
TARGET_MENSA  = Mensa/responsabile_mensa
TARGET_OP     = Operatori/operatori
TARGET_UTENTE = Utenti/utente
TARGET_NEW_USERS = aggiungi_utenti

# Tutti gli .o generati (per il clean)
ALL_OBJS      = $(OBJS_MAIN) $(OBJS_MENSA) $(OBJS_OP) $(OBJS_UTENTE) $(OBJS_NEW_USERS)

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------
.PHONY: all clean

all: $(TARGET) $(TARGET_MENSA) $(TARGET_OP) $(TARGET_UTENTE) $(TARGET_NEW_USERS)

# main (cartella root del progetto)
$(TARGET): $(OBJS_MAIN)
	$(CC) $(CFLAGS) -o $@ $^

# Mensa/responsabile_mensa
$(TARGET_MENSA): $(OBJS_MENSA)
	$(CC) $(CFLAGS) -o $@ $^

# Operatori/operatori
$(TARGET_OP): $(OBJS_OP)
	$(CC) $(CFLAGS) -o $@ $^

# Utenti/utente
$(TARGET_UTENTE): $(OBJS_UTENTE)
	$(CC) $(CFLAGS) -o $@ $^

$(TARGET_NEW_USERS): $(OBJS_NEW_USERS)
	$(CC) $(CFLAGS) -o $@ $^

# Regola generica: compila ogni .c nel relativo .o
%.o: %.c
	$(CC) $(CFLAGS) $(INCL) -c $< -o $@

# Rimuove tutti i file generati
clean:
	find . -name "*.o" -delete
	rm -f $(TARGET) $(TARGET_MENSA) $(TARGET_OP) $(TARGET_UTENTE) $(TARGET_NEW_USERS)