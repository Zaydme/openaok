"""Runs OpenAoK on a Switch from this computer, through the test agent.

Install the agent once with `python3 switch/send.py --agent` (from the Homebrew
Menu's network loader), or copy build-switch/OpenAoKAgent.nro to the card, and
leave "OpenAoK test agent" waiting. Then:

    python3 switch/agent.py --host IP run [--nro PATH] [--seconds N] -- ARGS...
    python3 switch/agent.py --host IP put LOCAL REMOTE
    python3 switch/agent.py --host IP get REMOTE LOCAL
    python3 switch/agent.py --host IP ls REMOTE

`run` sends the .nro when given, starts the game with the game's own options
ARGS, prints what the game prints, and returns once the game has ended and the
agent waits again. Paths on the console are sdmc:/...; the game's folder is
sdmc:/switch/openaok/. The host can also come from OPENAOK_SWITCH.
"""
import argparse
import os
import socket
import sys
import time

AGENT_PORT = 28290
OUTPUT_PORT = 28771   # nxlink's: where the game sends what it prints
GAME_ON_CARD = 'sdmc:/switch/openaok/OpenAoK.nro'


def connect(host, timeout=10):
    sock = socket.create_connection((host, AGENT_PORT), timeout=timeout)
    sock.settimeout(120)
    return sock


def read_line(sock):
    line = b''
    while not line.endswith(b'\n'):
        part = sock.recv(1)
        if not part:
            raise ConnectionError('the agent closed the connection')
        line += part
    return line[:-1].decode()


def put(host, local, remote):
    data = open(local, 'rb').read()
    with connect(host) as sock:
        sock.sendall(('PUT %d %s\n' % (len(data), remote)).encode() + data)
        answer = read_line(sock)
    if answer != 'OK':
        sys.exit('put %s: %s' % (remote, answer))
    print('==> put %s (%d KB)' % (remote, len(data) // 1024))


def get(host, remote, local):
    with connect(host) as sock:
        sock.sendall(('GET %s\n' % remote).encode())
        answer = read_line(sock)
        if answer.startswith('ERR'):
            sys.exit('get %s: %s' % (remote, answer))
        size = int(answer)
        data = b''
        while len(data) < size:
            part = sock.recv(min(1 << 16, size - len(data)))
            if not part:
                break
            data += part
    open(local, 'wb').write(data)
    print('==> got %s (%d KB)' % (remote, len(data) // 1024))


def ls(host, remote):
    with connect(host) as sock:
        sock.sendall(('LIST %s\n' % remote).encode())
        while True:
            line = read_line(sock)
            if line == 'END' or line.startswith('ERR'):
                if line.startswith('ERR'):
                    print(line)
                break
            print(line)


def wait_for_agent(host, seconds):
    give_up = time.time() + seconds
    while time.time() < give_up:
        try:
            with socket.create_connection((host, AGENT_PORT), timeout=3):
                return True
        except OSError:
            time.sleep(2)
    return False


def run(host, nro, seconds, options):
    if nro:
        put(host, nro, GAME_ON_CARD)
    output = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    output.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    output.bind(('', OUTPUT_PORT))
    output.listen(1)
    line = ' '.join('"%s"' % o if ' ' in o else o for o in options)
    with connect(host) as sock:
        sock.sendall(('RUN %s\n' % line).encode())
        answer = read_line(sock)
    if answer != 'OK':
        sys.exit('run: %s' % answer)
    print('==> running: %s' % line, flush=True)
    # The game checks its files before it connects, after a copy.
    output.settimeout(180)
    try:
        game, _ = output.accept()
    except socket.timeout:
        sys.exit('The game did not connect; it may have stopped at a message on the screen.')
    output.close()
    game.settimeout(5)
    give_up = time.time() + seconds
    ended = False
    while time.time() < give_up:
        try:
            data = game.recv(4096)
        except socket.timeout:
            continue
        except ConnectionResetError:
            ended = True
            break
        if not data:
            ended = True
            break
        sys.stdout.write(data.decode('utf-8', 'replace'))
        sys.stdout.flush()
    game.close()
    if not ended:
        print('==> still running after %d seconds; left running' % seconds)
        return
    if wait_for_agent(host, 90):
        print('==> the game has ended; the agent is waiting again')
    else:
        print('==> the game has ended, and the agent did not come back: it may have crashed')


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', default=os.environ.get('OPENAOK_SWITCH'))
    sub = parser.add_subparsers(dest='command', required=True)
    p = sub.add_parser('run')
    p.add_argument('--nro', help='a build to send first, such as build-switch/OpenAoK.nro')
    p.add_argument('--seconds', type=int, default=600, help='how long to follow the game at most')
    p.add_argument('options', nargs='*')
    p = sub.add_parser('put')
    p.add_argument('local')
    p.add_argument('remote')
    p = sub.add_parser('get')
    p.add_argument('remote')
    p.add_argument('local')
    p = sub.add_parser('ls')
    p.add_argument('remote')
    args = parser.parse_args()
    if not args.host:
        sys.exit("Give the Switch's address with --host, or in OPENAOK_SWITCH.")
    if args.command == 'run':
        run(args.host, args.nro, args.seconds, args.options)
    elif args.command == 'put':
        put(args.host, args.local, args.remote)
    elif args.command == 'get':
        get(args.host, args.remote, args.local)
    else:
        ls(args.host, args.remote)


if __name__ == '__main__':
    main()
