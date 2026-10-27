"""Sends OpenAoK.nro to a Switch and prints what the game prints, over Wi-Fi.

In the Homebrew Menu (started by holding R while starting any game), press Y
for its network loader, then run:

    python3 switch/send.py [--host SWITCH_IP] [--nro PATH] [-- ARGS...]

It finds the Switch on the network (or uses --host), sends the game to
sdmc:/switch/openaok/OpenAoK.nro, beside its data, and starts it with ARGS.
The game then sends what it prints here until it ends, or until Ctrl+C. This
is the protocol of devkitPro's nxlink.
"""
import argparse
import os
import socket
import struct
import sys
import time
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)

LOADER_PORT = 28280   # the Switch's network loader
OUTPUT_PORT = 28771   # the loader's answer to a search, and the game's output
CHUNK = 16 * 1024     # the largest compressed piece the loader takes
REMOTE_PATH = 'openaok/OpenAoK.nro'   # under sdmc:/switch/


def find_switch(tries=20):
    """The address of a Switch whose network loader answers a broadcast."""
    answers = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    answers.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    answers.bind(('', OUTPUT_PORT))
    answers.settimeout(0.25)
    ask = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    ask.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)
    try:
        for _ in range(tries):
            ask.sendto(b'nxboot', ('255.255.255.255', LOADER_PORT))
            try:
                data, (address, _) = answers.recvfrom(256)
            except socket.timeout:
                continue
            if data.startswith(b'bootnx'):
                return address
    finally:
        ask.close()
        answers.close()
    return None


def receive_exactly(sock, size):
    data = b''
    while len(data) < size:
        part = sock.recv(size - len(data))
        if not part:
            raise ConnectionError('the Switch closed the connection')
        data += part
    return data


def answer(sock):
    return struct.unpack('<i', receive_exactly(sock, 4))[0]


def connect(host, wait):
    """A connection to the loader, trying again while it is not yet listening."""
    give_up = time.time() + wait
    while True:
        try:
            return socket.create_connection((host, LOADER_PORT), timeout=30)
        except (ConnectionRefusedError, socket.timeout, OSError):
            if time.time() > give_up:
                raise
            time.sleep(2)


def send(host, nro, args, wait):
    data = open(nro, 'rb').read()
    name = REMOTE_PATH.encode()
    sock = connect(host, wait)
    sock.sendall(struct.pack('<I', len(name)) + name + struct.pack('<I', len(data)))
    refused = answer(sock)
    if refused != 0:
        reasons = {-1: 'cannot create the file', -2: 'not enough space', -3: 'not enough memory'}
        sys.exit('The Switch refused the file: %s' % reasons.get(refused, refused))
    packed = zlib.compress(data)
    for start in range(0, len(packed), CHUNK):
        piece = packed[start:start + CHUNK]
        sock.sendall(struct.pack('<I', len(piece)) + piece)
    if answer(sock) != 0:
        sys.exit('The Switch did not take the file.')
    line = b''.join(arg.encode() + b'\0' for arg in args)
    sock.sendall(struct.pack('<I', len(line)) + line)
    sock.shutdown(socket.SHUT_WR)
    sock.close()
    print('==> Sent %s (%d KB) to %s' % (REMOTE_PATH, len(data) // 1024, host), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', help="the Switch's address; found on the network when left out")
    parser.add_argument('--nro', default=os.path.join(ROOT, 'build-switch', 'OpenAoK.nro'))
    parser.add_argument('--wait', type=int, default=0,
                        help='seconds to keep trying while the loader is not yet listening')
    parser.add_argument('args', nargs='*', help="the game's own options, after --")
    args = parser.parse_args()

    host = args.host or find_switch()
    if host is None:
        sys.exit("No Switch answered. Press Y in the Homebrew Menu, or give its address with --host.")

    # Listening before the game starts, so its first lines are not lost.
    output = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    output.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    output.bind(('', OUTPUT_PORT))
    output.listen(1)

    send(host, args.nro, args.args, args.wait)

    game, _ = output.accept()
    output.close()
    try:
        while True:
            data = game.recv(4096)
            if not data:
                break
            sys.stdout.write(data.decode('utf-8', 'replace'))
            sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    print('==> The game has ended.')


if __name__ == '__main__':
    main()
