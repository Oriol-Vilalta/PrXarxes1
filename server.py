#!/usr/bin/python3.10 python3
import argparse
import random
import re
import struct
import threading
import socket

REGISTER_REQ = 0x00
REGISTER_ACK = 0x02
REGISTER_NACK = 0x04
REGISTER_REJ = 0x06

DISCONNECTED = 0xA0
WAIT_REG_RESPONSE = 0xA2
WAIT_DB_CHECK = 0xA4
REGISTERED = 0xA6
SEND_ALIVE = 0xA8

ALIVE_INF = 0x10
ALIVE_ACK = 0x12
ALIVE_NACK = 0x14
ALIVE_REJ = 0x16

# GLOBAL VARIABLES
debug_mode = False

server = None
equips = []

udp_sock = None


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
        self.state = DISCONNECTED


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
        id = s[1].decode()[:-1]
        mac = s[2].decode()[:-1]
        rand = s[3].decode()[:-1]
        try:
            data = s[4].decode()
        except UnicodeDecodeError:
            data = ""
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


def get_udp_type_name(type):
    if type == 0x00:
        return "REGISTER_REQ"
    elif type == 0x02:
        return "REGISTER_ACK"
    elif type == 0x04:
        return "REGISTER_NACK"
    elif type == 0x06:
        return "REGISTER_REJ"
    elif type == 0x0F:
        return "ERROR"
    elif type == 0x10:
        return "ALIVE_INF"
    elif type == 0x12:
        return "ALIVE_ACK"
    elif type == 0x14:
        return "ALIVE_NACK"
    elif type == 0x16:
        return "ALIVE_REJ"


def check_authorization(id, mac):
    equipment = None
    for equip in equips:
        if equip.id == id and equip.mac == mac:
            equipment = equip
    return equipment


def create_rand_num():
    rand = ""
    for i in range(6):
        rand += str(random.randint(0, 9))
    return rand


def process_register_request(reg_req, addr):
    equip = check_authorization(reg_req.id, reg_req.mac)
    if equip is not None and equip.state == DISCONNECTED:
        rand = create_rand_num()
        reg_ack = Udp_PDU(REGISTER_ACK, equip.id, equip.mac, rand, str(server.tcp_port))

        global udp_sock
        udp_sock.sendto(reg_ack.encode_package(), addr)
    elif equip.state != DISCONNECTED:
        print("[ERROR] Equipment {}")


def handle_udp_package(buffer, addr):
    package = Udp_PDU.decode_package(buffer)
    print(f"Packet rebut: type={get_udp_type_name(package.type)} ,id={package.id}, mac={package.mac}, rand={package.rand}, data={package.data}")

    if package.type == REGISTER_REQ:
        process_register_request(package, addr)


def udp_service():
    global udp_sock
    print("UDP Online")
    udp_sock = start_udp_socket()

    while True:
        buffer, conn = udp_sock.recvfrom(78)
        thread = threading.Thread(target=handle_udp_package, args=(buffer, conn,))
        thread.start()


def main():
    parse_files(parse_arguments())
    udp_tid = threading.Thread(target=udp_service)
    udp_tid.start()
    udp_tid.join()


if __name__ == '__main__':
    main()

