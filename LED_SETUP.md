# LED Setup

This project uses three cascaded `SN74HC595N` shift registers to drive the temperature and humidity indicator LEDs.

## ESP32 to Shift Registers

Connect only chip 1 directly to the ESP32 data pin. All three chips share the same clock and latch pins.

| ESP32 pin | SN74HC595N pin |
| --- | --- |
| `GPIO13` | Chip 1 `SER / DS`, pin `14` |
| `GPIO14` | All chips `SRCLK / SH_CP`, pin `11` |
| `GPIO15` | All chips `RCLK / ST_CP`, pin `12` |
| `3V3` | All chips `VCC`, pin `16` |
| `3V3` | All chips `MR / SRCLR`, pin `10` |
| `GND` | All chips `GND`, pin `8` |
| `GND` | All chips `OE`, pin `13` |

## Chain the Chips

| From | To |
| --- | --- |
| Chip 1 `Q7S / QH'`, pin `9` | Chip 2 `SER / DS`, pin `14` |
| Chip 2 `Q7S / QH'`, pin `9` | Chip 3 `SER / DS`, pin `14` |

Place the chips vertically on the breadboard with two empty rows between each chip. Make sure every chip notch faces the same direction.

## LED Outputs

Connect every LED through a `220-330 ohm` resistor.

| Output | LED meaning |
| --- | --- |
| Chip 1 `Q0` | Temperature low, below `20 C` |
| Chip 1 `Q1` | Temperature high, above `40 C` |
| Chip 1 `Q2` | Temperature bar `20 C` |
| Chip 1 `Q3` | Temperature bar `22 C` |
| Chip 1 `Q4` | Temperature bar `24 C` |
| Chip 1 `Q5` | Temperature bar `26 C` |
| Chip 1 `Q6` | Temperature bar `28 C` |
| Chip 1 `Q7` | Temperature bar `30 C` |
| Chip 2 `Q0` | Temperature bar `32 C` |
| Chip 2 `Q1` | Temperature bar `34 C` |
| Chip 2 `Q2` | Temperature bar `36 C` |
| Chip 2 `Q3` | Temperature bar `38 C` |
| Chip 2 `Q4` | Humidity low, below `65 %` |
| Chip 2 `Q5` | Humidity high, above `95 %` |
| Chip 2 `Q6` | Humidity bar `65 %` |
| Chip 2 `Q7` | Humidity bar `70 %` |
| Chip 3 `Q0` | Humidity bar `75 %` |
| Chip 3 `Q1` | Humidity bar `80 %` |
| Chip 3 `Q2` | Humidity bar `85 %` |
| Chip 3 `Q3` | Last DHT read OK |
| Chip 3 `Q4` | Last DHT read failed |
| Chip 3 `Q5-Q7` | Spare outputs |

## LED Direction

For each LED:

1. Connect the shift-register output pin to the resistor.
2. Connect the resistor to the LED anode, the long leg.
3. Connect the LED cathode, the short leg, to `GND`.

If an LED does not light, check:

- The LED direction.
- The resistor is in series, not parallel.
- The chip has `VCC`, `GND`, `OE`, and `MR` connected correctly.
- The chip chain uses pin `9` to the next chip pin `14`.
- The firmware uses `GPIO13`, `GPIO14`, and `GPIO15`.

## Quick Test Values

With the current firmware:

- Temperature below `20 C`: Chip 1 `Q0` turns on.
- Temperature `20-38 C`: temperature bar LEDs fill gradually.
- Temperature above `40 C`: Chip 1 `Q1` turns on.
- Humidity below `65 %`: Chip 2 `Q4` turns on.
- Humidity `65-85 %`: humidity bar LEDs fill gradually in `5 %` steps.
- Humidity above `95 %`: Chip 2 `Q5` turns on.
- Successful DHT read: Chip 3 `Q3` turns on.
- Failed DHT read: Chip 3 `Q4` turns on.
