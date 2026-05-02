#!/usr/bin/env python3
import struct
import os
import glob

GAME_SERVERS = {"210.64.10.55", "210.64.10.68"}

def analyze_pcap(filepath):
    print(f"\n分析: {filepath}")
    print("-" * 50)

    with open(filepath, 'rb') as f:
        data = f.read()

    # 只追蹤遊戲伺服器的流量
    game_traffic = []
    udp_packets = 0
    tcp_packets = 0

    # PCAP NG format - scan for IP packets
    for i in range(len(data) - 60):
        if data[i] == 0x45:  # IPv4, no options
            ip_header_len = (data[i] & 0x0F) * 4

            src_ip = f"{data[i+12]}.{data[i+13]}.{data[i+14]}.{data[i+15]}"
            dst_ip = f"{data[i+16]}.{data[i+17]}.{data[i+18]}.{data[i+19]}"

            # 只處理遊戲伺服器流量
            if src_ip in GAME_SERVERS or dst_ip in GAME_SERVERS:
                proto = data[i+9]
                transport_offset = i + ip_header_len

                if proto == 17:  # UDP
                    udp_packets += 1
                    src_port = struct.unpack('!H', data[transport_offset:transport_offset+2])[0]
                    dst_port = struct.unpack('!H', data[transport_offset+2:transport_offset+4])[0]
                    total_len = struct.unpack('!H', data[i+2:i+4])[0]
                    game_traffic.append({
                        'proto': 'UDP',
                        'src': f"{src_ip}:{src_port}",
                        'dst': f"{dst_ip}:{dst_port}",
                        'len': total_len
                    })
                elif proto == 6:  # TCP
                    tcp_packets += 1

    print(f"  UDP 封包數: {udp_packets}")
    print(f"  TCP 封包數: {tcp_packets}")

    if game_traffic:
        print(f"\n  遊戲流量:")
        for t in game_traffic[:30]:
            print(f"    {t['proto']} {t['src']} -> {t['dst']} len={t['len']}")

        # 統計唯一 port
        src_ports = set(t['src'].split(':')[1] for t in game_traffic)
        dst_ports = set(t['dst'].split(':')[1] for t in game_traffic)
        sizes = set(t['len'] for t in game_traffic)

        print(f"\n  源端口: {sorted(src_ports)}")
        print(f"  目標端口: {sorted(dst_ports)}")
        print(f"  封包大小: {sorted(sizes)}")
    else:
        print("  [無遊戲伺服器流量]")

# 只分析打怪的 pcap (210.64.10.68)
files = glob.glob(r"C:\Users\tian7\Downloads\*.pcapng")
for f in files:
    if "mob" in f.lower() or "打怪" in f:
        try:
            analyze_pcap(f)
        except Exception as e:
            print(f"  [ERROR] {e}")
