# Python
import argparse
import pathlib
import threading
import socket
import time
import random

RX_ADDR = "127.0.0.1"
PORT = 65432
UNACK_COUNT = 0
REACK_COUNT = 0

"""
 Reciever Socket
"""
def rx(pkt_loss_chance=1.0):
    global UNACK_COUNT
    global REACK_COUNT

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as r:
        r.bind((RX_ADDR, PORT))
        pkt_buffer = []

        while True:
            data, addr = r.recvfrom(1024)
            data_parsed = data.decode("utf-8").split(" ")
            i_data = data_parsed[0]
            # print(f'rx: {data_parsed}')

            # Return ACK
            if random.random() < pkt_loss_chance:

                # Buffer; EOF packet, push out buffer
                if data_parsed[1] == '-1':

                    # Simulation conclusion
                    print(f'Buffer seq (unsorted): {",".join([i[0] for i in pkt_buffer])}')
                    pkt_buffer = sorted(pkt_buffer, key=lambda pkt: int(pkt[0]))
                    print(f'Buffer seq (sorted): {",".join([i[0] for i in pkt_buffer])}')
                    print(f'UnACK\'d pkts: {UNACK_COUNT}')
                    print(f'Resent pkts: {REACK_COUNT}')
                    print("Delivered msg:", " ".join([w[-1].replace("\n","") for w in pkt_buffer]))
                    pkt_buffer.clear()
                else:
                    pkt_buffer.append(data_parsed)
                r.sendto(bytes(f'{i_data} 1 ACK', "utf-8"), addr)
            else:
                UNACK_COUNT += 1

"""
 Transmitter Socket
"""
def tx(pkt_seq=[], timeout_s=0.4):
    global REACK_COUNT

    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as t:
        t.settimeout(timeout_s)
        for pkt in pkt_seq:
            while True:
                t.sendto(bytes(pkt, "utf-8"), (RX_ADDR, PORT))
                t_start = time.time()

                # Await ACK, retry if UNACK
                try:
                    data, addr = t.recvfrom(1024)
                    t_d = (time.time() - t_start) * 1000
                    data_parsed = data.decode("utf-8").split(" ")
                    # print(f'tx: {data_parsed} {t_d:.2f} ms')
                    break
                except socket.timeout:
                    print(f'timeout ({timeout_s * 1000} ms)')
                    REACK_COUNT += 1


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


