/****************************************************************************
 *
 *   Copyright (C) 2019 PX4 Development Team. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name PX4 nor the names of its contributors may be
 *    used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 * ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 ****************************************************************************/

#include <gtest/gtest.h>
#include <ControlMath.hpp>
#include <px4_platform_common/defines.h>

using namespace matrix;
using namespace ControlMath;

class ControlMathAttitudeMappingTest : public ::testing::Test
{
public:
	void checkDirection(const Vector3f thrust_setpoint, const float yaw)
	{
		thrustToAttitude(thrust_setpoint, yaw, _attitude_setpoint);
		EXPECT_EQ(Quatf(_attitude_setpoint.q_d).dcm_z(), -thrust_setpoint.normalized());
		EXPECT_EQ(_attitude_setpoint.thrust_body[2], -thrust_setpoint.length());
	}

	void checkEuler(const float roll, const float pitch, const float yaw)
	{
		EXPECT_FLOAT_EQ(_attitude_setpoint.roll_body, roll);
		EXPECT_FLOAT_EQ(_attitude_setpoint.pitch_body, pitch);
		EXPECT_FLOAT_EQ(_attitude_setpoint.yaw_body, yaw);
	}

	vehicle_attitude_setpoint_s _attitude_setpoint{};
};

TEST_F(ControlMathAttitudeMappingTest, AttitudeMappingNoRotation)
{
	/* expected: zero roll, zero pitch, zero yaw, full thr mag
	 * reason: thrust pointing full upward */
	checkDirection(Vector3f(0.f, 0.f, -1.f), 0.f);
	checkEuler(0.f, 0.f, 0.f);
}

TEST_F(ControlMathAttitudeMappingTest, AttitudeMappingYaw90)
{
	/* expected: same as before but with 90 yaw
	 * reason: only yaw changed */
	checkDirection(Vector3f(0.f, 0.f, -1.f), M_PI_2_F);
	checkEuler(0.f, 0.f, M_PI_2_F);
}

TEST_F(ControlMathAttitudeMappingTest, AttitudeMappingUpsideDown)
{
	/* expected: same as before but roll 180
	 * reason: thrust points straight down and order Euler
	 * order is: 1. roll, 2. pitch, 3. yaw */
	checkDirection(Vector3f(0.f, 0.f, 1.f), 0.f);
	checkEuler(M_PI_F, 0.f, 0.f);
}

TEST_F(ControlMathAttitudeMappingTest, AttitudeMappingUpsideDownYaw90)
{
	/* expected: same as before but roll 180
	 * reason: thrust points straight down and order Euler
	 * order is: 1. roll, 2. pitch, 3. yaw */
	checkDirection(Vector3f(0.f, 0.f, 1.f), M_PI_2_F);
	checkEuler(M_PI_F, 0.f, -M_PI_2_F);
}

TEST_F(ControlMathAttitudeMappingTest, AttitudeMappingRandomDirections)
{
	checkDirection(Vector3f(0, .5f, -.5f), 1.f);
	checkDirection(Vector3f(-2.f, 8.f, .1f), 2.f);
	checkDirection(Vector3f(-.2f, -5.f, -30.f), 2.f);
}

TEST(ControlMathTest, ConstrainXYPriorities)
{
	const float max = 5.f;
	// v0 already at max
	Vector2f v0(max, 0);
	Vector2f v1(v0(1), -v0(0));

	Vector2f v_r = constrainXY(v0, v1, max);
	EXPECT_FLOAT_EQ(v_r(0), max);
	EXPECT_FLOAT_EQ(v_r(1), 0);

	// norm of v1 exceeds max but v0 is zero
	v0.zero();
	v_r = constrainXY(v0, v1, max);
	EXPECT_FLOAT_EQ(v_r(1), -max);
	EXPECT_FLOAT_EQ(v_r(0), 0.f);

	v0 = Vector2f(.5f, .5f);
	v1 = Vector2f(.5f, -.5f);
	v_r = constrainXY(v0, v1, max);
	const float diff = Vector2f(v_r - (v0 + v1)).length();
	EXPECT_FLOAT_EQ(diff, 0.f);

	// v0 and v1 exceed max and are perpendicular
	v0 = Vector2f(4.f, 0.f);
	v1 = Vector2f(0.f, -4.f);
	v_r = constrainXY(v0, v1, max);
	EXPECT_FLOAT_EQ(v_r(0), v0(0));
	EXPECT_GT(v_r(0), 0);
	const float remaining = sqrtf(max * max - (v0(0) * v0(0)));
	EXPECT_FLOAT_EQ(v_r(1), -remaining);
}

TEST(ControlMathTest, CrossSphereLine)
{
	/* Testing 9 positions (+) around waypoints (o):
	 *
	 * Far             +              +              +
	 *
	 * Near            +              +              +
	 * On trajectory --+----o---------+---------o----+--
	 *                    prev                curr
	 *
	 * Expected targets (1, 2, 3):
	 * Far             +              +              +
	 *
	 *
	 * On trajectory -------1---------2---------3-------
	 *
	 *
	 * Near            +              +              +
	 * On trajectory -------o---1---------2-----3-------
	 *
	 *
	 * On trajectory --+----o----1----+--------2/3---+-- */
	const Vector3f prev = Vector3f(0.f, 0.f, 0.f);
	const Vector3f curr = Vector3f(0.f, 0.f, 2.f);
	Vector3f res;
	bool retval = false;

	// on line, near, before previous waypoint
	retval = cross_sphere_line(Vector3f(0.f, 0.f, -.5f), 1.f, prev, curr, res);
	EXPECT_TRUE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 0.5f));

	// on line, near, before target waypoint
	retval = cross_sphere_line(Vector3f(0.f, 0.f, 1.f), 1.f, prev, curr, res);
	EXPECT_TRUE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 2.f));

	// on line, near, after target waypoint
	retval = cross_sphere_line(Vector3f(0.f, 0.f, 2.5f), 1.f, prev, curr, res);
	EXPECT_TRUE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 2.f));

	// near, before previous waypoint
	retval = cross_sphere_line(Vector3f(0.f, .5f, -.5f), 1.f, prev, curr, res);
	EXPECT_TRUE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, .366025388f));

	// near, before target waypoint
	retval = cross_sphere_line(Vector3f(0.f, .5f, 1.f), 1.f, prev, curr, res);
	EXPECT_TRUE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 1.866025448f));

	// near, after target waypoint
	retval = ControlMath::cross_sphere_line(matrix::Vector3f(0.f, .5f, 2.5f), 1.f, prev, curr, res);
	EXPECT_TRUE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 2.f));

	// far, before previous waypoint
	retval = ControlMath::cross_sphere_line(matrix::Vector3f(0.f, 2.f, -.5f), 1.f, prev, curr, res);
	EXPECT_FALSE(retval);
	EXPECT_EQ(res, Vector3f());

	// far, before target waypoint
	retval = ControlMath::cross_sphere_line(matrix::Vector3f(0.f, 2.f, 1.f), 1.f, prev, curr, res);
	EXPECT_FALSE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 1.f));

	// far, after target waypoint
	retval = ControlMath::cross_sphere_line(matrix::Vector3f(0.f, 2.f, 2.5f), 1.f, prev, curr, res);
	EXPECT_FALSE(retval);
	EXPECT_EQ(res, Vector3f(0.f, 0.f, 2.f));
}

TEST(ControlMathTest, addIfNotNan)
{
	float v = 1.f;
	// regular addition
	ControlMath::addIfNotNan(v, 2.f);
	EXPECT_EQ(v, 3.f);
	// addition is NAN and has no influence
	ControlMath::addIfNotNan(v, NAN);
	EXPECT_EQ(v, 3.f);
	v = NAN;
	// both summands are NAN
	ControlMath::addIfNotNan(v, NAN);
	EXPECT_TRUE(isnan(v));
	// regular value gets added to NAN and overwrites it
	ControlMath::addIfNotNan(v, 3.f);
	EXPECT_EQ(v, 3.f);
}
