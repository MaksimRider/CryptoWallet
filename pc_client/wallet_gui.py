import json
import os
import queue
import threading
import time
import tkinter as tk
import webbrowser
from pathlib import Path
from tkinter import ttk, messagebox

import serial
from serial.tools import list_ports

from wallet_client import (
    SEPOLIA_CHAIN_ID,
    GAS_LIMIT_ETH_TRANSFER,
    rpc_call,
    wei_to_eth_string_value,
    eth_to_wei_decimal,
    build_legacy_tx,
    build_signed_legacy_tx,
)

APP_TITLE = "ESP32 Hardware Wallet"
APP_VERSION = "1.2 Final"
POLL_INTERVAL_MS = 10000

# Default RPC аpi 
DEFAULT_ALCHEMY_KEY = "KUDjQ4yezVaMuld4Kxgvk"
DEFAULT_RPC_URL = f"https://eth-sepolia.g.alchemy.com/v2/{DEFAULT_ALCHEMY_KEY}"
CONFIG_FILE = Path(__file__).with_name("wallet_app_config.json")


class WalletGUI(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title(f"{APP_TITLE} · {APP_VERSION}")
        self.geometry("1120x740")
        self.minsize(1040, 680)

        self.ser = None
        self.reader_thread = None
        self.stop_reader = threading.Event()
        self.serial_lock = threading.Lock()
        self.inbox = queue.Queue()
        self.log_queue = queue.Queue()
        self.worker = None
        self.balance_after_id = None
        self.balance_worker_busy = False

        self.settings = self.load_settings()

        self.connected = tk.BooleanVar(value=False)
        self.status = tk.StringVar(value="Disconnected")
        self.port_var = tk.StringVar(value=self.settings.get("port", ""))
        self.rpc_var = tk.StringVar(value=self.settings.get("rpc_url", DEFAULT_RPC_URL))
        self.rpc_status = tk.StringVar(value="Sepolia RPC ready")
        self.current_address = tk.StringVar(value="—")
        self.current_balance = tk.StringVar(value="—")
        self.device_state = tk.StringVar(value="Connect the wallet to start")
        self.last_seen_balance = tk.StringVar(value="—")

        self.to_var = tk.StringVar()
        self.amount_var = tk.StringVar(value="0.001")
        self.simulation_var = tk.BooleanVar(value=False)
        self.send_unlocked = False
        self.send_from_address = ""
        self.last_balance_wei = None

        self._build_ui()
        self.refresh_ports(keep_existing=True)
        self.after(100, self._poll_log_queue)
        self.protocol("WM_DELETE_WINDOW", self.on_close)
        self.log("Application ready. RPC is preconfigured for Alchemy Sepolia.")
        self.log("Choose COM port if needed, press Connect, then use Show Balance / Send ETH on the device.")

    #Setting
    def load_settings(self):
        default = {"port": "", "rpc_url": DEFAULT_RPC_URL}
        if not CONFIG_FILE.exists():
            return default
        try:
            data = json.loads(CONFIG_FILE.read_text(encoding="utf-8"))
            if not isinstance(data, dict):
                return default
            return {
                "port": str(data.get("port", default["port"])),
                "rpc_url": str(data.get("rpc_url", default["rpc_url"])).strip() or default["rpc_url"],
            }
        except Exception:
            return default

    def save_settings(self):
        self.settings = {"port": self.port_var.get().strip(), "rpc_url": self.rpc_var.get().strip() or DEFAULT_RPC_URL}
        try:
            CONFIG_FILE.write_text(json.dumps(self.settings, indent=2, ensure_ascii=False), encoding="utf-8")
        except Exception as exc:
            self.log(f"Could not save settings: {exc}")

    #ui
    def _build_ui(self):
        self.configure(bg="#08111f")

        style = ttk.Style(self)
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass

        bg = "#08111f"
        card = "#101a2e"
        card2 = "#13233d"
        fg = "#e5eefb"
        muted = "#9fb0c8"
        accent = "#38bdf8"
        green = "#22c55e"
        red = "#ef4444"
        yellow = "#f59e0b"

        style.configure("TFrame", background=bg)
        style.configure("Card.TFrame", background=card)
        style.configure("Soft.TFrame", background=card2)
        style.configure("TLabel", background=bg, foreground=fg, font=("Segoe UI", 10))
        style.configure("Card.TLabel", background=card, foreground=fg, font=("Segoe UI", 10))
        style.configure("Muted.TLabel", background=card, foreground=muted, font=("Segoe UI", 9))
        style.configure("SoftMuted.TLabel", background=card2, foreground=muted, font=("Segoe UI", 9))
        style.configure("Title.TLabel", background=bg, foreground=fg, font=("Segoe UI", 23, "bold"))
        style.configure("Subtitle.TLabel", background=bg, foreground=muted, font=("Segoe UI", 10))
        style.configure("Hero.TLabel", background=card, foreground=fg, font=("Segoe UI", 14, "bold"))
        style.configure("Balance.TLabel", background=card, foreground=accent, font=("Segoe UI", 28, "bold"))
        style.configure("Success.TLabel", background=card, foreground=green, font=("Segoe UI", 10, "bold"))
        style.configure("Warn.TLabel", background=card, foreground=yellow, font=("Segoe UI", 10, "bold"))
        style.configure("Danger.TLabel", background=card, foreground=red, font=("Segoe UI", 10, "bold"))
        style.configure("Card.TLabelframe", background=card, foreground=fg, borderwidth=1, relief="solid", padding=13)
        style.configure("Card.TLabelframe.Label", background=card, foreground=fg, font=("Segoe UI", 10, "bold"))
        style.configure("TButton", font=("Segoe UI", 10), padding=7)
        style.configure("Accent.TButton", font=("Segoe UI", 10, "bold"), padding=8)
        style.configure("Ghost.TButton", font=("Segoe UI", 10), padding=6)
        style.configure("TEntry", fieldbackground="#07101d", foreground=fg, insertcolor=fg)
        style.configure("TCombobox", fieldbackground="#07101d", foreground=fg)
        style.configure("TCheckbutton", background=card, foreground=fg)

        root = ttk.Frame(self, padding=16)
        root.pack(fill="both", expand=True)

        header = ttk.Frame(root)
        header.pack(fill="x", pady=(0, 14))

        logo = tk.Canvas(header, width=82, height=82, bg=bg, highlightthickness=0)
        logo.pack(side="left", padx=(0, 14))
        logo.create_oval(7, 7, 75, 75, fill="#0ea5e9", outline="")
        logo.create_oval(12, 12, 70, 70, outline="#67e8f9", width=2)
        logo.create_polygon(41, 18, 61, 28, 57, 58, 41, 68, 25, 58, 21, 28, fill="#08111f", outline="#e0f2fe", width=2)
        logo.create_arc(32, 27, 50, 45, start=0, extent=180, outline="#e0f2fe", width=2)
        logo.create_rectangle(30, 39, 52, 55, outline="#e0f2fe", width=2)
        logo.create_text(41, 47, text="✓", fill="#22c55e", font=("Segoe UI", 12, "bold"))

        title_box = ttk.Frame(header)
        title_box.pack(side="left", fill="x", expand=True)
        ttk.Label(title_box, text="ESP32 Hardware Wallet", style="Title.TLabel").pack(anchor="w")
        ttk.Label(title_box, text="Sepolia ETH · hardware confirmation · cold-wallet workflow", style="Subtitle.TLabel").pack(anchor="w")

        top_buttons = ttk.Frame(header)
        top_buttons.pack(side="right")
        self.status_pill = tk.Label(top_buttons, textvariable=self.status, bg="#1f2937", fg="#fbbf24", padx=12, pady=6, font=("Segoe UI", 10, "bold"))
        self.status_pill.pack(side="left", padx=(0, 8))
        self.settings_button = tk.Button(top_buttons, text="⚙", command=self.open_settings, bg="#1f2937", fg="#e5eefb", bd=0, padx=12, pady=5, font=("Segoe UI", 14, "bold"), activebackground="#334155", activeforeground="#ffffff")
        self.settings_button.pack(side="left")

        body = ttk.Frame(root)
        body.pack(fill="both", expand=True)
        body.columnconfigure(0, weight=0)
        body.columnconfigure(1, weight=1)
        body.rowconfigure(0, weight=1)

        left = ttk.Frame(body)
        left.grid(row=0, column=0, sticky="nsw", padx=(0, 14))
        right = ttk.Frame(body)
        right.grid(row=0, column=1, sticky="nsew")
        right.columnconfigure(0, weight=1)
        right.rowconfigure(3, weight=1)

        # Connection card
        conn = ttk.LabelFrame(left, text="Connection", style="Card.TLabelframe")
        conn.pack(fill="x", pady=(0, 12))
        ttk.Label(conn, text="Hardware wallet", style="Card.TLabel").pack(anchor="w")
        self.port_label = ttk.Label(conn, textvariable=self.port_var, style="Muted.TLabel")
        self.port_label.pack(anchor="w", pady=(2, 8))
        self.connect_button = ttk.Button(conn, text="Connect", style="Accent.TButton", command=self.toggle_connection)
        self.connect_button.pack(fill="x", pady=(0, 8))
        ttk.Button(conn, text="Refresh ports", command=lambda: self.refresh_ports(keep_existing=False)).pack(fill="x")

        # Network card
        net = ttk.LabelFrame(left, text="Network", style="Card.TLabelframe")
        net.pack(fill="x", pady=(0, 12))
        ttk.Label(net, text="Ethereum Sepolia", style="Card.TLabel").pack(anchor="w")
        ttk.Label(net, textvariable=self.rpc_status, style="Muted.TLabel", wraplength=245).pack(anchor="w", pady=(2, 8))
        ttk.Button(net, text="Test RPC", command=self.test_rpc).pack(fill="x")

        # Actions
        actions = ttk.LabelFrame(left, text="Wallet actions", style="Card.TLabelframe")
        actions.pack(fill="x", pady=(0, 12))
        ttk.Button(actions, text="Get address", command=self.get_address).pack(fill="x", pady=(0, 6))
        ttk.Button(actions, text="Get balance", command=self.get_balance).pack(fill="x", pady=(0, 6))
        ttk.Button(actions, text="Copy address", command=self.copy_address).pack(fill="x", pady=(0, 6))
        ttk.Button(actions, text="Open address in Etherscan", command=self.open_address_explorer).pack(fill="x")

        flow = ttk.LabelFrame(left, text="Safe workflow", style="Card.TLabelframe")
        flow.pack(fill="x")
        ttk.Label(flow, text="1. Connect app to wallet", style="Muted.TLabel").pack(anchor="w")
        ttk.Label(flow, text="2. On device: Show Balance", style="Muted.TLabel").pack(anchor="w")
        ttk.Label(flow, text="3. On device: Send ETH", style="Muted.TLabel").pack(anchor="w")
        ttk.Label(flow, text="4. Enter recipient and amount", style="Muted.TLabel").pack(anchor="w")
        ttk.Label(flow, text="5. Device: Approve or Reject", style="Muted.TLabel").pack(anchor="w")

        # Wallet status
        wallet = ttk.LabelFrame(right, text="Wallet status", style="Card.TLabelframe")
        wallet.grid(row=0, column=0, sticky="ew", pady=(0, 12))
        wallet.columnconfigure(1, weight=1)
        ttk.Label(wallet, text="State", style="Card.TLabel").grid(row=0, column=0, sticky="w", padx=6, pady=4)
        ttk.Label(wallet, textvariable=self.device_state, style="Success.TLabel").grid(row=0, column=1, sticky="w", padx=6, pady=4)
        ttk.Label(wallet, text="Address", style="Card.TLabel").grid(row=1, column=0, sticky="w", padx=6, pady=4)
        ttk.Entry(wallet, textvariable=self.current_address, state="readonly").grid(row=1, column=1, sticky="ew", padx=6, pady=4)
        ttk.Label(wallet, text="Balance", style="Card.TLabel").grid(row=2, column=0, sticky="w", padx=6, pady=4)
        ttk.Label(wallet, textvariable=self.current_balance, style="Balance.TLabel").grid(row=2, column=1, sticky="w", padx=6, pady=2)
        ttk.Label(wallet, text="Last update", style="Card.TLabel").grid(row=3, column=0, sticky="w", padx=6, pady=4)
        ttk.Label(wallet, textvariable=self.last_seen_balance, style="Muted.TLabel").grid(row=3, column=1, sticky="w", padx=6, pady=4)

        # Send card
        self.send_frame = ttk.LabelFrame(right, text="Send ETH — unlocked by hardware wallet", style="Card.TLabelframe")
        self.send_frame.grid(row=1, column=0, sticky="ew", pady=(0, 12))
        self.send_frame.columnconfigure(1, weight=1)
        ttk.Label(self.send_frame, text="Recipient", style="Card.TLabel").grid(row=0, column=0, sticky="w", padx=6, pady=5)
        ttk.Entry(self.send_frame, textvariable=self.to_var).grid(row=0, column=1, columnspan=2, sticky="ew", padx=6, pady=5)
        ttk.Label(self.send_frame, text="Amount ETH", style="Card.TLabel").grid(row=1, column=0, sticky="w", padx=6, pady=5)
        ttk.Entry(self.send_frame, textvariable=self.amount_var, width=18).grid(row=1, column=1, sticky="w", padx=6, pady=5)
        ttk.Checkbutton(self.send_frame, text="Dry run: sign only, do not broadcast", variable=self.simulation_var).grid(row=2, column=1, columnspan=2, sticky="w", padx=6, pady=5)
        ttk.Button(self.send_frame, text="Cancel form", command=self.hide_send_form).grid(row=3, column=1, sticky="e", padx=6, pady=(10, 4))
        ttk.Button(self.send_frame, text="Prepare transaction", style="Accent.TButton", command=self.send_tx).grid(row=3, column=2, sticky="e", padx=6, pady=(10, 4))
        self.send_frame.grid_remove()

        # Demo panel
        hint = ttk.LabelFrame(right, text="Demo mode", style="Card.TLabelframe")
        hint.grid(row=2, column=0, sticky="ew", pady=(0, 12))
        ttk.Label(
            hint,
            text="The PC app prepares Sepolia data. The ESP32 device displays details and signs only after hardware approval.",
            style="Muted.TLabel",
        ).pack(anchor="w", padx=6, pady=4)

        log_frame = ttk.LabelFrame(right, text="Event log", style="Card.TLabelframe")
        log_frame.grid(row=3, column=0, sticky="nsew")
        log_frame.rowconfigure(0, weight=1)
        log_frame.columnconfigure(0, weight=1)
        self.log_text = tk.Text(log_frame, wrap="word", height=15, font=("Consolas", 9), bg="#020617", fg="#d1d5db", insertbackground="#e5e7eb", relief="flat")
        self.log_text.grid(row=0, column=0, sticky="nsew")
        scrollbar = ttk.Scrollbar(log_frame, orient="vertical", command=self.log_text.yview)
        scrollbar.grid(row=0, column=1, sticky="ns")
        self.log_text.configure(yscrollcommand=scrollbar.set)
        log_buttons = ttk.Frame(log_frame, style="Card.TFrame")
        log_buttons.grid(row=1, column=0, sticky="ew", pady=(8, 0))
        ttk.Button(log_buttons, text="Clear log", command=lambda: self.log_text.delete("1.0", "end")).pack(side="right")

    def open_settings(self):
        win = tk.Toplevel(self)
        win.title("Settings")
        win.geometry("560x275")
        win.resizable(False, False)
        win.configure(bg="#101a2e")
        win.transient(self)
        win.grab_set()

        ports = [p.device for p in list_ports.comports()]
        local_port = tk.StringVar(value=self.port_var.get())
        local_rpc = tk.StringVar(value=self.rpc_var.get())

        frame = ttk.Frame(win, padding=16, style="Card.TFrame")
        frame.pack(fill="both", expand=True)
        ttk.Label(frame, text="Application settings", style="Hero.TLabel").pack(anchor="w", pady=(0, 12))

        ttk.Label(frame, text="COM port", style="Card.TLabel").pack(anchor="w")
        combo = ttk.Combobox(frame, textvariable=local_port, values=ports, state="readonly")
        combo.pack(fill="x", pady=(4, 10))

        ttk.Label(frame, text="Sepolia RPC URL", style="Card.TLabel").pack(anchor="w")
        ttk.Entry(frame, textvariable=local_rpc, show="•").pack(fill="x", pady=(4, 6))
        ttk.Label(frame, text="Default RPC is already embedded for the diploma demo. You can replace it here.", style="Muted.TLabel").pack(anchor="w", pady=(0, 14))

        buttons = ttk.Frame(frame, style="Card.TFrame")
        buttons.pack(fill="x", pady=(6, 0))

        def use_default():
            local_rpc.set(DEFAULT_RPC_URL)

        def save():
            self.port_var.set(local_port.get().strip())
            self.rpc_var.set(local_rpc.get().strip() or DEFAULT_RPC_URL)
            self.rpc_status.set("Sepolia RPC configured")
            self.save_settings()
            win.destroy()
            self.log("Settings saved.")

        ttk.Button(buttons, text="Use default Alchemy", command=use_default).pack(side="left")
        ttk.Button(buttons, text="Cancel", command=win.destroy).pack(side="right", padx=(8, 0))
        ttk.Button(buttons, text="Save", style="Accent.TButton", command=save).pack(side="right")

    # pidkazku
    def refresh_ports(self, keep_existing=True):
        ports = [p.device for p in list_ports.comports()]
        if keep_existing and self.port_var.get() in ports:
            return
        if ports:
            preferred = self.settings.get("port", "")
            self.port_var.set(preferred if preferred in ports else ports[0])
        else:
            self.port_var.set("")

    def log(self, text=""):
        self.log_queue.put(str(text))

    def _poll_log_queue(self):
        try:
            while True:
                text = self.log_queue.get_nowait()
                self.log_text.insert("end", text + "\n")
                self.log_text.see("end")
        except queue.Empty:
            pass
        self.after(100, self._poll_log_queue)

    def set_status(self, text, connected=None):
        self.status.set(text)
        if connected is not None:
            self.connected.set(connected)
            self.connect_button.configure(text="Disconnect" if connected else "Connect")
            self.status_pill.configure(bg="#064e3b" if connected else "#1f2937", fg="#bbf7d0" if connected else "#fbbf24")

    def set_balance_label(self, balance_eth: str):
        self.current_balance.set(f"{balance_eth} ETH")
        self.last_seen_balance.set(time.strftime("%H:%M:%S"))

    #Log
    def toggle_connection(self):
        if self.ser:
            self.disconnect()
        else:
            self.connect()

    def connect(self):
        port = self.port_var.get().strip()
        if not port:
            messagebox.showerror("No COM port", "Select COM port in settings first.")
            return

        def task():
            self.after(0, lambda: self.set_status("Connecting..."))
            self.log(f"Opening {port}...")
            ser = serial.Serial(port, 115200, timeout=0.25)
            try:
                ser.setDTR(False)
                ser.setRTS(False)
            except Exception:
                pass
            time.sleep(2.4)
            ser.reset_input_buffer()
            ser.reset_output_buffer()

            self.ser = ser
            self.stop_reader.clear()
            self.reader_thread = threading.Thread(target=self.reader_loop, daemon=True)
            self.reader_thread.start()
            self.after(0, lambda: self.set_status(f"Connected to {port}", True))
            self.after(0, lambda: self.device_state.set("Connected"))
            self.save_settings()
            self.log("Connected. Hardware-driven workflow is enabled.")

            try:
                address = self.request_address()
                self.after(0, lambda: self.current_address.set(address))
                self.log(f"Address: {address}")
            except Exception as exc:
                self.log(f"Address read skipped: {exc}")

            self.start_balance_monitor()

        self.run_worker("Connect", task)

    def disconnect(self):
        self.stop_balance_monitor()
        self.stop_reader.set()
        with self.serial_lock:
            if self.ser:
                try:
                    self.ser.close()
                except Exception:
                    pass
                self.ser = None
        self.set_status("Disconnected", False)
        self.device_state.set("Disconnected")
        self.hide_send_form()
        self.log("Disconnected.")

    def reader_loop(self):
        while not self.stop_reader.is_set():
            try:
                with self.serial_lock:
                    ser = self.ser
                if not ser:
                    break
                raw = ser.readline().decode("utf-8", errors="ignore").strip()
                if not raw:
                    continue
                if raw.startswith("{"):
                    self.log("Device → " + raw)
                    try:
                        msg = json.loads(raw)
                    except json.JSONDecodeError:
                        continue
                    self.handle_device_json(msg)
                else:
                    if any(tag in raw for tag in ("FW", "ATECC", "TX", "Incoming", "Balance", "Send", "Wallet")):
                        self.log("Device: " + raw)
            except Exception as exc:
                if not self.stop_reader.is_set():
                    self.log(f"Serial error: {exc}")
                    self.after(0, self.disconnect)
                break

    def handle_device_json(self, msg):
        if msg.get("cmd") == "balance":
            self.after(0, lambda m=msg: self.handle_balance_request(m))
            return
        if msg.get("cmd") == "open_send":
            self.after(0, lambda m=msg: self.handle_open_send(m))
            return
        self.inbox.put(msg)

    def send_json(self, obj):
        line = json.dumps(obj, separators=(",", ":"))
        with self.serial_lock:
            if not self.ser:
                raise RuntimeError("Wallet is not connected.")
            self.ser.write((line + "\n").encode("utf-8"))
        self.log("PC → " + line)

    def wait_for(self, predicate, timeout=30):
        deadline = time.time() + timeout
        buffered = []
        while time.time() < deadline:
            try:
                msg = self.inbox.get(timeout=0.2)
            except queue.Empty:
                continue
            if predicate(msg):
                for item in buffered:
                    self.inbox.put(item)
                return msg
            buffered.append(msg)
        for item in buffered:
            self.inbox.put(item)
        raise TimeoutError("Timeout waiting for device response.")

    def request_address(self):
        self.send_json({"cmd": "address"})
        msg = self.wait_for(lambda m: m.get("type") in ("address", "error"), timeout=25)
        if msg.get("type") == "error":
            raise RuntimeError(msg.get("error", "Device error"))
        address = msg.get("address", "")
        if not address.startswith("0x"):
            raise RuntimeError("Device returned invalid address.")
        return address

    def require_connected(self):
        if not self.ser:
            raise RuntimeError("Connect hardware wallet first.")

    def require_rpc(self):
        rpc = self.rpc_var.get().strip() or DEFAULT_RPC_URL
        if not rpc:
            raise RuntimeError("RPC URL is missing. Open settings and set Sepolia RPC URL.")
        return rpc

    
    def start_balance_monitor(self):
        self.stop_balance_monitor()
        self.balance_after_id = self.after(2000, self.balance_poll_tick)

    def stop_balance_monitor(self):
        if self.balance_after_id:
            try:
                self.after_cancel(self.balance_after_id)
            except Exception:
                pass
            self.balance_after_id = None

    def balance_poll_tick(self):
        self.balance_after_id = self.after(POLL_INTERVAL_MS, self.balance_poll_tick)
        if self.balance_worker_busy or not self.ser:
            return
        address = self.current_address.get().strip()
        if not address.startswith("0x"):
            return
        self.balance_worker_busy = True
        self.run_worker("Background balance monitor", self._balance_poll_worker, allow_parallel=True, quiet=True)

    def _balance_poll_worker(self):
        try:
            rpc = self.require_rpc()
            address = self.current_address.get().strip()
            hex_balance = rpc_call(rpc, "eth_getBalance", [address, "latest"])
            balance_wei = int(hex_balance, 16)
            balance_eth = wei_to_eth_string_value(balance_wei, decimals=6)
            self.after(0, lambda b=balance_eth: self.set_balance_label(b))

            if self.last_balance_wei is None:
                self.last_balance_wei = balance_wei
                return

            if balance_wei > self.last_balance_wei:
                delta = balance_wei - self.last_balance_wei
                amount = wei_to_eth_string_value(delta, decimals=6)
                self.last_balance_wei = balance_wei
                self.log(f"Incoming transfer detected: +{amount} ETH")
                self.after(0, lambda a=amount: self.device_state.set(f"Incoming +{a} ETH"))
                try:
                    self.send_json({"type": "incoming", "amount": amount, "symbol": "ETH"})
                except Exception as exc:
                    self.log(f"Could not notify device about incoming transfer: {exc}")
                self.bell()
            elif balance_wei != self.last_balance_wei:
                self.last_balance_wei = balance_wei
                self.log(f"Balance updated: {balance_eth} ETH")
        finally:
            self.balance_worker_busy = False

    #Initializasion
    def handle_balance_request(self, msg):
        address = msg.get("address", "")
        if address:
            self.current_address.set(address)
        self.log("Balance requested from hardware wallet.")

        def task():
            try:
                rpc = self.require_rpc()
                hex_balance = rpc_call(rpc, "eth_getBalance", [address, "latest"])
                balance_wei = int(hex_balance, 16)
                balance = wei_to_eth_string_value(balance_wei, decimals=6)
                self.last_balance_wei = balance_wei
                response = {"type": "balance", "balance": balance, "symbol": "ETH"}
                self.after(0, lambda: self.set_balance_label(balance))
                self.log(f"Balance sent to device: {balance} ETH")
            except Exception as exc:
                response = {"type": "balance", "balance": "ERR", "symbol": "ETH"}
                self.log(f"Balance error: {exc}")
            try:
                self.send_json(response)
            except Exception as exc:
                self.log(f"Could not send balance to device: {exc}")

        self.run_worker("Device balance request", task, allow_parallel=True)

    def handle_open_send(self, msg):
        address = msg.get("address", "")
        self.send_from_address = address
        if address:
            self.current_address.set(address)
        self.send_unlocked = True
        self.device_state.set("Send ETH unlocked by device")
        self.send_frame.grid()
        self.to_var.set("")
        self.amount_var.set("0.001")
        self.log("Send form unlocked by hardware wallet.")
        self.bell()
        self.lift()
        self.focus_force()

    def hide_send_form(self):
        self.send_unlocked = False
        try:
            self.send_frame.grid_remove()
        except Exception:
            pass
        if self.connected.get():
            self.device_state.set("Connected")

    #  App buttons / workers 
    def run_worker(self, title, target, allow_parallel=False, quiet=False):
        if not allow_parallel and self.worker and self.worker.is_alive():
            messagebox.showwarning("Busy", "Another operation is already running.")
            return

        def wrapper():
            if not quiet:
                self.log(f"\n--- {title} ---")
            try:
                target()
            except Exception as exc:
                self.log(f"ERROR: {exc}")
                if not quiet:
                    self.after(0, lambda: messagebox.showerror("Error", str(exc)))
            finally:
                if title == "Background balance monitor":
                    self.balance_worker_busy = False

        t = threading.Thread(target=wrapper, daemon=True)
        if not allow_parallel:
            self.worker = t
        t.start()

    def test_rpc(self):
        def task():
            rpc = self.require_rpc()
            chain_id = int(rpc_call(rpc, "eth_chainId", []), 16)
            self.log(f"RPC chain id: {chain_id}")
            if chain_id != SEPOLIA_CHAIN_ID:
                raise RuntimeError(f"RPC is not Sepolia. Expected {SEPOLIA_CHAIN_ID}, got {chain_id}.")
            self.after(0, lambda: self.rpc_status.set("RPC OK · Ethereum Sepolia"))
            self.log("RPC OK: Sepolia")
        self.run_worker("Test RPC", task)

    def get_address(self):
        def task():
            self.require_connected()
            address = self.request_address()
            self.after(0, lambda: self.current_address.set(address))
            self.log(f"Address: {address}")
        self.run_worker("Get address", task)

    def get_balance(self):
        def task():
            self.require_connected()
            rpc = self.require_rpc()
            address = self.current_address.get().strip()
            if not address or address == "—":
                address = self.request_address()
                self.after(0, lambda: self.current_address.set(address))
            hex_balance = rpc_call(rpc, "eth_getBalance", [address, "latest"])
            balance_wei = int(hex_balance, 16)
            self.last_balance_wei = balance_wei
            balance = wei_to_eth_string_value(balance_wei, decimals=6)
            self.after(0, lambda: self.set_balance_label(balance))
            self.log(f"Balance: {balance} ETH")
        self.run_worker("Get balance", task)

    def copy_address(self):
        address = self.current_address.get().strip()
        if not address.startswith("0x"):
            messagebox.showwarning("No address", "Read wallet address first.")
            return
        self.clipboard_clear()
        self.clipboard_append(address)
        self.log("Address copied to clipboard.")

    def open_address_explorer(self):
        address = self.current_address.get().strip()
        if not address.startswith("0x"):
            messagebox.showwarning("No address", "Read wallet address first.")
            return
        webbrowser.open(f"https://sepolia.etherscan.io/address/{address}")

    def send_tx(self):
        if not self.send_unlocked:
            messagebox.showwarning("Locked", "Choose Send ETH on the hardware wallet first.")
            return

        def task():
            self.require_connected()
            rpc = self.require_rpc()
            recipient = self.to_var.get().strip()
            amount = self.amount_var.get().strip()
            if not recipient.startswith("0x") or len(recipient) != 42:
                raise RuntimeError("Recipient must be an Ethereum address like 0x...")
            if not amount:
                raise RuntimeError("Enter amount.")

            from_addr = self.send_from_address or self.current_address.get().strip()
            if not from_addr or from_addr == "—":
                from_addr = self.request_address()
                self.after(0, lambda: self.current_address.set(from_addr))

            self.log(f"From: {from_addr}")
            self.log(f"To:   {recipient}")

            nonce = int(rpc_call(rpc, "eth_getTransactionCount", [from_addr, "pending"]), 16)
            gas_price = int(rpc_call(rpc, "eth_gasPrice", []), 16)
            value = eth_to_wei_decimal(amount)
            fee_wei = gas_price * GAS_LIMIT_ETH_TRANSFER
            fee_eth = wei_to_eth_string_value(fee_wei, decimals=8)

            unsigned = {
                "nonce": nonce,
                "gas_price": gas_price,
                "gas_limit": GAS_LIMIT_ETH_TRANSFER,
                "to": recipient,
                "value": value,
            }
            digest = build_legacy_tx(unsigned, SEPOLIA_CHAIN_ID)
            digest_hex = "0x" + digest.hex()

            self.log(f"Nonce: {nonce}")
            self.log(f"Gas price: {gas_price} wei")
            self.log(f"Fee: {fee_eth} ETH")
            self.log(f"Unsigned tx hash: {digest_hex}")
            self.log("Confirm or reject transaction on hardware wallet.")

            req = {"cmd": "sign_tx", "coin": "ETH", "amount": amount, "to": recipient, "fee": fee_eth, "hash": digest_hex}
            self.send_json(req)

            msg = self.wait_for(
                lambda m: (m.get("type") == "tx_signature" and m.get("status") == "signed") or m.get("type") == "error" or m.get("status") == "rejected",
                timeout=180,
            )
            if msg.get("status") == "rejected":
                self.log("Device rejected transaction.")
                self.after(0, lambda: self.device_state.set("Transaction rejected"))
                return
            if msg.get("type") == "error":
                raise RuntimeError(msg.get("error", "Device error"))

            r = msg["r"]
            s = msg["s"]
            recid = int(msg.get("recid", 0))
            raw_tx = build_signed_legacy_tx(unsigned, SEPOLIA_CHAIN_ID, recid, r, s)
            self.log("Device signature received.")
            self.log("Signed raw tx:")
            self.log(raw_tx)

            if self.simulation_var.get():
                self.log("Dry run enabled. Transaction was not broadcast.")
                try:
                    self.send_json({"type": "tx_status", "status": "dry_run"})
                except Exception:
                    pass
                return

            try:
                tx_hash = rpc_call(rpc, "eth_sendRawTransaction", [raw_tx])
                self.log("Broadcast OK")
                self.log("TX hash: " + tx_hash)
                self.log("Explorer: https://sepolia.etherscan.io/tx/" + tx_hash)
                self.send_json({"type": "tx_status", "status": "sent", "hash": tx_hash})
                self.after(0, lambda: self.device_state.set("Transaction sent"))
                self.after(0, lambda: messagebox.showinfo("Broadcast OK", "TX hash:\n" + tx_hash))
                self.after(0, self.hide_send_form)
                self.last_balance_wei = None
            except Exception as exc:
                try:
                    self.send_json({"type": "tx_status", "status": "error", "error": str(exc)[:60]})
                except Exception:
                    pass
                raise

        self.run_worker("Send transaction", task)

    def on_close(self):
        self.disconnect()
        self.destroy()


if __name__ == "__main__":
    app = WalletGUI()
    app.mainloop()
