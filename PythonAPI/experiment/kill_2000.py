import psutil
import os

def find_procs_by_port(port):
    """Find a list of process IDs (PIDs) using the given port."""
    procs = []
    for proc in psutil.process_iter(['pid', 'name']):
        try:
            for conn in proc.connections(kind='inet'):
                if conn.laddr.port == port:
                    procs.append(proc.pid)
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            pass
    return procs

def kill_proc(pid):
    """Kill the process with the given PID."""
    try:
        p = psutil.Process(pid)
        p.terminate()  # or p.kill()
    except (psutil.NoSuchProcess, psutil.AccessDenied):
        print(f"Process {pid} not found or access denied.")

# Usage example:
port = 2000
pids = find_procs_by_port(port)

for pid in pids:
    print(f"Killing process with PID: {pid}")
    kill_proc(pid)