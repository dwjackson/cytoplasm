CFLAGS = -Wall -g -O0

EXE_NAME = cyto
OBJ_FILES = main.o
SRC_FILES = main.c

all: $(EXE_NAME)

$(EXE_NAME): $(OBJ_FILES)
	cc $(CFLAGS) -o $(EXE_NAME) $(OBJ_FILES)

$(OBJ_FILES): $(SRC_FILES)
	cc $(CFLAGS) -c $(SRC_FILES)

clean:
	rm -f $(EXE_NAME)
	rm *.o

.PHONY: all clean