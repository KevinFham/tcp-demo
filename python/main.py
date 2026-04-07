# Python
import argparse
import pathlib
import threading
import socket
import time
import random

RX_ADDR = "127.0.0.1"
PORT = 65432

"""
 Reciever Socket
"""
def rx(pkt_loss_chance=1.0):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as r:
        r.bind((RX_ADDR, PORT))
        while True:
            data, addr = r.recvfrom(1024)
            print(f'pkt: {data}')

            # Return ACK
            if random.random() < pkt_loss_chance:
                r.sendto(b'-1 1 ACK', addr)

"""
 Transmitter Socket
"""
tx = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
tx.settimeout(1.0)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='TCP Simulation')
    parser.add_argument('txt_file', type=pathlib.Path, nargs=1, help='TXT file containing test case')
    args = parser.parse_args()

    if args.txt_file[0].suffix != ".txt":
        raise Exception('File path does not point to a TXT file!')

    txt = open(args.txt_file[0], "r")
    PKT_LOSS_CHANCE = float(txt.readline().replace("\n",""))

    # Daemonize Reciever
    rx_thread = threading.Thread(target=rx, args=(PKT_LOSS_CHANCE,))
    rx_thread.start()

    # Transmit Packets
    pkt_seq = txt.readlines()
    for pkt in pkt_seq:
        t_start = time.time()
        tx.sendto(bytes(pkt, "utf-8"), (RX_ADDR, PORT))

        # Await ACK
        try:
            data, addr = tx.recvfrom(1024)
            t_d = (time.time() - t_start) * 1000
            print(f'{data} {t_d:.2f} ms')
        except socket.timeout:
            print("timeout")


    # End
    txt.close()


