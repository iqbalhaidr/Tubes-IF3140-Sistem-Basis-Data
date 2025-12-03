## Build

Build both server and client:
```bash
make build-cs
```

Or build individually:
```bash
make build-server
make build-client
```

## Running and Testing

### Step 1: Start the Server

Open Terminal 1:
```bash
./build/server
```

You should see:
```
[SERVER] Server started. Listening on localhost:8080
[SERVER] Waiting for client connections...
```

### Step 2: Start the Client

Open Terminal 2:
```bash
./build/client
```