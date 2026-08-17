CC       = gcc
CFLAGS   = -std=c11 -Wall -Wextra -Iinclude -O2
LLVM_CFLAGS = -I"C:/Program Files/LLVM/include" -D_GNU_SOURCE -D__STDC_CONSTANT_MACROS -D__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS
LLVM_LIBS   = -lLLVM-18
BUILD    = build

SRC      = src/lexer.c src/parser.c src/util.c src/checker.c src/semtype.c src/symtab.c src/comptime.c src/ir.c src/irgen.c src/codegen.c src/elf.c src/pe.c
OBJ      = $(SRC:src/%.c=$(BUILD)/%.o)

.PHONY: all clean test

all: $(BUILD)/tmc0

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/llvmgen.o: src/llvmgen.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) $(LLVM_CFLAGS) -c -o $@ src/llvmgen.c

$(BUILD)/main.o: src/main.c include/*.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ src/main.c

$(BUILD)/tmc0: $(OBJ) $(BUILD)/llvmgen.o $(BUILD)/main.o
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(BUILD)/llvmgen.o $(BUILD)/main.o $(LLVM_LIBS) -lm

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
	@echo "--- tmc0 build (direct backend, Linux) + run ---"
	@./$(BUILD)/tmc0 build tests/codegen/fib.tm --backend=direct -o $(BUILD)/t_fib_d && ./$(BUILD)/t_fib_d; [ $$? -eq 55 ] || exit 1
	@echo "--- tmc0 build (llvm backend, Linux) + run ---"
	@./$(BUILD)/tmc0 build tests/codegen/fib.tm --backend=llvm --target=x86_64-pc-linux-gnu -o $(BUILD)/t_fib_l && ./$(BUILD)/t_fib_l; [ $$? -eq 55 ] || exit 1
	@echo "--- tmc0 build (llvm backend, Windows) structural check ---"
	@./$(BUILD)/tmc0 build tests/codegen/fib.tm --backend=llvm --target=x86_64-pc-windows-gnu -o $(BUILD)/t_fib.exe
	@file $(BUILD)/t_fib.exe | grep -q "PE32+" || exit 1
	@echo "--- tmc0 build (direct backend, Windows) structural check ---"
	@./$(BUILD)/tmc0 build tests/codegen/fib.tm --backend=direct --target=x86_64-pc-windows-gnu -o $(BUILD)/t_fib_d.exe
	@file $(BUILD)/t_fib_d.exe | grep -q "PE32+" || exit 1
	@echo "ALL TESTS PASSED"

clean:
	rm -rf $(BUILD)