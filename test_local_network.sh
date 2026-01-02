#!/bin/bash

# Запускаем сервер в фоне
echo "Starting server..."
./voice_chat server &
SERVER_PID=$!

sleep 2

echo "Starting client..."
./voice_chat client 127.0.0.1 &
CLIENT_PID=$!

echo ""
echo "Network test running..."
echo "Press Enter to stop"
read

kill $SERVER_PID $CLIENT_PID 2>/dev/null
echo "Test stopped"
