CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -Iinclude -O2
BUILD   = build

SRC     = src/lexer.c src/parser.c src/util.c src/checker.c src/semtype.c src/symtab.c src/comptime.c src/ir.c src/irgen.c src/codegen.c src/elf.c
OBJ     = $(SRC:src/%.c=$(BUILD)/%.o)

.PHONY: all clean test

all: $(BUILD)/tmc0

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/tmc0: $(OBJ) $(BUILD)/main.o
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(BUILD)/main.o -lm

$(BUILD)/main.o: src/main.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ src/main.c

$(BUILD)/test_lexer: tests/test_lexer.c src/lexer.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_lexer.c src/lexer.c

$(BUILD)/test_parser: tests/test_parser.c src/parser.c src/lexer.c src/util.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -o $@ tests/test_parser.c src/parser.c src/lexer.c src/util.c

test: $(BUILD)/test_lexer $(BUILD)/test_parser $(BUILD)/tmc0
	@echo "--- lexer edge cases ---"
	@./$(BUILD)/test_lexer tests/test_lexer_edge.tm > /dev/null
	@echo "--- parser: all example programs ---"
	@for f in tests/examples/*.tm; do ./$(BUILD)/test_parser $$f --quiet || exit 1; done
	@echo "--- tmc0 check: all example programs ---"
	@for f in tests/examples/*.tm; do ./$(BUILD)/tmc0 check $$f || exit 1; done
	@echo "--- tmc0 build + run: codegen examples ---"
	@./$(BUILD)/tmc0 build tests/codegen/fib.tm -o $(BUILD)/t_fib && ./$(BUILD)/t_fib; [ $$? -eq 55 ] || exit 1
	@./$(BUILD)/tmc0 build tests/codegen/factorial.tm -o $(BUILD)/t_fact && ./$(BUILD)/t_fact; [ $$? -eq 120 ] || exit 1
	@./$(BUILD)/tmc0 build tests/codegen/loop_sum.tm -o $(BUILD)/t_loop && ./$(BUILD)/t_loop; [ $$? -eq 45 ] || exit 1
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD)
