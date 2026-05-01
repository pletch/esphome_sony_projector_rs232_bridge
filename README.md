# Sony Projector RS-232 Bridge — ESPHome

ESPHome external component that turns an ESP32 into a Home Assistant–native
bridge for Sony projectors over RS-232. The component speaks Sony's ADCP
(Advanced Display Control Protocol) — the same text protocol Sony exposes on
the network — but over the projector's serial port instead of TCP.

This is an alternative to the network-based ADCP integration: useful when the
projector is not on the network, when you want a hard-wired control path that
survives Wi-Fi outages, or when the model only exposes ADCP on its RS-232
port.

## Hardware

ESP32 UARTs are 3.3 V TTL; the projector's serial port is true RS-232 (±12 V),
so a level-shifting transceiver is required between them. A **MAX3232** breakout
(roughly $2 on AliExpress / Amazon) is the standard choice — it handles both
directions and runs from the ESP32's 3.3 V rail.

Wiring (matches [projector.yaml](projector.yaml)):

| ESP32       | MAX3232 (TTL side) | MAX3232 (RS-232 side) | Projector DB-9 |
|-------------|--------------------|-----------------------|----------------|
| GPIO17 (TX) | T1IN               | T1OUT                 | RX (pin 2)     |
| GPIO16 (RX) | R1OUT              | R1IN                  | TX (pin 3)     |
| GND         | GND                | GND                   | GND (pin 5)    |
| 3V3         | VCC                | —                     | —              |

UART is **38400 8E1** (8 data bits, even parity, 1 stop). Confirm against your
model's manual; the ADCP serial section lists the exact line settings.

## Layout

- [projector.yaml](projector.yaml) — device config (ESP32, esp-idf, UART, native API)
- [secrets.yaml.example](secrets.yaml.example) — copy to `secrets.yaml` and fill in Wi-Fi credentials
- [components/sony_projector/](components/sony_projector/) — external component implementing the protocol

## Entities exposed to Home Assistant

| Entity | Type | Description |
|--------|------|-------------|
| `switch.projector_power` | switch | Power on/off. Reflects `is_powered_on` (true during startup as well as fully on) so the toggle does not flicker while the projector warms up. |
| `text_sensor.projector_status` | text sensor | Power state reported by the projector: `standby`, `startup`, `on`, `cooling1`, `cooling2`, `saving_cooling1`, `saving_cooling2`, `saving_standby`. |
| `sensor.projector_light_source_hours` | sensor (h) | Laser/lamp run hours. Polled on the slow-poll cadence (15 min). |
| `sensor.projector_temperature` | sensor (°C) | Internal temperature, when the model supports the query. |
| `text_sensor.projector_filter_status` | text sensor | Filter condition reported by the projector. |
| `text_sensor.projector_model` | text sensor | Model name, queried once at boot. |
| `select.projector_input` | select | Active input. Options must be configured to match what your model exposes (e.g. `hdmi1`, `hdmi2`); see ADCP §2-2-1 in the *Supported Command List* for your projector. |
| `button.projector_bridge_restart` | button | Reboot the ESP32 itself (not the projector). |

Power status is polled on the component's `update_interval` (default 30 s).
Light-source hours, temperature, and filter status are polled less often — every
15 minutes — since they change slowly. If the projector returns `err_*` on the
first probe of any optional metric, that capability flag is cleared and the
bridge stops polling it.

Any individual entity can be omitted by removing its block from `projector.yaml`.

## API services

Exposed as Home Assistant services on the device (`esphome.<device>_<service>`):

| Service | Args | Description |
|---------|------|-------------|
| `set_input` | `input: string` | Switch to a named ADCP input (e.g. `"hdmi1"`). Equivalent to setting the `select` entity. |
| `send_command` | `command: string` | Send an arbitrary ADCP text command. CR+LF is appended automatically. Useful for SET/GET commands the component does not model directly (e.g. `'picture_mode "cinema_film1"'`, `calibration_preset?`). |
| `send_raw` | `payload: int[]` | Escape hatch: send raw bytes verbatim with no terminator added. For experimenting with non-ADCP byte sequences; not normally needed. |

Example Home Assistant action:

```yaml
service: esphome.rs232_bridge_send_command
data:
  command: 'picture_mode "cinema_film1"'
```

## Build / flash

```sh
cp secrets.yaml.example secrets.yaml   # edit with your Wi-Fi creds
esphome run projector.yaml
```

## Notes

- Framework is `esp-idf`.
- The reader is non-blocking and driven from `loop()`; commands queue up to 8
  deep, with a 1.5 s response timeout per command.
- ADCP responses are CR+LF-terminated and are one of: `ok`, `err_*`, a quoted
  string, a bare number, or a JSON-ish blob (e.g. the `timer?` response).
