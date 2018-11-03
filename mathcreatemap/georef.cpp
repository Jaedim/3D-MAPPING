#include "georef.hpp"
#include "math.hpp"
//Documentation: https://howardhinnant.github.io/date/tz.html
#include "date/tz.h"
//System libraries
#include <chrono>
#include <fstream>
#include <iomanip>

inline auto to_sys_time(const double val){
	
	using namespace std::chrono;
	using ms = duration<double, std::milli>;
	
    using namespace date;
    sys_time<milliseconds> toRet {round<milliseconds>(ms{val})};
    
	return toRet;
}

void georefMath(const std::vector<std::array<double, 50>>& lidarData, 
				const std::vector<std::array<double, 11>>& imuData,
				const std::string& output){

	std::ofstream ptCloudOFS(output);
	//std::ofstream test("testData.txt");

	//Angles of the 16 individual lasers are provided by Velodyne documentation.
    //double laserAngle[16] = { 105, 89, 103, 93, 101, 85, 99, 83, 97, 81, 95, 79, 93, 77, 91, 75 };
    //double laserAngle[16] = { 15, -1, 13,   3, 11,  -5, 9,  -7, 7, -9, 5,  -11, 3, -13, 1, -15 };
    //guess: the bottom array is just the top - 90
	std::array<double, 16> laserAngle = { 15, -1, 13, 3, 11, -5, 9, -7, 7, -9, 5, -11, 3, -13, 1, -15 };
    for (unsigned ctr = 0; ctr < laserAngle.size(); ctr++){
        laserAngle[ctr] = ConvertToRadians(laserAngle[ctr]);
    }

#pragma region VARIABLES FOR GEOREFERENCING MATH

    constexpr auto latOffset = 0;
    constexpr auto lonOffset = 0;
    
    constexpr auto testTime = 500;
    constexpr auto testAngle = 30;
    
    unsigned lRow = 0;			//for traversing LIDAR matrix rows
    unsigned lCol = 3;			//for traversing LIDAR matrix columns

#pragma endregion

#pragma region GEOREF MATH
    print("***Start math");
				
    for (unsigned imuRow = 0; imuRow < imuData.size(); imuRow++){
	    //prevents loop from throwing an index oob error
        if (imuRow + 1 >= imuData.size() || lRow + 1 >= lidarData.size()) { 
       		print("IOOB SAVE"); break; 
       	} 
		
        //Might be 20 seconds behind imu data, so add 20 seconds
        long long lidarTime = lidarData[lRow][lCol] + 20000000l; //microseconds

		//Time stamps needed for time stamp synchronization
        //put the values on a comparable scale
        auto imuTA = to_sys_time(imuData[imuRow][10]);
        auto imuTB = to_sys_time(imuData[imuRow + 1][10]);

		using namespace std::chrono;
        auto imuTA_msPH = imuTA - date::floor<hours>(imuTA);
        auto imuTB_msPH = imuTB - date::floor<hours>(imuTB);
        
		using namespace date;
		//go to next lidarTime until it's greater than imuTimeA
        while (microseconds(lidarTime) < imuTA_msPH){ 
			
			//The next data point's timestamp is three columns away. 
			//Refer to the Matrix organization document
            lCol += 3;	

			//lCol has reached the end of the row
            if (lCol > 48){ 
                lRow++;
                lCol = 3;
            }

            lidarTime = lidarData[lRow][lCol];
        }

		//while the lidarTime is between the two imu ts, keep incrementing through lidarTime
		while (microseconds(lidarTime) >= imuTA_msPH 
			&& microseconds(lidarTime) < imuTB_msPH){
        
            auto timeFlag = false;
            //will store the row number for which IMU data values to do the georef math with
			unsigned imuRowSelect;
			
			//lidarTime is closer to imuA than imuB
            if (abs(imuTA_msPH - microseconds(lidarTime))
            <= abs(imuTB_msPH - microseconds(lidarTime))) { 

               	imuRowSelect = imuRow; 
				//use imuTimeA
                timeFlag = (abs(imuTA_msPH - microseconds(lidarTime)) < microseconds(testTime));

			//lidarTime is closer to imuB than imuA
            }else{										
				//use imuTimeB
                imuRowSelect = imuRow + 1;	

               	timeFlag = (abs(imuTB_msPH - microseconds(lidarTime)) < microseconds(testTime));

            }

            if (timeFlag) {

                //begin pt cloud math
                auto lat = imuData[imuRowSelect][0];
                auto lon = imuData[imuRowSelect][1];
                auto alt = imuData[imuRowSelect][2];
                auto roll = ConvertToRadians(imuData[imuRowSelect][7]);
                auto pitch = ConvertToRadians(imuData[imuRowSelect][8]);
                auto yaw = ConvertToRadians(imuData[imuRowSelect][9]);
                
                auto alpha = ConvertToRadians(lidarData[lRow][0] / 100);
                auto distance = lidarData[lRow][lCol - 2];
                auto timeStamp = lidarData[lRow][lCol];
                auto omega = laserAngle[(lCol / 3) - 1];

                if (distance == 0) {	//skipping the data point if the distance is zero
                    lCol = lCol + 3;	//the next data point's timestamp is three columns away. Refer to the Matrix organization document
                    if (lCol > 48) { lRow++; lCol = 3; }

                    lidarTime = lidarData[lRow][lCol];
                    continue;
                }

                auto X = distance * sin(alpha) * cos(omega);
                auto Y = distance * cos(omega) * cos(alpha);
                auto Z = -distance * sin(omega);

               	auto X1 = X * cos(yaw) - Y * sin(yaw);
                auto Y1 = X * sin(yaw) + Y * cos(yaw);
               
                //X transform (pitch + y_offset)
                X1 = X;
                Y1 = Y * cos(pitch) - Z * sin(pitch);
                auto Z1 = Y * sin(pitch) + Z * cos(pitch);

                //Y transform (roll)
                X = X1 * cos(roll) - Z1 * sin(roll);
                Y = Y1;
                Z = -X1 * sin(roll) + Z1 * cos(roll);

                //Z transform (yaw)
                X1 = X * cos(yaw) - Y * sin(yaw);
                Y1 = X * sin(yaw) + Y * cos(yaw);
                Z1 = Z;

				int altOffset;
                //Position offset
                X1 = X1 + lonOffset;
                Y1 = Y1 - latOffset;
                Z1 = Z1 + altOffset;
                
                if (ConvertToDegrees(yaw) > testAngle) {
                	using namespace std;
                    ptCloudOFS << setw(12) << right << setprecision(5) << fixed 
                    << X1 << " " << setw(12) << right << setprecision(5) << fixed 
                    << Y1 << " " << setw(12) << right << setprecision(5) << fixed 
                    << Z << " " << setw(12) << right << setprecision(3) << 100 
                    << endl;
                } else {
                	using namespace std;
                    ptCloudOFS << setw(12) << right << setprecision(5) << fixed 
                    << X1 << " " << setw(12) << right << setprecision(5) << fixed 
                    << Y1 << " " << setw(12) << right << setprecision(5) << fixed 
                    << Z << " " << setw(12) << right << setprecision(3) << 0 << endl;
                }
                //end pt cloud math


                //increment lidarTime here
                lCol += 3;	//the next data point's timestamp is three columns away. Refer to the Matrix organization document
                if (lCol > 48) { lRow++; lCol = 3; }

                lidarTime = lidarData[lRow][lCol];

            } else {
                //increment lidarTime here
                lCol += 3;	//the next data point's timestamp is three columns away. Refer to the Matrix organization document
                if (lCol > 48) { lRow++; lCol = 3; }

                lidarTime = lidarData[lRow][lCol];
                std::cout << "lidartime: " << lidarTime;
                //test << std::endl;
            }
        }
    }
	//test.close();
	ptCloudOFS.close();
	
#pragma endregion

}
