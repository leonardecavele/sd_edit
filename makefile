SHELL := cmd.exe
.SHELLFLAGS := /C

CC = gcc

SRC = src/main.c src/effects.c src/preset.c
OBJ = $(SRC:.c=.o)
OUT = sd_edit.exe

FILE = "${NAME}.wav"

LIBSNDFILE_DIR = libsndfile-1.2.2-win64
CURDIR_WIN := $(subst /,\,$(CURDIR))
LIBSNDFILE_BIN_WIN := $(CURDIR_WIN)\$(subst /,\,$(LIBSNDFILE_DIR))\bin
OBJ_WIN := $(subst /,\,$(OBJ))

INCLUDE = -I$(LIBSNDFILE_DIR)/include -Iinclude

all: $(OUT)

re: clean all

run: $(OUT)
	set "PATH=$(LIBSNDFILE_BIN_WIN);%PATH%" && $(OUT) $(ARGS)

$(OUT): $(OBJ)
	$(CC) $(OBJ) -o $(OUT)

%.o: %.c
	$(CC) -c $< -o $@ $(INCLUDE)

clean:
	-del /Q "$(OUT)" 2>nul
	-del /Q $(OBJ_WIN) 2>nul

.PHONY: all re run clean
