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
#include "spline.h"

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

double logistic(double x){
	return 2.0 / (1 + exp(-x)) - 1.0;
}

int indexofSmallestElement(vector<double> array)
{
    int index = 0;

    for(int i = 1; i < array.size(); i++)
    {
        if(array[i] < array[index])
            index = i;              
    }

    return index;
}

int d2lane(double d){
	int Lane = 3;
	if (d>0 && d<4) Lane =0;
	else if (d>4 && d<8) Lane =1;
	else if (d>8 && d<12) Lane =2;
	return Lane;
}


vector<int> check_front(vector<vector<double>> sensor_fusion, double car_s, double car_d){
	vector<int> s_front = {0,0,0};
	int car_lane = d2lane(car_d);
	for (int i=0;i< sensor_fusion.size();i++){
		double check_car_s = sensor_fusion[i][5];
		if ((check_car_s>car_s) && ((check_car_s - car_s)<30)){
			double d2 = sensor_fusion[i][6];
			int lane2 = d2lane(d2);
			if (lane2 == car_lane) s_front[1] = 1;
			else if (lane2 == car_lane+1) s_front[2] = 1;
			else if (lane2 == car_lane-1) s_front[0] = 1;
		}
	}
	return s_front;
}

			

vector<int> check_back(vector<vector<double>> sensor_fusion, double car_s, double car_d){
	vector<int> s_back = {0, 0, 0};
	int car_lane = d2lane(car_d);
	for (int i=0;i< sensor_fusion.size();i++){
		double check_car_s = (sensor_fusion[i][5]);
		if ((check_car_s<car_s) && ((car_s - check_car_s)<30)){
			double d2 = sensor_fusion[i][6];
			int lane2 = d2lane(d2);
			if (lane2 == car_lane) s_back[1] = 1;
			else if (lane2 == car_lane+1) s_back[2] = 1;
			else if (lane2 == car_lane-1) s_back[0] = 1;
		}
	}
	return s_back;
}

int cost_func(int lane, int s_t_f, int s_t_r, double speed_e, double speed_t){
	
	int cost1 = 2*s_t_f;
	int cost2 = 2*s_t_r;
	int cost3 = 1*abs(lane-1);
	
	return cost1 + cost2 + cost3;
}

int decision_cost (double car_s, double car_d, double car_speed, double check_speed, int lane, vector<int> s_front, vector<int> s_back){
	
	vector<double> costs = {99999, 99999, 99999};
	
	for (int i=0; i<3; i++){
		costs[i] = cost_func(lane, s_front[i], s_back[i], car_speed, check_speed);
	}
	
	if (lane != 1) costs[lane] = 99999;
	
	int index = indexofSmallestElement(costs);
	if (index == 0) return lane-1;
	if (index == 1) return lane;
	if (index == 2) return lane+1;
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


double lane2d(int lane){
	double d = (double)lane*4 + 2;
	return d;
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
  
  int lane = 1;
  
  double des_vel = 49.5;
  double ref_vel = 0.0;

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


  
  h.onMessage([&ref_vel, &lane, &map_waypoints_x,&map_waypoints_y,&map_waypoints_s,&map_waypoints_dx,&map_waypoints_dy](uWS::WebSocket<uWS::SERVER> ws, char *data, size_t length,
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
          	//auto sensor_fusion = j[1]["sensor_fusion"];

            vector<vector<double>> sensor_fusion = j[1]["sensor_fusion"];

            //extra2

            int prev_size = previous_path_x.size();

            if (prev_size >0){
              car_s = end_path_s;
            }

            bool too_close = false;
			double check_speed = 99;
			// find ref_v to use
            for (int i = 0; i<sensor_fusion.size();i++){
				
				// car is in my lane
              float d = sensor_fusion[i][6];
              if (d < (2+4*lane+2) && d > (2+4*lane-2)){

                double vx = sensor_fusion[i][3];
                double vy = sensor_fusion[i][4];
                check_speed = sqrt(vx*vx + vy*vy);
                double check_car_s = sensor_fusion[i][5];
				

                check_car_s += ((double)prev_size*.02*check_speed); // 

				// check s values greater than mine and s gap
                if ((check_car_s>car_s) && ((check_car_s - car_s)<30)){
                  //ref_vel = 29.5;
                  too_close = true;
				  
                  
                }



              }
			}
			

			
			
			
            if (too_close){
              //ref_vel -= .224;
			  vector <int> s_front = check_front(sensor_fusion, car_s, car_d);
				  vector <int> s_back = check_back(sensor_fusion, car_s, car_d);
				  
				  int new_lane = decision_cost (car_s, car_d, car_speed, check_speed, lane, s_front, s_back);
				  
				  if (new_lane == lane) ref_vel -= .224;
				  else lane = new_lane;
            }
            else if (ref_vel<49.5){
              ref_vel += 0.224;
            }
			
			
			

          	json msgJson;

          	//vector<double> next_x_vals;
          	//vector<double> next_y_vals;


          	// TODO: define a path made up of (x,y) points that the car will visit sequentially every .02 seconds

              // create widely spaced waypoints evenly spaced at 30 m
  
              vector<double> ptsx;
              vector<double> ptsy;

			  // reference the starting point as where the car is at previous path end point
              double ref_x = car_x;
              double ref_y = car_y;
              double ref_yaw = deg2rad(car_yaw);
  
			// if previous size is empty, use car as starting reference
             if (prev_size<2){
				 // use two points that make the path tangent to car
              double prev_car_x = car_x - cos(car_yaw);
              double prev_car_y = car_y - sin(car_yaw);
              ptsx.push_back(prev_car_x);
              ptsx.push_back(car_x);

              ptsy.push_back(prev_car_y);
              ptsy.push_back(car_y);
            }
			
			// use previous path's end point as starting reference
            else{
    
              ref_x = previous_path_x[prev_size-1];
              ref_y = previous_path_y[prev_size-1];
    
			//redefine reference state as previous path end point
              double ref_x_prev = previous_path_x[prev_size-2];
              double ref_y_prev = previous_path_y[prev_size-2];
              ref_yaw = atan2(ref_y-ref_y_prev, ref_x-ref_x_prev);
    
			// use two points that make the path tangent to the previous path's end point
              ptsx.push_back(ref_x_prev);
              ptsx.push_back(ref_x);
    
              ptsy.push_back(ref_y_prev);
              ptsy.push_back(ref_y);
            }
			// in Frenet add evenly 30m spaced points ahead of the starting reference
            vector<double> next_wp0 = getXY(car_s+30, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp1 = getXY(car_s+60, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
            vector<double> next_wp2 = getXY(car_s+90, (2+4*lane), map_waypoints_s, map_waypoints_x, map_waypoints_y);
  
            ptsx.push_back(next_wp0[0]);
            ptsx.push_back(next_wp1[0]);
            ptsx.push_back(next_wp2[0]);
  
            ptsy.push_back(next_wp0[1]);
            ptsy.push_back(next_wp1[1]);
            ptsy.push_back(next_wp2[1]);

            for (int i=0; i<ptsx.size();i++){
				//shift car reference to 0 degrees
              double shift_x = ptsx[i]-ref_x;
              double shift_y = ptsy[i]-ref_y;

              ptsx[i] = (shift_x*cos(0-ref_yaw) - shift_y*sin(0-ref_yaw));
              ptsy[i] = (shift_x*sin(0-ref_yaw) + shift_y*cos(0-ref_yaw));
              }
  
			// create a spline
            tk::spline s;
  
			// set (x,y) points to the spline
            s.set_points(ptsx,ptsy);
			
			// define actual (x,y) points used for the planner
            vector<double> next_x_vals;
            vector<double> next_y_vals;
  
			// start with all of previous path points from last time
            for (int i=0; i<previous_path_x.size();i++){
              next_x_vals.push_back(previous_path_x[i]);
              next_y_vals.push_back(previous_path_y[i]);
            }
			
			// calculate how to break up spline points so we travel at the desired ref velocity
            double target_x = 30;
            double target_y = s(target_x);
            double target_dist = sqrt((target_x*target_x)+(target_y*target_y));
  
            double x_add_on = 0;

            
			// fill up the rest of our path planner after filling it with previous points (here output is 50 pts)
  
            for (int i = 1; i<=50-previous_path_x.size();i++){
              double N = (target_dist/(.02*ref_vel/2.24));
              double x_point = x_add_on+(target_x)/N;
              double y_point = s(x_point);
    
              x_add_on = x_point;
			  
			  double x_ref = x_point;
			  double y_ref = y_point;
			  
			  // rotate back to normal after rotating earlier
              x_point = (x_ref*cos(ref_yaw)-y_ref*sin(ref_yaw));
              y_point = (x_ref*sin(ref_yaw)+y_ref*cos(ref_yaw));
              x_point += ref_x;
              y_point += ref_y;
              next_x_vals.push_back(x_point);
			  next_y_vals.push_back(y_point);
            }
			/*
			double dist_inc = 0.3;
			for(int i = 0; i < 50; i++)
			{
				double next_s = car_s + (i+1)*dist_inc;
				double next_d = 6;
				
				vector<double> xy = getXY(next_s, next_d, map_waypoints_s, map_waypoints_x, map_waypoints_y);
				
				next_x_vals.push_back(xy[0]);
				next_y_vals.push_back(xy[1]);
			}
			*/
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
