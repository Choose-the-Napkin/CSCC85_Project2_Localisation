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
  for (int i = 0; i < 3; i++) {
    if (BT_read_touch_sensor(port) == 0) return 0;
  }
  return 1;
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
  BT_setEV3name("R2D2");
  BT_all_stop(0);
  
  int RGB[3], gyro, extended_psh, retracted_psh;

  int direction = 1;
  int e, r;
  //BT_motor_port_start(MOTOR_B, direction * 30);
  while (1) {
    BT_motor_port_stop(MOTOR_B, 1);
    e = read_touch_robust(PORT_4);
    r = read_touch_robust(PORT_3);
    if (e == 1 || r == 1) {
      direction = e * 2 - 1;
    }
    BT_motor_port_start(MOTOR_B, direction * 40);
    BT_read_colour_sensor_RGB(PORT_1, RGB);
    gyro = BT_read_gyro_sensor(PORT_3);

    printf("Gyro: %d, Extended: %d, Retracted: %d, Color Hexcode: %d %d %d\n", gyro, e, r, (int) ((double)RGB[0]/(305.0/256)), (int) ((double)RGB[1]/(305.0/256)), (int) ((double)RGB[2]/(305.0/256)));
    fflush(stdout);
    //printf("2");
    //fflush(stdout);
    //usleep(100*1000);
  }


  BT_all_stop(0); // Allow for free rotation of the motors
  BT_close();
  fprintf(stderr, "Done!\n");
}