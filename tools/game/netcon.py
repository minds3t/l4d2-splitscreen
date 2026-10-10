"""Send commands to L4D2 via -netconport and print output.
usage: netcon.py "cmd1" "cmd2" ... [--wait SECONDS]"""
import socket, sys, time
args = sys.argv[1:]; wait = 2.0
if "--wait" in args:
    i = args.index("--wait"); wait = float(args[i + 1]); del args[i:i + 2]
s = socket.create_connection(("127.0.0.1", 27999), timeout=5)
s.settimeout(0.3)
def drain(t):
    end = time.time() + t; out = b""
    while time.time() < end:
        try:
            d = s.recv(65536)
            if not d: break
            out += d
        except socket.timeout: pass
    return out.decode("utf-8", "replace")
drain(0.5)
for c in args:
    s.sendall((c + "\n").encode())
    time.sleep(0.15)
print(drain(wait))
