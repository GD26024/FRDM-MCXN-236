# FRDM-MCXN236 Dashboard

This PC-side dashboard communicates with the board through its MCU-Link USB serial port. It does not require an LCD or other add-on hardware.

## Setup

1. Close any other program using the board's COM port, including the serial terminal.
2. From the project root, install the Python dependency:

   ```powershell
   py -m pip install -r host_gui/requirements.txt
   ```

3. Flash and run the `mcxn236_uart_lab` firmware.
4. Start the dashboard:

   ```powershell
   py host_gui/uart_dashboard.py
   ```

5. Select the board's COM port, usually COM3, and click Connect.

The dashboard sends the existing `LED RED`, `LED GREEN`, `LED BLUE`, `LED OFF`, `TEMP?`, and `UPTIME?` commands. It refreshes uptime every second and temperature every five seconds.