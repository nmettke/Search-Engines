## Testing

### Test chunk writer
```
g++ -std=c++17 tests/test_chunk_writer.cpp src/lib/disk_chunk_writer.cpp src/lib/vbyte.cpp -o test_chunk_writer

./test_chuk_writer
```

### Test write integration
```
g++ -std=c++17 tests/test_integration.cpp src/lib/in_memory_index.cpp src/lib/disk_chunk_writer.cpp src/lib/vbyte.cpp src/lib/Common.cpp src/lib/chunk_flusher.cpp -o test_integration

./test_inegration
```

### Test

### Test chunk reader
```
g++ -std=c++17 tests/test_chunk_reader.cpp src/lib/disk_chunk_reader.cpp src/lib/chunk_flusher.cpp src/lib/disk_chunk_writer.cpp src/lib/vbyte.cpp src/lib/Common.cpp src/lib/in_memory_index.cpp src/lib/isr.cpp -o test_chunk_reader

./test_chunk_reader
```
 