#include "main.h"

pros::Controller master(CONTROLLER_MASTER);

void initialize() {
	chassis::init();
	odom::init();
	pid::init();
	selector::init();
}

void disabled() {
}

void competition_initialize() {
}

void autonomous() {
	odom::move({24, 0});
}

void opcontrol() {

	while (true) {
		if (master.get_digital(DIGITAL_LEFT) && !competition::is_connected())
			autonomous();

		/*	
		chassis::arcade(master.get_analog(ANALOG_LEFT_Y) * (double)100 / 127,
		                master.get_analog(ANALOG_RIGHT_X) * (double)100 / 127);
		*/

		chassis::holonomic(master.get_analog(ANALOG_LEFT_Y) * (double)100 / 127,
						master.get_analog(ANALOG_RIGHT_X) * (double)100 / 127,
						master.get_analog(ANALOG_LEFT_X) * (double)100 / 127);

		pros::delay(10);
	}
}
