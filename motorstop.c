/* EV3 API
 *  Copyright (C) 2018-2019 Francisco Estrada and Lioudmila Tishkina
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

// Initial testing of a bare-bones BlueTooth communication
// library for the EV3 - Thank you Lego for changing everything
// from the NXT to the EV3!

#include "./EV3_RobotControl/btcomm.h"
#include <time.h>
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>

int read_touch_robust(int port) {
  for (int i = 0; i < 3; i++) { // Too many bluetooth calls slows down tha program
    if (BT_read_touch_sensor(port) == 0) return 0;
  }
  return 1;
}

#define mode 2 // Debug mode

int COLOUR_BLACK = 0;
int COLOUR_YELLOW = 1;
int COLOUR_WHITE = 2;
int COLOUR_GREEN = 3;
int COLOUR_BLUE = 4;
int COLOUR_RED = 5;
int COLOUR_UNKNOWN = 6;

char COLOUR_INPUT = PORT_1;
char BACK_TOUCH_INPUT = PORT_3;
char TOP_TOUCH_INPUT = PORT_4;
char RIGHT_WHEEL_OUTPUT = MOTOR_A;
char LEFT_WHEEL_OUTPUT = MOTOR_D;
char SENSOR_WHEEL_OUTPUT = MOTOR_B;

#define SENSOR_WHEEL_POWER 50
#define whiteMax 305.0

int colourFromRGB(int RGB[3]){
  if (RGB[0] < 0 || RGB[0] > 1020 || RGB[1] < 0 || RGB[1] > 1020 || RGB[2] < 0 || RGB[2] > 1020) return COLOUR_UNKNOWN;
  if (RGB[0] > 200 && RGB[1] > 200 && RGB[2] > 200) return COLOUR_WHITE;
  if (RGB[0] > 200 && RGB[1] < 100 && RGB[2] < 100) return COLOUR_RED;
  if (RGB[0] > 100 && RGB[1] > 100 && RGB[2] < 100) return COLOUR_YELLOW;
  if (RGB[0] < 50 && RGB[1] > 50 && RGB[2] < 50) COLOUR_GREEN;
  if (RGB[0] < 50 && RGB[1] < 50 && RGB[2] < 50) return COLOUR_BLACK;
  if (RGB[2] > 75) return COLOUR_BLUE;
  return COLOUR_UNKNOWN;
}

int getRGBFromSensor(){
  int RGB[3];
  BT_read_colour_sensor_RGB(COLOUR_INPUT, RGB);
  for (int i = 0; i < 3; i++){
    RGB[i] = (int) ((double)RGB[i] * 256.0 / whiteMax);
    printf("%d ", RGB[i]);
  }
  printf("\n");
  
  int colour = colourFromRGB(RGB);
  return colour;
}



int *shift_color_sensor(int shift_mode) {
  // shift_mode 1: Extended, 0: Retracted
  int *samples = (int*)calloc(100, sizeof(int));
  int i = 0;
  int flag = 0; // Result of last touch sensor read
  int touch_port = shift_mode == 0 ? BACK_TOUCH_INPUT : TOP_TOUCH_INPUT;
  int power_direction = shift_mode == 0 ? 1 : -1;
  while (flag == 0) {
    BT_motor_port_stop(SENSOR_WHEEL_OUTPUT, 1);
    samples[i] = getRGBFromSensor();
    flag = read_touch_robust(touch_port);
    i++;
    BT_motor_port_start(SENSOR_WHEEL_OUTPUT, SENSOR_WHEEL_POWER * power_direction);
    usleep(1000*100);
  }

  samples[i] = -1; // Termination Value

  BT_timed_motor_port_start_v2(SENSOR_WHEEL_OUTPUT, SENSOR_WHEEL_POWER * -power_direction, 100);

  return samples;
}

int main(int argc, char *argv[]) {
  char test_msg[8] = {0x06, 0x00, 0x2A, 0x00, 0x00, 0x00, 0x00, 0x01};
  char reply[1024];
  int tone_data[50][3];
  memset(&reply[0], 0, 1024);

// just uncomment your bot's hex key to compile for your bot, and comment the
// other ones out.
#ifndef HEXKEY
#define HEXKEY "00:16:53:56:56:03"  // <--- SET UP YOUR EV3's HEX ID here
#endif

  BT_open(HEXKEY);
  printf("Connected!\n");
  fflush(stdout);

  // name must not contain spaces or special characters
  // max name length is 12 characters
  printf("Stopping motors");
  BT_all_stop(0); // Allow for free rotation of the motors
  BT_close();
  fprintf(stderr, "Done!\n");
}