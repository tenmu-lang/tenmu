CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Iinclude -O2
BUILD   = build

SRC     = src/lexer.c src/parser.c src/util.c
OBJ     = $(SRC:src/%.c=$(BUILD)/%.o)

.PHONY: all clean test

all: $(BUILD)/tmc0

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/tmc0: $(OBJ) $(BUILD)/main.o
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(BUILD)/main.o

$(BUILD)/main.o: src/main.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ src/main.c

$(BUILD)/test_lexer: tests/test_lexer.c src/lexer.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_lexer.c src/lexer.c

$(BUILD)/test_parser: tests/test_parser.c $(SRC) include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_parser.c $(SRC)

test: $(BUILD)/test_lexer $(BUILD)/test_parser $(BUILD)/tmc0
	@echo "--- lexer edge cases ---"
	@./$(BUILD)/test_lexer tests/test_lexer_edge.tm > /dev/null
	@echo "--- parser: all example programs ---"
	@for f in tests/examples/*.tm; do ./$(BUILD)/test_parser $$f --quiet || exit 1; done
	@echo "--- tmc0 check: all example programs ---"
	@for f in tests/examples/*.tm; do ./$(BUILD)/tmc0 check $$f || exit 1; done
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD)
