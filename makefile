SHELL := cmd.exe
.SHELLFLAGS := /C

CC = gcc

SRC = src/main.c src/effects.c src/preset.c
OBJ = $(SRC:.c=.o)
OUT = main.exe

FILE = "${NAME}.wav"

LIBSNDFILE_DIR = libsndfile-1.2.2-win64
CURDIR_WIN := $(subst /,\,$(CURDIR))
LIBSNDFILE_BIN_WIN := $(CURDIR_WIN)\$(subst /,\,$(LIBSNDFILE_DIR))\bin
OBJ_WIN := $(subst /,\,$(OBJ))

INCLUDE = -I$(LIBSNDFILE_DIR)/include -Iinclude
LIB = -L$(LIBSNDFILE_DIR)/lib -lsndfile

all: $(OUT)

run: $(OUT)
	set "PATH=$(LIBSNDFILE_BIN_WIN);%PATH%" && $(OUT) $(ARGS)

$(OUT): $(OBJ)
	$(CC) $(OBJ) -o $(OUT) $(LIB)

%.o: %.c
	$(CC) -c $< -o $@ $(INCLUDE)

clean:
	-del /Q "$(OUT)" 2>nul
	-del /Q $(OBJ_WIN) 2>nul

.PHONY: all run clean
