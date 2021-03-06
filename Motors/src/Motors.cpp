/*******************************************************************************
 * File: Motors.cpp
 * Auth: Chris Bessent <cmbq76>
 *
 * Desc: Motor controller for ELMOs using serial communication.  Subscribes
 *       to 'motion' topic and translates the messages into the appropiate
 *       commands for the ELMO controllers.  Includes a watchdog functionality
 *       so it will kill the motors after a certain amount of time has passed
 *       without a message being posted.
 ******************************************************************************/

/***********************************************************
* ROS specific includes
***********************************************************/
#include <ros/ros.h>
#include <ros/callback_queue.h>

/***********************************************************
* Message includes
***********************************************************/
#include "mst_common/Velocity.h"
//#include ""

/***********************************************************
* Other includes
***********************************************************/
#include "Motors.h"
#include "drivers/motorController.h"
#include <pthread.h>
#include <string.h>

/***********************************************************
* Global variables
***********************************************************/
bool   g_watchdog_tripped;      // TRUE if message receieved in last N seconds
bool   g_motors_enabled;        // TRUE if all motors are turned on
double g_linear_velocity;
double g_angular_velocity;

/***********************************************************
* Function prototypes
***********************************************************/
bool        setVelocity( motorController*, double, double );
bool        initMotors( motorController* );
bool        killMotors( motorController* );
void*       receiverControl( void* );
void*       encoderControl( void* );

/***********************************************************
* Message Callbacks
***********************************************************/
void motionCallback(const mst_common::Velocity::ConstPtr& msg)
{
    g_watchdog_tripped = true;
    g_linear_velocity  = msg->linear;
    g_angular_velocity = msg->angular;
}

int main(int argc, char **argv)
{
    ros::init(argc, argv, "Motors");
    ros::NodeHandle n;

    ros::Subscriber sub = n.subscribe(MOTION_TOPIC, 1000, motionCallback);

    /***********************************************************
    * Global variable initialization
    ***********************************************************/
    g_watchdog_tripped = false;
    g_motors_enabled = false;
    g_linear_velocity = 0.0;
    g_angular_velocity = 0.0;

    /***********************************************************
    * Motor initialization
    ***********************************************************/
    motorController     m[] =
        { motorController( (char*)RIGHT_MOTOR_LOCATION ),
          motorController( (char*)LEFT_MOTOR_LOCATION ) };
    g_motors_enabled = initMotors( m );
    if( g_motors_enabled == false )
    {
        // Bail out since a failure here will need troubleshooting to fix.
        return -1;
    }

    /***********************************************************
    * Create 2 threads for receiving random data back from
    * the serial ports
    ***********************************************************/
    pthread_t       receiver_pid[2];

    pthread_create(&receiver_pid[0], NULL, receiverControl, (void*)(&(m[0])));
    pthread_create(&receiver_pid[1], NULL, receiverControl, (void*)(&(m[1])));

    /***********************************************************
    * Create thread for periodically requesting encoder data
    ***********************************************************/
    pthread_t       encoder_pid;
    pthread_create( &encoder_pid, NULL, encoderControl, (void*)(&n) );

    while (ros::ok())
    {
        g_watchdog_tripped = false;

        /***********************************************************
        * The following call will block the process until a msg is
        * available or until WATCHDOG_TIMEOUT seconds has passed.
        ***********************************************************/
        ros::getGlobalCallbackQueue()->
            callAvailable(ros::WallDuration(WATCHDOG_TIMEOUT));

        if( g_watchdog_tripped == false )
        {
            ROS_WARN("Watchdog timed out!");
            killMotors(m);
            g_motors_enabled = false;
        }
        else
        {
            if( g_motors_enabled == false )
            {
                g_motors_enabled = initMotors( m );
            }

            setVelocity( m, g_linear_velocity, g_angular_velocity );
        }
    }

    return 0;
}

void* receiverControl( void* arg )
{
    motorController* m = (motorController*)(arg);
    char buf[20];

    while(ros::ok())
    {
        m->sp.readSerial( buf, 20 );
        if( strstr( buf, "a?" ) != NULL ) // If estop is reported
        {
            g_motors_enabled = false;
        }
        if( strstr( buf, ":?" ) != NULL ) // Cmd sent to OFF motor
        {
            g_motors_enabled = false;
        }
    }

    pthread_exit( arg );
}

void* encoderControl( void* arg )
{
    while(ros::ok())
    {
        sleep(1);
    }
    pthread_exit( arg );
}

bool setVelocity( motorController* m, double linear, double angular )
{
    ROS_INFO("\nlinear %f\nangular %f",linear,angular);
	//Motor velocity command -> RPM*200=JV --M.C. 4/11/2011
    bool    success         = true;
    double  left_velocity   = 0.0;
    double  right_velocity  = 0.0;

    if( abs(linear) > TOPSPEED )
    {
        linear = 0.0;
        angular = 0.0;
        ROS_ERROR("GO SLOWER!");
        //const double scalar = TOPSPEED/abs(linear);
        //angular *= scalar;
        //linear  *= scalar;
    }

    const double MPS2TPS = ENCODER_RESOLUTION * GEARRATIO/(2*WHEEL_RADIUS*M_PI);
    //const double TURNOFFSET = ENCODER_RESOLUTION * GEARRATIO * (WHEEL_DIAMETER / ROBOT_DIAMETER);
    const double TURNOFFSET = MPS2TPS * ROBOT_RADIUS;

    left_velocity = ((linear * MPS2TPS) - (TURNOFFSET * angular)) * LEFT_MOTOR_WARP;
    right_velocity = ((linear * MPS2TPS) + (TURNOFFSET * angular)) * RIGHT_MOTOR_WARP;

    ROS_INFO("\nleft-velocity: %f\nrght_velocity: %f\n",left_velocity,right_velocity);

    success &= m[LEFT_MOTOR_CHANNEL].setVelocity( left_velocity );
    success &= m[RIGHT_MOTOR_CHANNEL].setVelocity( right_velocity );

    if( success == false )
    {
        ROS_ERROR("Motors failed to setVelocity!");
    }

    return success;
}

bool initMotors( motorController* m )
{
    bool success = true;

    for( int i = 0; i < NUMBER_OF_MOTORS; ++i )
    {
        if( m[i].stopMotor()!=1 )           success = false;
        if( m[i].setMode(5)!=1 )            success = false;
        if( m[i].setEncoder(0)!=1 )         success = false;
        if( m[i].toggleMotor(true)!=1 )     success = false;
        if(success==false) ROS_INFO("FAIL MOTOR%d\n",i);
    }

    if( success == false )
    {
        ROS_ERROR("Motors failed to initialize!");
    }

    return success;
}

bool killMotors( motorController* m )
{
    bool success = true;

    for( int i = 0; i < NUMBER_OF_MOTORS; ++i )
    {
        if( m[i].stopMotor()!=1 )           success = false;
        if( m[i].toggleMotor(false)!=1 )    success = false;
    }

    return success;
}
