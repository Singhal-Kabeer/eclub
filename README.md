# Firmware Communication Layer
---

## Table of Contents
- [Overview](#overview)
- [The Problem](#the-problem)
- [Protocol Design](#protocol-design)
- [Implementation](#implementation)
- [Building and Running](#building-and-running)
- [Interface Reference](#interface-reference)
- [Testing and Simulation](#testing-and-simulation)
- [Demos](#demos)
- [Known Limitations](#known-limitations)

---

## Overview

The task was to build a communication layer for sensor data exchange between two devices over a non-ideal channel with strict memory constraints. My implementation is a c program that compiles to a terminal based application. The maximum packet size is 32 bytes and the total memory consumed is less than 256 bytes.

---

## The Problem

Two embedded devices continuously exchange sensor data. The channel between them is non-ideal and can:

- Corrupt individual bytes
- Drop packets entirely
- Duplicate packets
- Delay transmissions
- Insert arbitrary garbage bytes
- Split packets across multiple transmission windows
- Merge multiple packets into a single transmission window

The implementation recovers gracefully from all of the above without exceeding 256 bytes of RAM.

---

## Protocol Design

### Packet Structure

| Byte Index | Field | Description |
| :--- | :--- | :--- |
| 0 | `start` | Fixed start byte `0xDB` (219, chosen at random). Marks the beginning of every packet. |
| 1 | `metadata` | Upper 5 bits: payload size in bytes (max 28). Lower 3 bits: 0 to 7 to 0 counter to identify packet. |
| 2 to N | `payload` | The actual data bytes. |
| N+1 | `type` | `0` = sensor data, `1` = acknowledgement, `2` = re-ask. |
| N+2 | `checksum` | All bytes in a correct packet must sum to a multiple of 256. |

**Example:** A valid packet carrying 3 bytes of payload with counter 0 is

```
219 24 24 19 50 0 176
```

The metadata byte `24` is `00011 000` in binary. The upper 5 bits `00011` (3, in base 10) is the number of bytes in payload and the lower 3 bits `000` (0, in base 10) is the identification counter of the packet.

### Counter

The lower 3 bits of the metadata byte form a rolling counter. They increase from 0 to 1 to 2 and so on until 7 and then wrap around to 0 and then 1 and so on. Counters are used only for sensor data packets. Reply packets always carry counter 0.

Counters serve two purposes:
- **Identity**: each packet has a known sequence number.
- **Gap detection**: receiving counter 5 when the last correctly received counter was 3 means counter 4 was dropped.

NOTE : Reply packets do not have (valid) counters. The reasons, which will be clearer after reading the rest of the implementation, are
1. Replies are not generated for replies, since this accumulates network traffic
2. Replies are also not stored and transmitted together with the sensor data packets. Reason is that during retransmission of lost packets, if the sensor transmission and replies are interleaved then same replies will be sent twice. This is catastrophic. Since replies and sensor-packets are not stored together, they cannot be transmitted in the correct counter sequence even if replies are assigned counters.

### Packet Types

**Sensor data (type 0):** payload is raw sensor bytes. On receipt, the receiver generates an acknowledgement.

**Acknowledgement (type 1):** one byte payload carrying the counter of the last correctly received packet. The sender uses this to free its output buffer.

**Re-ask (type 2):** one byte payload carrying the counter of the packet that needs to be resent. The sender resends that packet and all subsequent ones. Receiving a re-ask for counter N also implicitly acknowledges all packets before counter N.

### Error States

The receiver operates in one of three states:

| State | Meaning |
| :--- | :--- |
| `NORMAL` | Operating correctly. |
| `IN_ERROR` | Corruption or gap detected. Re-ask generated but not yet sent. Receiver goes deaf to all sensor-data packets that come in this state, while still acting on acknowledgements and reasks. This decision to not going deaf to replies reduces network traffic and allows faster communication. Other option, to go deaf to all incoming packets would allow for interleaved reply and output buffers but increases network traffic |
| `IN_RECOVERY` | Re-ask has been sent. Ignoring all further sensor packets until the expected counter arrives. |

Once the expected counter is correctly received, the receiver returns to `NORMAL`.

---

## Implementation

### Memory Architecture

The 256-byte RAM budget is allocated as follows:

| Buffer | Size |
| :--- | :--- |
| Output buffer (circular) | 128 bytes |
| Reply buffer | 20 bytes |
| Input buffer | 32 bytes |

The memory left is left to make room for state variables and the stack variables.

### Output Buffer

The output buffer is a circular buffer of 128 bytes with three pointers:

- `start_of_unacknowledged_output` — oldest packet not yet acknowledged. Nothing after this is ever overwritten.
- `start_of_unsent_output` — next byte to transmit. On re-ask, this rewinds to `start_of_unacknowledged_output` and sweeps forward to the requested counter.
- `end_of_output` — where the next packet will be written. Never allowed to reach `start_of_unacknowledged_output` (one slot kept empty to distinguish full from empty buffer).

New sensor packets are written at `end_of_output`. When output is flushed, `start_of_unsent_output` advances. On acknowledgement, `start_of_unacknowledged_output` advances, freeing space. On reask, `start_of_unsent_output` goes back.

### Input Processing

`read_input()` processes `n` bytes from `input.txt` in a single pass. It scans for the start byte `0xDB`, then accumulates subsequent bytes into the input buffer until the packet is complete, then calls `act_on_input()`. It then continues scanning for the next packet.

Packets split across multiple `r n` calls are handled via two state variables: `remaining_bytes_in_current_payload` tracks how many bytes of the current packet are still expected, and `is_payload_count_left` handles the edge case where the start byte and metadata byte arrive in separate calls.

### Key Functions

**`package_sensor(n)`** — reads `n` bytes from `sensor.txt` and writes one or more packets into the output buffer. Packets are at most 28 bytes of payload each. Checks available space before writing.

**`output()`** — flushes the output buffer and reply buffer to `output.txt`. Advances `start_of_unsent_output`. Transitions error state from `IN_ERROR` to `IN_RECOVERY`.

**`read_input(n)`** — reads `n` bytes from `input.txt` and processes any complete packets found.

**`act_on_input()`** — dispatches on packet type. Verifies checksum, checks counter sequence, generates replies or re-asks, and advances output buffer pointers on acknowledgement or re-ask.

**`make_reply(type, payload)`** — writes a reply packet into the reply buffer. Silently fails if the buffer already holds 4 replies or if the receiver is not in `NORMAL` state.

**`get_counter_in_output_buffer(pos)`** — given the position of the start byte of a packet in the output buffer, returns its counter value.

**`nxt(pos)`** — given the position of the start byte of a packet in the output buffer, returns the position of the start byte of the next packet.

---

## Building and Running

```bash
clang device.c
./a.out
```

To simulate two devices, create two folders, copy the executable into each, and run one in each folder.

**Note:** The application creates blank `input.txt`, `output.txt`, and `sensor.txt` on launch, overwriting any existing files. Tester should not write to these files before launching.

---

## Interface Reference

The terminal provides an interface to test the communication layer. The tester (one who executes the executable) will act as

- the second device, when processing the output by the application to generate adequate input
- the channel, when providing input data to the application and reading the output data from it
- the sensor, when providing sensor data to the application and asking the application to package that data
- a clock, while providing timeout signals

Simulations can be run by using two executables each in a separate folder. Python can generate random sensor data for a device and manual copy paste operations (while corrupting the data after copy before paste) can simulate a non-ideal channel.

| Command | Description |
| :--- | :--- |
| `m <n>` | Read `n` bytes from `sensor.txt` and package them into the output buffer. |
| `o` | Flush output buffer and reply buffer to `output.txt`. Overwrites previous contents. |
| `r <n>` | Read `n` bytes from `input.txt` and process any complete packets found. |
| `t` | Trigger a timeout. Rewinds `start_of_unsent_output` to `start_of_unacknowledged_output`, scheduling all unacknowledged packets for retransmission on the next `o`. |
| `e` | Exit the application. |

**Important notes on file handling:**
- `sensor.txt` and `input.txt` are always read from the beginning. Overwrite them (do not append) before each `m` or `r` call.
- `output.txt` is overwritten on every `o` call. Save its contents elsewhere before calling `o` again if you need the previous output.
### Timeout

If an acknowledgement is dropped or tampered with, the sender's output buffer freezes — it holds unacknowledged packets it can never free, and the receiver has no mechanism to re-ask for reply packets. In a real system this would be resolved by a hardware timer. In this simulation, the tester issues a `t` command followed by `o` to force retransmission of all unacknowledged packets.

---

## Testing and Simulation

### Simulation Setup

1. Two executables are placed in separate folders (`/device1/`, `/device2/`).
2. Each is launched to initialise its file streams.
3. A Python script generates random integers in the range 0–255 as sensor data.
4. Sensor data is pasted into `sensor.txt` of whichever device is sending.
5. The output of one device is copied into the `input.txt` of the other to simulate the channel. Bytes can be tampered with during this copy to simulate channel faults.

---

## Demos

### No corruption

#### I. Device 1 sends, device 2 recieves
1. Blue is given 30 byte sensor data (paste in `sensor.txt`, prompt `m 30`)
> 100 23 203 115 162 4 112 179 18 142 178 211 93 173 217 2 60 18 207 145 110 206 29 72 217 238 73 83 245 83 
2. Blue outputs (prompt `o`)
> 219 225 100 23 203 115 162 4 112 179 18 142 178 211 93 173 217 2 60 18 207 145 110 206 29 72 217 238 73 83 0 6 219 18 245 83 0 203 
3. This is inputted into Red (paste in `sensor.txt`, prompt `r 38`, prompt `o`). It outputs two acknowledgements
> 219 8 1 1 27 219 8 2 1 26
4. This is inputted into Blue. When prompt for output, blue outputs blank file.

#### II. Device 1 and 2 simultaneously send and recieve
1. Blue and Red are given 30 bytes sensor data
> (blue) 100 23 203 115 162 4 112 179 18 142 178 211 93 173 217 2 60 18 207 145 110 206 29 72 217 238 73 83 245 83
> (red) 229 131 222 198 9 199 87 202 241 9 111 20 86 202 140 220 32 29 108 169 179 55 50 133 231 131 81 59 127 5
2. They package the data and output
> (blue) 219 225 100 23 203 115 162 4 112 179 18 142 178 211 93 173 217 2 60 18 207 145 110 206 29 72 217 238 73 83 0 6 219 18 245 83 0 203 
> (red) 219 225 229 131 222 198 9 199 87 202 241 9 111 20 86 202 140 220 32 29 108 169 179 55 50 133 231 131 81 59 0 89 219 18 127 5 0 143 
3. They are inputted into each other. They generate replies
> (blue) 219 8 1 1 27 219 8 2 1 26 
> (red) 219 8 1 1 27 219 8 2 1 26 
4. The replies are inputted into eachother

### Some Corruption

#### I. Dropping a packet
1. Blue is given 60 byte sensor data
> 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 233 124 249 141
2. Blue packets it to
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 195  
> 219 35 233 124 249 141 0 23 
3. Second packet is dropped
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 35 233 124 249 141 0 23 
4. This is inputted into red. Red replies
> 219 8 1 1 27  
> 219 8 2 2 25
5. This is inputted into blue. Blue resends packets 2 and 3 both
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 195  
> 219 35 233 124 249 141 0 23
6. This is inputted into red. It gives two acknowledgements. They go to red. Clean exit.

#### II. Corrupting a checksum
1. Blue is given 60 byte sensor data
> 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 233 124 249 141
2. Blue packets it to
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 195  
> 219 35 233 124 249 141 0 23
3. Checksum of second packet is corrupted
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 196  
> 219 35 233 124 249 141 0 23 
4. This is inputted into red. Red replies
> 219 8 1 1 27  
> 219 8 2 2 25
5. This is inputted into blue. Blue resends packets 2 and 3 both
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 195  
> 219 35 233 124 249 141 0 23
6. This is inputted into red. It gives two acknowledgements. They go to red. Clean exit.

#### III. Dropping the last packet
```Relies on timeout at sender```
1. Blue is given 60 byte sensor data
> 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 233 124 249 141
2. Blue packets it to
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 195  
> 219 35 233 124 249 141 0 23 
3. Third packet is dropped
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 226 206 53 243 191 188 136 252 59 105 9 239 200 154 7 28 114 197 146 120 208 205 144 33 144 252 211 248 132 0 195
4. This is inputted into red. Red replies
> 219 8 1 1 27  
> 219 8 2 1 26
5. This is inputted into blue. Blue accepts acknowledgements but is waiting for an acknowledgement on third packet. Asking Blue to output right now yields empty output file
6. Trigger a timeout at blue. Then ask blue to output
> 219 35 233 124 249 141 0 23 

#### IV. Corrrupting the metadata
```Relies on timeout at sender if payload count is increased but future bytes are never given. Timeout at sendor will cause sendor to send the packets again. These will provide the residual bytes needed. The reciever will read these bytes but will not understand the meaning and thus generate a reask. Now the sendor will transmit this packet the third time, thus completing delivery```
1. Blue is given 30 byte sensor data
> 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 206 53
2. Blue packets it to
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 18 206 53 0 16 
3. Header is corrupted. (payload count in second packet is increased by 1)
> 219 225 177 233 97 25 70 105 180 239 123 37 126 253 166 148 100 69 27 94 162 230 156 24 194 226 124 138 75 135 0 175  
> 219 26 206 53 0 16 
4. This is inputted into red. Red replies
> 219 8 1 1 27
5. This is inputted into blue. Blue accepts acknowledgements but is waiting for an acknowledgement on second packet. Asking Blue to output right now yields empty output file
6. Trigger a timeout at blue. Then ask blue to output
> 219 18 206 53 0 16
7. Input this into red. Red replies
> 219 8 2 2 25 
8. Input this into blue. Blue resends one more time 
> 219 18 206 53 0 16

### Packet Fragmentation

I test this with the valid packet `219 24 24 19 50 0 176`. I fragment this packet in multiple ways, then input it into a device. Console output tells whether the packet is succesfully read. All the following splits worked:

1. Split across two `r n` calls mid payload
2. Split across three `r n` calls `219` and `24 24 19 50` and `0 176`

### Counter wraparound and garbage bytes.
I send eight valid packets, beginning with counter 1. Along with garbage bytes separating the packets. The garbage here does not have `219` as a byte since then reask is triggered which has already been tested. Console output verifies that the following eigth packets:

| Counter | Payload | Packet |
| :--- | :--- | :--- |
| 1 | 0 | `219 1 0 36` |
| 2 | 1 | `219 10 161 0 122` |
| 3 | 3 | `219 27 106 86 12 0 62` |
| 4 | 2 | `219 20 148 103 0 22` |
| 5 | 4 | `219 37 193 176 159 150 0 90` |
| 6 | 0 | `219 6 0 31` |
| 7 | 2 | `219 23 216 70 0 240` |
| 0 | 1 | `219 8 235 0 50` |
| 1 | 4 | `219 33 155 176 175 192 0 74` |

can be recovered from


> 195 150 160 100 159 157 `219 1 0 36` 216 173 94 94 128 82 68 193 44 196 47 164 10 9 63 139 200 53 93 5 113 `219 10 161 0 122` 45 48 106 203 242 `219 27 106 86 12 0 62` 21 176 15 `219 20 148 103 0 22` 45 39 239 70 208 163 74 40 231 `219 37 193 176 159 150 0 90` 145 243 153 72 71 43 221 82 184 50 11 71 224 `219 6 0 31` 112 248 101 153 155 59 252 64 31 241 40 6 `219 23 216 70 0 240` 46 204 55 165 116 237 `219 8 235 0 50` 90 153 13 104 184 72 214 59 154 125 132 71 116 114 189 157 108 45 228 211 `219 33 155 176 175 192 0 74` 176 11 194 200 126 



NOTE : Such a tranmission is not ideal. The implementation offers scheduling of only 4 replies at a time. The other replies are dropped completely silently. Thus the sender will never recieve acknowledgements for the remaining four packets. Eventual timeout will cause these packets to be resent. But they will then not match the counters at the reciever. The reciever will send a reask for counter 2. The implemetation treats a reask for counter 2 as an implicit acknowledgement for earlier counters. Thus the sender does free memory and communication can continue.

It might seem like cumulative replies can fix such faults. Problem is, the counter wraps around every 8 packets. Cumulative replies fail in wraparound cases anyways.

---

## Known Limitations

**Reply buffer capacity.**  
The transmission in last demo (of 8 packets in one call) not ideal. The implementation offers scheduling of only 4 replies at a time. The other replies are dropped completely silently. Thus the sender will never recieve acknowledgements for the remaining four packets. Eventual timeout will cause these packets to be resent. But they will then not match the counters at the reciever. The reciever will send a reask for counter 2. The implemetation treats a reask for counter 2 as an implicit acknowledgement for earlier counters. Thus the sender does free memory and communication can continue.

It might seem like cumulative replies can fix such faults. Problem is, the counter wraps around every 8 packets. Cumulative replies fail in wraparound cases anyways.

**Sustained corruption during recovery.** 
If the channel continues injecting corrupted packets while the receiver is in `IN_RECOVERY` state, the corrupted packets are silently dropped and no secondary re-ask is generated. The receiver waits indefinitely for the correct counter. A timeout mechanism on the receiver side would resolve this but is not implemented.

**Counter range.** 
The 3-bit counter allows only 8 distinct values. This is sufficient for normal operation but creates ambiguity if the sender has more than 8 unacknowledged packets in flight simultaneously, which cannot happen within the 128-byte output buffer constraint.
