import os
import json
import asyncio
from dotenv import load_dotenv
from typing import Dict, List
from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.staticfiles import StaticFiles
from fastapi.responses import FileResponse
import uvicorn
from contextlib import asynccontextmanager

# Import the IoT Hub reader function
from libs.iot_hub_reader import receive_events_from_iothub

# Check required environment variables
if load_dotenv():
    iot_hub_connection_string = os.getenv('IotHubConnectionString')
    event_hub_consumer_group = os.getenv('EventHubConsumerGroup')
if not iot_hub_connection_string:
    print("Environment variable IotHubConnectionString must be specified.")
    exit(1)
print(f"Using IoT Hub connection string [{iot_hub_connection_string}]")

if not event_hub_consumer_group:
    print("Environment variable EventHubConsumerGroup must be specified.")
    exit(1)
print(f"Using event hub consumer group [{event_hub_consumer_group}]")

# Create a lifespan context manager to start/stop background tasks
@asynccontextmanager
async def lifespan(app: FastAPI):
    # Start the IoT Hub message reader task
    iot_task = asyncio.create_task(start_iot_message_processing())
    yield
    # Cancel the task when shutting down
    iot_task.cancel()
    try:
        await iot_task
    except asyncio.CancelledError:
        print("IoT Hub message processing task was cancelled")

app = FastAPI(lifespan=lifespan)

# Serve static files from the 'public' directory
app.mount("/public", StaticFiles(directory="public", html=True), name="public")

# WebSocket connection manager
class ConnectionManager:
    def __init__(self):
        self.active_connections: List[WebSocket] = []

    async def connect(self, websocket: WebSocket):
        await websocket.accept()
        self.active_connections.append(websocket)
        print(f"WebSocket client connected. Total connections: {len(self.active_connections)}")

    def disconnect(self, websocket: WebSocket):
        if websocket in self.active_connections:
            self.active_connections.remove(websocket)
            print(f"WebSocket client disconnected. Remaining connections: {len(self.active_connections)}")

    async def broadcast(self, message: dict):
        message_json = json.dumps(message)
        print(f"Broadcasting data to {len(self.active_connections)} clients: {message_json}")
        
        # Create a copy of the list to avoid modification during iteration
        connections_to_remove = []
        
        for connection in self.active_connections:
            try:
                await connection.send_text(message_json)
            except Exception as e:
                print(f"Error sending message: {e}")
                connections_to_remove.append(connection)
        
        # Clean up any dead connections
        for connection in connections_to_remove:
            if connection in self.active_connections:
                self.active_connections.remove(connection)
                print(f"Removed dead connection. Remaining connections: {len(self.active_connections)}")

manager = ConnectionManager()

# Function to process IoT Hub messages and broadcast to WebSocket clients
async def process_iot_message(message):
    # Broadcast the message to all connected WebSocket clients
    await manager.broadcast(message)

# Start the IoT Hub message processing
async def start_iot_message_processing():
    try:
        print("Starting IoT Hub message processing...")
        await receive_events_from_iothub(
            iot_hub_connection_string, 
            event_hub_consumer_group,
            process_iot_message
        )
    except Exception as e:
        print(f"Error in IoT Hub message processing: {e}")

# WebSocket endpoint
@app.websocket("/ws")
async def websocket_endpoint(websocket: WebSocket):
    await manager.connect(websocket)
    try:
        while True:
            # Wait for client messages
            try:
                # Use a short timeout to also allow sending messages
                data = await asyncio.wait_for(websocket.receive_text(), timeout=10)
                print(f"Received message from client: {data}")
            except asyncio.TimeoutError:
                # No message received within timeout, continue
                pass
            except Exception as e:
                print(f"Error receiving message: {e}")
                break
    except WebSocketDisconnect:
        print("WebSocket disconnected")
    finally:
        manager.disconnect(websocket)

# Redirect root to index.html
@app.get("/")
async def root():
    return FileResponse("public/index.html")

if __name__ == "__main__":
    uvicorn.run("main:app", host="0.0.0.0", port=3000, reload=True)
