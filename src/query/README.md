## Dummy Index
```
make create_dummy_index
```

## Worker Server

```
make worker_node

./worker_node <port> <index_file>
```

```
echo "cat" | nc localhost <port>
```

## Broker Server

```
make broker_server

./broker_server
```

## Test

1. Create files and executables
```
make create_dummy_index
make worker_node
make broker_server
```

2. Run two worker in separate terminal
```
./worker_node 8081 chunk_0001.idx
./worker_node 8082 chunk_0002.idx
```

3. Start broker server
```
./broker_server
```

4. Query through your web browser. e.g. http://localhost:8080/search?q=cat


## TODOs
- Change worker to handle a directory of files, not one (Every worker should manage two sets of files: body_chunks/ and anchor_chunks/)
