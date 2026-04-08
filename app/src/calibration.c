#include <stdio.h>
#include "calibration.h"

// Calibration constants
const float STANDARD_NET_HEIGHT = 2.43; // Standard volleyball net height in meters
const float STANDARD_COURT_LENGTH = 18.0; // Standard volleyball court length in meters
const float STANDARD_COURT_WIDTH = 9.0; // Standard volleyball court width

const float MIN_NET_HEIGHT = 1.0; // Minimum net height in meters
const float MAX_NET_HEIGHT = 3.0; // Maximum net height in meters

const float MIN_COURT_LENGTH = 10.0; // Minimum court length in meters
const float MAX_COURT_LENGTH = 30.0; // Maximum court length in meters

const float MIN_COURT_WIDTH = 5.0; // Minimum court width in meters
const float MAX_COURT_WIDTH = 20.0; // Maximum court width in meters

// Calibration variables
float net_height = 2.43;
float court_length = 18.0;
float court_width = 9.0;

void set_net_height(float height) {

    if (height < MIN_NET_HEIGHT || height > MAX_NET_HEIGHT) {
        fprintf(stderr, "Net height must be between %.2f and %.2f meters.\n", MIN_NET_HEIGHT, MAX_NET_HEIGHT);
        return;
    }
    net_height = height;

    arc_calc_params(net_height, court_length, court_width);

    //test
    printf("Net height set to %.2f meters.\n", net_height);
}

void set_court_dimensions(float length, float width) {

    if (length < MIN_COURT_LENGTH || length > MAX_COURT_LENGTH) {
        fprintf(stderr, "Court length must be between %.2f and %.2f meters.\n", MIN_COURT_LENGTH, MAX_COURT_LENGTH);
        return;
    }
    if (width < MIN_COURT_WIDTH || width > MAX_COURT_WIDTH) {
        fprintf(stderr, "Court width must be between %.2f and %.2f meters.\n", MIN_COURT_WIDTH, MAX_COURT_WIDTH);
        return;
    }
    court_length = length;
    court_width = width;

    arc_calc_params(net_height, court_length, court_width);

    //test
    printf("Court dimensions set to %.2f meters (length) x %.2f meters (width).\n", court_length, court_width);
    
}

void calibrate_user_input(char input, float value) {

    switch (input) {
        case 'w':
            set_net_height(value);
            break;
        case 'e':
            set_court_dimensions(value, court_width);
            break;
        case 'r':
            set_court_dimensions(court_length, value);
            break;
        default:
            fprintf(stderr, "Invalid input. Please try again.\n");
    }    
}

float get_net_height() {
    return net_height;
}

float get_court_length() {
    return court_length;
}

float get_court_width() {
    return court_width;
}