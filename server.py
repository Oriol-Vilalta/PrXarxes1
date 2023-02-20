#!/usr/bin/python3.10 python3
import argparse
import re
import struct
import threading
import socket

# GLOBAL VARIABLES
debug_mode = False

server = None
equips = []


class Server:
    def __init__(self, id, mac, udp_port, tcp_port):
        self.id = id
        self.mac = mac
        self.udp_port = udp_port
        self.tcp_port = tcp_port


class Equip:
    def __init__(self, id, mac):
        self.id = id
        self.mac = mac


class Udp_PDU:
    def __init__(self, type, id, mac, rand, data):
        self.type = type
        self.id = id
        self.mac = mac
        self.rand = rand
        self.data = data

    def encode_package(self):
        return struct.pack("B 7s 13s 7s 50s", self.type, self.id.encode(), self.mac.encode(),
                           self.rand.encode(), self.data.encode())

    @staticmethod
    def decode_package(buffer):
        s = struct.unpack("B 7s 13s 7s 50s", buffer)
        id = s[1].decode('utf-8')
        mac = s[2].decode('ascii')
        rand = s[3].decode('ascii')
        data = s[4].decode('ascii')
        return Udp_PDU(s[0], id, mac, rand, data)


def parse_arguments():
    parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
    parser.add_argument('-c', '--config_file_name', type=str, help="Configuration file name", default="server.cfg")
    parser.add_argument('-u', '--valid_clients_file_name', type=str, help="Equipments", default="equips.dat")
    parser.add_argument('--debug_mode', '-d', action="store_true", default=False)

    args = parser.parse_args()
    global debug_mode
    debug_mode = args.debug_mode

    return args.config_file_name, args.valid_clients_file_name


def eliminate_prompt(line):
    return re.split(" |\n", line)[1]


def parse_config_file(config_file_name):
    global server

    try:
        config_file = open(config_file_name, 'r')
    except IOError:
        print(f"[REGISTER] ERROR: Cannot open {config_file_name}")
        exit(1)

    line_num = 0
    id, mac, udp_port, tcp_port = "", "", 0, 0

    for line in iter(lambda: config_file.readline(), ''):

        if line_num == 0:
            id = eliminate_prompt(line)
        elif line_num == 1:
            mac = eliminate_prompt(line)
        elif line_num == 2:
            udp_port = int(eliminate_prompt(line))
        elif line_num == 3:
            tcp_port = int(eliminate_prompt(line))

        line_num += 1

    server = Server(id, mac, udp_port, tcp_port)


def parse_equipment_file(equip_file_name):
    global equips
    try:
        equip_file = open(equip_file_name, 'r')
    except IOError:
        print(f"[PARSER] ERROR: Cannot find {equip_file_name}")
        exit(1)

    for line in iter(lambda: equip_file.readline(), ''):
        equip_id, equip_mac = tuple(line.split(" "))
        equips.append(Equip(equip_id, equip_mac[:-1]))


def parse_files(file_names):
    parse_config_file(file_names[0])
    parse_equipment_file(file_names[1])


def start_udp_socket():
    global server
    addr = "localhost", server.udp_port

    udp_sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_sock.bind(addr)
    return udp_sock


def udp_service():
    udp_sock = start_udp_socket()

    while True:
        pack, conn = udp_sock.recvfrom(78)
        pack_cond = Udp_PDU.decode_package(pack)

        print(pack_cond.id)



def main():
    parse_files(parse_arguments())
    udp_tid = threading.Thread(target=udp_service)
    udp_tid.start()
    udp_tid.join()


if __name__ == '__main__':
    main()
