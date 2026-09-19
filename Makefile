CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
TARGET = femlang
SRC = src/eval_main.c src/evaluator.c src/parser.c src/ast.c src/lexer.c src/token.c
OBJ = $(SRC:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)

.PHONY: all clean
