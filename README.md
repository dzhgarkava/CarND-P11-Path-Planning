# Path Planning
Self-Driving Car Engineer Nanodegree Program
   
In this project I implemented an algorithm for navigation around a virtual highway with other traffic that is driving +-10 MPH of the 50 MPH speed limit. Algorithm makes decision based on the car's localization and sensor fusion data. The car goes as close as possible to the 50 MPH speed limit and changes lanes to pass slower traffic when possible. Car avoids hitting other cars at all cost as well as driving inside of the marked road lanes at all times, unless going from one lane to another. The car is able to make one complete loop around the 6946m highway. Also the car avoids total acceleration over 10 m/s^2 and jerk that is greater than 10 m/s^3.

![alt text][image1]

The car is able to drive at least 4.32 miles without incident. 

The car drives according to the speed limit. The speed limit set at 49.5 MPH (main.cpp, line: 389). I check that car does not drive over the limit during the acceleration (main.cpp, lines: 430-434). 

Car is able to avoid collisions by slowing down behind slow vehicle (main.cpp, lines: 400-420). When car is slowing down or accelerating I check that max acceleration and jerk are not exceeded (main.cpp, line: 427-442).

The car is able to change lanes to overtake slow traffic. Every tick car checks the best lane to continue driving. The best lane calculated based on cost function (main.cpp, lines: 229-298).

[//]: # (Image References)
[image1]: ./path_planning.png