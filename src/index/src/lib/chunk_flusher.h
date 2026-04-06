// src/lib/chunk_flusher.h
#pragma once

#include "disk_chunk_writer.h"
#include "in_memory_index.h"

void flushIndexChunk(const InMemoryIndex &mem_index, const ::string &filename);
