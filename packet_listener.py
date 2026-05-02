#!/usr/bin/env python3
# ============================================================
# RAN2 Packet Listener - 座標擷取 (Scapy 版)
# 使用 Scapy 監聽網路封包，解析位置同步封包，寫入共享記憶體
# ============================================================
import struct
import time
import ctypes
import mmap
import sys

# RAN2 Protocol Constants
SERVER_IPS = ["210.64.10.55", "210.64.10.68", "203.211.8.141"]
GAME_SERVER = "203.211.8.141"
GAME_PORT = 443  # 遊戲主伺服器使用 port 443
TARGET_PORTS = [443, 1292, 6870, 5502]
MSGID_POSITION_SYNC = 0x27C0

# Shared Memory
SHMEM_NAME = "Local\\RanBot_Coords"
SHMEM_SIZE = 128

# Scapy 導入
try:
    from scapy.all import sniff, IP, UDP, TCP
    SCAPY_AVAILABLE = True
except ImportError:
    SCAPY_AVAILABLE = False

# ============================================================
# 共享記憶體結構
# ============================================================
class CoordsData(ctypes.Structure):
    _fields_ = [
        ("magic", ctypes.c_uint32),
        ("version", ctypes.c_uint32),
        ("valid", ctypes.c_uint32),
        ("x", ctypes.c_float),
        ("z", ctypes.c_float),
        ("y", ctypes.c_float),
        ("heading", ctypes.c_float),
        ("mapId", ctypes.c_uint32),
        ("timestamp", ctypes.c_uint32),
        ("_pad", ctypes.c_uint32 * 24),
    ]

# ============================================================
# RAN2 封包解析器
# ============================================================
def parse_udp_payload(data):
    if len(data) < 4:
        return None
    try:
        size = struct.unpack('<H', data[0:2])[0]
        msgId = struct.unpack('<H', data[2:4])[0]
        return {'size': size, 'msgId': msgId, 'payload': data[4:]}
    except:
        return None

def parse_position_sync(data):
    if len(data) < 20:
        return None
    try:
        objId = struct.unpack('<I', data[0:4])[0]
        x = struct.unpack('<f', data[4:8])[0]
        z = struct.unpack('<f', data[8:12])[0]
        y = struct.unpack('<f', data[12:16])[0]
        heading = struct.unpack('<f', data[16:20])[0]
        if abs(x) < 0.1 and abs(z) < 0.1:
            return None
        return {'msgId': 0x27C0, 'objId': objId, 'x': x, 'z': z, 'y': y, 'heading': heading}
    except:
        return None

# ============================================================
# 共享記憶體客戶端
# ============================================================
class CoordsShmem:
    def __init__(self):
        self.handle = None
        self.data = None
        self.coords = CoordsData()
        self.coords.magic = 0xCAFEBABE
        self.coords.version = 1
        self.coords.valid = 0
        self.coord_count = 0
        self.last_x = 0.0
        self.last_z = 0.0

    def connect(self):
        try:
            self.handle = mmap.mmap(0, SHMEM_SIZE, SHMEM_NAME, mmap.ACCESS_WRITE)
            self.data = self.handle
            return True
        except Exception as e:
            print(f"[CoordsShmem] 連接失敗: {e}")
            return False

    def write(self, x, z, y=0.0, heading=0.0, mapId=0):
        if not self.data:
            return
        # 過濾重複
        if abs(x - self.last_x) < 0.1 and abs(z - self.last_z) < 0.1:
            return
        self.last_x = x
        self.last_z = z
        try:
            self.coords.valid = 1
            self.coords.x = x
            self.coords.z = z
            self.coords.y = y
            self.coords.heading = heading
            self.coords.mapId = mapId
            self.coords.timestamp = int(time.time())
            self.data.seek(0)
            self.data.write(bytes(self.coords))
            self.data.flush()
            self.coord_count += 1
            if self.coord_count % 50 == 0:
                print(f"[Coords] X={x:.2f} Z={z:.2f} Y={y:.2f}")
        except:
            pass

    def invalidate(self):
        if self.data:
            try:
                self.coords.valid = 0
                self.data.seek(0)
                self.data.write(bytes(self.coords))
                self.data.flush()
            except:
                pass

    def close(self):
        if self.data:
            self.data.close()
            self.data = None

# ============================================================
# 封包處理器
# ============================================================
class PacketHandler:
    def __init__(self):
        self.shmem = CoordsShmem()
        self.connected = False
        self.packet_count = 0
        self.last_coords = (None, None)  # 上一個有效座標
        self.confirm_count = 0  # 確認計數
        self.min_confirm = 3  # 最少需要確認3次

    def process(self, pkt):
        self.packet_count += 1
        if not self.connected:
            self.connected = self.shmem.connect()

        if IP not in pkt:
            return

        ip_src = pkt[IP].src
        ip_dst = pkt[IP].dst

        # 只處理 210.64.10.115 的封包
        if ip_src not in SERVER_IPS and ip_dst not in SERVER_IPS:
            return

        # 調試：顯示所有封包
        if self.packet_count < 100:
            if UDP in pkt:
                sport = pkt[UDP].sport
                dport = pkt[UDP].dport
                payload_len = len(bytes(pkt[UDP].payload))

                # 顯示小封包的原始數據
                if payload_len <= 50:
                    raw_data = bytes(pkt[UDP].payload)
                    hex_data = raw_data.hex()
                    print(f"[DEBUG] UDP {ip_src}:{sport} -> {ip_dst}:{dport} len={payload_len}")
                    print(f"       HEX: {hex_data}")

        # 只處理 UDP 封包
        if UDP not in pkt:
            return

        sport = pkt[UDP].sport
        dport = pkt[UDP].dport
        raw_data = bytes(pkt[UDP].payload)

        # 嘗試解析小封包 (32-42 bytes) - 可能是未加密的心跳或位置封包
        if 20 <= len(raw_data) <= 50:
            self._try_parse_small_packet(raw_data)
        else:
            # 嘗試解析為標準 RAN2 封包
            parsed = parse_udp_payload(raw_data)
            if parsed and parsed['msgId'] == MSGID_POSITION_SYNC:
                coords = parse_position_sync(parsed['payload'])
                if coords:
                    self.shmem.write(
                        coords['x'],
                        coords['z'],
                        coords.get('y', 0.0),
                        coords.get('heading', 0.0)
                    )

    def _try_parse_small_packet(self, data):
        """嘗試解析小封包為位置資料"""
        if len(data) < 12:
            return

        # 嘗試直接解析 float 座標（小端序和大端序）
        valid_coords = []

        for offset in [6, 7, 10, 11, 14, 15, 2, 0, 12, 13, 4, 8]:
            if offset + 12 <= len(data):
                try:
                    # 小端序
                    x_le = struct.unpack('<f', data[offset:offset+4])[0]
                    z_le = struct.unpack('<f', data[offset+4:offset+8])[0]
                    y_le = struct.unpack('<f', data[offset+8:offset+12])[0]

                    # 大端序
                    x_be = struct.unpack('>f', data[offset:offset+4])[0]
                    z_be = struct.unpack('>f', data[offset+4:offset+8])[0]
                    y_be = struct.unpack('>f', data[offset+8:offset+12])[0]

                    # 小端序候選：X 和 Z 都非零且合理
                    if (abs(x_le) > 50 and abs(z_le) > 50 and
                        -10000 < x_le < 10000 and -10000 < z_le < 10000):
                        valid_coords.append((x_le, z_le, y_le, offset, 'LE'))

                    # 大端序候選
                    if (abs(x_be) > 50 and abs(z_be) > 50 and
                        -10000 < x_be < 10000 and -10000 < z_be < 10000):
                        valid_coords.append((x_be, z_be, y_be, offset, 'BE'))

                except:
                    pass

        # 只報告最可能的座標（X 和 Z 都非零）
        if valid_coords:
            # 選擇一個最合理的座標
            best = max(valid_coords, key=lambda c: abs(c[0]) + abs(c[1]))
            x, z, y, offset, endian = best

            # 確認機制 - 連續多個封包座標相同才報告
            if self.last_coords == (x, z):
                self.confirm_count += 1
                if self.confirm_count >= self.min_confirm:
                    print(f"[POS!] {endian} offset={offset} X={x:.2f} Z={z:.2f} Y={y:.2f}")
                    self.shmem.write(x, z, y)
            else:
                self.confirm_count = 1
                self.last_coords = (x, z)

    def stop(self):
        if self.connected:
            self.shmem.invalidate()
            self.shmem.close()
        print(f"[關閉] 處理封包數: {self.packet_count}")

# ============================================================
# 主程式
# ============================================================
def main():
    print("[DEBUG] 1. 開始初始化...")
    from scapy.all import conf, get_if_list, sniff

    print("[DEBUG] 2. Scapy 導入成功")
    print("=" * 60)
    print("RAN2 Packet Listener - 座標擷取 (Scapy 版)")
    print("=" * 60)
    print(f"遊戲主伺服器: {GAME_SERVER}:{GAME_PORT} (UDP)")
    print(f"其他伺服器: {', '.join([s for s in SERVER_IPS if s != GAME_SERVER])}")
    print()

    if not SCAPY_AVAILABLE:
        print("[ERROR] Scapy 未安裝")
        print("執行: pip install scapy")
        return 1

    # 列出可用網卡
    print("[DEBUG] 3. 列出網卡...")
    print("[INFO] 可用網卡:")
    try:
        ifaces = get_if_list()
        for iface in ifaces:
            print(f"  - {iface}")
        print()

        # 自動檢測正確的網卡
        print("[INFO] 掃描所有網卡的 IP 地址...")
        from scapy.arch import get_if_addr
        best_iface = None
        best_ip = None

        for iface in ifaces:
            try:
                iface_ip = get_if_addr(iface)
                if iface_ip:
                    print(f"  {iface_ip} -> {iface[:60]}...")
                    # 優先選擇非 169.254 的網卡
                    if not iface_ip.startswith("169.254.") and not iface_ip.startswith("127."):
                        best_iface = iface
                        best_ip = iface_ip
            except:
                pass

        if best_iface:
            print(f"[OK] 選擇網卡 (IP: {best_ip})")
            conf.iface = best_iface
        else:
            print("[WARN] 沒有找到合適的網卡，嘗試所有網卡...")
            conf.iface = None

        print(f"[INFO] 監聽網卡: {conf.iface or '所有網卡'}")
    except Exception as e:
        print(f"[WARN] 無法列出網卡: {e}")

    handler = PacketHandler()

    # 嘗試連接共享記憶體
    handler.connected = handler.shmem.connect()
    if not handler.connected:
        print("[WARN] 共享記憶體連接失敗（Trainer 可能未啟動）")

    print()
    print("=" * 60)
    print("開始擷取... 進入遊戲走動測試")
    print("按 Ctrl+C 停止")
    print("=" * 60)
    print()

    # 先測試能否捕獲任何 UDP 封包
    test_count = [0]
    seen_ips = set()
    seen_ports = set()

    def test_handler(pkt):
        test_count[0] += 1
        if IP in pkt and UDP in pkt:
            src_ip = pkt[IP].src
            dst_ip = pkt[IP].dst
            sport = pkt[UDP].sport
            dport = pkt[UDP].dport
            seen_ips.add(src_ip)
            seen_ips.add(dst_ip)
            seen_ports.add(sport)
            seen_ports.add(dport)
            # 只顯示前 30 個封包
            if test_count[0] <= 30:
                print(f"[TEST] UDP {src_ip}:{sport} -> {dst_ip}:{dport}")
            # 每 100 個報告一次
            elif test_count[0] % 100 == 0:
                print(f"[TEST] 已捕獲 {test_count[0]} 個封包...")

    print("[DEBUG] 4. 開始測試封包捕獲 (15秒)...")
    print("[INFO] 顯示所有 UDP 封包...")
    try:
        sniff(filter="udp", prn=test_handler, store=0, timeout=15)
        print(f"\n[TEST] 15秒內捕獲 {test_count[0]} 個 UDP 封包")
        print(f"[TEST] 見過的 IP: {sorted(seen_ips)}")
        print(f"[TEST] 見過的 Port: {sorted(seen_ports)}")

        # 檢查是否有遊戲伺服器 IP
        game_ips = [ip for ip in seen_ips if ip.startswith("210.64")]
        if game_ips:
            print(f"[OK] 找到遊戲伺服器 IP: {game_ips}")
        else:
            print(f"[WARN] 沒有看到 210.64.x.x (遊戲伺服器) 的封包")
            print(f"[INFO] 你的 IP 是 49.158.236.211")
    except Exception as e:
        print(f"[ERROR] 測試失敗: {e}")
        import traceback
        traceback.print_exc()

    print()
    print("[DEBUG] 5. 開始正式監聽...")
    print()

    # 直接監聽遊戲主伺服器
    sniff_filter = f"host {GAME_SERVER}"
    print(f"[INFO] 監聽過濾器: {sniff_filter}")
    print(f"[INFO] 遊戲伺服器: {GAME_SERVER}:{GAME_PORT} (UDP)")
    print()

    try:
        sniff(filter=sniff_filter, prn=handler.process, store=0, iface=conf.iface)
    except KeyboardInterrupt:
        print("\n[停止]")
    except Exception as e:
        print(f"[ERROR] {e}")

    handler.stop()
    return 0

if __name__ == "__main__":
    sys.exit(main())
