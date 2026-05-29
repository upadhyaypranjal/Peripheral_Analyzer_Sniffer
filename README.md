cat > /home/claude/shrike-sniffer/README.md << 'ENDOFREADME'
<div align="center">

# Peripheral Sniffer Analyzer

</div>

<div align="center">

![Platform](https://img.shields.io/badge/Platform-Vicharak%20Shrike%20Lite-blueviolet?style=for-the-badge)
![FPGA](https://img.shields.io/badge/FPGA-Renesas%20ForgeFPGA%20SLG47910-orange?style=for-the-badge)
![MCU](https://img.shields.io/badge/MCU-RP2040%20Dual--Core-blue?style=for-the-badge)
![Protocol](https://img.shields.io/badge/Protocols-UART%20%7C%20I2C-green?style=for-the-badge)
![Status](https://img.shields.io/badge/Status-Phase%201%20Complete-success?style=for-the-badge)
![License](https://img.shields.io/badge/License-MIT-yellow?style=for-the-badge)

*A hardware-level, multi-protocol sniffer and analyzer with FPGA metastability mitigation frontend*

[Overview](#-overview) • [Architecture](#-system-architecture) • [FPGA Design](#-fpga-synchronization-design) • [Firmware](#-rp2040-firmware) • [Progress](#-current-progress) • [Build](#-build-instructions) • [Roadmap](#-roadmap)

---

</div>

## 🎯 Overview

Most protocol sniffers feed external signals directly into a microcontroller GPIO. At high baud rates or on electrically noisy lines, this creates a real risk of **metastability** — the input flip-flop captures a signal mid-transition and resolves to an indeterminate voltage that corrupts the decoded frame.

This project inserts a **Renesas ForgeFPGA (SLG47910)** between the external signal source and the **Raspberry Pi RP2040** decoder. The FPGA runs hardened two-stage synchronizers on every incoming line, removing metastability before the signal reaches any firmware logic. The RP2040 then operates on a clean, stable, clock-domain-safe input.

### ✨ Key Design Decisions

| Decision | Rationale |
|:---------|:----------|
| FPGA as sync frontend only | Separates metastability concerns from protocol logic cleanly |
| 2-stage flip-flop synchronizer | Provides ~20 ns resolution time per stage at 50 MHz — sufficient for all target protocols |
| OSC_CLK internal oscillator | No external crystal needed; SLG47910 internal RC macro at 50 MHz |
| RP2040 dual-core firmware | Core 0 handles UART decode; Core 1 handles I2C sniffer independently |
| PIO UART on bridge GPIOs | GPIO 14/15 are the only PCB traces to FPGA fabric; hardware UART cannot be used there |

> **Critical Design Principle:** The FPGA is **not** a protocol decoder. It captures asynchronous signals, mitigates metastability, and forwards stabilized outputs. All protocol intelligence — UART framing, I2C address/data parsing, timestamps, and logging — lives entirely in the RP2040 firmware.

---

## System Architecture and Signal Flow

> *ESP8266 Signal Generator → ForgeFPGA Synchronization Frontend → RP2040 Decode Engine*

<div align="center">
    <img src="images/signal_flow.png" alt="Multi-Protocol Hardware Sniffer Architecture" width="700">
</div>

**Figure:** End-to-end signal flow of the multi-protocol hardware sniffer/analyzer.

- **ESP8266:** Generates UART and I²C test traffic.
- **ForgeFPGA SLG47910:** Synchronizes incoming signals using two-stage synchronizers.
- **RP2040:** Performs protocol decoding, monitoring, and USB serial logging.

---


### Why the FPGA Is In the Middle

#### The Metastability Problem

When a signal crosses clock domains — or arrives from an entirely asynchronous source like an external UART transmitter — any flip-flop that captures it risks entering a **metastable state**. The output voltage sits between valid logic 0 and logic 1, resolves after an indeterminate time, and can propagate corrupted data downstream.

```
Normal capture:                     Metastable capture:
                                    
  D ──┤▔▔│── Q (clean HIGH)           D ──┤▔▔│── Q (??? — undefined)
      CLK                                  CLK
                                               │
                                               └── Resolves randomly to 0 or 1
                                                   May corrupt entire frame
```

#### The 2-Stage Synchronizer Solution

The two flip-flop synchronizer gives the first stage a **full clock period** (20 ns at 50 MHz) to resolve before the second stage captures it. By the time the signal exits FF2, the probability of residual metastability is reduced to a negligible level.

```
Async input
     │
     ▼
  ┌──────────┐   posedge clk    ┌──────────┐   posedge clk    ┌──────────┐
  │  FF1     │ ──────────────►  │  FF2     │ ──────────────►  │ Output   │
  │          │                  │          │                  │  reg     │
  └──────────┘                  └──────────┘                  └──────────┘
        │                             │
        │ May be metastable           │ Resolved with high probability
        │ (20 ns to settle)           │ (20 ns additional resolution time)
        │                             │
        └── Stage 1 resolution        └── Stage 2 confirmation
```

#### Timing Budget

| Parameter | Value | Notes |
|:----------|:-----:|:------|
| FPGA clock frequency | 50 MHz | Internal OSC_CLK |
| Clock period | 20 ns | Per FF resolution time |
| Synchronizer stages | 2 | FF1 + FF2 |
| Total sync latency | 3 cycles = 60 ns | FF1 + FF2 + registered output |
| UART bit period @ 115200 | 8,680 ns | 145× the sync latency |
| UART bit period @ 9600 | 104,167 ns | Far exceeds any timing concern |
| Residual metastability risk | Negligible | Two full resolution windows |

The 60 ns latency introduced by the synchronizer is completely invisible to the RP2040 UART decoder at any sane baud rate.

---

## 🔧 Hardware

### Bill of Materials

| Component | Part | Specification | Notes |
|:----------|:-----|:-------------|:------|
| Development Board | Vicharak Shrike Lite | RP2040 + SLG47910 on single PCB | All-in-one embedded platform |
| FPGA | Renesas SLG47910V | 1120 LUTs, 19 GPIOs, internal 50 MHz RC OSC | ForgeFPGA family |
| MCU | Raspberry Pi RP2040 | Dual-core Arm Cortex-M0+ @ 133 MHz, PIO | 264 KB SRAM, 8 PIO state machines |
| Signal Generator | ESP8266 (external) | 3.3V UART + I2C capable | Used only in Phase 2+ |
| Interface | USB Type-C | USB 1.1 device | Programming and serial logging |
| Bridge Traces | Onboard PCB | GPIO14↔Pin18, GPIO15↔Pin17 | Direct 3.3V CMOS, no level shifter |

### RP2040 ↔ FPGA Bridge Topology

The Shrike Lite routes direct PCB traces between the ForgeFPGA fabric and the RP2040. There is no bus multiplexer, level shifter IC, or registered interface — these are passive copper traces between GPIO pads on a shared 3.3V supply.

```
RP2040                        ForgeFPGA SLG47910
─────────────────────────     ──────────────────────────
GPIO 0  ──SPI MISO──────────► Pin 6   (FPGA programming — reserved)
GPIO 1  ──SPI SS────────────► Pin 4   (FPGA programming — reserved)
GPIO 2  ──SPI SCLK──────────► Pin 3   (FPGA programming — reserved)
GPIO 3  ──SPI MOSI──────────► Pin 5   (FPGA programming — reserved)
GPIO 12 ──FPGA PWR──────────► PWR     (power control)
GPIO 13 ──FPGA EN───────────► EN      (logic enable)
GPIO 14 ──────────────────── Pin 18   ◄── Runtime bridge · also FPGA RESET (active-low)
GPIO 15 ──────────────────── Pin 17   ◄── Runtime bridge · clean general-purpose line
```

> **GPIO 14 / FPGA RESET caveat:** Pin 18 doubles as the FPGA RESET line (active-low, 10 ms hold required). UART idle is HIGH so normal operation is safe. A start bit (LOW) lasts at most ~8.68 µs at 115200 baud — far below the reset threshold. Do not drive this line LOW for extended periods after the FPGA is configured.

> **GPIO 4/5/6 are NOT connected to the FPGA fabric.** GPIO 4 is the RP2040 user LED. GPIO 5/6 go to the external header. This is a common source of confusion when assigning IO Planner pins.

---

## ⚙️ FPGA Synchronization Design

### RTL Architecture

The FPGA implements three independent two-stage synchronizers, one per protocol line. All three share the internal 50 MHz oscillator as the synchronization clock domain.

```verilog
(* top *)
module uart_bridge_sync (
    (* iopad_external_pin, clkbuf_inhibit *) input  wire clk,       // OSC_CLK
    (* iopad_external_pin *)                 output wire osc_en,    // OSC_EN
    (* iopad_external_pin *)                 input  wire uart_in,   // FPGA Pin 18
    (* iopad_external_pin *)                 output reg  uart_out   // FPGA Pin 17
);
    assign osc_en = 1'b1;   // Enable internal 50 MHz oscillator

    (* keep = "true" *) reg ff1 = 1'b1;   // Stage 1: capture (may be metastable)
    (* keep = "true" *) reg ff2 = 1'b1;   // Stage 2: resolve

    always @(posedge clk) begin
        ff1      <= uart_in;   // Captures async input
        ff2      <= ff1;       // Gives FF1 one full period to settle
        uart_out <= ff2;       // Stable, synchronized output
    end
endmodule
```

### IO Planner Mapping

The ForgeFPGA IO Planner binds Verilog port names to physical pad resources. These assignments must be verified after every re-synthesis — the tool can silently drop them if a port name changes.

| IO Planner Resource | Verilog Port | Direction | Physical Connection |
|:-------------------:|:------------:|:---------:|:--------------------|
| `OSC_CLK` | `clk` | Input | Dedicated internal RC oscillator macro |
| `OSC_EN` | `osc_en` | Output | Oscillator enable — must be HIGH |
| FPGA Pin 18 | `uart_in` | Input | RP2040 GPIO 14 (PCB trace) |
| FPGA Pin 17 | `uart_out` | Output | RP2040 GPIO 15 (PCB trace) |

See [`fpga/rtl/uart_bridge_sync.v`](fpga/rtl/uart_bridge_sync.v) for the complete annotated source.

### Critical ForgeFPGA Toolchain Notes

These are hard-won observations from bring-up. Each item represents a failure mode that produces a valid-looking bitstream but silently broken hardware behavior.

**`(* clkbuf_inhibit *)` on the clock port — mandatory**

Without this attribute, the synthesis tool inserts a clock buffer and renames the internal clock net. The IO Planner assignment `OSC_CLK → clk` is string-matched by net name. After renaming, the mapping loses its target and the flip-flops end up clocked by combinational logic — or nothing at all. This produces the `"network is combinational"` warning in the Issues tab and is a **hard failure**. The bitstream will still generate cleanly; PnR success does not imply correct clock routing.

**`osc_en` must map to the `OSC_EN` dedicated resource**

The SLG47910 internal oscillator start is gated by a dedicated control input, not a GPIO. Mapping `osc_en` to a GPIO pad does nothing. It must be assigned to the `OSC_EN` resource specifically in the IO Planner. If the oscillator is not enabled, the FPGA fabric has no clock and produces no output edges regardless of the input signal.

**`(* keep = "true" *)` on synchronizer flip-flops — required**

Without this attribute, the synthesis optimizer may merge the two flip-flops into one (recognizing that `ff2 = ff1` delayed) or eliminate them entirely if it determines the combinational function is equivalent. This collapses the two-stage synchronizer into a single stage or a wire, completely defeating the metastability protection.

**IO Planner assignments silently drop after re-synthesis**

After any Verilog edit and re-synthesis run, every IO Planner assignment should be manually verified. The tool does not warn you when a port rename causes an assignment to become unbound.

---

## 💻 RP2040 Firmware

### Architecture

The firmware uses both RP2040 cores independently:

```
Core 0                              Core 1
──────────────────────────          ────────────────────────────────
Initialization                      I2C bit-bang sniffer loop
FPGA bitstream loading              SCL/SDA frame capture
PIO UART TX (test patterns)         Address decode
PIO UART RX on GPIO 15              Data payload extract
Frame reconstruction                Error detection (NACK, timeout)
USB console logging                 Metrics accumulation
```

### Why PIO UART — Not Hardware UART

The RP2040's hardware UART peripherals are mapped to fixed GPIO pairs that do not include GPIO 14 or GPIO 15. Since these are the only pins with PCB traces to the FPGA fabric, the PIO (Programmable I/O) subsystem must be used instead. PIO UART is fully bit-accurate and supports arbitrary baud rates — it is not a workaround, it is the correct solution.

```c
// GPIO 14/15 are UART0 CTS/RTS in the hardware peripheral map.
// PIO UART is required for TX/RX on these specific pins.
#define BRIDGE_TX_PIN  14    // → FPGA Pin 18 (uart_in)
#define BRIDGE_RX_PIN  15    // ← FPGA Pin 17 (uart_out)
```

### Validated Firmware Output (Phase 1)

```
[SHRIKE] Peripheral Sniffer firmware starting...
[UART]   TX: Sending marker byte 0xAA
[UART]   TX: Sending counter 0x00..0x0F
[UART]   TX: Sending string SHRIKE_SNIFFER_MVP
[UART]   RX: Confirmed loopback through FPGA synchronizer
```

See [`firmware/`](firmware/) for source. The firmware structure is detailed in the [Repository Structure](#-repository-structure) section.

---

## 📊 Current Progress

### Phase Status

| Phase | Description | Status |
|:------|:------------|:------:|
| **Phase 1** | Synchronizer MVP | ✅ Complete |
| **Phase 2** | UART Decode | 🔄 In Progress |
| **Phase 3** | I2C Frontend | 🔄 Planned |
| **Phase 4** | Protocol Framing | ⬜ Planned |
| **Phase 5** | Multi-Protocol | ⬜ Planned |

### Detailed Task Tracker

| Task | Status | Notes |
|:-----|:------:|:------|
| FPGA clock routing — `OSC_CLK` + `OSC_EN` | ✅ Done | `clkbuf_inhibit` required |
| IO Planner mapping — Pin 17 / Pin 18 | ✅ Done | Verified post-synthesis |
| 2-stage UART synchronizer RTL | ✅ Done | `keep=true` on both FFs |
| FPGA synthesis + PnR clean | ✅ Done | Zero clock warnings |
| FPGA LED blink validation (clock alive) | ✅ Done | 50 MHz confirmed functional |
| `bridge_outputs_en` housekeeping output | ✅ Done | Held HIGH, level-shifters active |
| RP2040 multicore debug firmware | ✅ Done | Core 0 TX, Core 1 RX dispatch |
| RP2040 PIO UART TX on GPIO 14 | ✅ Done | 0xAA, counter, ASCII string |
| End-to-end UART loopback validated | ✅ Done | Full FPGA sync path confirmed |
| RP2040 PIO UART RX on GPIO 15 | 🔄 In Progress | Byte capture + logging |
| I2C dual-line synchronizer (SDA + SCL) | 🔄 In Progress | PMOD routing for second line |
| USB serial structured logging | ⬜ Planned | Timestamped packet output |
| I2C address + data frame decode | ⬜ Planned | RP2040 Core 1 |
| Multi-protocol decode (UART + I2C) | ⬜ Planned | Simultaneous capture |
| Timestamping + packet framing | ⬜ Planned | `time_us_64()` at RX interrupt |
| ESP8266 external signal injection | ⬜ Planned | Phase 2 bring-up |

---

## 🔬 UART Validation Detail

### Validated Signal Path

The Phase 1 MVP validates the complete synchronizer path using only the onboard hardware — no external components, no breadboard, no logic analyzer.

```
RP2040 GPIO 14 (PIO UART TX, idle HIGH)
        │
        └──[PCB trace 3.3V]──► FPGA Pin 18 (uart_in, IO Planner: Input)
                                        │
                               ┌────────▼────────────────────────────┐
                               │  50 MHz OSC_CLK                     │
                               │                                     │
                               │  always @(posedge clk) begin        │
                               │    ff1      <= uart_in;  // Capture │
                               │    ff2      <= ff1;      // Resolve │
                               │    uart_out <= ff2;      // Output  │
                               │  end                                │
                               └─────────────────┬───────────────────┘
                                                 │  3 cycles = ~60 ns latency
                               FPGA Pin 17 (uart_out, IO Planner: Output)
                                                 │
                               ──[PCB trace 3.3V]──► RP2040 GPIO 15 (PIO UART RX)
                                                                │
                                                   PIO decodes UART byte
                                                                │
                                                       USB serial log
```

### Debug Methodology (No External Equipment)

The following bring-up sequence was used to validate each stage independently, requiring no oscilloscope, logic analyzer, or jumper wires:

| Step | Test | Pass Condition |
|:----:|:-----|:--------------|
| 1 | FPGA LED blink at ~1 Hz using 50 MHz OSC divided down | LED blinks → clock path confirmed |
| 2 | GPIO echo: assign `uart_out = uart_in` combinationally | GPIO 15 mirrors GPIO 14 toggle → bridge traces alive |
| 3 | Add synchronizer: reload sync bitstream, repeat GPIO toggle | GPIO 15 follows with ~60 ns delay → sync path functional |
| 4 | PIO UART loopback at 9600 baud | RP2040 receives own transmission → full path validated |
| 5 | Increase to 115200 baud | No RX errors → timing margin confirmed |

---

## 📁 Repository Structure

```
shrike-peripheral-sniffer/
│
├── 📂 firmware/                         RP2040 C firmware (Pico SDK)
│   ├── 📂 core/
│   │   ├── main.c                       Entry point, multicore dispatch, init
│   │   └── CMakeLists.txt               Pico SDK build configuration
│   ├── 📂 uart/
│   │   ├── pio_uart.c / .h              PIO UART TX/RX on GPIO 14/15
│   │   └── uart_loopback.c              Phase 1 loopback test sequence
│   ├── 📂 i2c/
│   │   ├── i2c_sniffer.c / .h           I2C bit-bang monitor (Core 1)
│   │   └── i2c_decode.c / .h            Address + data frame parsing (planned)
│   └── 📂 utils/
│       ├── logger.c / .h                Timestamped USB serial logging
│       └── timing.c / .h               time_us_64() based timestamping
│
├── 📂 fpga/                             ForgeFPGA Verilog + toolchain files
│   ├── 📂 rtl/
│   │   ├── uart_bridge_sync.v           UART 2-stage synchronizer (Phase 1) ✅
│   │   └── i2c_bridge_sync.v            I2C dual-line synchronizer (planned)
│   ├── 📂 constraints/
│   │   └── timing_constraints.sdc       Timing constraints (if applicable)
│   └── 📂 io_planner/
│       ├── io_planner_notes.md          Pin assignment documentation
│       └── *.png                        IO Planner screenshots (per milestone)
│
├── 📂 docs/                             Engineering documentation
│   ├── 📂 architecture/
│   │   ├── design_notes.md              Philosophy, bridge topology, clock details
│   │   └── metastability_theory.md      Theoretical background on 2FF sync
│   ├── 📂 progress/
│   │   ├── phase1_uart_sync.md          Phase 1 bring-up log and lessons
│   │   └── phase2_uart_decode.md        (in progress)
│   └── 📂 notes/
│       ├── forgefpga_toolchain_notes.md  ForgeFPGA quirks and tool behaviour
│       └── rp2040_pio_uart_notes.md      PIO UART implementation notes
│
├── 📂 images/                           Visual documentation
│   ├── 📂 hardware/                     Board photographs
│   ├── 📂 waveforms/                    Serial terminal captures, timing screenshots
│   ├── 📂 fpga/                         IO Planner, synthesis, PnR screenshots
│   └── 📂 diagrams/
│       └── architecture_flow.png        Full system architecture diagram ← (this file)
│
├── 📂 tests/                            Test and validation scripts
│   ├── 📂 uart/
│   │   └── uart_loopback_test.py        Python serial validator for loopback
│   ├── 📂 i2c/
│   │   └── i2c_frame_test.py            I2C frame validation (planned)
│   └── 📂 loopback/
│       └── end_to_end_check.py          Full path integrity test
│
├── 📂 hardware/                         Board reference material
│   ├── bridge_notes.md                  RP2040 ↔ FPGA bridge pin documentation
│   └── shrike_pinouts.md                Shrike Lite pinout reference
│
├── .gitignore
├── LICENSE
├── CONTRIBUTING.md
└── README.md
```

---

## 🚀 Build Instructions

### Prerequisites

| Tool | Version | Purpose |
|:-----|:-------:|:--------|
| [Pico SDK](https://github.com/raspberrypi/pico-sdk) | ≥ 1.5.0 | RP2040 firmware compilation |
| [Go Configure Software Hub](https://www.renesas.com/us/en/software-tool/go-configure-software-hub) | Latest | ForgeFPGA synthesis + PnR + bitstream |
| `cmake` | ≥ 3.13 | Firmware build system |
| `arm-none-eabi-gcc` | ≥ 10.x | ARM cross-compiler |
| Python 3 | ≥ 3.9 | Validation and test scripts |

### 1. FPGA Bitstream

```
1. Open Go Configure Software Hub
2. Load project: fpga/rtl/uart_bridge_sync.v
3. Verify IO Planner assignments:
       OSC_CLK   →  clk       (dedicated clock resource, not GPIO)
       OSC_EN    →  osc_en    (dedicated oscillator enable resource)
       Pin 18    →  uart_in   (Input)
       Pin 17    →  uart_out  (Output)
4. Run: Synthesize → Place & Route → Generate Bitstream
5. Check Issues/Logger tab — confirm zero clock topology warnings
   ⚠  PnR success ≠ correct clock routing. Always check the Issues tab.
6. Flash via RP2040 SPI programmer (ShrikeFPGA library)
```

### 2. RP2040 Firmware

```bash
git clone https://github.com/YOUR_USERNAME/shrike-peripheral-sniffer.git
cd shrike-peripheral-sniffer/firmware

mkdir build && cd build
cmake .. -DPICO_SDK_PATH=/path/to/pico-sdk
make -j$(nproc)

# Hold BOOTSEL on the Shrike Lite, connect USB, release BOOTSEL
# Drag the generated .uf2 onto the RPI-RP2 mass storage device
```

### 3. Validation

```bash
cd tests/uart
python3 uart_loopback_test.py --port /dev/ttyACM0 --baud 115200

# Expected output:
# [PASS] Received marker byte: 0xAA
# [PASS] Received counter sequence: 0x00..0x0F
# [PASS] Received string: SHRIKE_SNIFFER_MVP
```

---

## 🗺 Roadmap

### Phase 1 — Synchronizer MVP ✅ *Complete*

- [x] FPGA clock routing (`OSC_CLK` + `OSC_EN` with `clkbuf_inhibit`)
- [x] IO Planner mapping (Pin 17/18 to Verilog ports)
- [x] 2-stage UART synchronizer RTL
- [x] FPGA synthesis + PnR clean (zero clock warnings)
- [x] LED blink clock validation
- [x] `bridge_outputs_en` housekeeping
- [x] RP2040 PIO UART TX on GPIO 14
- [x] End-to-end loopback path confirmed

### Phase 2 — UART Decode 🔄 *In Progress*

- [ ] RP2040 PIO UART RX on GPIO 15
- [ ] Byte-level frame reconstruction
- [ ] USB structured serial logging with `time_us_64()` timestamps
- [ ] Baud rate runtime configuration
- [ ] ESP8266 external UART injection at 9600 baud

### Phase 3 — I2C Frontend

- [ ] Dual-line synchronizer — independent FF chains for SDA and SCL
- [ ] Second signal routing via PMOD connector (internal bridge only has 2 traces)
- [ ] RP2040 Core 1 I2C bit-bang sniffer
- [ ] I2C address decode and data frame parsing
- [ ] NACK / error detection

### Phase 4 — Protocol Framing

- [ ] Timestamped packet capture with `time_us_64()` at RX interrupt
- [ ] FIFO buffering between Core 1 capture and Core 0 logging
- [ ] Structured USB output: `[T=xxxxxxxx] [PROTO] [ADDR] [DATA] [STATUS]`
- [ ] Packet boundary detection (UART idle gap, I2C STOP condition)

### Phase 5 — Multi-Protocol and Extensions

- [ ] Runtime protocol selection (UART / I2C / SPI)
- [ ] SPI sniffer frontend (MOSI, MISO, SCK, CS — 4 FPGA inputs)
- [ ] Configurable capture triggers (start on pattern, stop on error)
- [ ] Baud rate auto-detection for unknown UART streams
- [ ] USB bulk transfer mode for high-throughput capture

---

## 📐 Technical Reference

### Synchronizer Latency Budget

```
Signal edge from ESP8266
        │
        │  PCB trace propagation (< 1 ns, negligible)
        ▼
  FPGA uart_in input buffer  →  registered on next posedge clk
        │  20 ns (FF1 capture window)
        ▼
  FF1 output  →  registered on next posedge clk
        │  20 ns (FF2 confirmation window)
        ▼
  FF2 output  →  registered on next posedge clk
        │  20 ns (output register)
        ▼
  uart_out  →  PCB trace  →  RP2040 GPIO 15
        │  < 1 ns
        ▼
  PIO UART RX state machine
        │
        ▼
  RP2040 frame decoder

Total FPGA latency: 3 cycles × 20 ns = 60 ns
Equivalent bit periods missed @ 115200 baud: 60 ns / 8680 ns = 0.007 bits
```

### ForgeFPGA Resource Utilization

| Resource | Used | Available | Utilization |
|:---------|:----:|:---------:|:-----------:|
| LUTs | ~12 | 1120 | < 1.1% |
| Flip-Flops | 6 (3 sync pairs) | ~560 | ~1.1% |
| GPIO | 4 | 19 | 21% |
| OSC Macrocell | 1 | 1 | 100% |

---

## 📚 References

- [Vicharak Shrike Lite Documentation](https://vicharak-in.github.io/shrike/)
- [Renesas SLG47910V Datasheet](https://www.renesas.com/en/document/dst/slg47910-datasheet)
- [ForgeFPGA Workshop User Guide](https://www.renesas.com/en/document/gde/forgefpga-workshop-user-guide)
- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [RP2040 PIO Reference — Pico SDK](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf#section_pio)
- [Shrike Lite Complete Pin Reference — DeepWiki](https://deepwiki.com/vicharak-in/shrike-lite/10.1-complete-pin-reference)
- Cummings, C.E., "Synthesis and Scripting Techniques for Designing Multi-Asynchronous Clock Designs", SNUG 2001 — foundational metastability reference

---

## 📝 License

MIT License — see [LICENSE](LICENSE) for full terms.

---

## 🤝 Contributing

Contributions, bug reports, and suggestions are welcome. Please read [CONTRIBUTING.md](CONTRIBUTING.md) for branch naming conventions, commit style, Verilog formatting rules, and C firmware style guidelines before submitting a pull request.

---

<div align="center">

### 👨‍💻 Pranjal Upadhyay

Indian Institute of Information Technology Design and Manufacturing, Kurnool
Hardware: Vicharak Shrike Lite · Renesas ForgeFPGA SLG47910 · RP2040

---

⭐ *Star this repository if the synchronizer design or bring-up notes were useful.*


