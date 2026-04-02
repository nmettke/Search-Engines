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
 
### Test VBytes
```
g++ -std=c++17 tests/test_vbyte.cpp src/lib/vbyte.cpp -o test_vbyte

./test_vbyte
```

### Test Tokenizer
```
g++ -std=c++17 tests/test_tokenizer.cpp src/lib/tokenizer.cpp -o test_tokenizer

./test_tokenizer
```

## Main
`main.cpp` shows the overall process of building inverted index, save index, read index, and query documents

```
g++ -std=c++17 src/main.cpp src/lib/disk_chunk_writer.cpp src/lib/vbyte.cpp src/lib/in_memory_index.cpp src/lib/Common.cpp src/lib/chunk_flusher.cpp src/lib/disk_chunk_reader.cpp src/lib/isr.cpp src/lib/tokenizer.cpp -o main

./main
```