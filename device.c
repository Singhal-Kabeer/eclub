#include <stdio.h>
#include <stdint.h>

FILE *input_stream, *output_stream, *sensor_stream;

void output();
void read_input(uint8_t);
void package_sensor(uint8_t);

// most of the 256 bytes memory limit is being used up here. About 160 bytes allocated here
uint8_t input_buffer[32];
uint8_t output_buffer[128];

// some state variables
uint8_t remaining_bytes_in_current_payload;
uint8_t is_payload_count_left;
uint8_t current_input_buffer_index;
uint8_t last_correctly_recieved_counter;

int main(void){

    // Initial State
    remaining_bytes_in_current_payload = 0;
    is_payload_count_left = 0;
    current_input_buffer_index = 0;
    last_correctly_recieved_counter = 0;

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
            output();
        } else if (choice == 'r'){
            read_input(number);
        } else if (choice == 'm'){
            package_sensor(number);
        }
    }
    
    
    return 0;
}


void output(){
    output_stream = fopen("output.txt", "w");

    fprintf(output_stream, "Look here buddy\n");

    fclose(output_stream);
}

void act_on_input(){
    /*
    printf("I have a packet: ");
    printf("%d %d ", input_buffer[0], input_buffer[1]);
    uint8_t i = 0;
    for (; i < (input_buffer[1]>>3); i++)
    {
        printf("%d ", input_buffer[i+2]);
    }
    printf("%d %d\n\n", input_buffer[i+2], input_buffer[i+3]);    
    */

    uint8_t counter = ((input_buffer[1])&(0b111));
    uint8_t payload_count = ((input_buffer[1])>>3);
    uint8_t type = input_buffer[payload_count+2];
    uint8_t checksum = input_buffer[payload_count+3];
    
    checksum+=type;
    checksum+= 0xDB;
    checksum+= input_buffer[1];
    uint8_t i = 0;
    for (; i < (input_buffer[1]>>3); i++) {
        checksum+= input_buffer[i+2];
    }
    if (checksum!=0) printf("error packet\n\n");
    else printf("nice packet\n\n");
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
    
    uint8_t byte = 0;

    for (uint8_t i = 0; i < number_of_bytes; i++){
        fscanf(sensor_stream, " %hhu", &byte);
        printf("I found byte %hhu\n", byte);
        output_buffer[i] = byte;
    }

    fclose(sensor_stream);
}