## Dummy Index
```
make create_dummy_index
```

This creates `chunk1/` and `chunk2/` with:
- `body_index/*.idx`
- `meta/*.meta`

## Worker Server

```
make worker_node

./worker_node <port> <index_dir>
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
./worker_node 8081 chunk1
./worker_node 8082 chunk2
```

3. Start broker server
```
./broker_server
```

4. Query through your web browser. e.g. http://localhost:8080/search?q=cat

You can also query from the terminal:
```
curl "http://localhost:8080/search?q=cat"
```

The JSON response includes the worker-computed static `score` for each result.


## TODOs
- Implement two-tier searching (Every worker should search anchor_chunks/ first then search body_chunks/).
- load the config using config files.
