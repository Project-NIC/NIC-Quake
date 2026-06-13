# NIC HW Block — Bus: internal Modbus sensor bus

*Shared block. Only on nodes with external Modbus sensors: NIC-Weather, starDust.
STM32H503 is the Modbus master (UART3), THVD1450 transceiver. Keep in sync across
projects.*

- Standard RS-485 **Modbus RTU**, 9600–19200 baud (8N1).
- **Termination: 120 Ω** across A/B at the bus ends (standard RS-485).
- **Protection: SM712** at our transceiver (see block `protection-485`).
  **No series-R matching** — slow bus.
- **No protection on the sealed COTS sensors.**
- **Master / output-side power protection on our board: TBD (owner).**
