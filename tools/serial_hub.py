"""Serial hub: share one physical COM port with many consumers.

One process owns the serial port and mirrors every byte to:
  * a log file (instant flush -> tail it live), and
  * any number of TCP clients on 127.0.0.1:<tcpport>.

Bytes received FROM any TCP client are forwarded to the serial port, so an
interactive monitor works through the hub:

    idf.py monitor -p socket://127.0.0.1:2323          (full monitor UX)
    Get-Content live.log -Wait -Tail 50                (plain live tail)

Usage:
    python serial_hub.py <com> <baud> <logfile> <tcpport> [--reset]

--reset pulses EN (RTS) only, leaving IO0 (DTR) alone, so the target reboots
into its application over either link.  Do not "helpfully" drive DTR here:
on the ESP32-S3's native USB-Serial-JTAG that requests download mode and the
board goes silent.
"""
import datetime
import socket
import sys
import threading
import time

import serial

com, baud, logpath, tcpport = (sys.argv[1], int(sys.argv[2]),
                               sys.argv[3], int(sys.argv[4]))
do_reset = "--reset" in sys.argv

ser = serial.Serial()
ser.port = com
ser.baudrate = baud
ser.timeout = 0.2

# Always open with the strapping lines pre-cleared, so the act of opening
# cannot disturb the target; reset explicitly afterwards if asked.
ser.dtr = False
ser.rts = False
ser.open()

if do_reset:
    # EN pulse via RTS *only*.  DTR must stay low the whole time: on the
    # ESP32-S3's native USB-Serial-JTAG, DTR is IO0, and releasing EN while
    # IO0 is asserted is a download-mode request rather than a reboot — the
    # board then sits mute in the ROM loader and looks dead.  Pulsing EN
    # alone reboots into the application on both links.
    ser.rts = True
    ser.dtr = ser.dtr   # Windows latches RTS only on a following DTR write
    time.sleep(0.1)
    ser.rts = False
    ser.dtr = ser.dtr

clients = []
clients_lock = threading.Lock()


def serial_reader():
    with open(logpath, "ab") as f:
        stamp = datetime.datetime.now().isoformat(timespec="seconds")
        f.write(("\n===== hub attached %s (reset=%s) =====\n"
                 % (stamp, do_reset)).encode())
        f.flush()
        while True:
            data = ser.read(4096)
            if not data:
                continue
            f.write(data)
            f.flush()
            with clients_lock:
                dead = []
                for c in clients:
                    try:
                        c.sendall(data)
                    except OSError:
                        dead.append(c)
                for c in dead:
                    clients.remove(c)


def client_writer(conn):
    try:
        while True:
            data = conn.recv(4096)
            if not data:
                break
            ser.write(data)
    except OSError:
        pass
    finally:
        with clients_lock:
            if conn in clients:
                clients.remove(conn)
        conn.close()


def pulse_reset():
    """EN pulse via RTS.  On Windows, RTS changes only latch when DTR is
    re-applied afterwards (the esptool quirk), hence the dtr re-writes."""
    ser.rts = True
    ser.dtr = ser.dtr
    time.sleep(0.2)
    ser.rts = False
    ser.dtr = ser.dtr


def reset_listener():
    """Any TCP connection to <tcpport>+1 triggers a target reset."""
    ctl = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    ctl.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    ctl.bind(("127.0.0.1", tcpport + 1))
    ctl.listen(2)
    while True:
        conn, _ = ctl.accept()
        pulse_reset()
        try:
            conn.sendall(b"reset\n")
        except OSError:
            pass
        conn.close()


threading.Thread(target=serial_reader, daemon=True).start()
threading.Thread(target=reset_listener, daemon=True).start()

srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
srv.bind(("127.0.0.1", tcpport))
srv.listen(5)
while True:
    conn, _ = srv.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    with clients_lock:
        clients.append(conn)
    threading.Thread(target=client_writer, args=(conn,), daemon=True).start()
