#!/bin/bash

PORT=8080
SERVER=./bin/voxos_server
CLIENT=./bin/voxos_client

echo "=== Fault Injection Test ==="

# Start server
$SERVER $PORT &
SERVER_PID=$!
sleep 1

# Create a room and verify it persists
echo "LOGIN admin admin123" | $CLIENT 127.0.0.1 $PORT
echo "CREATE TestRoom" | $CLIENT 127.0.0.1 $PORT
sleep 0.5

echo "Verify room exists:"
echo "LOGIN admin admin123
LIST" | $CLIENT 127.0.0.1 $PORT | grep TestRoom

if [ $? -eq 0 ]; then
    echo "Room creation successful."
else
    echo "Room creation failed."
fi

# Crash the server
echo "Killing server (simulating crash)..."
kill -9 $SERVER_PID
sleep 1

# Restart and verify recovery
echo "Restarting server..."
$SERVER $PORT &
NEW_PID=$!
sleep 1

echo "Checking if TestRoom survived crash:"
echo "LOGIN admin admin123
LIST" | $CLIENT 127.0.0.1 $PORT | grep TestRoom

if [ $? -eq 0 ]; then
    echo "Crash recovery SUCCESS."
else
    echo "Crash recovery FAILED."
fi

kill $NEW_PID 2>/dev/null