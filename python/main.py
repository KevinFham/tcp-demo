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
            data_parsed = data.decode("utf-8").split(" ")
            i_data = data_parsed[0]
            print(f'rx: {data_parsed}')

            # Return ACK
            if random.random() < pkt_loss_chance:
                r.sendto(bytes(f'{i_data} 1 ACK', "utf-8"), addr)

"""
 Transmitter Socket
"""
def tx(pkt_seq=[]):
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as t:
        t.settimeout(1.0)
        for pkt in pkt_seq:
            t_start = time.time()
            t.sendto(bytes(pkt, "utf-8"), (RX_ADDR, PORT))

            # Await ACK
            try:
                data, addr = t.recvfrom(1024)
                t_d = (time.time() - t_start) * 1000
                data_parsed = data.decode("utf-8").split(" ")
                print(f'tx: {data_parsed} {t_d:.2f} ms')
            except socket.timeout:
                print("timeout")


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
    tx(pkt_seq)

    # End
    txt.close()


