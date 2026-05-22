#include <stdio.h>
#include <stdint.h>

FILE *input_stream, *output_stream, *sensor_stream;

void output();
void read_input(int);
void package_sensor(int);

int main(void){

    // Initialising the streams
    input_stream = fopen("input.txt", "r");
    fclose(input_stream);
    output_stream = fopen("output.txt", "w");
    fclose(output_stream);
    sensor_stream = fopen("sensor.txt", "r");
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

void read_input(int number_of_bytes){
    input_stream = fopen("input.txt", "r");
    
    uint8_t byte=0;
    fscanf(input_stream, " %hhu", &byte);
    printf("I found byte %hhu\n\n", byte);

    fclose(input_stream);
}

void package_sensor(int number_of_bytes){
    sensor_stream = fopen("sensor.txt", "r");
    
    uint8_t byte=0;
    fscanf(sensor_stream, " %hhu", &byte);
    printf("I found byte %hhu\n\n", byte);

    fclose(sensor_stream);
}