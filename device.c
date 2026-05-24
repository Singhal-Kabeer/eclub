#include <stdio.h>
#include <stdint.h>

// The code has two components, a send and a reciever,
// and some functions which interact with both components

#define OUTPUT_BUFFER_SIZE (128)
#define REPLY_BUFFER_SIZE (20)
#define START_BYTE (0xDB)



// --- Sender Variables ----
// streams
FILE *output_stream, *sensor_stream;
// output buffer and state
uint8_t output_buffer[OUTPUT_BUFFER_SIZE];
uint8_t start_of_unsent_output = 0;
uint8_t start_of_unacknowledged_output = 0;
uint8_t end_of_output = 0;
// outputting and state
void output();
uint8_t last_written_counter = 0;
// making data to send
void package_sensor(uint8_t);



// --- Reciever Variables ---
// streams
FILE *input_stream;
// input buffer and state
uint8_t input_buffer[32];
uint8_t current_input_buffer_index = 0;
// reading and state
void read_input(uint8_t);
uint8_t remaining_bytes_in_current_payload = 0;
uint8_t is_payload_count_left = 0;
uint8_t last_correctly_recieved_counter = 0;



// --- Replying --- triggered by reciever, but sent by sender
const uint8_t reply_packet_size = 5;
// buffer and state
uint8_t reply_buffer[REPLY_BUFFER_SIZE];
uint8_t number_of_reply = 0;

// Error State : triggered by reciever, but sent by sender
#define NORMAL 0
#define IN_ERROR 1
#define IN_RECOVERY 2
uint8_t error_state = NORMAL;

// Packet structure
#define SENSOR_DATA_TYPE 0
#define ACKNOWLEDGED_TYPE 1
#define REASK_TYPE 2
const uint8_t maximum_payload_size = 28;
const uint8_t maximum_packet_size = 32;
const uint8_t packet_header_size = 4;


int main(void){

    // Initialising the streams
    input_stream = fopen("input.txt", "w"); fclose(input_stream);
    output_stream = fopen("output.txt", "w"); fclose(output_stream);
    sensor_stream = fopen("sensor.txt", "w"); fclose(sensor_stream);

    // Prompt Loop
    char choice = '\0';
    uint8_t number = 0;

    while (choice != 'e')
    {
        printf("Please enter a choice: ");
        scanf(" %c", &choice);
        if (choice == 'r' || choice == 'm') scanf(" %hhu", &number);

        if (choice == 'o') {
            printf("Output Sent to output.txt\n");
            output();
        } else if (choice == 'r'){
            printf("Reading from input.txt\n");
            read_input(number);
        } else if (choice == 'm'){
            printf("Reading from sensor.txt\n");
            package_sensor(number);
        } else if (choice == 't') {
            printf("Timeout triggered\n");
            start_of_unsent_output = start_of_unacknowledged_output;
        }
    }
    
    return 0;
}

// Sender functions
void output(){
    
    output_stream = fopen("output.txt", "w");

    // Sender sends packaged sensor data
    while (start_of_unsent_output != end_of_output){
        fprintf(output_stream, "%d ", output_buffer[start_of_unsent_output]);
        start_of_unsent_output = (start_of_unsent_output+1)%OUTPUT_BUFFER_SIZE;
    }

    // Sender sends replies. This is triggered by reciever
    // If reciever put a reask in the reply buffer we must correct error state
    if (error_state==IN_ERROR) error_state=IN_RECOVERY;
    for (uint8_t i = 0; i < number_of_reply * reply_packet_size; i++) {
        fprintf(output_stream, "%d ", reply_buffer[i]);
    }
    number_of_reply = 0;

    fclose(output_stream);
}

void package_sensor(uint8_t number_of_bytes){

    sensor_stream = fopen("sensor.txt", "r");

    uint8_t number_of_full_packets = number_of_bytes/maximum_payload_size;
    uint8_t last_packet_payload = number_of_bytes - (number_of_full_packets*maximum_payload_size);

    uint16_t space_required = (number_of_full_packets * maximum_packet_size) + last_packet_payload + packet_header_size;

    // we subtract one here since end_of_output==start_of_unacknowledged_output means empty buffer so we need to keep end_of_output one before start_of_unacknowledged_output when writing to the buffer
    uint16_t space_available;
    if (start_of_unacknowledged_output > end_of_output) {
        space_available = start_of_unacknowledged_output - end_of_output - 1;
    } else {
        space_available = (OUTPUT_BUFFER_SIZE - end_of_output) + (start_of_unacknowledged_output - 1);
    }

    if (space_required > space_available){
        printf("Cannot fullfill this due to memory constraints\n\n");
        return;
    }

    uint8_t byte = 0;
    uint8_t checksum = 0;

    for (uint8_t i = 0; i < number_of_full_packets; i++)
    {
        checksum = 0;

        output_buffer[end_of_output] = START_BYTE;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        output_buffer[end_of_output] = ((maximum_payload_size<<3) + ((last_written_counter+1)%8));
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        for (uint8_t j = 0; j < maximum_payload_size; j++)
        {
            fscanf(sensor_stream, " %hhu", &byte);
            output_buffer[end_of_output] = byte;
            checksum+=output_buffer[end_of_output];
            end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;
        }

        output_buffer[end_of_output] = 0;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        output_buffer[end_of_output] = -checksum;
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        last_written_counter = (last_written_counter + 1) % 8;
    }
    
    if (last_packet_payload>0){
        checksum = 0;

        output_buffer[end_of_output] = START_BYTE;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        output_buffer[end_of_output] = ((last_packet_payload<<3) + ((last_written_counter+1)%8));
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        for (uint8_t i = 0; i < last_packet_payload; i++){
            fscanf(sensor_stream, " %hhu", &byte);
            output_buffer[end_of_output] = byte;
            checksum+=output_buffer[end_of_output];
            end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;
        }

        output_buffer[end_of_output] = SENSOR_DATA_TYPE;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        output_buffer[end_of_output] = -checksum;
        end_of_output = (end_of_output+1)%OUTPUT_BUFFER_SIZE;

        last_written_counter = (last_written_counter + 1) % 8;
    }

    fclose(sensor_stream);
}

// helper function, used by reciever
uint8_t get_counter_in_output_buffer(uint8_t pos){
    pos = (pos+1)%OUTPUT_BUFFER_SIZE;
    return (uint8_t) ((output_buffer[pos])&(0b111));
}

// Reciever Functions
void act_on_input(); // acts on a packet once the packet is completed in the input buffer

void read_input(uint8_t number_of_bytes){
    input_stream = fopen("input.txt", "r");
    
    uint8_t byte = 0;

    if (is_payload_count_left) {
        fscanf(input_stream, " %hhu", &byte);
        input_buffer[0] = START_BYTE;
        input_buffer[1] = byte;
        remaining_bytes_in_current_payload = ((byte>>3)+2);
        is_payload_count_left = 0;
        number_of_bytes--;
    }

    for (uint8_t i = 0; i < number_of_bytes; i++){
        fscanf(input_stream, " %hhu", &byte);
        if (remaining_bytes_in_current_payload > 0){
            input_buffer[current_input_buffer_index++] = byte;
            remaining_bytes_in_current_payload--;
            if (remaining_bytes_in_current_payload == 0) act_on_input();
        } else if (byte == START_BYTE) {
            current_input_buffer_index = 2;
            if (i == number_of_bytes-1) {
                is_payload_count_left = 1;
            }
            else {
                fscanf(input_stream, " %hhu", &byte);
                i++;
                input_buffer[0] = START_BYTE;
                input_buffer[1] = byte;
                remaining_bytes_in_current_payload = ((byte>>3)+2);
            }
        }
    }

    fclose(input_stream);
}

uint8_t nxt(uint8_t pos){
    pos = (pos+1)%OUTPUT_BUFFER_SIZE;
    uint8_t count = ((output_buffer[pos])>>3);
    pos = ((pos+count)%OUTPUT_BUFFER_SIZE);
    pos = ((pos+3)%OUTPUT_BUFFER_SIZE);
    return pos;
}

uint8_t verify_packet(uint8_t num){
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < num; i++)
    {
        checksum = checksum + input_buffer[i];
    }
    return checksum;
}

int8_t make_reply(uint8_t type_of_reply, uint8_t payload){
    if (error_state==NORMAL){
        if (number_of_reply == 4) {
            return -1;
        }
        uint8_t start = number_of_reply * reply_packet_size;
        reply_buffer[start] = START_BYTE;
        reply_buffer[start+1] = (1<<3);
        reply_buffer[start+2] = payload;
        reply_buffer[start+3] = type_of_reply;
        
        uint8_t total = START_BYTE + (1<<3) + payload + type_of_reply;
        total = -total;
        reply_buffer[start+4] = total;

        printf("Generated reply: %d %d %d %d %d\n\n", START_BYTE, (1<<3), payload, type_of_reply, total);    
        number_of_reply++;

        if (type_of_reply==REASK_TYPE) error_state=IN_ERROR;
        return 1;
    } else return -1;
}

void act_on_input(){

    // extracting metadata
    uint8_t counter = ((input_buffer[1])&(0b111));
    uint8_t payload_count = ((input_buffer[1])>>3);
    uint8_t type = input_buffer[payload_count+2];
    uint8_t checksum = input_buffer[payload_count+3];

    // report complete packets
    printf("I have a complete packet: ");
    printf("%d %d ", input_buffer[0], input_buffer[1]);
    uint8_t count = (input_buffer[1]>>3);
    for (int i = 0; i < count; i++)
    {
        printf("%d ", input_buffer[i+2]);
    }
    printf("%d %d\n", input_buffer[count+2], input_buffer[count+3]);
    printf("Counter is %d, payload is %d, type is %d, check is %d\n\n", counter, payload_count, type, checksum);

    // verify the check sum (this should be zero)
    checksum = verify_packet(payload_count+packet_header_size);

    // manipulated packet. push system into error, unless already in recovery
    if ((checksum!=0) && (error_state==NORMAL))
    {
        printf("Error packet detected. Generating re-ask.\n\n");
        make_reply(REASK_TYPE, (last_correctly_recieved_counter+1)%8); //put bad reply in output
        return;
    } else if (checksum!=0) return;

    if (type == 0) {
        // missed a packet somewhere. push system into error, unless already in recovery
        if ((counter != (last_correctly_recieved_counter+1)%8) && (error_state==NORMAL)) {
            printf("Out-of-sequence data. Expected %d, got %d. Generating re-ask.\n\n", (last_correctly_recieved_counter+1)%8, counter);
            make_reply(REASK_TYPE, (last_correctly_recieved_counter+1)%8);
            return;
        } else if (counter != (last_correctly_recieved_counter+1)%8) return;

        // simple payload
        if (error_state==IN_RECOVERY) error_state=NORMAL; //if in recovery, and reached this stage, then recovery is complete
        printf("I recieved payload: ");
        for (uint8_t i = 0; i < (input_buffer[1]>>3); i++)
        {
            printf("%d ", input_buffer[i+2]);
        }
        make_reply(ACKNOWLEDGED_TYPE, counter);
        last_correctly_recieved_counter = ((last_correctly_recieved_counter+1)%8);
    } else if (type == 1){
        // acknowledgement. the one byte payload tells the counter which was correctly recieved
        // reciever interacts with sender.
        // move the pointer in output buffer here
        uint8_t acknowledged_counter = input_buffer[2];
        acknowledged_counter = (acknowledged_counter+1)%8;

        while (start_of_unacknowledged_output != start_of_unsent_output && get_counter_in_output_buffer(start_of_unacknowledged_output) != acknowledged_counter){
            start_of_unacknowledged_output = nxt(start_of_unacknowledged_output);
        }
        
    } else if (type == 2){
        // re-ask. the one byte payload tells the counter which needs to be resent
        // reciever interacts with sender.
        // move the pointer in output buffer here

        uint8_t unacknowledged_counter = input_buffer[2];
        
        // Snap back to the absolute floor of valid memory
        start_of_unsent_output = start_of_unacknowledged_output;

        // Safely sweep forward until we find the requested counter
        while (start_of_unsent_output != end_of_output && get_counter_in_output_buffer(start_of_unsent_output) != unacknowledged_counter) {
            start_of_unsent_output = nxt(start_of_unsent_output);
        }

        //recieving a reask for counter 3 implicitly means counters 2 and earlier were recieved well
        start_of_unacknowledged_output = start_of_unsent_output;
    }
}