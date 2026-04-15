CXX      := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -I..

SRC_DIR  := ../index/src
LIB_DIR  := $(SRC_DIR)/lib

# Library sources
VBYTE       := $(LIB_DIR)/vbyte.cpp
COMMON      := $(LIB_DIR)/Common.cpp
TOKENIZER   := $(LIB_DIR)/tokenizer.cpp
IN_MEM_IDX  := $(LIB_DIR)/in_memory_index.cpp
DISK_WRITER := $(LIB_DIR)/disk_chunk_writer.cpp
DISK_READER := $(LIB_DIR)/disk_chunk_reader.cpp
FLUSHER     := $(LIB_DIR)/chunk_flusher.cpp
SEEK_TABLE  := $(LIB_DIR)/seek_table.cpp
STATIC_RANK := $(LIB_DIR)/static_rank.cpp
QUERY_ENGINE := $(LIB_DIR)/query_engine.cpp
QUERY_TOKENIZER := $(LIB_DIR)/query_tokenizer.cpp
QUERY_COMPILER := $(LIB_DIR)/query_compiler.cpp

ISR         := $(LIB_DIR)/isr.cpp
ISR_AND     := $(LIB_DIR)/isr_and.cpp
ISR_OR	  := $(LIB_DIR)/isr_or.cpp
ISR_PHRASE   := $(LIB_DIR)/isr_phrase.cpp
ISR_CONTAINER  := $(LIB_DIR)/isr_container.cpp
DOC_FEATURES := $(LIB_DIR)/document_features.cpp
URL_FEATURES := $(LIB_DIR)/url_features.cpp
HTML_PARSER  := ../parser/HtmlParser.cpp
UTF8         := ../utils/Utf8.cpp

# All library sources (for main)
ALL_LIB_SRCS := $(DISK_WRITER) $(VBYTE) $(IN_MEM_IDX) $(COMMON) \
                $(FLUSHER) $(DISK_READER) $(TOKENIZER) $(SEEK_TABLE) \
				$(STATIC_RANK) $(QUERY_ENGINE) $(QUERY_TOKENIZER) $(QUERY_COMPILER) \
				$(ISR) $(ISR_AND) $(ISR_OR) $(ISR_PHRASE) $(ISR_CONTAINER) \
				$(DOC_FEATURES) $(URL_FEATURES) $(HTML_PARSER) $(UTF8)

.PHONY: all main tests test clean lint help

dummy_index: create_dummy_index.cpp $(ALL_LIB_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@

create_dummy_index: dummy_index
	./dummy_index

worker_node: worker_node.cpp $(ALL_LIB_SRCS)
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

broker_server: broker_server.cpp SearchPlugin.cpp
	$(CXX) $(CXXFLAGS) $^ -o $@ -pthread

# ─── Lint / Format ─────────────────────────────────────────

lint:
	find . \( -name '*.cpp' -o -name '*.cc' -o -name '*.hpp' -o -name '*.h' \) -exec clang-format -i {} +

# ─── Clean ─────────────────────────────────────────────────

clean:
	rm -f dummy_index worker_node broker_server
	rm -rf chunk1 chunk2
	rm -f *.idx

# ─── Help ──────────────────────────────────────────────────

help:
	@echo "Usage:"
	@echo "  make            Build main and all tests"
	@echo "  make lint       Format all source files with clang-format"
	@echo "  make clean      Remove binaries and .idx files"
	@echo "  make help       Show this help message"
