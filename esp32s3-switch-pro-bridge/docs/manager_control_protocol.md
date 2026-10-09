# Manager control framing

The serial console accepts one UTF-8 command per line. Commands are trimmed,
limited to 127 text bytes, and rejected rather than silently truncated. The
response is one JSON object. JSON string values escape quotes, backslashes and
control characters. A configuration or BLE operation that fails returns
`"ok":false` with the ESP error name. Status rates use Hz with three decimal
places; `live_age_ms` is `-1` when no live sample exists.

The vendor bulk control interface uses one full-speed USB OUT packet per
command:

```text
OUT: "Y7CTL1" + command text [+ NUL, CR or LF terminator/padding]
IN:  "Y7RSP1" + JSON length (uint16 little-endian) + JSON bytes
```

- The packet is at most 64 bytes. Command text is at most 57 bytes after the
  six-byte magic. Short packets of at most 63 bytes keep their existing framing
  and need no explicit terminator.
- A packet of exactly 64 bytes must end in NUL, CR or LF. This prevents executing
  the first fragment of an oversized USB transfer as if it were a full command.
- Multi-packet commands are unsupported. Commands must be sent in one transfer
  of the allowed length. Internal NUL bytes followed by non-padding data are
  rejected. Trailing NUL/CR/LF padding is accepted.
- Read the complete response before sending another command. Bulk replies
  continue across IN packets when the FIFO is full. Pending response bytes are
  discarded when the USB session changes.

HID feature control uses report ID `0x7f`:

```text
SET_REPORT: "Y7HID1" + command text [+ trailing NUL/CR/LF padding]
GET_REPORT: "Y7HRS1" + total length (u16 LE) + offset (u16 LE)
            + chunk length (u8) + JSON chunk
```

Feature requests fit within the 64-byte HID report, including its report ID.
Read chunks until the advertised total length is reached. Embedded NUL followed
by text is rejected, and a new USB session clears prior feature responses.

These endpoints share the same command parser and command names. They do not
provide independent concurrent request/response streams.

USB controller input currently supports full-state report mode `0x30`. Simple
mode `0x3f` and MCU mode `0x31` are not implemented as live output modes; the
bridge continues emitting `0x30`. HD rumble translation retains the existing
simplified amplitude mapping, with standard neutral/stop frames recognized.
