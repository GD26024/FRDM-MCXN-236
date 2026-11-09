from collections import deque
import queue
import re
import threading
import time
import tkinter as tk
from tkinter import messagebox, ttk

from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
from matplotlib.figure import Figure
from matplotlib.ticker import FuncFormatter
import serial
from serial.tools import list_ports


BAUD_RATE = 115200
POLL_INTERVAL_MS = 1000
TEMPERATURE_INTERVAL_TICKS = 5
MAX_TEMPERATURE_SAMPLES = 3600


class UartDashboard:
    def __init__(self, root):
        self.root = root
        self.root.title("FRDM-MCXN236 Dashboard")
        self.root.geometry("720x560")
        self.root.minsize(620, 480)

        self.connection = None
        self.reader_stop = None
        self.messages = queue.Queue()
        self.poll_ticks = 0
        self.temperature_samples = deque(maxlen=MAX_TEMPERATURE_SAMPLES)
        self.graph_started_at = time.monotonic()
        self.graph_window = None
        self.graph_axes = None
        self.graph_line = None
        self.graph_canvas = None
        self.port_var = tk.StringVar()
        self.connection_var = tk.StringVar(value="Disconnected")
        self.led_var = tk.StringVar(value="Unknown")
        self.temperature_var = tk.StringVar(value="--.- C")
        self.uptime_var = tk.StringVar(value="--:--:--")
        self.graph_status_var = tk.StringVar(value="No temperature samples yet")

        self._build_ui()
        self.refresh_ports()
        self.root.after(100, self.process_messages)
        self.root.protocol("WM_DELETE_WINDOW", self.close)

    def _build_ui(self):
        self.root.configure(bg="#edf1f4")
        style = ttk.Style(self.root)
        style.theme_use("clam")
        style.configure("TFrame", background="#edf1f4")
        style.configure("Panel.TFrame", background="#ffffff")
        style.configure("Title.TLabel", background="#edf1f4", foreground="#17212b", font=("Segoe UI", 18, "bold"))
        style.configure("Caption.TLabel", background="#ffffff", foreground="#53616f", font=("Segoe UI", 10))
        style.configure("Value.TLabel", background="#ffffff", foreground="#17212b", font=("Segoe UI", 24, "bold"))
        style.configure("Status.TLabel", background="#edf1f4", foreground="#53616f", font=("Segoe UI", 10))
        style.configure("TButton", font=("Segoe UI", 10), padding=(10, 7))

        outer = ttk.Frame(self.root, padding=18)
        outer.pack(fill="both", expand=True)

        heading = ttk.Frame(outer)
        heading.pack(fill="x", pady=(0, 14))
        ttk.Label(heading, text="Board Dashboard", style="Title.TLabel").pack(side="left")
        ttk.Label(heading, textvariable=self.connection_var, style="Status.TLabel").pack(side="right", pady=8)

        connection = ttk.Frame(outer)
        connection.pack(fill="x", pady=(0, 14))
        ttk.Label(connection, text="Serial port:").pack(side="left", padx=(0, 8))
        self.port_box = ttk.Combobox(connection, textvariable=self.port_var, width=18, state="readonly")
        self.port_box.pack(side="left", padx=(0, 8))
        ttk.Button(connection, text="Rescan Ports", command=self.refresh_ports).pack(side="left", padx=(0, 8))
        self.connect_button = ttk.Button(connection, text="Connect", command=self.toggle_connection)
        self.connect_button.pack(side="left")

        metrics = ttk.Frame(outer)
        metrics.pack(fill="x", pady=(0, 14))
        self._metric_panel(metrics, "LED state", self.led_var, 0)
        self._metric_panel(metrics, "MCU temperature", self.temperature_var, 1)
        self._metric_panel(metrics, "Uptime", self.uptime_var, 2)

        led_panel = ttk.Frame(outer, style="Panel.TFrame", padding=12)
        led_panel.pack(fill="x", pady=(0, 14))
        ttk.Label(led_panel, text="Onboard RGB LED", style="Caption.TLabel").pack(anchor="w", pady=(0, 8))
        controls = ttk.Frame(led_panel, style="Panel.TFrame")
        controls.pack(anchor="w")
        for label, command in (("Red", "LED RED"), ("Green", "LED GREEN"), ("Blue", "LED BLUE"), ("Off", "LED OFF")):
            ttk.Button(controls, text=label, command=lambda value=command: self.send_command(value)).pack(
                side="left", padx=(0, 8)
            )

        graph_panel = ttk.Frame(outer, style="Panel.TFrame", padding=12)
        graph_panel.pack(fill="x", pady=(0, 14))
        ttk.Label(graph_panel, text="Temperature history", style="Caption.TLabel").pack(anchor="w", pady=(0, 8))
        graph_controls = ttk.Frame(graph_panel, style="Panel.TFrame")
        graph_controls.pack(fill="x")
        ttk.Button(graph_controls, text="Show Graph", command=self.show_temperature_graph).pack(side="left", padx=(0, 8))
        ttk.Button(graph_controls, text="Refresh Graph", command=self.refresh_temperature_graph).pack(
            side="left", padx=(0, 12)
        )
        ttk.Label(graph_controls, textvariable=self.graph_status_var, style="Caption.TLabel").pack(side="left")

        log_panel = ttk.Frame(outer, style="Panel.TFrame", padding=12)
        log_panel.pack(fill="both", expand=True)
        ttk.Label(log_panel, text="Board messages", style="Caption.TLabel").pack(anchor="w", pady=(0, 6))
        self.log = tk.Text(log_panel, height=7, wrap="word", state="disabled", font=("Consolas", 9),
                           bg="#f7f9fa", fg="#263746", relief="flat", padx=8, pady=6)
        self.log.pack(fill="both", expand=True)

    def _metric_panel(self, parent, caption, variable, column):
        panel = ttk.Frame(parent, style="Panel.TFrame", padding=12)
        panel.grid(row=0, column=column, sticky="nsew", padx=(0 if column == 0 else 6, 6 if column < 2 else 0))
        parent.columnconfigure(column, weight=1)
        ttk.Label(panel, text=caption, style="Caption.TLabel").pack(anchor="w")
        ttk.Label(panel, textvariable=variable, style="Value.TLabel").pack(anchor="w", pady=(7, 0))

    def refresh_ports(self):
        ports = list(list_ports.comports())
        devices = [port.device for port in ports]
        self.port_box["values"] = devices
        if self.port_var.get() not in devices:
            self.port_var.set(devices[0] if devices else "")

    def toggle_connection(self):
        if self.connection is not None:
            self.disconnect()
            return

        port = self.port_var.get()
        if not port:
            messagebox.showwarning("No serial port", "Connect the board and select its COM port first.")
            return

        try:
            connection = serial.Serial(port, BAUD_RATE, timeout=0.1, write_timeout=1)
        except serial.SerialException as error:
            messagebox.showerror("Serial connection failed", f"Could not open {port}:\n{error}")
            return

        self.connection = connection
        self.reader_stop = threading.Event()
        self.connect_button.configure(text="Disconnect")
        self.connection_var.set(f"Connected: {port}")
        self._append_log(f"Connected to {port} at {BAUD_RATE} baud.")
        threading.Thread(target=self.read_serial, args=(connection, self.reader_stop), daemon=True).start()
        self.root.after(1500, lambda: self.initialize_board(connection))

    def initialize_board(self, connection):
        if self.connection is not connection:
            return
        self._reset_temperature_graph(request_sample=False, log_reset=False)
        self.send_command("LED OFF")
        self.poll_ticks = 0
        self.poll_device(connection)

    def disconnect(self):
        connection = self.connection
        if connection is None:
            return

        self.connection = None
        if self.reader_stop is not None:
            self.reader_stop.set()
        try:
            connection.close()
        except serial.SerialException:
            pass

        self.connect_button.configure(text="Connect")
        self.connection_var.set("Disconnected")
        self._append_log("Serial connection closed.")

    def send_command(self, command):
        connection = self.connection
        if connection is None:
            self.connection_var.set("Connect to the board first")
            return

        try:
            connection.write((command + "\r").encode("ascii"))
        except serial.SerialException as error:
            self.messages.put(("error", str(error)))

    def read_serial(self, connection, stop_event):
        while not stop_event.is_set():
            try:
                line = connection.readline().decode("ascii", errors="replace").strip()
            except serial.SerialException as error:
                if not stop_event.is_set():
                    self.messages.put(("error", str(error)))
                return
            if line:
                self.messages.put(("line", line))

    def process_messages(self):
        while True:
            try:
                kind, message = self.messages.get_nowait()
            except queue.Empty:
                break

            if kind == "error":
                self._append_log(f"Serial error: {message}")
                self.disconnect()
            else:
                self.handle_board_line(message)

        self.root.after(100, self.process_messages)

    def handle_board_line(self, line):
        self._append_log(line)

        temperature = re.search(r"MCU internal temperature:\s*(-?\d+\.\d+) C", line)
        if temperature:
            temperature_value = float(temperature.group(1))
            self.temperature_var.set(f"{temperature.group(1)} C")
            elapsed_seconds = time.monotonic() - self.graph_started_at
            self.temperature_samples.append((elapsed_seconds, temperature_value))
            self.graph_status_var.set(f"{len(self.temperature_samples)} samples")
            self.update_temperature_graph()

        uptime = re.search(r"(?:Uptime: |SW2 pressed\. Uptime: )(\d+:\d{2}:\d{2})", line)
        if uptime:
            self.uptime_var.set(uptime.group(1))

        if "Red LED is on." in line:
            self.led_var.set("RED ON")
        elif "Green LED is on." in line:
            self.led_var.set("GREEN ON")
        elif "Blue LED is on." in line:
            self.led_var.set("BLUE ON")
        elif "All LEDs are off." in line:
            self.led_var.set("OFF")
        elif "Red LED toggled." in line:
            self.led_var.set("RED toggled")

    @staticmethod
    def format_elapsed_time(value, _position):
        total_seconds = max(0, int(value))
        hours = total_seconds // 3600
        minutes = (total_seconds % 3600) // 60
        seconds = total_seconds % 60
        return f"{hours:02d}:{minutes:02d}:{seconds:02d}"

    def show_temperature_graph(self):
        if self.graph_window is not None and self.graph_window.winfo_exists():
            self.graph_window.deiconify()
            self.graph_window.lift()
            self.update_temperature_graph()
            return

        self.graph_window = tk.Toplevel(self.root)
        self.graph_window.title("Temperature over time")
        self.graph_window.geometry("820x520")
        self.graph_window.minsize(600, 380)
        self.graph_window.protocol("WM_DELETE_WINDOW", self.close_temperature_graph)

        figure = Figure(figsize=(8, 4.8), dpi=100)
        self.graph_axes = figure.add_subplot(111)
        self.graph_axes.set_title("MCU Internal Temperature")
        self.graph_axes.set_xlabel("Elapsed time (hh:mm:ss)")
        self.graph_axes.set_ylabel("Temperature (C)")
        self.graph_axes.xaxis.set_major_formatter(FuncFormatter(self.format_elapsed_time))
        self.graph_axes.grid(True, color="#d9e0e5", linewidth=0.8)
        self.graph_line, = self.graph_axes.plot([], [], color="#168b83", marker="o", markersize=4, linewidth=1.8)

        self.graph_canvas = FigureCanvasTkAgg(figure, master=self.graph_window)
        self.graph_canvas.get_tk_widget().pack(fill="both", expand=True, padx=10, pady=10)
        self.update_temperature_graph()

    def update_temperature_graph(self):
        if self.graph_axes is None or self.graph_canvas is None:
            return
        if self.graph_window is None or not self.graph_window.winfo_exists():
            return

        if self.temperature_samples:
            elapsed, temperatures = zip(*self.temperature_samples)
            self.graph_line.set_data(elapsed, temperatures)
            self.graph_axes.relim()
            self.graph_axes.autoscale_view(scalex=False, scaley=True)
            self.graph_axes.set_xlim(0, max(5, elapsed[-1] * 1.05))
        else:
            self.graph_line.set_data([], [])
            self.graph_axes.set_xlim(0, 5)
            self.graph_axes.set_ylim(0, 1)

        self.graph_canvas.draw_idle()

    def refresh_temperature_graph(self):
        self._reset_temperature_graph(request_sample=True, log_reset=True)

    def _reset_temperature_graph(self, request_sample, log_reset):
        self.temperature_samples.clear()
        self.graph_started_at = time.monotonic()
        self.graph_status_var.set("Waiting for temperature sample")
        self.update_temperature_graph()

        if log_reset:
            self._append_log("Temperature graph refreshed; time restarted at 00:00:00.")
        if request_sample:
            self.send_command("TEMP?")

    def close_temperature_graph(self):
        if self.graph_window is not None and self.graph_window.winfo_exists():
            self.graph_window.destroy()
        self.graph_window = None
        self.graph_axes = None
        self.graph_line = None
        self.graph_canvas = None

    def poll_device(self, connection):
        if self.connection is not connection:
            return

        self.send_command("UPTIME?")
        self.poll_ticks += 1
        if self.poll_ticks % TEMPERATURE_INTERVAL_TICKS == 0:
            self.send_command("TEMP?")
        self.root.after(POLL_INTERVAL_MS, lambda: self.poll_device(connection))

    def _append_log(self, text):
        self.log.configure(state="normal")
        self.log.insert("end", text + "\n")
        self.log.see("end")
        self.log.configure(state="disabled")

    def close(self):
        self.disconnect()
        self.close_temperature_graph()
        self.root.destroy()


def main():
    root = tk.Tk()
    UartDashboard(root)
    root.mainloop()


if __name__ == "__main__":
    main()