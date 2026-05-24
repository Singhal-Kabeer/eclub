#include <stdio.h>
#include <stdint.h>

#define obs 128

FILE *input_stream, *output_stream, *sensor_stream;

void output();
void read_input(uint8_t);
void package_sensor(uint8_t);

// most of the 256 bytes memory limit is being used up here. About 180 bytes allocated here
uint8_t input_buffer[32];
uint8_t output_buffer[obs];
uint8_t reply_buffer[20]; // one reply is 5 bytes.

// some state variables
uint8_t remaining_bytes_in_current_payload = 0;
uint8_t is_payload_count_left = 0;
uint8_t current_input_buffer_index = 0;
uint8_t last_correctly_recieved_counter = 0;
uint8_t last_written_counter = 0;
uint8_t number_of_reply = 0;

// state of cyclic buffer for output_buffer
uint8_t start_of_unsent_output = 0;
uint8_t start_of_unacknowledged_output = 0;
uint8_t end_of_output = 0;

// error state of program. 0 means normal, 1 means currently in failure (nack not sent yet), 2 means recovering (nack sent, waiting for correct counter)
uint8_t error_state = 0;

int main(void){

    // Initialising the streams
    input_stream = fopen("input.txt", "w");
    fclose(input_stream);
    output_stream = fopen("output.txt", "w");
    fclose(output_stream);
    sensor_stream = fopen("sensor.txt", "w");
    fclose(sensor_stream);

    char choice = '\0';
    uint8_t number = 0;

    while (choice != 'e')
    {
        printf("Please enter a choice: ");
        scanf(" %c", &choice);
        if (choice == 'r' || choice == 'm') scanf(" %hhu", &number);

        if (choice == 'o') {
            printf("I am outputting\n");
            output();
        } else if (choice == 'r'){
            printf("I am reading %d from input file\n", number);
            read_input(number);
        } else if (choice == 'm'){
            printf("I am reading %d from output file\n", number);
            package_sensor(number);
        } else if (choice == 't') {
            printf("Timeout triggered. Rewinding transmission queue.\n");
            start_of_unsent_output = start_of_unacknowledged_output;
        }
    }
    

    return 0;
}

void output(){
    if (error_state==1) error_state=2;
    output_stream = fopen("output.txt", "w");

    // write the replies
    for (uint8_t i = 0; i < number_of_reply * 5; i++) {
        fprintf(output_stream, "%d ", reply_buffer[i]);
    }
    number_of_reply = 0;

    // write the messages
    while (start_of_unsent_output != end_of_output){
        fprintf(output_stream, "%d ", output_buffer[start_of_unsent_output]);
        start_of_unsent_output = (start_of_unsent_output+1)%obs;
    }

    fclose(output_stream);
}

uint8_t countter(uint8_t pos){
    pos = (pos+1)%obs;
    return (uint8_t) ((output_buffer[pos])&(0b111));
}

uint8_t nxt(uint8_t pos){
    pos = (pos+1)%obs;
    uint8_t count = ((output_buffer[pos])>>3);
    pos = ((pos+count)%obs);
    pos = ((pos+3)%obs);
    return pos;
}

uint8_t check_the_sum(uint8_t num){
    uint8_t checksum = 0;
    for (uint8_t i = 0; i < num; i++)
    {
        checksum = checksum + input_buffer[i];
    }
    return checksum;
}

int8_t make_reply(uint8_t type_of_reply, uint8_t payload){
    if (error_state==0){
        if (number_of_reply == 4) {
            return -1;
        }
        uint8_t start = number_of_reply * 5;
        reply_buffer[start] = 0xDB;
        reply_buffer[start+1] = (1<<3);
        reply_buffer[start+2] = payload;
        reply_buffer[start+3] = type_of_reply;
        
        uint8_t total = 0xDB + (1<<3) + payload + type_of_reply;
        total = -total;
        reply_buffer[start+4] = total;

        printf("Generated reply: %d %d %d %d %d\n\n", 0xDB, (1<<3), payload, type_of_reply, total);    
        number_of_reply++;

        if (type_of_reply==2) error_state=1;
        return 1;
    } else return -1;
}

void act_on_input(){

    // extracting metadata
    uint8_t counter = ((input_buffer[1])&(0b111));
    uint8_t payload_count = ((input_buffer[1])>>3);
    uint8_t type = input_buffer[payload_count+2];
    uint8_t checksum = input_buffer[payload_count+3];

    // report correct packets
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
    checksum = check_the_sum(payload_count+4);

    // manipulated packet. push system into error, unless already in recovery
    if ((checksum!=0) && (error_state==0))
    {
        printf("Error packet detected. Generating NACK.\n\n");
        make_reply(2, (last_correctly_recieved_counter+1)%8); //put bad reply in output
        return;
    } else if (checksum!=0) return;

    if (type == 0) {
        // missed a packet somewhere. push system into error, unless already in recovery
        if ((counter != (last_correctly_recieved_counter+1)%8) && (error_state==0)) {
            printf("Out-of-sequence data. Expected %d, got %d. Generating NACK.\n\n", (last_correctly_recieved_counter+1)%8, counter);
            make_reply(2, (last_correctly_recieved_counter+1)%8);
            return;
        } else if (counter != (last_correctly_recieved_counter+1)%8) return;

        // simple payload
        if (error_state==2) error_state=0; //if in recovery, and reached this stage, then recovery is complete
        printf("I recieved payload: ");
        for (uint8_t i = 0; i < (input_buffer[1]>>3); i++)
        {
            printf("%d ", input_buffer[i+2]);
        }
        make_reply(1, counter);
        last_correctly_recieved_counter = ((last_correctly_recieved_counter+1)%8);
    } else if (type == 1){
        // acknowledgement. the one byte payload tells the counter which was correctly recieved
        // move the pointer in output buffer here
        uint8_t acknowledged_counter = input_buffer[2];
        acknowledged_counter = (acknowledged_counter+1)%8;

        while (start_of_unacknowledged_output != start_of_unsent_output && countter(start_of_unacknowledged_output) != acknowledged_counter) start_of_unacknowledged_output = nxt(start_of_unacknowledged_output);
        
    } else if (type == 2){
        // negative acknowledgement. the one byte payload tells the counter which needs to be resent
        // move the pointer in output buffer here

        uint8_t unacknowledged_counter = input_buffer[2];
        
        // Snap back to the absolute floor of valid memory
        start_of_unsent_output = start_of_unacknowledged_output;

        // Safely sweep forward until we find the requested counter
        while (start_of_unsent_output != end_of_output &&countter(start_of_unsent_output) != unacknowledged_counter) {
            start_of_unsent_output = nxt(start_of_unsent_output);
        }
    }
}

void read_input(uint8_t number_of_bytes){
    input_stream = fopen("input.txt", "r");
    
    uint8_t byte = 0;

    if (is_payload_count_left) {
        fscanf(input_stream, " %hhu", &byte);
        input_buffer[0] = 0xDB;
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
            if (remaining_bytes_in_current_payload == 0){
                act_on_input();
            }
        } else if (byte == 0xDB) {
            current_input_buffer_index = 2;
            if (i == number_of_bytes-1) {
                is_payload_count_left = 1;
            }
            else {
                fscanf(input_stream, " %hhu", &byte);
                i++;
                input_buffer[0] = 0xDB;
                input_buffer[1] = byte;
                remaining_bytes_in_current_payload = ((byte>>3)+2);
            }
        }
    }

    fclose(input_stream);
}

void package_sensor(uint8_t number_of_bytes){

    sensor_stream = fopen("sensor.txt", "r");

    uint8_t number_of_full_packets = number_of_bytes/28;
    uint8_t last_packet_payload = number_of_bytes - (number_of_full_packets*28);


    uint16_t space_required = (number_of_full_packets * 32) + last_packet_payload + 4;
    uint16_t space_available = (start_of_unacknowledged_output - end_of_output - 1 + obs) % obs;

    if (space_required > space_available){
        printf("Cannot fullfill this due to memory constraints\n\n");
        return;
    }

    uint8_t byte = 0;
    uint8_t checksum = 0;

    for (uint8_t i = 0; i < number_of_full_packets; i++)
    {
        checksum = 0;

        output_buffer[end_of_output] = 0xDB;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%obs;

        output_buffer[end_of_output] = ((28<<3) + ((last_written_counter+1)%8));
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%obs;

        for (uint8_t j = 0; j < 28; j++)
        {
            fscanf(sensor_stream, " %hhu", &byte);
            output_buffer[end_of_output] = byte;
            checksum+=output_buffer[end_of_output];
            end_of_output = (end_of_output+1)%obs;
        }

        output_buffer[end_of_output] = 0;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%obs;

        output_buffer[end_of_output] = -checksum;
        end_of_output = (end_of_output+1)%obs;

        last_written_counter = (last_written_counter + 1) % 8;
    }
    
    if (last_packet_payload>0){
        checksum = 0;

        output_buffer[end_of_output] = 0xDB;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%obs;

        output_buffer[end_of_output] = ((last_packet_payload<<3) + ((last_written_counter+1)%8));
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%obs;

        for (uint8_t i = 0; i < last_packet_payload; i++){
            fscanf(sensor_stream, " %hhu", &byte);
            output_buffer[end_of_output] = byte;
            checksum+=output_buffer[end_of_output];
            end_of_output = (end_of_output+1)%obs;
        }

        output_buffer[end_of_output] = 0;
        checksum+=output_buffer[end_of_output];
        end_of_output = (end_of_output+1)%obs;

        output_buffer[end_of_output] = -checksum;
        end_of_output = (end_of_output+1)%obs;

        last_written_counter = (last_written_counter + 1) % 8;
    }

    fclose(sensor_stream);
}