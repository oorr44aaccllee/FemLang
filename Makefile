CC = cc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -Iinclude
TARGET = femlang
TEST_TARGET = femlang-tests
SRC = src/eval_main.c src/evaluator.c src/parser.c src/ast.c src/lexer.c src/token.c
TEST_SRC = tests/test_runner.c src/evaluator.c src/parser.c src/ast.c src/lexer.c src/token.c
OBJ = $(SRC:.c=.o)
TEST_OBJ = $(TEST_SRC:.c=.o)

.PHONY: all clean test debug asan

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $(OBJ)

$(TEST_TARGET): $(TEST_OBJ)
	$(CC) $(CFLAGS) -o $@ $(TEST_OBJ)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

test: $(TEST_TARGET)
	./$(TEST_TARGET)

debug: CFLAGS += -g -O0
debug: clean all

asan: CFLAGS += -g -O1 -fsanitize=address,undefined
asan: LDFLAGS += -fsanitize=address,undefined
asan: clean $(TARGET)
	$(CC) $(CFLAGS) -o $(TEST_TARGET) $(TEST_OBJ) $(LDFLAGS)
	./$(TEST_TARGET)

clean:
	rm -f $(OBJ) $(TEST_OBJ) $(TARGET) $(TEST_TARGET)
