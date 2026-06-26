import socket
import threading
import time
import struct

SERVER_IP = "127.0.0.1"
SERVER_PORT = 8080
MAX_CLIENTS = 50
MAX_ROOMS = 32

# response_packet is: uint32_t opcode (4), uint32_t status (4), char message[256], char data[4096]
PACKET_FORMAT = "<II256s4096s"
PACKET_SIZE = struct.calcsize(PACKET_FORMAT)

def read_response(s):
    try:
        s.settimeout(2.0)
        raw_data = s.recv(PACKET_SIZE)
        if len(raw_data) < PACKET_SIZE:
            return f"Incomplete packet ({len(raw_data)} bytes)"
        
        opcode, status, message_b, data_b = struct.unpack(PACKET_FORMAT, raw_data)
        message = message_b.split(b'\0', 1)[0].decode('utf-8', errors='ignore')
        data = data_b.split(b'\0', 1)[0].decode('utf-8', errors='ignore')
        return f"[{status}] {message} {data}".strip()
    except socket.timeout:
        return "Timeout"
    except Exception as e:
        return f"Decode Error: {e}"

def send_cmd(s, cmd):
    """Send a raw command string and receive the response"""
    try:
        s.sendall(cmd.encode('utf-8'))
        return read_response(s)
    except Exception as e:
        return f"Error: {e}"

def run_bot(bot_id):
    """Simulate a single concurrent user"""
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.connect((SERVER_IP, SERVER_PORT))
    except Exception as e:
        print(f"[Bot {bot_id}] Failed to connect: {e}")
        return

    username = f"bot_user_{bot_id}"
    
    # 1. Register and Login
    send_cmd(s, f"REGISTER {username} password")
    resp_login = send_cmd(s, f"LOGIN {username} password")
    if "200" not in resp_login:
        print(f"[Bot {bot_id}] Login failed: {resp_login}")
    
    # 2. Join the first room created by admin
    resp_join = send_cmd(s, "JOIN Room_0 roompass")
    
    # 3. Check Status
    resp_status = send_cmd(s, "STATUS")
    if "Room_0" in resp_status:
        print(f"[Bot {bot_id}] Successfully joined Room_0 and is waiting.")
    else:
        print(f"[Bot {bot_id}] Failed to verify room: {resp_status}")

    # Hold connection open to stress test concurrency
    time.sleep(3)
    
    # 4. Cleanup
    send_cmd(s, "LOGOUT")
    s.close()
    print(f"[Bot {bot_id}] Logged out and disconnected.")

def main():
    print("==================================================")
    print(" EchoLink Load Test & Edge Case Simulator")
    print("==================================================")
    
    print("\n[1] Connecting as Admin...")
    admin_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        admin_sock.connect((SERVER_IP, SERVER_PORT))
    except ConnectionRefusedError:
        print("ERROR: Server is not running! Please start ./bin/echolink_server first.")
        return

    resp = send_cmd(admin_sock, "LOGIN admin admin123")
    print("Admin Login Response:", resp)
    
    print(f"\n[2] Edge Case: Creating Max Rooms ({MAX_ROOMS})...")
    for i in range(MAX_ROOMS):
        send_cmd(admin_sock, f"CREATE Room_{i} roompass")
    print(f"Created {MAX_ROOMS} rooms.")
    
    print(f"\n[3] Edge Case: Attempting to create Room {MAX_ROOMS + 1} (Should Fail)...")
    resp_fail = send_cmd(admin_sock, f"CREATE Room_Overflow roompass")
    print("Server Response:", resp_fail)
    if "507" in resp_fail:
        print("-> SUCCESS: Server correctly rejected the extra room!")
    else:
        print("-> FAILED: Server allowed too many rooms.")

    print(f"\n[4] Stress Test: Spawning {MAX_CLIENTS - 1} concurrent bots to join Room_0...")
    threads = []
    # -1 because Admin is taking up 1 connection slot!
    for i in range(MAX_CLIENTS - 1):
        t = threading.Thread(target=run_bot, args=(i,))
        threads.append(t)
        t.start()
        time.sleep(0.01) # stagger slightly to prevent local socket exhaustion
        
    print("\n[5] Edge Case: Attempting to connect a 51st user (Should be ignored)...")
    overflow_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    try:
        overflow_sock.connect((SERVER_IP, SERVER_PORT))
        resp_overflow = send_cmd(overflow_sock, "LOGIN overflow pass")
        print("51st User Response:", resp_overflow)
    except Exception as e:
        print("51st User Connection Result:", e)
        
    print("\nWaiting for all bots to finish...")
    for t in threads:
        t.join()

    print("\n[6] Cleaning up...")
    send_cmd(admin_sock, "LOGOUT")
    admin_sock.close()
    
    print("\n==================================================")
    print(" Load Test Complete!")
    print("==================================================")

if __name__ == "__main__":
    main()
