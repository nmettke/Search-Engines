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