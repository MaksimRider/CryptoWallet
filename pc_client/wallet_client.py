import argparse
import json
import os
import sys
import time
import urllib.request
from decimal import Decimal, getcontext

import serial
from Crypto.Hash import keccak

getcontext().prec = 80
SEPOLIA_CHAIN_ID = 11155111
GAS_LIMIT_ETH_TRANSFER = 21000


def rpc_call(rpc_url: str, method: str, params: list):
    payload = json.dumps({
        "jsonrpc": "2.0",
        "id": 1,
        "method": method,
        "params": params,
    }).encode("utf-8")

    req = urllib.request.Request(
        rpc_url,
        data=payload,
        headers={"Content-Type": "application/json"},
        method="POST",
    )

    with urllib.request.urlopen(req, timeout=30) as resp:
        data = json.loads(resp.read().decode("utf-8"))

    if "error" in data:
        raise RuntimeError(data["error"])
    return data["result"]


def wei_to_eth_string_value(wei: int, decimals: int = 6) -> str:
    whole = wei // 10**18
    frac = wei % 10**18
    frac_str = f"{frac:018d}"[:decimals].rstrip("0")
    return f"{whole}.{frac_str}" if frac_str else str(whole)


def wei_to_eth_string(hex_wei: str) -> str:
    return wei_to_eth_string_value(int(hex_wei, 16))


def eth_to_wei_decimal(amount_eth: str) -> int:
    value = Decimal(amount_eth)
    if value <= 0:
        raise ValueError("amount must be positive")
    return int(value * Decimal(10**18))


def int_to_min_bytes(value: int) -> bytes:
    if value == 0:
        return b""
    return value.to_bytes((value.bit_length() + 7) // 8, "big")


def clean_hex(value: str) -> str:
    value = value.strip()
    if value.startswith("0x") or value.startswith("0X"):
        value = value[2:]
    return value


def address_to_bytes(address: str) -> bytes:
    h = clean_hex(address)
    if len(h) != 40:
        raise ValueError("recipient address must be 20 bytes / 40 hex chars")
    return bytes.fromhex(h)


def rlp_encode(item):
    if isinstance(item, int):
        return rlp_encode(int_to_min_bytes(item))

    if isinstance(item, bytes):
        if len(item) == 1 and item[0] < 0x80:
            return item
        if len(item) <= 55:
            return bytes([0x80 + len(item)]) + item
        length_bytes = int_to_min_bytes(len(item))
        return bytes([0xB7 + len(length_bytes)]) + length_bytes + item

    if isinstance(item, list):
        payload = b"".join(rlp_encode(x) for x in item)
        if len(payload) <= 55:
            return bytes([0xC0 + len(payload)]) + payload
        length_bytes = int_to_min_bytes(len(payload))
        return bytes([0xF7 + len(length_bytes)]) + length_bytes + payload

    raise TypeError(f"Unsupported RLP item type: {type(item)!r}")


def keccak256(data: bytes) -> bytes:
    h = keccak.new(digest_bits=256)
    h.update(data)
    return h.digest()


def open_serial(port: str):
    ser = serial.Serial(port, 115200, timeout=1)
    time.sleep(2.3)
    try:
        ser.reset_input_buffer()
        ser.reset_output_buffer()
    except Exception:
        pass
    return ser


def read_json_line(ser, timeout_s: float = 20):
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        raw = ser.readline().decode("utf-8", errors="ignore").strip()
        if not raw:
            continue
        print("ESP32:", raw)
        try:
            return json.loads(raw)
        except json.JSONDecodeError:
            continue
    raise TimeoutError("No JSON response from ESP32")


def request_address(ser) -> str:
    ser.write(b'{"cmd":"address"}\n')
    while True:
        msg = read_json_line(ser, timeout_s=20)
        if msg.get("type") == "address":
            return msg.get("address", "")
        if msg.get("type") == "error":
            raise RuntimeError(msg.get("error", "Unknown device error"))


def cmd_address(args):
    with open_serial(args.port) as ser:
        address = request_address(ser)
        print("\nHardware wallet address:")
        print(address)


def cmd_balance(args):
    rpc_url = args.rpc or os.environ.get("RPC_URL", "").strip()
    if not rpc_url:
        print("ERROR: set RPC_URL or pass --rpc")
        sys.exit(1)

    with open_serial(args.port) as ser:
        address = request_address(ser)
        print("Address:", address)
        hex_balance = rpc_call(rpc_url, "eth_getBalance", [address, "latest"])
        balance = wei_to_eth_string(hex_balance)
        print("Balance:", balance, "ETH")


def cmd_watch(args):
    rpc_url = args.rpc or os.environ.get("RPC_URL", "").strip()
    if not rpc_url:
        print("ERROR: set RPC_URL or pass --rpc")
        sys.exit(1)

    print(f"Opening {args.port}. Waiting for requests from ESP32...")
    with open_serial(args.port) as ser:
        while True:
            raw = ser.readline().decode("utf-8", errors="ignore").strip()
            if not raw:
                continue

            print("ESP32:", raw)
            try:
                msg = json.loads(raw)
            except json.JSONDecodeError:
                continue

            if msg.get("cmd") == "balance":
                address = msg.get("address", "")
                try:
                    hex_balance = rpc_call(rpc_url, "eth_getBalance", [address, "latest"])
                    response = {
                        "type": "balance",
                        "balance": wei_to_eth_string(hex_balance),
                        "symbol": "ETH",
                    }
                except Exception as exc:
                    response = {"type": "balance", "balance": "ERR", "symbol": str(exc)[:10]}

                line = json.dumps(response, separators=(",", ":"))
                print("PC -> ESP32:", line)
                ser.write((line + "\n").encode("utf-8"))


def build_legacy_tx(unsigned: dict, chain_id: int):
    tx_for_signing = [
        unsigned["nonce"],
        unsigned["gas_price"],
        unsigned["gas_limit"],
        address_to_bytes(unsigned["to"]),
        unsigned["value"],
        b"",
        chain_id,
        0,
        0,
    ]
    encoded = rlp_encode(tx_for_signing)
    digest = keccak256(encoded)
    return digest


def build_signed_legacy_tx(unsigned: dict, chain_id: int, recid: int, r_hex: str, s_hex: str) -> str:
    r = int(clean_hex(r_hex), 16)
    s = int(clean_hex(s_hex), 16)
    y_parity = int(recid) & 1
    v = chain_id * 2 + 35 + y_parity

    signed_items = [
        unsigned["nonce"],
        unsigned["gas_price"],
        unsigned["gas_limit"],
        address_to_bytes(unsigned["to"]),
        unsigned["value"],
        b"",
        v,
        r,
        s,
    ]
    return "0x" + rlp_encode(signed_items).hex()


def cmd_tx(args):
    rpc_url = args.rpc or os.environ.get("RPC_URL", "").strip()
    if not rpc_url:
        print("ERROR: set RPC_URL or pass --rpc")
        sys.exit(1)

    chain_id = args.chain_id
    gas_limit = args.gas

    with open_serial(args.port) as ser:
        from_addr = request_address(ser)
        print("From:", from_addr)
        print("To:  ", args.to)

        nonce = int(rpc_call(rpc_url, "eth_getTransactionCount", [from_addr, "pending"]), 16)
        gas_price = int(args.gas_price_gwei * Decimal(10**9)) if args.gas_price_gwei else int(rpc_call(rpc_url, "eth_gasPrice", []), 16)
        value = eth_to_wei_decimal(args.amount)
        fee_wei = gas_price * gas_limit
        fee_eth = wei_to_eth_string_value(fee_wei, decimals=8)

        unsigned = {
            "nonce": nonce,
            "gas_price": gas_price,
            "gas_limit": gas_limit,
            "to": args.to,
            "value": value,
        }
        digest = build_legacy_tx(unsigned, chain_id)
        digest_hex = "0x" + digest.hex()

        print("Nonce:", nonce)
        print("Gas price:", gas_price, "wei")
        print("Gas limit:", gas_limit)
        print("Fee:", fee_eth, "ETH")
        print("Unsigned tx hash:", digest_hex)

        req = {
            "cmd": "sign_tx",
            "coin": "ETH",
            "amount": args.amount,
            "to": args.to,
            "fee": fee_eth,
            "hash": digest_hex,
        }
        line = json.dumps(req, separators=(",", ":"))
        print("\nPC -> ESP32:", line)
        ser.write((line + "\n").encode("utf-8"))

        while True:
            msg = read_json_line(ser, timeout_s=180)
            if msg.get("status") == "rejected":
                print("\nDevice rejected transaction.")
                return
            if msg.get("type") == "error":
                raise RuntimeError(msg.get("error", "Device error"))
            if msg.get("type") == "tx_signature" and msg.get("status") == "signed":
                r = msg["r"]
                s = msg["s"]
                recid = int(msg.get("recid", 0))
                print("\nDevice signature received.")
                print("r:", r)
                print("s:", s)
                print("recid:", recid)
                break

        raw_tx = build_signed_legacy_tx(unsigned, chain_id, recid, r, s)
        print("\nSigned raw tx:")
        print(raw_tx)

        if args.dry_run:
            print("\nDry run enabled. Transaction was not broadcast.")
            return

        tx_hash = rpc_call(rpc_url, "eth_sendRawTransaction", [raw_tx])
        print("\nBroadcast OK")
        print("TX hash:", tx_hash)
        print("Explorer:", f"https://sepolia.etherscan.io/tx/{tx_hash}")


def main():
    parser = argparse.ArgumentParser(description="PC client for ESP32 hardware wallet prototype")
    parser.add_argument("port", help="Serial port, for example COM3")
    parser.add_argument("command", choices=["address", "balance", "watch", "tx"])
    parser.add_argument("--rpc", help="Sepolia RPC URL. You can also set RPC_URL env variable.")
    parser.add_argument("--to", help="Recipient address for tx command")
    parser.add_argument("--amount", help="Amount in ETH for tx command")
    parser.add_argument("--chain-id", type=int, default=SEPOLIA_CHAIN_ID)
    parser.add_argument("--gas", type=int, default=GAS_LIMIT_ETH_TRANSFER)
    parser.add_argument("--gas-price-gwei", type=Decimal, help="Manual gas price in gwei. Default: eth_gasPrice from RPC.")
    parser.add_argument("--dry-run", action="store_true", help="Sign transaction but do not broadcast it.")

    args = parser.parse_args()

    if args.command == "address":
        cmd_address(args)
    elif args.command == "balance":
        cmd_balance(args)
    elif args.command == "watch":
        cmd_watch(args)
    elif args.command == "tx":
        if not args.to or not args.amount:
            parser.error("tx command requires --to and --amount")
        cmd_tx(args)


if __name__ == "__main__":
    main()
