CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra

SRC_DIR  := src
LIB_DIR  := $(SRC_DIR)/lib
TEST_DIR := tests

# Library sources
VBYTE       := $(LIB_DIR)/vbyte.cpp
COMMON      := $(LIB_DIR)/Common.cpp
TOKENIZER   := $(LIB_DIR)/tokenizer.cpp
IN_MEM_IDX  := $(LIB_DIR)/in_memory_index.cpp
DISK_WRITER := $(LIB_DIR)/disk_chunk_writer.cpp
DISK_READER := $(LIB_DIR)/disk_chunk_reader.cpp
FLUSHER     := $(LIB_DIR)/chunk_flusher.cpp
ISR         := $(LIB_DIR)/isr.cpp
ISR_AND     := $(LIB_DIR)/isr_and.cpp
SEEK_TABLE  := $(LIB_DIR)/seek_table.cpp
QUERY_ENGINE := $(LIB_DIR)/query_engine.cpp

# All library sources (for main)
ALL_LIB_SRCS := $(DISK_WRITER) $(VBYTE) $(IN_MEM_IDX) $(COMMON) \
                $(FLUSHER) $(DISK_READER) $(ISR) $(ISR_AND) \
				$(TOKENIZER) $(SEEK_TABLE) $(QUERY_ENGINE)

# Test binaries
TEST_BINS := test_vbyte test_tokenizer test_chunk_writer test_chunk_flusher test_chunk_reader

# ─── Targets ───────────────────────────────────────────────

.PHONY: all main tests test clean lint help

all: main tests

# ─── Main ──────────────────────────────────────────────────

main: $(SRC_DIR)/main.cpp $(ALL_LIB_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

# ─── Tests ─────────────────────────────────────────────────

tests: $(TEST_BINS)

test_vbyte: $(TEST_DIR)/test_vbyte.cpp $(VBYTE)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_tokenizer: $(TEST_DIR)/test_tokenizer.cpp $(TOKENIZER)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_chunk_writer: $(TEST_DIR)/test_chunk_writer.cpp $(DISK_WRITER) $(VBYTE) $(SEEK_TABLE)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_chunk_flusher: $(TEST_DIR)/test_chunk_flusher.cpp $(IN_MEM_IDX) $(DISK_WRITER) $(VBYTE) $(COMMON) $(FLUSHER) $(SEEK_TABLE)
	$(CXX) $(CXXFLAGS) $^ -o $@

test_chunk_reader: $(TEST_DIR)/test_chunk_reader.cpp $(DISK_READER) $(FLUSHER) $(DISK_WRITER) $(VBYTE) $(COMMON) $(IN_MEM_IDX) $(ISR) $(SEEK_TABLE)
	$(CXX) $(CXXFLAGS) $^ -o $@

# ─── Run tests ─────────────────────────────────────────────

test: tests
	@for t in $(TEST_BINS); do \
		echo "\n=== Running $$t ==="; \
		./$$t || exit 1; \
	done

# ─── Lint / Format ─────────────────────────────────────────

lint:
	find src tests \( -name '*.cpp' -o -name '*.cc' -o -name '*.hpp' -o -name '*.h' \) -exec clang-format -i {} +

# ─── Clean ─────────────────────────────────────────────────

clean:
	rm -f main $(TEST_BINS)
	rm -f *.idx

# ─── Help ──────────────────────────────────────────────────

help:
	@echo "Usage:"
	@echo "  make            Build main and all tests"
	@echo "  make main       Build the main binary"
	@echo "  make tests      Build all test binaries"
	@echo "  make test       Build and run all tests"
	@echo "  make lint       Format all source files with clang-format"
	@echo "  make clean      Remove binaries and .idx files"
	@echo "  make help       Show this help message"
