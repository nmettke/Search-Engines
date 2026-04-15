## Dummy Index

```
make create_dummy_index
```

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

./broker_server <port> <rootdirectory>
```

broker / search server serves static files and intercepts magic path via search plugon

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
./broker_server 8080 website
```

4. Open http://localhost:8080 in your browser

## TODOs

- Implement two-tier searching (Every worker should search anchor_chunks/ first then search body_chunks/).
- load the config using config files.
- implement ranking.
