CC = gcc

SRC = src/main.c src/effects.c
OBJ = $(SRC:.c=.o)
OUT = main.exe

FILE = "${NAME}.wav"

LIBSNDFILE_DIR = libsndfile-1.2.2-win64
PROJECT_DIR := $(shell echo $(CURDIR) | sed 's#^\([A-Za-z]\):#\1#')

INCLUDE = -I$(LIBSNDFILE_DIR)/include -Iinclude
LIB = -L$(LIBSNDFILE_DIR)/lib -lsndfile

all: $(OUT)
	echo "$(PROJECT_DIR)"
	export PATH="$PATH:/$(PROJECT_DIR)/$(LIBSNDFILE_DIR)/bin"; ./$(OUT)

$(OUT): $(OBJ)
	$(CC) $(OBJ) -o $(OUT) $(LIB)

%.o: %.c
	$(CC) -c $< -o $@ $(INCLUDE)

clean:
	rm -f $(OBJ) $(OUT)
