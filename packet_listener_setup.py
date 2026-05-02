#!/usr/bin/env python3
# ============================================================
# Packet Listener 依賴檢查與設定腳本
# ============================================================
import subprocess
import sys
import os

def check_npcap():
    """檢查是否已安裝 Npcap"""
    try:
        import ctypes
        # 嘗試加載 wpcap.dll
        dll = ctypes.WinDLL("wpcap")
        print("[OK] Npcap 已安裝")
        return True
    except (OSError, ImportError):
        print("[X] Npcap 未安裝")
        return False

def check_admin():
    """檢查是否以管理員身份執行"""
    try:
        import ctypes
        return ctypes.windll.shell32.IsUserAnAdmin() != 0
    except:
        return False

def install_npcap():
    """開啟 Npcap 下載頁面"""
    url = "https://npcap.com/#download"
    print(f"\n請下載並安裝 Npcap:")
    print(f"  1. 訪問: {url}")
    print(f"  2. 下載 npcap-1.x.exe")
    print(f"  3. 安裝時選擇 'WinPcap API-compatible Mode'")
    print(f"  4. 重新執行本腳本")

    try:
        import webbrowser
        webbrowser.open(url)
    except:
        pass

def main():
    print("=" * 60)
    print("RAN2 Packet Listener 環境檢查")
    print("=" * 60)
    print()

    # 檢查 Npcap
    npcap_ok = check_npcap()

    # 檢查管理員權限
    admin = check_admin()
    if admin:
        print("[OK] 管理員權限")
    else:
        print("[!] 需要管理員權限")

    print()
    print("=" * 60)

    if npcap_ok and admin:
        print("環境就緒！可以執行 packet_listener.py")
        print()
        print("使用方法:")
        print("  python packet_listener.py")
        return 0
    else:
        print("環境未就緒，需要修復:")
        print()

        if not npcap_ok:
            print("  - 安裝 Npcap")
            install_npcap()

        if not admin:
            print("  - 以管理員身份執行")

        return 1

if __name__ == "__main__":
    sys.exit(main())
