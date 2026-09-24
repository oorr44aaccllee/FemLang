CC ?= cc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
LDFLAGS ?=
TARGET = femlang
TEST_TARGET = femlang-tests
SRC = src/eval_main.c src/evaluator.c src/parser.c src/ast.c src/lexer.c src/token.c src/native.c
TEST_SRC = tests/test_runner.c src/evaluator.c src/parser.c src/ast.c src/lexer.c src/token.c src/native.c
OBJ = $(SRC:.c=.o)
TEST_OBJ = $(TEST_SRC:.c=.o)

.PHONY: all clean test asan

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJ)

$(TEST_TARGET): $(TEST_OBJ)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(TEST_OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

asan: CFLAGS += -g -O1 -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean $(TARGET) $(TEST_TARGET)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(TARGET) $(TEST_TARGET)
