#include "ARMS/api.h"
#include "api.h"
#include "pros/motors.h"

#include <tuple>

namespace arms::chassis {

// chassis motors
std::shared_ptr<pros::Motor_Group> leftMotors;
std::shared_ptr<pros::Motor_Group> rightMotors;

// slew control (autonomous only)
double slew_step; // smaller number = more slew

// default exit error
double linear_exit_error;
double angular_exit_error;

// settling
double settle_thresh_linear;
double settle_thresh_angular;
int settle_time;

// chassis variables
double maxSpeed = 100;
double min_linear_speed;
double min_angular_speed;
double min_turn_speed;
double leftPrev = 0;
double rightPrev = 0;
double leftDriveSpeed = 0;
double rightDriveSpeed = 0;

bool previous_end_angle_unknown = false;

/**************************************************/
// motor control
void motorMove(std::shared_ptr<pros::Motor_Group> motor, double speed,
               bool velocity) {
	if (velocity)
		motor->move_velocity(speed * (double)motor->get_gearing()[0] / 100);
	else
		motor->move_voltage(speed * 120);

	if (motor == leftMotors)
		leftPrev = speed;
	else
		rightPrev = speed;
}

void setBrakeMode(pros::motor_brake_mode_e_t b) {
	leftMotors->set_brake_modes((pros::motor_brake_mode_e_t)b);
	rightMotors->set_brake_modes((pros::motor_brake_mode_e_t)b);
	motorMove(leftMotors, 0, true);
	motorMove(rightMotors, 0, true);
}

/**************************************************/
// speed control
double limitSpeed(double speed, double max) {
	if (speed > max)
		speed = max;
	if (speed < -max)
		speed = -max;

	return speed;
}

double slew(double target_speed, double step, double current_speed) {

	if (fabs(current_speed) > fabs(target_speed))
		step = 200;

	if (target_speed > current_speed + step)
		current_speed += step;
	else if (target_speed < current_speed - step)
		current_speed -= step;
	else
		current_speed = target_speed;

	return current_speed;
}

/**************************************************/
// settling
bool settled() {
	// previous position values
	static Point p_pos = {0, 0};
	static double p_ang = 0;

	static int settle_count = 0;

	Point pos = odom::getPosition();
	double ang = odom::getHeading();

	if (fabs(pos.x - p_pos.x) > settle_thresh_linear) {
		p_pos.x = pos.x;
		settle_count = 0;
	} else if (fabs(pos.y - p_pos.y) > settle_thresh_linear) {
		p_pos.y = pos.y;
		settle_count = 0;
	} else if (fabs(ang - p_ang) > settle_thresh_angular) {
		p_ang = ang;
		settle_count = 0;
	} else {
		settle_count += 10;
	}

	if (settle_count > settle_time)
		return true;
	else
		return false;
}

void waitUntilFinished(double exit_error) {
	pros::delay(400); // minimum movement time
	switch (pid::mode) {
	case TRANSLATIONAL:
		while (odom::getDistanceError(pid::pointTarget) > exit_error &&
		       !settled()) {
			pros::delay(10);
		}

		// if doing a pose movement, make sure we are at the target theta
		if (pid::angularTarget != 361) {
			while (fabs(odom::getHeading() - pid::angularTarget) > exit_error &&
			       !settled())
				pros::delay(10);
		}

		break;
	case ANGULAR:
		while (fabs(odom::getHeading() - pid::angularTarget) > exit_error &&
		       !settled())
			pros::delay(10);
		break;
	}
}

/**************************************************/
// 2D movement
void move(std::vector<double> target, double max, double exit_error, double lp,
          double ap, MoveFlags flags) {
	pid::mode = TRANSLATIONAL;

	if (previous_end_angle_unknown) {
		// we need to set the desired angle to the current angle
		odom::setDesiredHeading(odom::getHeading(true));
		previous_end_angle_unknown = false;
	}

	double x = target.at(0);
	double y = target.at(1);
	double theta =
	    target.size() == 3 ? fmod(target.at(2), 360) : 361; // setinel value

	if (flags & TRUE_RELATIVE) {
		// This will do relative movements based on our current position, and adjust
		// the coordinate plane based on our current heading
		Point p = odom::getPosition();     // robot position
		double h = odom::getHeading(true); // robot heading in radians
		double x_new = p.x + x * cos(h) - y * sin(h);
		double y_new = p.y + x * sin(h) + y * cos(h);
		x = x_new;
		y = y_new;
		if (target.size() == 3)
			theta += fmod(odom::getHeading(), 360);
	} else if (flags & RELATIVE) {
		// This will do relative movements based on our desired position, and will
		// ignore our current heading
		Point p = odom::getDesiredPosition();     // robot position
		double h = odom::getDesiredHeading(true); // robot heading in radians
		double x_new = p.x + x * cos(h) - y * sin(h);
		double y_new = p.y + x * sin(h) + y * cos(h);
		x = x_new;
		y = y_new;
		if (target.size() == 3)
			theta += fmod(odom::getDesiredHeading(), 360);
	} else {
		if (theta == 361)
			previous_end_angle_unknown = true;
	}

	pid::pointTarget = Point{x, y};
	pid::angularTarget = theta;

	odom::setDesiredPosition(Point{x, y});
	// convert theta to radians
	odom::setDesiredHeading(theta == 361 ? odom::getDesiredHeading(true)
	                                     : theta * M_PI / 180);

	maxSpeed = max;
	pid::linearKP = lp;
	pid::trackingKP = ap;
	pid::thru = (flags & THRU);
	pid::reverse = (flags & REVERSE);
	pid::canReverse = false;

	// reset the integrals
	pid::in_lin = 0;
	pid::in_ang = 0;

	if (!(flags & ASYNC)) {
		waitUntilFinished(exit_error);
		pid::mode = DISABLE;
		if (!(flags & THRU))
			chassis::setBrakeMode(pros::E_MOTOR_BRAKE_BRAKE);
	}
}

void move(std::vector<double> target, double max, double exit_error,
          MoveFlags flags) {
	move(target, max, exit_error, -1, -1, flags);
}

void move(std::vector<double> target, double max, MoveFlags flags) {
	move(target, max, linear_exit_error, -1, -1, flags);
}

void move(std::vector<double> target, MoveFlags flags) {
	move(target, 100, linear_exit_error, -1, -1, flags);
}

/**************************************************/
// 1D movement
void move(double target, double max, double exit_error, double lp,
          MoveFlags flags) {
	move({target, 0}, max, exit_error, lp, -1, flags | RELATIVE);
}

void move(double target, double max, double exit_error, MoveFlags flags) {
	move({target, 0}, max, exit_error, -1, -1, flags | RELATIVE);
}

void move(double target, double max, MoveFlags flags) {
	move({target, 0}, max, linear_exit_error, -1, -1, flags | RELATIVE);
}

void move(double target, MoveFlags flags) {
	move({target, 0}, 100, linear_exit_error, -1, -1, flags | RELATIVE);
}

/**************************************************/
// rotational movement
void turn(double target, double max, double exit_error, double ap,
          MoveFlags flags) {
	pid::mode = ANGULAR;

	double bounded_heading = (int)(odom::getHeading()) % 360;
	double unbounded_heading = (int)odom::getHeading();

	if (flags & RELATIVE) {
		bounded_heading = (int)(odom::getDesiredHeading()) % 360;
		unbounded_heading = (int)odom::getDesiredHeading();
	}

	double diff = target - bounded_heading;

	diff = ((flags & TRUE_RELATIVE) || (flags & RELATIVE)) ? target : diff;
	while (diff > 180)
		diff -= 360;
	while (diff < -180)
		diff += 360;

	double true_target = diff + unbounded_heading;

	// convert true target to radians
	odom::setDesiredHeading(true_target * M_PI / 180);

	pid::angularTarget = true_target;
	maxSpeed = max;
	pid::angularKP = ap;
	pid::in_ang = 0; // reset the integral value to zero

	if (!(flags & ASYNC)) {
		waitUntilFinished(exit_error);
		pid::mode = DISABLE;
		if (!(flags & THRU))
			chassis::setBrakeMode(pros::E_MOTOR_BRAKE_BRAKE);
	}
}

void turn(double target, double max, double exit_error, MoveFlags flags) {
	turn(target, max, exit_error, -1, flags);
}

void turn(double target, double max, MoveFlags flags) {
	turn(target, max, angular_exit_error, -1, flags);
}

void turn(double target, MoveFlags flags) {
	turn(target, 100, angular_exit_error, -1, flags);
}

/**************************************************/
// turn to point
void turn(Point target, double max, double exit_error, double ap,
          MoveFlags flags) {
	double angle_error = odom::getAngleError(target) * 180 / M_PI;
	turn(angle_error, max, exit_error, ap, flags | RELATIVE);
}

void turn(Point target, double max, double exit_error, MoveFlags flags) {
	turn(target, max, exit_error, -1, flags);
}

void turn(Point target, double max, MoveFlags flags) {
	turn(target, max, angular_exit_error, -1, flags);
}

void turn(Point target, MoveFlags flags) {
	turn(target, 100, angular_exit_error, -1, flags);
}

void moveVectorEnd(double magnitude, double angle, double max,
                   double exit_error, double lp, double ap, MoveFlags flags) {
	double target_heading = ((flags & RELATIVE)        ? odom::getDesiredHeading()
	                         : (flags & TRUE_RELATIVE) ? odom::getHeading()
	                                                   : 0) +
	                        angle;

	Point target =
	    Point{magnitude * cos(target_heading), magnitude * sin(target_heading)};
	move({target.x, target.y}, max, exit_error, lp, ap, arms::RELATIVE | flags);
}

void moveVectorEnd(double magnitude, double angle, double max,
                   double exit_error, MoveFlags flags) {
	moveVectorEnd(magnitude, angle, max, exit_error, -1, -1, flags);
}

void moveVectorEnd(double magnitude, double angle, double max,
                   MoveFlags flags) {
	moveVectorEnd(magnitude, angle, max, linear_exit_error, -1, -1, flags);
}

void moveVectorEnd(double magnitude, double angle, MoveFlags flags) {
	moveVectorEnd(magnitude, angle, 100, linear_exit_error, -1, -1, flags);
}

void moveVectorPath(double magnitude, double angle, double max,
                    double exit_error, double lp, double ap, MoveFlags flags) {
	// turn to the target heading, make sure we are not relative
	turn(angle, max, exit_error, ap, flags & NOT_RELATIVE);
	// move forward our magnitude
	move(magnitude, max, exit_error, lp, flags | RELATIVE);
}

void moveVectorPath(double magnitude, double angle, double max,
                    double exit_error, MoveFlags flags) {
	moveVectorPath(magnitude, angle, max, exit_error, -1, -1, flags);
}

void moveVectorPath(double magnitude, double angle, double max,
                    MoveFlags flags) {
	moveVectorPath(magnitude, angle, max, linear_exit_error, -1, -1, flags);
}

void moveVectorPath(double magnitude, double angle, MoveFlags flags) {
	moveVectorPath(magnitude, angle, 100, linear_exit_error, -1, -1, flags);
}

/**************************************************/
// task control
int chassisTask() {
	while (1) {
		pros::delay(10);

		std::array<double, 2> speeds = {0, 0}; // left, right

		if (pid::mode == TRANSLATIONAL)
			speeds = pid::translational();
		else if (pid::mode == ANGULAR)
			speeds = pid::angular();
		else
			speeds = {leftDriveSpeed, rightDriveSpeed};

		// speed limiting
		speeds[0] = limitSpeed(speeds[0], maxSpeed);
		speeds[1] = limitSpeed(speeds[1], maxSpeed);

		// slew
		speeds[0] = slew(speeds[0], slew_step, leftPrev);
		speeds[1] = slew(speeds[1], slew_step, rightPrev);

		// output
		motorMove(leftMotors, speeds[0], false);
		motorMove(rightMotors, speeds[1], false);
	}
}

/**************************************************/
// initialization
void init(std::initializer_list<int8_t> leftMotors,
          std::initializer_list<int8_t> rightMotors,
          pros::motor_gearset_e_t gearset, double slew_step,
          double linear_exit_error, double angular_exit_error,
          double settle_thresh_linear, double settle_thresh_angular,
          int settle_time, double min_linear_speed, double min_angular_speed, double min_turn_speed) {

	// assign constants
	chassis::slew_step = slew_step;
	chassis::linear_exit_error = linear_exit_error;
	chassis::angular_exit_error = angular_exit_error;
	chassis::settle_thresh_linear = settle_thresh_linear;
	chassis::settle_thresh_angular = settle_thresh_angular;
	chassis::settle_time = settle_time;

	// configure chassis motors
	chassis::leftMotors =
	    std::make_shared<pros::Motor_Group>(std::vector<int8_t>(leftMotors));
	chassis::rightMotors =
	    std::make_shared<pros::Motor_Group>(std::vector<int8_t>(rightMotors));
	chassis::leftMotors->set_gearing(gearset);
	chassis::rightMotors->set_gearing(gearset);

	pros::Task chassis_task(chassisTask);

	chassis::min_linear_speed = min_linear_speed;
	chassis::min_angular_speed = min_angular_speed;
	chassis::min_turn_speed = min_turn_speed;
}

/**************************************************/
// operator control
void tank(double left_speed, double right_speed, bool velocity) {
	pid::mode = DISABLE; // turns off autonomous tasks
	maxSpeed = 100;
	chassis::leftDriveSpeed = left_speed;
	chassis::rightDriveSpeed = right_speed;
}

void arcade(double vertical, double horizontal, bool velocity) {
	pid::mode = DISABLE; // turns off autonomous task
	maxSpeed = 100;
	chassis::leftDriveSpeed = vertical + horizontal;
	chassis::rightDriveSpeed = vertical - horizontal;
}

} // namespace arms::chassis
