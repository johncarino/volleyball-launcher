#include "calibration.h"

void set_net_height(float height) {
    if (height < MIN_NET_HEIGHT || height > MAX_NET_HEIGHT) {
        fprintf(stderr, "Net height must be between %.2f and %.2f meters.\n", MIN_NET_HEIGHT, MAX_NET_HEIGHT);
        return;
    }
    net_height = height;

    calculation(net_height, court_length, court_width);
}

void set_court_dimensions() {
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

    calculation(net_height, court_length, court_width);
}

void calibrate_user_input(int input, float value) {
    switch (input) {
        case 1:
            set_net_height(value);
            break;
        case 2:
            set_court_dimensions(value, court_width);
            break;
        case 3:
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