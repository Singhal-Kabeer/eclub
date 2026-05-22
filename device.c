#include <stdio.h>
#include <stdint.h>

FILE *input_stream, *output_stream, *sensor_stream;

void output();
void read_input(uint8_t);
void package_sensor(uint8_t);

// most of the 256 bytes memory limit is being used up here. About 160 bytes allocated here
uint8_t input_buffer[32];
uint8_t output_buffer[128];

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

void read_input(uint8_t number_of_bytes){
    input_stream = fopen("input.txt", "r");
    
    uint8_t byte = 0;

    for (uint8_t i = 0; i < number_of_bytes; i++){
        fscanf(input_stream, " %hhu", &byte);

        if (byte == 0xDB) {
            if (i == number_of_bytes-1) printf("I found a start but payload size comes afterwards\n");
            else {
                fscanf(input_stream, " %hhu", &byte);
                i++;
                printf("I found a start and expect a %d byte payload\n", byte);
            }
        }

        printf("I found byte %hhu\n", byte);
        input_buffer[i] = byte;
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