#include <fstream>
#include <math.h>
#include <uWS/uWS.h>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
#include "Eigen-3.3/Eigen/Core"
#include "Eigen-3.3/Eigen/QR"
#include "json.hpp"
#include "Spline/src/spline.h"
#include "vehicle.h"

using namespace std;

// for convenience
using json = nlohmann::json;

// For converting back and forth between radians and degrees.
constexpr double pi() { return M_PI; }
double deg2rad(double x) { return x * pi() / 180; }
double rad2deg(double x) { return x * 180 / pi(); }

// Checks if the SocketIO event has JSON data.
// If there is data the JSON object in string format will be returned,
// else the empty string "" will be returned.
string hasData(string s) {
  auto found_null = s.find("null");
  auto b1 = s.find_first_of("[");
  auto b2 = s.find_first_of("}");
  if (found_null != string::npos) {
    return "";
  } else if (b1 != string::npos && b2 != string::npos) {
    return s.substr(b1, b2 - b1 + 2);
  }
  return "";
}

double distance(double x1, double y1, double x2, double y2)
{
	return sqrt((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1));
}
int ClosestWaypoint(double x, double y, const vector<double> &maps_x, const vector<double> &maps_y)
{

	double closestLen = 100000; //large number
	int closestWaypoint = 0;

	for(int i = 0; i < maps_x.size(); i++)
	{
		double map_x = maps_x[i];
		double map_y = maps_y[i];
		double dist = distance(x,y,map_x,map_y);
		if(dist < closestLen)
		{
			closestLen = dist;
			closestWaypoint = i;
		}

	}

	return closestWaypoint;

}

int NextWaypoint(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{

	int closestWaypoint = ClosestWaypoint(x,y,maps_x,maps_y);

	double map_x = maps_x[closestWaypoint];
	double map_y = maps_y[closestWaypoint];

	double heading = atan2((map_y-y),(map_x-x));

	double angle = fabs(theta-heading);
  angle = min(2*pi() - angle, angle);

  if(angle > pi()/4)
  {
    closestWaypoint++;
  if (closestWaypoint == maps_x.size())
  {
    closestWaypoint = 0;
  }
  }

  return closestWaypoint;
}

// Transform from Cartesian x,y coordinates to Frenet s,d coordinates
vector<double> getFrenet(double x, double y, double theta, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int next_wp = NextWaypoint(x,y, theta, maps_x,maps_y);

	int prev_wp;
	prev_wp = next_wp-1;
	if(next_wp == 0)
	{
		prev_wp  = maps_x.size()-1;
	}

	double n_x = maps_x[next_wp]-maps_x[prev_wp];
	double n_y = maps_y[next_wp]-maps_y[prev_wp];
	double x_x = x - maps_x[prev_wp];
	double x_y = y - maps_y[prev_wp];

	// find the projection of x onto n
	double proj_norm = (x_x*n_x+x_y*n_y)/(n_x*n_x+n_y*n_y);
	double proj_x = proj_norm*n_x;
	double proj_y = proj_norm*n_y;

	double frenet_d = distance(x_x,x_y,proj_x,proj_y);

	//see if d value is positive or negative by comparing it to a center point

	double center_x = 1000-maps_x[prev_wp];
	double center_y = 2000-maps_y[prev_wp];
	double centerToPos = distance(center_x,center_y,x_x,x_y);
	double centerToRef = distance(center_x,center_y,proj_x,proj_y);

	if(centerToPos <= centerToRef)
	{
		frenet_d *= -1;
	}

	// calculate s value
	double frenet_s = 0;
	for(int i = 0; i < prev_wp; i++)
	{
		frenet_s += distance(maps_x[i],maps_y[i],maps_x[i+1],maps_y[i+1]);
	}

	frenet_s += distance(0,0,proj_x,proj_y);

	return {frenet_s,frenet_d};

}

// Transform from Frenet s,d coordinates to Cartesian x,y
vector<double> getXY(double s, double d, const vector<double> &maps_s, const vector<double> &maps_x, const vector<double> &maps_y)
{
	int prev_wp = -1;

	while(s > maps_s[prev_wp+1] && (prev_wp < (int)(maps_s.size()-1) ))
	{
		prev_wp++;
	}

	int wp2 = (prev_wp+1)%maps_x.size();

	double heading = atan2((maps_y[wp2]-maps_y[prev_wp]),(maps_x[wp2]-maps_x[prev_wp]));
	// the x,y,s along the segment
	double seg_s = (s-maps_s[prev_wp]);

	double seg_x = maps_x[prev_wp]+seg_s*cos(heading);
	double seg_y = maps_y[prev_wp]+seg_s*sin(heading);

	double perp_heading = heading-pi()/2;

	double x = seg_x + d*cos(perp_heading);
	double y = seg_y + d*sin(perp_heading);

	return {x,y};

}

vector<Vehicle> getAllVehiclesInLane(double car_s, double car_speed, int lane, int prev_size, vector<vector<double>> sensor_fusion)
{
    vector<Vehicle> ret;

    for(int i = 0; i < sensor_fusion.size(); i++)
    {
        float d = sensor_fusion[i][6];
        double vx = sensor_fusion[i][3];
        double vy = sensor_fusion[i][4];
        double check_speed = sqrt(vx * vx + vy * vy) * 2.24;
        double check_car_s = sensor_fusion[i][5];
        //check_car_s += prev_size * 0.02 * check_speed;

        //car_s = car_s + prev_size * 0.02 * car_speed;
        double delta = check_car_s - car_s;

        if (delta > -20)
        {
            if (d < (4 + 4 * lane) && d > (4 * lane))
            {
                Vehicle v = Vehicle();
                v.delta_s = delta;
                v.speed = check_speed;
                ret.push_back(v);
            }
        }
    }

    return ret;
}

Vehicle getNearestVehicle(double car_s, double car_speed, int lane, int prev_size, vector<vector<double>> sensor_fusion)
{
    vector<Vehicle> vehicles = getAllVehiclesInLane(car_s, car_speed, lane, prev_size, sensor_fusion);

    if (vehicles.size() == 0)
    {
        Vehicle v = Vehicle();
        v.delta_s = 1000;
        v.speed = 0;
        return v;
    }
    sort(vehicles.begin(), vehicles.end());

    return vehicles[0];
}

Vehicle getNearestVehicle(vector<Vehicle> vehicles)
{
    if (vehicles.size() == 0)
    {
        Vehicle v = Vehicle();
        v.delta_s = 1000;
        v.speed = 0;
        return v;
    }
    sort(vehicles.begin(), vehicles.end());

    return vehicles[0];
}

int getTheBestLane(double car_s, double car_speed, int current_lane, int prev_size, vector<vector<double>> sensor_fusion)
{
    int cost_0 = 0;
    int cost_1 = 0;
    int cost_2 = 0;

    vector<Vehicle> vehiclesInLane0 = getAllVehiclesInLane(car_s, car_speed, 0, prev_size, sensor_fusion);
    vector<Vehicle> vehiclesInLane1 = getAllVehiclesInLane(car_s, car_speed, 1, prev_size, sensor_fusion);
    vector<Vehicle> vehiclesInLane2 = getAllVehiclesInLane(car_s, car_speed, 2, prev_size, sensor_fusion);
    Vehicle nearestVehicleInLane0 = getNearestVehicle(vehiclesInLane0);
    Vehicle nearestVehicleInLane1 = getNearestVehicle(vehiclesInLane1);
    Vehicle nearestVehicleInLane2 = getNearestVehicle(vehiclesInLane2);

    if (nearestVehicleInLane0.delta_s < 30) { cost_0 += 1000000; }
    else
    {
        cost_0 += 600.0 / nearestVehicleInLane0.delta_s;
        if (current_lane == 2 && nearestVehicleInLane1.delta_s < 22) cost_0 += 1000000;
    }

    if (nearestVehicleInLane1.delta_s < 30) { cost_1 += 1000000; }
    else { cost_1 += 600.0 / nearestVehicleInLane1.delta_s; }

    if (nearestVehicleInLane2.delta_s < 30) { cost_2 += 1000000; }
    else
    {
        cost_2 += 600.0 / nearestVehicleInLane2.delta_s;
        if (current_lane == 0 && nearestVehicleInLane1.delta_s < 22) cost_2 += 1000000;
    }

    cost_0 += abs(current_lane - 0) * 10;
    cost_1 += abs(current_lane - 1) * 10;
    cost_2 += abs(current_lane - 2) * 10;

    int bestLine = 0;

    if (cost_0 < cost_1)
    {
        if (cost_0 < cost_2)
        {
            bestLine = 0;
        }
        else
        {
            if (cost_1 < cost_2)
            {
                bestLine = 1;
            }
            else
            {
                bestLine =2;
            }
        }
    }
    else
    {
        if (cost_1 < cost_2)
        {
            bestLine = 1;
        }
        else
        {
            bestLine = 2;
        }
    }

    cout << "cost_0: " << cost_0 << " cost_1: " << cost_1 << " cost_2: " << cost_2 << " best: " << bestLine << endl;

    return bestLine;
}


int main() {
  uWS::Hub h;

  // Load up map values for waypoint's x,y,s and d normalized normal vectors
  vector<double> map_waypoints_x;
  vector<double> map_waypoints_y;
  vector<double> map_waypoints_s;
  vector<double> map_waypoints_dx;
  vector<double> map_waypoints_dy;

  // Waypoint map to read from
  string map_file_ = "../data/highway_map.csv";
  // The max s value before wrapping around the track back to 0
  double max_s = 6945.554;

  ifstream in_map_(map_file_.c_str(), ifstream::in);

  string line;
  while (getline(in_map_, line)) {
  	istringstream iss(line);
  	double x;
  	double y;
  	float s;
  	float d_x;
  	float d_y;
  	iss >> x;
  	iss >> y;
  	iss >> s;
  	iss >> d_x;
  	iss >> d_y;
  	map_waypoints_x.push_back(x);
  	map_waypoints_y.push_back(y);
  	map_waypoints_s.push_back(s);
  	map_waypoints_dx.push_back(d_x);
  	map_waypoints_dy.push_back(d_y);
  }

  int lane = 1;
  double ref_vel = 0;
  double a = 20;

  h.onMessage([&ref_vel,&map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy,&lane, &a](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
                     uWS::OpCode opCode) {
    // "42" at the start of the message means there's a websocket message event.
    // The 4 signifies a websocket message
    // The 2 signifies a websocket event
    //auto sdata = string(data).substr(0, length);
    //cout << sdata << endl;
    if (length && length > 2 && data[0] == '4' && data[1] == '2') {

      auto s = hasData(data);

      if (s != "") {
        auto j = json::parse(s);
        
        string event = j[0].get<string>();
        
        if (event == "telemetry") {
          // j[1] is the data JSON object
          
        	// Main car's localization Data
          	double car_x = j[1]["x"];
          	double car_y = j[1]["y"];
          	double car_s = j[1]["s"];
          	double car_d = j[1]["d"];
          	double car_yaw = j[1]["yaw"];
          	double car_speed = j[1]["speed"];

          	// Previous path data given to the Planner
          	auto previous_path_x = j[1]["previous_path_x"];
          	auto previous_path_y = j[1]["previous_path_y"];
          	// Previous path's end s and d values 
          	double end_path_s = j[1]["end_path_s"];
          	double end_path_d = j[1]["end_path_d"];

          	// Sensor Fusion Data, a list of all other cars on the same side of the road.
          	auto sensor_fusion = j[1]["sensor_fusion"];

          	int prev_size = previous_path_x.size();

          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds
          	if(prev_size > 0)
            {
                car_s = end_path_s;
            }

            bool too_close = false;
          	bool follow_car = false;
            double speed_limit = 49.5;

            if (ref_vel >= 20)
            {
                // Calculate best lane
                lane = getTheBestLane(car_s, car_speed, lane, prev_size, sensor_fusion);
            }

            // Get nearest vehicle in lane
            Vehicle nv = getNearestVehicle(car_s, car_speed, lane, prev_size, sensor_fusion);

            double deltaV = nv.speed - car_speed;
            double th = deltaV * 0.447 * deltaV * 0.447 / (2 * 5) + 30;

            if (nv.delta_s < th)
            {
                follow_car = true;
            }

            if (follow_car)
            {
                if (abs(deltaV) < 0.4)
                {
                    ref_vel = nv.speed;
                    a = 0;
                }
                else
                {
                    a = -5;
                }
            }
            else a = 20;

            if (nv.delta_s < 7)
            {
                a = -20;
            }

            double inc = a / 50;
            if (inc > 0)
            {
                if (ref_vel + inc <= 49.5)
                {
                    ref_vel += inc;
                }
                else ref_vel = 49.5;
            }
            else
            {
                if (ref_vel + inc > 0)
                {
                    ref_vel += inc;
                }
                else ref_vel = 0;
            }

            // Create a list of widely spaced (x,y) waypoints, evenly spaced at 30m
            // Later we will interpolate these waypoints with a spline and fill it in with more points that control speed
            vector<double> ptsx;
            vector<double> ptsy;

            // Reference x, y, yaw states
            // Either we will reference the starting point as where the car is or at the previous path's end point
            double ref_x = car_x;
            double ref_y = car_y;
            double ref_yaw = deg2rad(car_yaw);

            if (prev_size < 2)
            {
                // Use two points that make the path tangent to the car
                double prev_car_x = car_x - cos(car_yaw);
                double prev_car_y = car_y - sin(car_yaw);

                ptsx.push_back(prev_car_x);
                ptsx.push_back(car_x);

                ptsy.push_back(prev_car_y);
                ptsy.push_back(car_y);
            }
            else
            {
                // Redefine reference state as previous path's end point
                ref_x = previous_path_x[prev_size-1];
                ref_y = previous_path_y[prev_size-1];

                double ref_x_prev = previous_path_x[prev_size-2];
                double ref_y_prev = previous_path_y[prev_size-2];
                ref_yaw = atan2(ref_y-ref_y_prev,ref_x-ref_x_prev);

                // Use two points that make the path tangent to the previous path's end point
                ptsx.push_back(ref_x_prev);
                ptsx.push_back(ref_x);

                ptsy.push_back(ref_y_prev);
                ptsy.push_back(ref_y);
            }

            // In Frenet add evently 30m spaced points ahead of the starting reference
            vector<double> next_wp0 = getXY(car_s+50, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp1 = getXY(car_s+60, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp2 = getXY(car_s+70, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp3 = getXY(car_s+80, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp4 = getXY(car_s+90, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp5 = getXY(car_s+100, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);

            ptsx.push_back(next_wp0[0]);
            ptsx.push_back(next_wp1[0]);
            ptsx.push_back(next_wp2[0]);
            ptsx.push_back(next_wp3[0]);
            ptsx.push_back(next_wp4[0]);
            ptsx.push_back(next_wp5[0]);

            ptsy.push_back(next_wp0[1]);
            ptsy.push_back(next_wp1[1]);
            ptsy.push_back(next_wp2[1]);
            ptsy.push_back(next_wp3[1]);
            ptsy.push_back(next_wp4[1]);
            ptsy.push_back(next_wp5[1]);

            for (int i = 0; i < ptsx.size(); i++)
            {
                // Shift car reference angle to 0 degrees
                double shift_x = ptsx[i]-ref_x;
                double shift_y = ptsy[i]-ref_y;

                ptsx[i] = shift_x * cos(0-ref_yaw) - shift_y * sin(0-ref_yaw);
                ptsy[i] = shift_x * sin(0-ref_yaw) + shift_y * cos(0-ref_yaw);
            }

            // Create a spline
            tk::spline s;

            // Set (x,y) points to the spline
            s.set_points(ptsx, ptsy);

            vector<double> next_x_vals;
            vector<double> next_y_vals;

            // Start with all of the previous path points from last time
            for (int i = 0; i < previous_path_x.size(); i++)
            {
                next_x_vals.push_back(previous_path_x[i]);
                next_y_vals.push_back(previous_path_y[i]);
            }

            // Calculate how to break up spline so that we travel at our desired reference velocity
            double target_x = 50.0;
            double target_y = s(target_x);
            double target_dist = sqrt(target_x * target_x + target_y * target_y);

            double x_add_on = 0;

            // Fill up the rest of our path planner after filling it with previous points, here we will
            // always output 30 points
            for (int i = 1; i <= 30-previous_path_x.size(); i++)
            {
                double N = target_dist / (0.02 * ref_vel / 2.24);
                double x_point = x_add_on + target_x / N;
                double y_point = s(x_point);

                x_add_on = x_point;

                double x_ref = x_point;
                double y_ref = y_point;

                // Rotate back to normal after rotating it earlier
                x_point = x_ref * cos(ref_yaw) - y_ref * sin(ref_yaw);
                y_point = x_ref * sin(ref_yaw) + y_ref * cos(ref_yaw);

                x_point += ref_x;
                y_point += ref_y;

                next_x_vals.push_back(x_point);
                next_y_vals.push_back(y_point);
            }

            json msgJson;
          	msgJson["next_x"] = next_x_vals;
          	msgJson["next_y"] = next_y_vals;

          	auto msg = "42[\"control\","+ msgJson.dump()+"]";

          	//this_thread::sleep_for(chrono::milliseconds(1000));
          	ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
          
        }
      } else {
        // Manual driving
        std::string msg = "42[\"manual\",{}]";
        ws.send(msg.data(), msg.length(), uWS::OpCode::TEXT);
      }
    }
  });

  // We don't need this since we're not using HTTP but if it's removed the
  // program
  // doesn't compile :-(
  h.onHttpRequest([](uWS::HttpResponse *res, uWS::HttpRequest req, char *data,
                     size_t, size_t) {
    const std::string s = "<h1>Hello world!</h1>";
    if (req.getUrl().valueLength == 1) {
      res->end(s.data(), s.length());
    } else {
      // i guess this should be done more gracefully?
      res->end(nullptr, 0);
    }
  });

  h.onConnection([&h](uWS::WebSocket<uWS::SERVER> ws, uWS::HttpRequest req) {
    std::cout << "Connected!!!" << std::endl;
  });

  h.onDisconnection([&h](uWS::WebSocket<uWS::SERVER> ws, int code,
                         char *message, size_t length) {
    ws.close();
    std::cout << "Disconnected" << std::endl;
  });

  int port = 4567;
  if (h.listen(port)) {
    std::cout << "Listening to port " << port << std::endl;
  } else {
    std::cerr << "Failed to listen to port" << std::endl;
    return -1;
  }
  h.run();
}
