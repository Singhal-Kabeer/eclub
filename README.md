# Firmware Task

## COMPILATION & EXECUTION
This project relies strictly on standard C libraries. 
Compile: `clang device.c`
Execute: `./a.out`

## THE TASK
My task was to build a communication layer for communication between two devices which send sensor data to each other through a non-ideal channel. The channel can
- corrupt some bytes
- drop packets in the middle
- duplicate packets
- delay transmissions
- insert garbage bytes
- split packets across multiple updates
- merge multiple packets together

## THE IMPLEMENTATION
### Tester Role
My implementation is a c code. The code compiles to a terminal based application. The terminal provides an interface to test the communication layer. The tester (one who executes the executable) will act as

- the second device, when processing the output by the application to generate adequate input
- the channel, when providing input data to the application and reading the output data from it
- the sensor, when providing sensor data to the application and asking the application to package that data
- a clock, while providing timeout signals

Simulations can be run by using two executables each in a separate folder. Python can generate random sensor data for a device and manual copy paste operations (while corrupting the data after copy before paste) can simulate a non-ideal channel.


### Interface
As soon as the the application is launched, it creats three blank text files in the same folder. Further an infinite loop is initiated that repeatedly prompts for input. Inputting `e` ends the application.
- `sensor.txt` this file stream is filled by tester. Application reads the data, packages it and schedules it to be sent on command of the tester. The tester writes data into this file. Then in the prompt of the application writes `m` followed by a space and the number of bytes the testers wants to packet. NOTE: APPLICATION EXPECTS THE FILES TO BE OVERWRITTEN WITH NEW DATA WHENEVER A READ OPERATION IS REQUIRED (SO IT READS FROM THE BEGINNING ONLY, NOT FROM PREVIOUS OFFSET) THUS BEFORE EACH CALL THE TESTER IS EXPECTED TO OVERWRITE (AND NOT APPEND TO) SENSOR AND INPUT FILES
- `output.txt` this file stream is filled by the application on command of tester. NOTE: THIS FILE IS OVERWRITTEN (AND NOT APPENDED TO) ON EVERY CALL. TESTER MUST STORE OUTPUT ELSEWHERE IF PREVIOUS OUTPUT IS NEEDED. The command used by tester is a simple `o`. Thereafter the application will overwrite the `output.txt` with all the output it has scheduled (replies as well as new sensor packets).
- `input.txt` this file stream is filled by tester. ideally this is the data coming from the other device's output stream. The command used by tester is `r` followed by a space and a number. The number represents the number of bytes the tester wants the application to read.

NOTE: AS SOON AS THE APP LAUNCHES, IT OVERWRITES ALL FILES (MAKES THEM BLANK). SO DO NOT STORE DATA INTO SENSOR OUT INPUT BEFORE STARTING THE APPLICATION.

All files are text files. Bytes are written to and read from these as simple decimal integers (`uint8_t`)

The sendor has access to another command. The timeout command `t`. The purpose of this command is written in the communication layer header.

## THE COMMUNICATION LAYER
### Packet Structure
| Index | Field | Description |
| :--- | :--- | :--- |
| 0 | `start` | I chose a random byte : 0xDB (in decimal, 219). Indicates start of a packet |
| 1 | `meta-data` | First five bits represent the number of bytes of payload the packet is carrying (maximum 28). The last three bits represent a counter in packets. This counter counts from 0 to 7 then rolls back to 0. This is used as an identity of the packets and for identifying missing packets (recieving a packet with counter 5 when the last recieved counter is 3 indicates that the packet 4 was dropped). |
| 2 to N | `payload` | The actual data |
| N+1 | `type` | Represents the type of the byte. `0` means that this packet's payload is sensor data, `1` means this packet's payload is the counter of the packet we sent which was succefully recieved, `2` means this packet's payload is the counter the packet corresponding to which needs to be resent (along with all packets after that one). |
| N+2 | `checksum` | The total of all bytes in the packet must sum up to a multiple of 256. |

So a sample packet carrying 3 bytes of sensor data (`24 19 50`) would be `219 24 24 19 50 0 176`. This is because `24` in base two is `00011 000` where the first five bits `00011` represents a payload of 3 bytes and the last three bits `000` represent the counter of this packet (here taken to be zero).

Communication layer is designed such that counters are used only for sensor-data packets and not replies. Thus replies always have a zero counter. Further each reply has a payload of only one byte thus its second byte is always `00001 000` (in base 10, 8)

### Replies
One a device gets a sensor-data packet it generates replies. Reply can be an acknowledgement or a re-ask. Replies are not generated for replies (acknowledging acknowledgements accumulates network traffic). If a device sends a reask for a packet with counter 4, the other device resends all packets with counters after 4. 

For this purpose the output buffer of a device is a circular buffer. Old output is overwritten only when it has been acknowledged already. 

## Memory Architecture
I have one input buffer (32 bytes). `read_input()` reads input of `n` bytes from the `input.txt` and simultaneously processes this data. It continuously looks for packets. Once it finds a packet, it starts storing the packet into the buffer. Then it calls `act_on_input()` which acts on this recieved packet and generates replies. `read_input` then continues finding another valid packet. The function is built such that packets can be split over multiple `r n` prompts and in fact even the payload count byte and starting byte can appear in two different calls.

The output buffer has size 128 bytes. It is modeled as a circular buffer. I have three pointers : 
- `start_of_unsent_output`
- `start_of_unacknowledged_output`
- `end_of_output`
New sensor data packets are written at `end_of_output` until it hits `start_of_unacknowledged_output`. Unacknowledged output is never overwritten. Once acknowleded, this pointer advances (addition modulo `obs`, object buffer size) to clear space for more writing. When output is written to `output.txt` the `start_of_unsent_output` advances. This never advances past `end_of_output`. When a reask is recieved, the `start_of_unsent_output` drops to `start_of_unacknowledged_output` and advances till the reask, waiting for an `o` prompt to sweep till `end_of_output` thus resending the requisite data

Combined with the reply buffer, these buffers take up `180` bytes. The limit was `256` bytes. Remaining bytes I have left so that the stack can function without any problems.

## Timeout command

If the tester tampers with reply packets or drops them entirely the system stalls. The reciever will never know it missed an acknowledgement since they do not have any counters. And if these packets are tampered with, the reciever generates a reask only for the sensor data packet counter it is expecting, not the reply packet. Thus the output buffer on the reciever device will freeze. 

In such situtation, a real world system would force a clearing of the output buffer by detecting timeout. In this virtual firmware testing application, the tester is expected to give `t` prompt when they want to force a timeout on a device (followed by an `o` to actually resend the data). 

## Demo
I present some simultations next. 
1. Two executables are created and stored in different folders. Red device and blue device
2. Each is run so that they populate respective folders with requisite files.
3. A python script generates random integers from 0 to 255
4. Random numbers are sent to the sensor file for any device
5. The device's outputs and inputs are communicated to the other's by simple copy paste operations, with some corrupting in between

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