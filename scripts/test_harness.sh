#!/bin/bash

PORT=8080
SERVER=./bin/voxos_server
CLIENT=./bin/voxos_client

echo "=== VoxOS Test Harness ==="

# Start server in background
$SERVER $PORT &
SERVER_PID=$!
sleep 1

# Start two clients, log in, create/join room, send audio
(
 echo "LOGIN admin admin123"
 sleep 0.5
 echo "CREATE General"
 sleep 0.5
 echo "JOIN General"
 sleep 0.5
 echo "START"
 sleep 3
 echo "QUIT"
) | $CLIENT 127.0.0.1 $PORT &
CLIENT1_PID=$!

sleep 2

(
 echo "LOGIN user1 pass1"
 sleep 0.5
 echo "JOIN General"
 sleep 0.5
 echo "START"
 sleep 3
 echo "QUIT"
) | $CLIENT 127.0.0.1 $PORT &
CLIENT2_PID=$!

wait $CLIENT1_PID $CLIENT2_PID
kill $SERVER_PID 2>/dev/null
echo "=== Test complete ==="